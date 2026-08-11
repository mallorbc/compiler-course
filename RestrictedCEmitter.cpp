#include "RestrictedCEmitter.h"

#include "BuiltinCatalog.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
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
    bool procedures = false;
    bool procedure_mode = false;
    std::vector<bool> reachable_functions;
};

struct ProcedureFrame
{
    std::size_t words = 0;
    std::vector<std::size_t> parameter_offsets;
    std::vector<std::size_t> local_offsets;
    std::vector<std::size_t> value_offsets;
};

struct ProcedureRegisterLayout
{
    std::size_t count = 0;
    std::size_t exit = 0;
    std::size_t temporary_a = 0;
    std::size_t temporary_b = 0;
    std::size_t temporary_c = 0;
    std::size_t division_zero = 0;
    std::size_t division_overflow = 0;
    std::size_t return_value = 0;
    std::size_t return_site = 0;
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

//This deliberately lives entirely in the restricted-C backend.  The IR knows
//only normal calls/returns and canonical storage ownership; frame offsets and
//the flat-C continuation ABI are not frontend or IR concepts.
RestrictedCResult preflight_with_procedures(const ir::Module &module,
                                            const ir::Function *&program,
                                            RuntimeRequirements &runtime)
{
    if (module.functions.empty() || module.functions[0].kind != ir::FunctionKind::Program ||
        module.functions[0].id != ir::FunctionId(0))
    {
        return failure(RestrictedCStatus::Unsupported,
                       "restricted C requires a verified Program function f0");
    }
    program = &module.functions[0];
    std::size_t global_count = 0;
    for (const ir::Storage &storage : module.storages)
    {
        global_count += storage.kind == ir::StorageKind::Global ? 1U : 0U;
    }
    if (!RestrictedCEmitter::storage_count_fits_memory(global_count))
    {
        return failure(RestrictedCStatus::Unsupported,
                       "restricted C global storage exceeds the fixed memory capacity");
    }
    const BuiltinSpec *put_integer = find_builtin(BuiltinId::PutInteger);
    if (put_integer == NULL)
    {
        return failure(RestrictedCStatus::InvalidIR, "builtin catalog is incomplete");
    }
    runtime.reachable_functions.assign(module.functions.size(), false);
    runtime.reachable_functions[0] = true;
    std::vector<std::size_t> pending{0};
    while (!pending.empty())
    {
        const std::size_t function_index = pending.back();
        pending.pop_back();
        const ir::Function &function = module.functions[function_index];
        for (const ir::BasicBlock &block : function.blocks)
        {
            for (const ir::Instruction &instruction : block.instructions)
            {
                const ir::Call *call = std::get_if<ir::Call>(&instruction);
                if (call == NULL)
                {
                    continue;
                }
                const ir::Function *callee = function_for(module, call->callee);
                if (callee == NULL)
                {
                    return failure(RestrictedCStatus::InvalidIR,
                                   "restricted C call has no canonical callee");
                }
                if (callee->kind == ir::FunctionKind::Procedure &&
                    !runtime.reachable_functions[callee->id.index])
                {
                    runtime.reachable_functions[callee->id.index] = true;
                    pending.push_back(callee->id.index);
                }
            }
        }
    }
    for (const ir::Function &function : module.functions)
    {
        runtime.procedure_mode = runtime.procedure_mode ||
                                 function.kind == ir::FunctionKind::Procedure;
        if (function.kind == ir::FunctionKind::ExternalBuiltin ||
            !runtime.reachable_functions[function.id.index])
        {
            continue;
        }
        if (function.kind != ir::FunctionKind::Program &&
            function.kind != ir::FunctionKind::Procedure)
        {
            return failure(RestrictedCStatus::Unsupported,
                           "restricted C encountered an unknown defined function");
        }
        if (function.kind == ir::FunctionKind::Procedure)
        {
            runtime.procedures = true;
            if (!supported_shape(function.return_type))
            {
                return failure(RestrictedCStatus::Unsupported,
                               "restricted C supports scalar Integer and Bool procedure returns only");
            }
        }
        for (const value_shape &shape : function.parameter_types)
        {
            if (!supported_shape(shape))
            {
                return failure(RestrictedCStatus::Unsupported,
                               "restricted C supports scalar Integer and Bool parameters only");
            }
        }
        for (const ir::Value &value : function.values)
        {
            if (!supported_shape(value.type))
            {
                return failure(RestrictedCStatus::Unsupported,
                               "restricted C supports scalar Integer and Bool values only");
            }
        }
        for (const ir::BasicBlock &block : function.blocks)
        {
            for (const ir::Instruction &instruction : block.instructions)
            {
                if (const ir::Call *call = std::get_if<ir::Call>(&instruction))
                {
                    const ir::Function *callee = function_for(module, call->callee);
                    if (callee->kind == ir::FunctionKind::Procedure)
                    {
                        continue;
                    }
                    const SymbolRef put_reference{0, put_integer->spelling};
                    if (callee->kind != ir::FunctionKind::ExternalBuiltin ||
                        callee->symbol != put_reference ||
                        callee->return_type != put_integer->return_shape ||
                        callee->parameter_types != put_integer->parameter_shapes)
                    {
                        return failure(RestrictedCStatus::Unsupported,
                                       "restricted C only lowers canonical putInteger external calls");
                    }
                    runtime.put_integer = true;
                }
            }
        }
    }
    for (const ir::Storage &storage : module.storages)
    {
        if ((storage.kind == ir::StorageKind::Global ||
             runtime.reachable_functions[storage.owner.index]) &&
            !supported_shape(storage.type))
        {
            return failure(RestrictedCStatus::Unsupported,
                           "restricted C supports scalar Integer and Bool storage only");
        }
    }
    RestrictedCResult result;
    result.status = RestrictedCStatus::Success;
    return result;
}

RestrictedCResult preflight(const ir::Module &module, const ir::Function *&program,
                            RuntimeRequirements &runtime)
{
    const ir::VerificationResult verified = ir::verify_module(module);
    if (!verified.valid)
    {
        return failure(RestrictedCStatus::InvalidIR, verified.reason);
    }
    bool contains_procedure = false;
    for (const ir::Function &function : module.functions)
    {
        contains_procedure = contains_procedure || function.kind == ir::FunctionKind::Procedure;
    }
    if (contains_procedure)
    {
        RestrictedCResult procedure_checked = preflight_with_procedures(module, program, runtime);
        if (!procedure_checked.succeeded() || runtime.procedure_mode)
        {
            return procedure_checked;
        }
        //No user procedure is reachable from Program.  Retain the original
        //straight-line preflight below while ignoring declarations that have
        //no emitted labels or runtime footprint.
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
        if (function.kind == ir::FunctionKind::Procedure)
        {
            continue;
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
    for (const ir::Storage &storage : module.storages)
    {
        if (storage.kind != ir::StorageKind::Global)
        {
            continue;
        }
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

std::string binary_expression_text(ir::BinaryOp operation, data_types type,
                                   const std::string &left, const std::string &right)
{
    switch (operation)
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

std::string binary_expression(const ir::Binary &binary, data_types type)
{
    return binary_expression_text(binary.operation, type, value_register(binary.left),
                                  value_register(binary.right));
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

bool make_procedure_frames(const ir::Module &module, const std::vector<bool> &reachable,
                           std::vector<ProcedureFrame> &frames, std::size_t static_words)
{
    frames.assign(module.functions.size(), ProcedureFrame{});
    if (static_words > RestrictedCEmitter::memory_word_capacity())
    {
        return false;
    }
    for (const ir::Function &function : module.functions)
    {
        if (function.kind != ir::FunctionKind::Procedure || !reachable[function.id.index])
        {
            continue;
        }
        ProcedureFrame &frame = frames[function.id.index];
        frame.words = 4U; //old SP, old FP, continuation, result-word address
        for (ir::StorageId ignored : function.parameters)
        {
            (void)ignored;
            frame.parameter_offsets.push_back(frame.words++);
        }
        for (ir::StorageId ignored : function.locals)
        {
            (void)ignored;
            frame.local_offsets.push_back(frame.words++);
        }
        for (std::size_t value = 0; value < function.values.size(); value++)
        {
            frame.value_offsets.push_back(frame.words++);
        }
        if (frame.words > RestrictedCEmitter::memory_word_capacity() - static_words)
        {
            return false;
        }
    }
    return true;
}

bool make_procedure_register_layout(const ir::Function &program,
                                    ProcedureRegisterLayout &layout)
{
    const std::size_t reserved = 2U;
    const std::size_t extra = 8U;
    const std::size_t maximum = static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max());
    if (program.values.size() > maximum || program.values.size() > maximum - reserved - extra)
    {
        return false;
    }
    layout.exit = program.values.size() + reserved;
    layout.temporary_a = layout.exit + 1U;
    layout.temporary_b = layout.exit + 2U;
    layout.temporary_c = layout.exit + 3U;
    layout.division_zero = layout.exit + 4U;
    layout.division_overflow = layout.exit + 5U;
    layout.return_value = layout.exit + 6U;
    layout.return_site = layout.exit + 7U;
    layout.count = layout.exit + 8U;
    return true;
}

std::size_t frame_storage_offset(const ir::Function &function, const ProcedureFrame &frame,
                                 ir::StorageId storage)
{
    for (std::size_t index = 0; index < function.parameters.size(); index++)
    {
        if (function.parameters[index] == storage)
        {
            return frame.parameter_offsets[index];
        }
    }
    for (std::size_t index = 0; index < function.locals.size(); index++)
    {
        if (function.locals[index] == storage)
        {
            return frame.local_offsets[index];
        }
    }
    return std::numeric_limits<std::size_t>::max();
}

RestrictedCResult emit_with_procedures(const ir::Module &module, const ir::Function &program,
                                       const RuntimeRequirements &runtime)
{
    std::vector<std::size_t> global_words(module.storages.size(),
                                           std::numeric_limits<std::size_t>::max());
    std::size_t static_words = 0;
    for (const ir::Storage &storage : module.storages)
    {
        if (storage.kind == ir::StorageKind::Global)
        {
            if (static_words == RestrictedCEmitter::memory_word_capacity())
            {
                return failure(RestrictedCStatus::Unsupported,
                               "restricted C globals exceed fixed memory capacity");
            }
            global_words[storage.id.index] = static_words++;
        }
    }
    std::vector<std::size_t> program_spills(program.values.size(),
                                            std::numeric_limits<std::size_t>::max());
    for (const ir::BasicBlock &block : program.blocks)
    {
        for (const ir::Instruction &instruction : block.instructions)
        {
            const ir::Call *call = std::get_if<ir::Call>(&instruction);
            if (call != NULL && module.functions[call->callee.index].kind ==
                                    ir::FunctionKind::Procedure)
            {
                if (static_words == RestrictedCEmitter::memory_word_capacity())
                {
                    return failure(RestrictedCStatus::Unsupported,
                                   "restricted C Program call spills exceed fixed memory capacity");
                }
                program_spills[call->result.index] = static_words++;
            }
        }
    }
    std::vector<ProcedureFrame> frames;
    if (!make_procedure_frames(module, runtime.reachable_functions, frames, static_words))
    {
        return failure(RestrictedCStatus::Unsupported,
                       "restricted C static/frame layout exceeds fixed memory capacity");
    }
    ProcedureRegisterLayout layout;
    if (!make_procedure_register_layout(program, layout))
    {
        return failure(RestrictedCStatus::Unsupported,
                       "restricted C register model cannot represent this many values");
    }

    const auto word = [](std::size_t index) {
        return std::string("MM[") + std::to_string(index) + "u]";
    };
    const auto frame_word = [&word](std::size_t offset) {
        return std::string("MM[(uint32_t)Reg[1u] + ") + std::to_string(offset) + "u]";
    };
    const auto value_read = [&frames, &frame_word](const ir::Function &function, ir::ValueId value) {
        if (function.kind == ir::FunctionKind::Program)
        {
            return value_register(value);
        }
        return frame_word(frames[function.id.index].value_offsets[value.index]);
    };
    const auto storage_read = [&module, &frames, &global_words, &word, &frame_word](const ir::Function &function,
                                                                        ir::StorageId storage) {
        const ir::Storage &entry = module.storages[storage.index];
        if (entry.kind == ir::StorageKind::Global)
        {
            return word(global_words[storage.index]);
        }
        return frame_word(frame_storage_offset(function, frames[function.id.index], storage));
    };
    const auto value_write = [&frames, &frame_word](const ir::Function &function, ir::ValueId value) {
        if (function.kind == ir::FunctionKind::Program)
        {
            return value_register(value);
        }
        return frame_word(frames[function.id.index].value_offsets[value.index]);
    };
    const auto result_address = [&frames, &program_spills](const ir::Function &function,
                                                             ir::ValueId value) {
        if (function.kind == ir::FunctionKind::Program)
        {
            return std::to_string(program_spills[value.index]) + "u";
        }
        return std::string("((uint32_t)Reg[1u] + ") +
               std::to_string(frames[function.id.index].value_offsets[value.index]) + "u)";
    };

    std::ostringstream output;
    output << "#include <stdint.h>\n";
    if (runtime.put_integer)
    {
        output << "#include <inttypes.h>\n#include <stdio.h>\n";
    }
    output << "\n#define MM_BYTES (" << RestrictedCEmitter::memory_byte_capacity() << "u)\n";
    output << "#define MM_WORDS (MM_BYTES / sizeof(int32_t))\n";
    output << "#define REGISTER_COUNT " << layout.count << "u\n\n";
    output << "#define I32_FROM_U32(value) ((value) <= UINT32_C(2147483647) ? "
           << "(int32_t)(value) : INT32_MIN + (int32_t)((uint32_t)(value) - "
           << "UINT32_C(2147483648)))\n\n";
    output << "int32_t MM[MM_WORDS];\nint32_t Reg[REGISTER_COUNT];\n\n";
    if (runtime.put_integer)
    {
        output << "static int32_t R_put_i32(int32_t r0)\n{\n"
               << "    return printf(\"%\" PRId32 \"\\n\", r0) < 0 ? INT32_C(0) : INT32_C(1);\n}\n\n";
    }
    output << "int main(void)\n{\n";
    output << "    " << register_slot(layout.exit) << " = INT32_C(0);\n";
    output << "    Reg[0u] = INT32_C(" << static_words << ");\n";
    output << "    Reg[1u] = INT32_C(0);\n";
    output << "    goto L_f0_b0;\n";

    std::size_t division_number = 0;
    std::size_t continuation_number = 0;
    std::vector<std::string> continuations;
    for (const ir::Function &function : module.functions)
    {
        if (function.kind == ir::FunctionKind::ExternalBuiltin ||
            !runtime.reachable_functions[function.id.index])
        {
            continue;
        }
        for (const ir::BasicBlock &block : function.blocks)
        {
            output << "L_f" << function.id.index << "_b" << block.id.index << ":\n";
            const bool procedure_function = function.kind == ir::FunctionKind::Procedure;
            for (const ir::Instruction &instruction : block.instructions)
            {
                if (const ir::Constant *constant = std::get_if<ir::Constant>(&instruction))
                {
                    const data_types type = function.values[constant->result.index].type.element_type;
                    const std::string literal = type == TYPE_INT ? int32_literal(std::get<int>(constant->payload)) :
                        (std::get<bool>(constant->payload) ? "INT32_C(1)" : "INT32_C(0)");
                    if (procedure_function)
                    {
                        output << "    " << register_slot(layout.temporary_a) << " = " << literal << ";\n";
                        output << "    " << value_write(function, constant->result) << " = "
                               << register_slot(layout.temporary_a) << ";\n";
                    }
                    else
                    {
                        output << "    " << value_write(function, constant->result) << " = " << literal << ";\n";
                    }
                }
                else if (const ir::Load *load = std::get_if<ir::Load>(&instruction))
                {
                    if (procedure_function)
                    {
                        output << "    " << register_slot(layout.temporary_a) << " = "
                               << storage_read(function, load->source) << ";\n";
                        output << "    " << value_write(function, load->result) << " = "
                               << register_slot(layout.temporary_a) << ";\n";
                    }
                    else
                    {
                        output << "    " << value_write(function, load->result) << " = "
                               << storage_read(function, load->source) << ";\n";
                    }
                }
                else if (const ir::Store *store = std::get_if<ir::Store>(&instruction))
                {
                    if (procedure_function)
                    {
                        output << "    " << register_slot(layout.temporary_a) << " = "
                               << value_read(function, store->value) << ";\n";
                        output << "    " << storage_read(function, store->destination) << " = "
                               << register_slot(layout.temporary_a) << ";\n";
                    }
                    else
                    {
                        output << "    " << storage_read(function, store->destination) << " = "
                               << value_read(function, store->value) << ";\n";
                    }
                }
                else if (const ir::Unary *unary = std::get_if<ir::Unary>(&instruction))
                {
                    const std::string operand = value_read(function, unary->operand);
                    const data_types type = function.values[unary->operand.index].type.element_type;
                    const std::string result = procedure_function ? register_slot(layout.temporary_b) :
                                                                    value_write(function, unary->result);
                    if (procedure_function)
                    {
                        output << "    " << register_slot(layout.temporary_a) << " = " << operand << ";\n";
                    }
                    const std::string staged_operand = procedure_function ? register_slot(layout.temporary_a) : operand;
                    output << "    " << result << " = ";
                    if (unary->operation == ir::UnaryOp::Negate)
                    {
                        output << "I32_FROM_U32(UINT32_C(0) - (uint32_t)" << staged_operand << ")";
                    }
                    else if (type == TYPE_BOOL)
                    {
                        output << "(" << staged_operand << " == INT32_C(0)) ? INT32_C(1) : INT32_C(0)";
                    }
                    else
                    {
                        output << "I32_FROM_U32(~(uint32_t)" << staged_operand << ")";
                    }
                    output << ";\n";
                    if (procedure_function)
                    {
                        output << "    " << value_write(function, unary->result) << " = "
                               << register_slot(layout.temporary_b) << ";\n";
                    }
                }
                else if (const ir::Binary *binary = std::get_if<ir::Binary>(&instruction))
                {
                    const std::string left = procedure_function ? register_slot(layout.temporary_a) :
                                                                   value_read(function, binary->left);
                    const std::string right = procedure_function ? register_slot(layout.temporary_b) :
                                                                    value_read(function, binary->right);
                    const std::string result = procedure_function ? register_slot(layout.temporary_c) :
                                                                     value_write(function, binary->result);
                    const data_types type = function.values[binary->left.index].type.element_type;
                    if (procedure_function)
                    {
                        output << "    " << left << " = " << value_read(function, binary->left) << ";\n";
                        output << "    " << right << " = " << value_read(function, binary->right) << ";\n";
                    }
                    if (binary->operation == ir::BinaryOp::Divide)
                    {
                        const std::string prefix = "L_f" + std::to_string(function.id.index) + "_d" +
                                                   std::to_string(division_number++) + "_";
                        output << "    " << register_slot(layout.division_zero) << " = (" << right
                               << " == INT32_C(0)) ? INT32_C(1) : INT32_C(0);\n";
                        output << "    if (" << register_slot(layout.division_zero) << ") goto "
                               << prefix << "0;\n";
                        output << "    " << register_slot(layout.division_overflow) << " = (" << left
                               << " == INT32_MIN && " << right
                               << " == (-INT32_C(1))) ? INT32_C(1) : INT32_C(0);\n";
                        output << "    if (" << register_slot(layout.division_overflow) << ") goto "
                               << prefix << "1;\n";
                        output << "    " << result << " = "
                               << binary_expression_text(binary->operation, type, left, right) << ";\n";
                        output << "    goto " << prefix << "2;\n";
                        output << prefix << "0:\n    " << register_slot(layout.exit)
                               << " = INT32_C(1);\n    goto L_f0_x0;\n";
                        output << prefix << "1:\n    " << result << " = INT32_MIN;\n";
                        output << prefix << "2:\n";
                    }
                    else
                    {
                        output << "    " << result << " = "
                               << binary_expression_text(binary->operation, type, left, right) << ";\n";
                    }
                    if (procedure_function)
                    {
                        output << "    " << value_write(function, binary->result) << " = "
                               << register_slot(layout.temporary_c) << ";\n";
                    }
                }
                else if (const ir::Cast *cast = std::get_if<ir::Cast>(&instruction))
                {
                    const std::string operand = procedure_function ? register_slot(layout.temporary_a) :
                                                                      value_read(function, cast->operand);
                    const std::string result = procedure_function ? register_slot(layout.temporary_b) :
                                                                     value_write(function, cast->result);
                    if (procedure_function)
                    {
                        output << "    " << operand << " = " << value_read(function, cast->operand) << ";\n";
                    }
                    output << "    " << result << " = ";
                    if (cast->operation == ir::CastOp::IntToBool)
                    {
                        output << "(" << operand
                               << " != INT32_C(0)) ? INT32_C(1) : INT32_C(0)";
                    }
                    else
                    {
                        output << operand;
                    }
                    output << ";\n";
                    if (procedure_function)
                    {
                        output << "    " << value_write(function, cast->result) << " = "
                               << register_slot(layout.temporary_b) << ";\n";
                    }
                }
                else if (const ir::Call *call = std::get_if<ir::Call>(&instruction))
                {
                    const ir::Function &callee = module.functions[call->callee.index];
                    if (callee.kind == ir::FunctionKind::ExternalBuiltin)
                    {
                        if (procedure_function)
                        {
                            output << "    " << register_slot(layout.temporary_a) << " = "
                                   << value_read(function, call->arguments[0]) << ";\n";
                            output << "    " << register_slot(layout.temporary_b) << " = R_put_i32("
                                   << register_slot(layout.temporary_a) << ");\n";
                            output << "    " << value_write(function, call->result) << " = "
                                   << register_slot(layout.temporary_b) << ";\n";
                        }
                        else
                        {
                            output << "    " << value_write(function, call->result) << " = R_put_i32("
                                   << value_read(function, call->arguments[0]) << ");\n";
                        }
                    }
                    else
                    {
                        const ProcedureFrame &callee_frame = frames[callee.id.index];
                        const std::size_t continuation = continuation_number++;
                        continuations.push_back("L_f" + std::to_string(function.id.index) + "_c" +
                                                std::to_string(continuation));
                        output << "    " << register_slot(layout.temporary_a) << " = ((uint32_t)Reg[0u] > "
                               << "MM_WORDS - " << callee_frame.words
                               << "u) ? INT32_C(1) : INT32_C(0);\n";
                        output << "    if (" << register_slot(layout.temporary_a) << ") goto L_f0_s0;\n";
                        for (std::size_t argument = 0; argument < call->arguments.size(); argument++)
                        {
                            output << "    " << register_slot(layout.temporary_b) << " = "
                                   << value_read(function, call->arguments[argument]) << ";\n";
                            output << "    MM[(uint32_t)Reg[0u] + "
                                   << callee_frame.parameter_offsets[argument] << "u] = "
                                   << register_slot(layout.temporary_b) << ";\n";
                        }
                        output << "    MM[(uint32_t)Reg[0u]] = Reg[0u];\n";
                        output << "    MM[(uint32_t)Reg[0u] + 1u] = Reg[1u];\n";
                        output << "    MM[(uint32_t)Reg[0u] + 2u] = INT32_C(" << continuation << ");\n";
                        output << "    " << register_slot(layout.temporary_c) << " = "
                               << result_address(function, call->result) << ";\n";
                        output << "    MM[(uint32_t)Reg[0u] + 3u] = "
                               << register_slot(layout.temporary_c) << ";\n";
                        for (std::size_t offset = 4U + callee_frame.parameter_offsets.size();
                             offset < callee_frame.words; offset++)
                        {
                            output << "    MM[(uint32_t)Reg[0u] + " << offset << "u] = INT32_C(0);\n";
                        }
                        output << "    Reg[1u] = Reg[0u];\n";
                        output << "    Reg[0u] = I32_FROM_U32((uint32_t)Reg[0u] + "
                               << callee_frame.words << "u);\n";
                        output << "    goto L_f" << callee.id.index << "_b0;\n";
                        output << continuations.back() << ":\n";
                        if (function.kind == ir::FunctionKind::Program)
                        {
                            output << "    " << register_slot(layout.temporary_b) << " = "
                                   << word(program_spills[call->result.index])
                                   << ";\n";
                            output << "    " << value_write(function, call->result) << " = "
                                   << register_slot(layout.temporary_b) << ";\n";
                        }
                    }
                }
            }
            const ir::Terminator &terminator = std::get<ir::Terminator>(block.terminator);
            if (const ir::JumpTerminator *jump = std::get_if<ir::JumpTerminator>(&terminator))
            {
                output << "    goto L_f" << function.id.index << "_b" << jump->target.index << ";\n";
            }
            else if (const ir::BranchTerminator *branch = std::get_if<ir::BranchTerminator>(&terminator))
            {
                if (procedure_function)
                {
                    output << "    " << register_slot(layout.temporary_a) << " = "
                           << value_read(function, branch->condition) << ";\n";
                }
                output << "    if (" << (procedure_function ? register_slot(layout.temporary_a) :
                                                           value_read(function, branch->condition)) << ") goto L_f"
                       << function.id.index << "_b" << branch->when_true.index << ";\n";
                output << "    goto L_f" << function.id.index << "_b" << branch->when_false.index << ";\n";
            }
            else if (const ir::ReturnTerminator *returned = std::get_if<ir::ReturnTerminator>(&terminator))
            {
                output << "    " << register_slot(layout.temporary_a) << " = "
                       << value_read(function, returned->value) << ";\n";
                output << "    " << register_slot(layout.return_value) << " = "
                       << register_slot(layout.temporary_a) << ";\n";
                output << "    " << register_slot(layout.return_site) << " = " << frame_word(2U) << ";\n";
                output << "    " << register_slot(layout.temporary_c) << " = " << frame_word(3U) << ";\n";
                output << "    MM[(uint32_t)" << register_slot(layout.temporary_c) << "] = "
                       << register_slot(layout.return_value) << ";\n";
                output << "    " << register_slot(layout.temporary_a) << " = " << frame_word(0U) << ";\n";
                output << "    " << register_slot(layout.temporary_b) << " = " << frame_word(1U) << ";\n";
                output << "    Reg[0u] = " << register_slot(layout.temporary_a) << ";\n";
                output << "    Reg[1u] = " << register_slot(layout.temporary_b) << ";\n";
                output << "    goto L_f0_r0;\n";
            }
            else
            {
                output << "    goto L_f0_x0;\n";
            }
        }
    }
    if (runtime.procedures)
    {
        output << "L_f0_r0:\n";
        for (std::size_t continuation = 0; continuation < continuations.size(); continuation++)
        {
            output << "    " << register_slot(layout.temporary_a) << " = ("
                   << register_slot(layout.return_site) << " == INT32_C(" << continuation
                   << ")) ? INT32_C(1) : INT32_C(0);\n";
            output << "    if (" << register_slot(layout.temporary_a) << ") goto "
                   << continuations[continuation] << ";\n";
        }
        output << "    " << register_slot(layout.exit) << " = INT32_C(1);\n    goto L_f0_x0;\n";
        output << "L_f0_s0:\n    " << register_slot(layout.exit)
               << " = INT32_C(1);\n    goto L_f0_x0;\n";
    }
    output << "L_f0_x0:\n    return " << register_slot(layout.exit) << ";\n}\n";
    RestrictedCResult result;
    result.status = RestrictedCStatus::Success;
    result.text = output.str();
    return result;
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

    if (runtime.procedure_mode)
    {
        return emit_with_procedures(module, *program, runtime);
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
