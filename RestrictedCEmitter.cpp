#include "RestrictedCEmitter.h"

#include "BuiltinCatalog.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <fstream>
#include <limits>
#include <sstream>
#include <system_error>
#include <variant>

namespace
{

RestrictedCResult failure(RestrictedCStatus status, const std::string &diagnostic)
{
    RestrictedCResult result;
    result.status = status;
    result.diagnostic = diagnostic;
    return result;
}

bool supported_shape(const value_shape &shape)
{
    return !shape.is_array && shape.array_upper_bound == -1 &&
           (shape.element_type == TYPE_INT || shape.element_type == TYPE_BOOL);
}

bool supported_unary(ir::UnaryOp operation, data_types type)
{
    return (operation == ir::UnaryOp::Negate && type == TYPE_INT) ||
           (operation == ir::UnaryOp::Not && (type == TYPE_INT || type == TYPE_BOOL));
}

bool supported_binary(ir::BinaryOp operation, data_types type)
{
    switch (operation)
    {
    case ir::BinaryOp::Add:
    case ir::BinaryOp::Subtract:
    case ir::BinaryOp::Multiply:
    case ir::BinaryOp::Divide:
        return type == TYPE_INT;
    case ir::BinaryOp::And:
    case ir::BinaryOp::Or:
        return type == TYPE_INT || type == TYPE_BOOL;
    case ir::BinaryOp::Less:
    case ir::BinaryOp::LessEqual:
    case ir::BinaryOp::Greater:
    case ir::BinaryOp::GreaterEqual:
    case ir::BinaryOp::Equal:
    case ir::BinaryOp::NotEqual:
        return type == TYPE_INT || type == TYPE_BOOL;
    }
    return false;
}

bool supported_cast(ir::CastOp operation)
{
    return operation == ir::CastOp::IntToBool || operation == ir::CastOp::BoolToInt;
}

const ir::Value *value_for(const ir::Function &function, ir::ValueId id)
{
    if (!id.valid() || id.function != function.id || id.index >= function.values.size())
    {
        return NULL;
    }
    const ir::Value &value = function.values[id.index];
    return value.id == id ? &value : NULL;
}

const ir::Storage *storage_for(const ir::Module &module, ir::StorageId id)
{
    if (!id.valid() || id.index >= module.storages.size())
    {
        return NULL;
    }
    const ir::Storage &storage = module.storages[id.index];
    return storage.id == id ? &storage : NULL;
}

const ir::Function *function_for(const ir::Module &module, ir::FunctionId id)
{
    if (!id.valid() || id.index >= module.functions.size())
    {
        return NULL;
    }
    const ir::Function &function = module.functions[id.index];
    return function.id == id ? &function : NULL;
}

std::string value_register(ir::ValueId id)
{
    return "Reg[" + std::to_string(static_cast<std::uint64_t>(id.index) + 2U) + "u]";
}

std::string storage_word(ir::StorageId id)
{
    return "MM[" + std::to_string(id.index) + "u]";
}

std::string register_slot(std::size_t index)
{
    return "Reg[" + std::to_string(index) + "u]";
}

struct RegisterLayout
{
    std::size_t count = 0;
    std::size_t exit = 0;
    std::size_t division_zero = 0;
    std::size_t division_overflow = 0;
    bool has_division = false;
};

struct RuntimeRequirements
{
    bool put_integer = false;
};

bool make_register_layout(const ir::Function &program, RegisterLayout &layout)
{
    for (const ir::BasicBlock &block : program.blocks)
    {
        for (const ir::Instruction &instruction : block.instructions)
        {
            const ir::Binary *binary = std::get_if<ir::Binary>(&instruction);
            layout.has_division = layout.has_division ||
                                  (binary != NULL && binary->operation == ir::BinaryOp::Divide);
        }
    }
    const std::size_t extra = layout.has_division ? 3U : 1U;
    const std::size_t reserved = 2U;
    const std::size_t maximum = static_cast<std::size_t>(
        std::numeric_limits<std::uint32_t>::max());
    if (program.values.size() > maximum ||
        program.values.size() > maximum - reserved - extra)
    {
        return false;
    }
    layout.exit = program.values.size() + reserved;
    layout.division_zero = layout.exit + 1U;
    layout.division_overflow = layout.exit + 2U;
    layout.count = program.values.size() + reserved + extra;
    return layout.count != 0U;
}

std::string int32_literal(int value)
{
    const std::int64_t widened = value;
    if (widened < static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min()) ||
        widened > static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max()))
    {
        return std::string();
    }
    if (widened == static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min()))
    {
        return "INT32_MIN";
    }
    if (widened < 0)
    {
        return "(-INT32_C(" + std::to_string(-widened) + "))";
    }
    return "INT32_C(" + std::to_string(value) + ")";
}

RestrictedCResult preflight(const ir::Module &module, const ir::Function *&program,
                            RuntimeRequirements &runtime)
{
    const ir::VerificationResult verified = ir::verify_module(module);
    if (!verified.valid)
    {
        return failure(RestrictedCStatus::InvalidIR, verified.reason);
    }
    if (module.functions.empty() || module.functions[0].kind != ir::FunctionKind::Program ||
        module.functions[0].id != ir::FunctionId(0) || module.functions[0].blocks.empty())
    {
        return failure(RestrictedCStatus::Unsupported,
                       "restricted C requires a verified Program function f0");
    }
    program = &module.functions[0];
    if (!RestrictedCEmitter::storage_count_fits_memory(module.storages.size()))
    {
        return failure(RestrictedCStatus::Unsupported,
                       "restricted C global storage exceeds the fixed memory capacity");
    }
    for (const ir::Function &function : module.functions)
    {
        if (function.kind == ir::FunctionKind::ExternalBuiltin)
        {
            continue; //Declarations are harmless until a Call uses one.
        }
        if (function.kind != ir::FunctionKind::Program)
        {
            return failure(RestrictedCStatus::Unsupported,
                           "restricted C does not yet lower procedures");
        }
        if (function.id != program->id || !function.parameters.empty() || !function.locals.empty() ||
            !function.parameter_types.empty())
        {
            return failure(RestrictedCStatus::Unsupported,
                           "restricted C only supports a parameterless Program");
        }
    }
    //The IR verifier permits cycles with an exit so future loop lowering can
    //reuse the CFG representation.  This restricted-C slice intentionally
    //accepts only acyclic Program flow produced by if/else lowering.
    std::vector<unsigned char> visit_state(program->blocks.size(), 0U);
    const std::function<bool(std::size_t)> has_cycle =
        [&program, &visit_state, &has_cycle](std::size_t block_index) -> bool {
            visit_state[block_index] = 1U;
            const ir::Terminator &terminator =
                std::get<ir::Terminator>(program->blocks[block_index].terminator);
            std::vector<std::size_t> targets;
            if (const ir::JumpTerminator *jump = std::get_if<ir::JumpTerminator>(&terminator))
            {
                targets.push_back(jump->target.index);
            }
            else if (const ir::BranchTerminator *branch =
                         std::get_if<ir::BranchTerminator>(&terminator))
            {
                targets.push_back(branch->when_true.index);
                targets.push_back(branch->when_false.index);
            }
            for (std::size_t target : targets)
            {
                if (visit_state[target] == 1U ||
                    (visit_state[target] == 0U && has_cycle(target)))
                {
                    return true;
                }
            }
            visit_state[block_index] = 2U;
            return false;
        };
    if (has_cycle(0))
    {
        return failure(RestrictedCStatus::Unsupported,
                       "restricted C does not yet lower cyclic control flow");
    }
    for (const ir::Storage &storage : module.storages)
    {
        if (storage.kind != ir::StorageKind::Global || storage.owner != program->id ||
            !supported_shape(storage.type))
        {
            return failure(RestrictedCStatus::Unsupported,
                           "restricted C supports scalar Integer and Bool globals only");
        }
    }
    for (const ir::Value &value : program->values)
    {
        if (!supported_shape(value.type))
        {
            return failure(RestrictedCStatus::Unsupported,
                           "restricted C supports scalar Integer and Bool values only");
        }
    }
    for (const ir::BasicBlock &block : program->blocks)
    {
        for (const ir::Instruction &instruction : block.instructions)
        {
        if (const ir::Constant *constant = std::get_if<ir::Constant>(&instruction))
        {
            const ir::Value *result = value_for(*program, constant->result);
            if (result == NULL ||
                (result->type.element_type == TYPE_INT &&
                 (!std::holds_alternative<int>(constant->payload) ||
                  int32_literal(std::get<int>(constant->payload)).empty())) ||
                (result->type.element_type == TYPE_BOOL &&
                 !std::holds_alternative<bool>(constant->payload)))
            {
                return failure(RestrictedCStatus::InvalidIR,
                               "restricted C constant does not match its result");
            }
        }
        else if (const ir::Load *load = std::get_if<ir::Load>(&instruction))
        {
            const ir::Value *result = value_for(*program, load->result);
            const ir::Storage *source = storage_for(module, load->source);
            if (result == NULL || source == NULL || result->type != source->type ||
                source->kind != ir::StorageKind::Global)
            {
                return failure(RestrictedCStatus::InvalidIR,
                               "restricted C load is not a typed global load");
            }
        }
        else if (const ir::Store *store = std::get_if<ir::Store>(&instruction))
        {
            const ir::Storage *destination = storage_for(module, store->destination);
            const ir::Value *value = value_for(*program, store->value);
            if (destination == NULL || value == NULL || destination->kind != ir::StorageKind::Global ||
                destination->type != value->type)
            {
                return failure(RestrictedCStatus::InvalidIR,
                               "restricted C store is not a typed global store");
            }
        }
        else if (const ir::Unary *unary = std::get_if<ir::Unary>(&instruction))
        {
            const ir::Value *result = value_for(*program, unary->result);
            const ir::Value *operand = value_for(*program, unary->operand);
            if (result == NULL || operand == NULL || result->type != operand->type ||
                !supported_unary(unary->operation, operand->type.element_type))
            {
                return failure(RestrictedCStatus::InvalidIR,
                               "restricted C unary instruction is invalid");
            }
        }
        else if (const ir::Binary *binary = std::get_if<ir::Binary>(&instruction))
        {
            const ir::Value *result = value_for(*program, binary->result);
            const ir::Value *left = value_for(*program, binary->left);
            const ir::Value *right = value_for(*program, binary->right);
            if (result == NULL || left == NULL || right == NULL || left->type != right->type ||
                !supported_binary(binary->operation, left->type.element_type))
            {
                return failure(RestrictedCStatus::InvalidIR,
                               "restricted C binary instruction is invalid");
            }
            const data_types expected =
                binary->operation == ir::BinaryOp::Less ||
                        binary->operation == ir::BinaryOp::LessEqual ||
                        binary->operation == ir::BinaryOp::Greater ||
                        binary->operation == ir::BinaryOp::GreaterEqual ||
                        binary->operation == ir::BinaryOp::Equal ||
                        binary->operation == ir::BinaryOp::NotEqual
                    ? TYPE_BOOL
                    : left->type.element_type;
            if (result->type.element_type != expected)
            {
                return failure(RestrictedCStatus::InvalidIR,
                               "restricted C binary result has the wrong type");
            }
        }
        else if (const ir::Cast *cast = std::get_if<ir::Cast>(&instruction))
        {
            const ir::Value *result = value_for(*program, cast->result);
            const ir::Value *operand = value_for(*program, cast->operand);
            if (result == NULL || operand == NULL || !supported_cast(cast->operation) ||
                (cast->operation == ir::CastOp::IntToBool &&
                 (operand->type.element_type != TYPE_INT || result->type.element_type != TYPE_BOOL)) ||
                (cast->operation == ir::CastOp::BoolToInt &&
                 (operand->type.element_type != TYPE_BOOL || result->type.element_type != TYPE_INT)))
            {
                return failure(RestrictedCStatus::InvalidIR,
                               "restricted C cast is invalid");
            }
        }
        else if (const ir::Call *call = std::get_if<ir::Call>(&instruction))
        {
            const ir::Function *callee = function_for(module, call->callee);
            const BuiltinSpec *put_integer = find_builtin(BuiltinId::PutInteger);
            const ir::Value *result = value_for(*program, call->result);
            if (callee == NULL || put_integer == NULL)
            {
                return failure(RestrictedCStatus::InvalidIR,
                               "restricted C call has no canonical callee metadata");
            }
            const SymbolRef expected_reference{0, put_integer->spelling};
            if (callee->kind != ir::FunctionKind::ExternalBuiltin ||
                callee->name != put_integer->spelling || callee->symbol != expected_reference ||
                callee->return_type != put_integer->return_shape ||
                callee->parameter_types != put_integer->parameter_shapes)
            {
                return failure(RestrictedCStatus::Unsupported,
                               "restricted C only lowers the canonical putInteger builtin");
            }
            if (result == NULL || result->type != put_integer->return_shape ||
                call->arguments.size() != put_integer->parameter_shapes.size())
            {
                return failure(RestrictedCStatus::InvalidIR,
                               "restricted C putInteger call has an invalid result or arity");
            }
            for (std::size_t argument_index = 0; argument_index < call->arguments.size();
                 argument_index++)
            {
                const ir::Value *argument = value_for(*program, call->arguments[argument_index]);
                if (argument == NULL || argument->type != put_integer->parameter_shapes[argument_index])
                {
                    return failure(RestrictedCStatus::InvalidIR,
                                   "restricted C putInteger call has an invalid argument");
                }
            }
            runtime.put_integer = true;
        }
        else
        {
            return failure(RestrictedCStatus::Unsupported,
                           "restricted C encountered an unknown instruction");
        }
        }
    }
    RegisterLayout layout;
    if (!make_register_layout(*program, layout))
    {
        return failure(RestrictedCStatus::Unsupported,
                       "restricted C register model cannot represent this many values");
    }
    RestrictedCResult success;
    success.status = RestrictedCStatus::Success;
    return success;
}

std::string binary_expression(const ir::Binary &binary, data_types type)
{
    const std::string left = value_register(binary.left);
    const std::string right = value_register(binary.right);
    switch (binary.operation)
    {
    case ir::BinaryOp::Add:
        return "I32_FROM_U32((uint32_t)" + left + " + (uint32_t)" + right + ")";
    case ir::BinaryOp::Subtract:
        return "I32_FROM_U32((uint32_t)" + left + " - (uint32_t)" + right + ")";
    case ir::BinaryOp::Multiply:
        return "I32_FROM_U32((uint32_t)" + left + " * (uint32_t)" + right + ")";
    case ir::BinaryOp::Divide:
        return left + " / " + right;
    case ir::BinaryOp::Less:
        return "(" + left + " < " + right + ") ? INT32_C(1) : INT32_C(0)";
    case ir::BinaryOp::LessEqual:
        return "(" + left + " <= " + right + ") ? INT32_C(1) : INT32_C(0)";
    case ir::BinaryOp::Greater:
        return "(" + left + " > " + right + ") ? INT32_C(1) : INT32_C(0)";
    case ir::BinaryOp::GreaterEqual:
        return "(" + left + " >= " + right + ") ? INT32_C(1) : INT32_C(0)";
    case ir::BinaryOp::Equal:
        return "(" + left + " == " + right + ") ? INT32_C(1) : INT32_C(0)";
    case ir::BinaryOp::NotEqual:
        return "(" + left + " != " + right + ") ? INT32_C(1) : INT32_C(0)";
    case ir::BinaryOp::And:
        if (type == TYPE_BOOL)
        {
            return "(" + left + " != INT32_C(0) && " + right +
                   " != INT32_C(0)) ? INT32_C(1) : INT32_C(0)";
        }
        return "I32_FROM_U32((uint32_t)" + left + " & (uint32_t)" + right + ")";
    case ir::BinaryOp::Or:
        if (type == TYPE_BOOL)
        {
            return "(" + left + " != INT32_C(0) || " + right +
                   " != INT32_C(0)) ? INT32_C(1) : INT32_C(0)";
        }
        return "I32_FROM_U32((uint32_t)" + left + " | (uint32_t)" + right + ")";
    }
    return std::string();
}

std::filesystem::path sibling_temp_directory(const std::filesystem::path &output,
                                             std::error_code &error)
{
    static std::atomic<unsigned long> serial{0};
    const std::filesystem::path parent = output.parent_path().empty() ?
                                               std::filesystem::path(".") : output.parent_path();
    const std::string base = "." + output.filename().string() + ".restricted-c-tmp-";
    for (unsigned int attempt = 0; attempt < 1024U; attempt++)
    {
        const std::filesystem::path candidate = parent /
            (base + std::to_string(serial.fetch_add(1, std::memory_order_relaxed)));
        if (std::filesystem::create_directory(candidate, error))
        {
            return candidate;
        }
        if (error == std::errc::file_exists)
        {
            error.clear();
            continue;
        }
        if (error)
        {
            return std::filesystem::path();
        }
    }
    error = std::make_error_code(std::errc::file_exists);
    return std::filesystem::path();
}

void cleanup_temp_directory(const std::filesystem::path &directory)
{
    std::error_code ignored;
    const std::filesystem::path file = directory / "output.c";
    std::filesystem::remove(file, ignored);
    ignored.clear();
    std::filesystem::remove(directory, ignored);
}

bool is_direct_regular_or_missing(const std::filesystem::path &path, std::error_code &error)
{
    const std::filesystem::file_status status = std::filesystem::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory)
    {
        error.clear();
        return true;
    }
    if (error)
    {
        return false;
    }
    if (status.type() == std::filesystem::file_type::not_found)
    {
        return true;
    }
    return status.type() == std::filesystem::file_type::regular;
}

} // namespace

RestrictedCResult RestrictedCEmitter::emit(const ir::Module &module) const
{
    const ir::Function *program = NULL;
    RuntimeRequirements runtime;
    RestrictedCResult checked = preflight(module, program, runtime);
    if (!checked.succeeded())
    {
        return checked;
    }

    RegisterLayout layout;
    if (!make_register_layout(*program, layout))
    {
        return failure(RestrictedCStatus::Unsupported,
                       "restricted C register model cannot represent this many values");
    }
    std::ostringstream output;
    output << "#include <stdint.h>\n";
    if (runtime.put_integer)
    {
        output << "#include <inttypes.h>\n";
        output << "#include <stdio.h>\n";
    }
    output << "\n";
    output << "#define MM_BYTES (" << RestrictedCEmitter::memory_byte_capacity() << "u)\n";
    output << "#define REGISTER_COUNT " << layout.count << "u\n\n";
    output << "#define I32_FROM_U32(value) ((value) <= UINT32_C(2147483647) ? "
           << "(int32_t)(value) : INT32_MIN + (int32_t)((uint32_t)(value) - "
           << "UINT32_C(2147483648)))\n\n";
    output << "int32_t MM[MM_BYTES / sizeof(int32_t)];\n";
    output << "int32_t Reg[REGISTER_COUNT];\n\n";
    if (runtime.put_integer)
    {
        output << "static int32_t R_put_i32(int32_t r0)\n{\n";
        output << "    return printf(\"%\" PRId32 \"\\n\", r0) < 0 ? INT32_C(0) : INT32_C(1);\n";
        output << "}\n\n";
    }
    output << "int main(void)\n{\n";
    output << "    " << register_slot(layout.exit) << " = INT32_C(0);\n";
    output << "    goto L_f0_b0;\n";

    std::size_t division_number = 0;
    for (const ir::BasicBlock &block : program->blocks)
    {
        output << "L_f0_b" << block.id.index << ":\n";
        for (const ir::Instruction &instruction : block.instructions)
        {
        if (const ir::Constant *constant = std::get_if<ir::Constant>(&instruction))
        {
            const ir::Value &result = program->values[constant->result.index];
            output << "    " << value_register(constant->result) << " = ";
            if (result.type.element_type == TYPE_INT)
            {
                output << int32_literal(std::get<int>(constant->payload));
            }
            else
            {
                output << (std::get<bool>(constant->payload) ? "INT32_C(1)" : "INT32_C(0)");
            }
            output << ";\n";
        }
        else if (const ir::Load *load = std::get_if<ir::Load>(&instruction))
        {
            output << "    " << value_register(load->result) << " = "
                   << storage_word(load->source) << ";\n";
        }
        else if (const ir::Store *store = std::get_if<ir::Store>(&instruction))
        {
            output << "    " << storage_word(store->destination) << " = "
                   << value_register(store->value) << ";\n";
        }
        else if (const ir::Unary *unary = std::get_if<ir::Unary>(&instruction))
        {
            const data_types type = program->values[unary->operand.index].type.element_type;
            output << "    " << value_register(unary->result) << " = ";
            if (unary->operation == ir::UnaryOp::Negate)
            {
                output << "I32_FROM_U32(UINT32_C(0) - (uint32_t)" << value_register(unary->operand)
                       << ")";
            }
            else if (type == TYPE_BOOL)
            {
                output << "(" << value_register(unary->operand)
                       << " == INT32_C(0)) ? INT32_C(1) : INT32_C(0)";
            }
            else
            {
                output << "I32_FROM_U32(~(uint32_t)" << value_register(unary->operand) << ")";
            }
            output << ";\n";
        }
        else if (const ir::Binary *binary = std::get_if<ir::Binary>(&instruction))
        {
            const data_types type = program->values[binary->left.index].type.element_type;
            if (binary->operation == ir::BinaryOp::Divide)
            {
                const std::string label_prefix = "L_f0_d" + std::to_string(division_number) + "_";
                const std::string zero_label = label_prefix + "0";
                const std::string overflow_label = label_prefix + "1";
                const std::string done_label = label_prefix + "2";
                output << "    " << register_slot(layout.division_zero) << " = ("
                       << value_register(binary->right)
                       << " == INT32_C(0)) ? INT32_C(1) : INT32_C(0);\n";
                output << "    if (" << register_slot(layout.division_zero) << ") goto "
                       << zero_label << ";\n";
                output << "    " << register_slot(layout.division_overflow) << " = ("
                       << value_register(binary->left) << " == INT32_MIN && "
                       << value_register(binary->right)
                       << " == (-INT32_C(1))) ? INT32_C(1) : INT32_C(0);\n";
                output << "    if (" << register_slot(layout.division_overflow) << ") goto "
                       << overflow_label << ";\n";
                output << "    " << value_register(binary->result) << " = "
                       << binary_expression(*binary, type) << ";\n";
                output << "    goto " << done_label << ";\n";
                output << zero_label << ":\n";
                output << "    " << register_slot(layout.exit) << " = INT32_C(1);\n";
                output << "    goto L_f0_x0;\n";
                output << overflow_label << ":\n";
                output << "    " << value_register(binary->result) << " = INT32_MIN;\n";
                output << done_label << ":\n";
                division_number++;
            }
            else
            {
                output << "    " << value_register(binary->result) << " = "
                       << binary_expression(*binary, type) << ";\n";
            }
        }
        else if (const ir::Cast *cast = std::get_if<ir::Cast>(&instruction))
        {
            output << "    " << value_register(cast->result) << " = ";
            if (cast->operation == ir::CastOp::IntToBool)
            {
                output << "(" << value_register(cast->operand)
                       << " != INT32_C(0)) ? INT32_C(1) : INT32_C(0)";
            }
            else
            {
                output << value_register(cast->operand);
            }
            output << ";\n";
        }
        else if (const ir::Call *call = std::get_if<ir::Call>(&instruction))
        {
            output << "    " << value_register(call->result) << " = R_put_i32("
                   << value_register(call->arguments[0]) << ");\n";
        }
        }
        const ir::Terminator &terminator = std::get<ir::Terminator>(block.terminator);
        if (const ir::JumpTerminator *jump = std::get_if<ir::JumpTerminator>(&terminator))
        {
            output << "    goto L_f0_b" << jump->target.index << ";\n";
        }
        else if (const ir::BranchTerminator *branch =
                     std::get_if<ir::BranchTerminator>(&terminator))
        {
            output << "    if (" << value_register(branch->condition) << ") goto L_f0_b"
                   << branch->when_true.index << ";\n";
            output << "    goto L_f0_b" << branch->when_false.index << ";\n";
        }
        else if (std::holds_alternative<ir::HaltTerminator>(terminator))
        {
            output << "    goto L_f0_x0;\n";
        }
    }
    output << "L_f0_x0:\n";
    output << "    return " << register_slot(layout.exit) << ";\n}\n";
    checked.text = output.str();
    return checked;
}

RestrictedCResult RestrictedCEmitter::emit_to_file(const ir::Module &module,
                                                    const std::filesystem::path &output) const
{
    RestrictedCResult rendered = emit(module);
    if (!rendered.succeeded())
    {
        return rendered;
    }
    if (output.empty())
    {
        return failure(RestrictedCStatus::IoError, "output path is empty");
    }

    std::error_code error;
    const std::filesystem::path parent = output.parent_path().empty() ?
                                               std::filesystem::path(".") : output.parent_path();
    if (!std::filesystem::exists(parent, error) || error ||
        !std::filesystem::is_directory(parent, error) || error)
    {
        return failure(RestrictedCStatus::IoError, "output parent directory is unavailable");
    }
    if (!is_direct_regular_or_missing(output, error))
    {
        return failure(RestrictedCStatus::IoError, error ?
            "cannot inspect output path" : "output path is not a direct regular file");
    }

    const std::filesystem::path temporary_directory = sibling_temp_directory(output, error);
    if (error || temporary_directory.empty())
    {
        return failure(RestrictedCStatus::IoError, "cannot reserve a sibling temporary output");
    }
    std::filesystem::permissions(temporary_directory, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::replace, error);
    if (error)
    {
        cleanup_temp_directory(temporary_directory);
        return failure(RestrictedCStatus::IoError, "cannot restrict temporary output directory");
    }
    const std::filesystem::path temporary = temporary_directory / "output.c";
    const std::filesystem::file_status temporary_status =
        std::filesystem::symlink_status(temporary, error);
    if (error == std::errc::no_such_file_or_directory)
    {
        error.clear();
    }
    if (error || temporary_status.type() != std::filesystem::file_type::not_found)
    {
        cleanup_temp_directory(temporary_directory);
        return failure(RestrictedCStatus::IoError, "temporary output file is not safely empty");
    }
    {
        std::ofstream stream(temporary, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!stream.is_open())
        {
            cleanup_temp_directory(temporary_directory);
            return failure(RestrictedCStatus::IoError, "cannot open temporary output");
        }
        stream << rendered.text;
        stream.close();
        if (!stream)
        {
            cleanup_temp_directory(temporary_directory);
            return failure(RestrictedCStatus::IoError, "cannot write temporary output");
        }
    }
    const std::filesystem::file_status written_status =
        std::filesystem::symlink_status(temporary, error);
    if (error || written_status.type() != std::filesystem::file_type::regular)
    {
        cleanup_temp_directory(temporary_directory);
        return failure(RestrictedCStatus::IoError, "temporary output file is not a regular file");
    }
    if (!is_direct_regular_or_missing(output, error))
    {
        cleanup_temp_directory(temporary_directory);
        return failure(RestrictedCStatus::IoError, error ?
            "cannot inspect output path" : "output path is not a direct regular file");
    }
    std::filesystem::rename(temporary, output, error);
    if (error)
    {
        cleanup_temp_directory(temporary_directory);
        return failure(RestrictedCStatus::IoError, "cannot replace output atomically");
    }
    std::error_code cleanup_error;
    std::filesystem::remove(temporary_directory, cleanup_error);
    return rendered;
}
