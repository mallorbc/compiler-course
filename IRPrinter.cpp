#include "IRPrinter.h"

#include <atomic>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <system_error>

namespace
{

IRPrintResult failure(IRPrintStatus status, const std::string &diagnostic)
{
    IRPrintResult result;
    result.status = status;
    result.diagnostic = diagnostic;
    return result;
}

const char *type_name(data_types type)
{
    switch (type)
    {
    case TYPE_NONE: return "none";
    case TYPE_INT: return "int";
    case TYPE_FLOAT: return "float";
    case TYPE_STRING: return "string";
    case TYPE_BOOL: return "bool";
    }
    return "invalid";
}

std::string shape_name(const value_shape &shape)
{
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << type_name(shape.element_type);
    if (shape.is_array)
    {
        output << "[0.." << shape.array_upper_bound << "]";
    }
    return output.str();
}

std::string escaped(const std::string &value)
{
    static const char hex[] = "0123456789abcdef";
    std::string output = "\"";
    for (unsigned char byte : value)
    {
        switch (byte)
        {
        case '\\': output += "\\\\"; break;
        case '"': output += "\\\""; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (byte >= 0x20U && byte <= 0x7eU)
            {
                output.push_back(static_cast<char>(byte));
            }
            else
            {
                output += "\\x";
                output.push_back(hex[(byte >> 4U) & 0x0fU]);
                output.push_back(hex[byte & 0x0fU]);
            }
            break;
        }
    }
    output += '"';
    return output;
}

std::string function_name(ir::FunctionId id)
{
    return "@f" + std::to_string(id.index);
}

std::string storage_name(ir::StorageId id)
{
    return "@s" + std::to_string(id.index);
}

std::string value_name(ir::ValueId id)
{
    return "%v" + std::to_string(id.index);
}

std::string block_name(ir::BlockId id)
{
    return "^b" + std::to_string(id.index);
}

const char *function_kind_name(ir::FunctionKind kind)
{
    switch (kind)
    {
    case ir::FunctionKind::Program: return "program";
    case ir::FunctionKind::Procedure: return "procedure";
    case ir::FunctionKind::ExternalBuiltin: return "external";
    }
    return "invalid";
}

const char *storage_kind_name(ir::StorageKind kind)
{
    switch (kind)
    {
    case ir::StorageKind::Global: return "global";
    case ir::StorageKind::Parameter: return "parameter";
    case ir::StorageKind::Local: return "local";
    }
    return "invalid";
}

const char *unary_name(ir::UnaryOp operation)
{
    switch (operation)
    {
    case ir::UnaryOp::Negate: return "negate";
    case ir::UnaryOp::Not: return "not";
    }
    return "invalid-unary";
}

const char *binary_name(ir::BinaryOp operation)
{
    switch (operation)
    {
    case ir::BinaryOp::Add: return "add";
    case ir::BinaryOp::Subtract: return "subtract";
    case ir::BinaryOp::Multiply: return "multiply";
    case ir::BinaryOp::Divide: return "divide";
    case ir::BinaryOp::Less: return "less";
    case ir::BinaryOp::LessEqual: return "less_equal";
    case ir::BinaryOp::Greater: return "greater";
    case ir::BinaryOp::GreaterEqual: return "greater_equal";
    case ir::BinaryOp::Equal: return "equal";
    case ir::BinaryOp::NotEqual: return "not_equal";
    case ir::BinaryOp::And: return "and";
    case ir::BinaryOp::Or: return "or";
    }
    return "invalid-binary";
}

const char *cast_name(ir::CastOp operation)
{
    switch (operation)
    {
    case ir::CastOp::IntToFloat: return "int_to_float";
    case ir::CastOp::FloatToInt: return "float_to_int";
    case ir::CastOp::BoolToInt: return "bool_to_int";
    case ir::CastOp::IntToBool: return "int_to_bool";
    }
    return "invalid-cast";
}

std::string payload_name(const ir::Constant &constant)
{
    if (const int *integer = std::get_if<int>(&constant.payload))
    {
        return std::to_string(*integer);
    }
    if (const float *floating = std::get_if<float>(&constant.payload))
    {
        std::ostringstream output;
        output.imbue(std::locale::classic());
        output << std::hexfloat << *floating;
        return output.str();
    }
    if (const bool *boolean = std::get_if<bool>(&constant.payload))
    {
        return *boolean ? "true" : "false";
    }
    return escaped(std::get<std::string>(constant.payload));
}

const value_shape &value_type(const ir::Function &function, ir::ValueId value)
{
    return function.values[value.index].type;
}

void print_result(std::ostringstream &output, const ir::Function &function,
                  ir::ValueId result)
{
    output << value_name(result) << " : " << shape_name(value_type(function, result)) << " = ";
}

void print_instruction(std::ostringstream &output, const ir::Function &function,
                       const ir::Instruction &instruction)
{
    output << "      ";
    if (const ir::Constant *constant = std::get_if<ir::Constant>(&instruction))
    {
        print_result(output, function, constant->result);
        output << "constant " << payload_name(*constant);
    }
    else if (const ir::Load *load = std::get_if<ir::Load>(&instruction))
    {
        print_result(output, function, load->result);
        output << "load " << storage_name(load->source);
    }
    else if (const ir::Store *store = std::get_if<ir::Store>(&instruction))
    {
        output << "store " << storage_name(store->destination) << ", "
               << value_name(store->value);
    }
    else if (const ir::CheckIndex *check = std::get_if<ir::CheckIndex>(&instruction))
    {
        print_result(output, function, check->result);
        output << "check_index " << storage_name(check->storage) << ", "
               << value_name(check->raw_index);
    }
    else if (const ir::ElementLoad *load = std::get_if<ir::ElementLoad>(&instruction))
    {
        print_result(output, function, load->result);
        output << "element_load " << storage_name(load->storage) << ", "
               << value_name(load->checked_index);
    }
    else if (const ir::ElementStore *store = std::get_if<ir::ElementStore>(&instruction))
    {
        output << "element_store " << storage_name(store->storage) << ", "
               << value_name(store->checked_index) << ", " << value_name(store->value);
    }
    else if (const ir::Unary *unary = std::get_if<ir::Unary>(&instruction))
    {
        print_result(output, function, unary->result);
        output << unary_name(unary->operation) << ' ' << value_name(unary->operand);
    }
    else if (const ir::Binary *binary = std::get_if<ir::Binary>(&instruction))
    {
        print_result(output, function, binary->result);
        output << binary_name(binary->operation) << ' ' << value_name(binary->left)
               << ", " << value_name(binary->right);
    }
    else if (const ir::Cast *cast = std::get_if<ir::Cast>(&instruction))
    {
        print_result(output, function, cast->result);
        output << cast_name(cast->operation) << ' ' << value_name(cast->operand);
    }
    else if (const ir::Call *call = std::get_if<ir::Call>(&instruction))
    {
        print_result(output, function, call->result);
        output << "call " << function_name(call->callee) << '(';
        for (std::size_t index = 0; index < call->arguments.size(); index++)
        {
            if (index != 0)
            {
                output << ", ";
            }
            output << value_name(call->arguments[index]);
        }
        output << ')';
    }
    output << '\n';
}

void print_terminator(std::ostringstream &output, const ir::BasicBlock &block)
{
    const ir::Terminator &terminator = std::get<ir::Terminator>(block.terminator);
    output << "      ";
    if (const ir::ReturnTerminator *returned = std::get_if<ir::ReturnTerminator>(&terminator))
    {
        output << "return " << value_name(returned->value);
    }
    else if (std::holds_alternative<ir::HaltTerminator>(terminator))
    {
        output << "halt";
    }
    else if (const ir::JumpTerminator *jump = std::get_if<ir::JumpTerminator>(&terminator))
    {
        output << "jump " << block_name(jump->target);
    }
    else if (const ir::BranchTerminator *branch = std::get_if<ir::BranchTerminator>(&terminator))
    {
        output << "branch " << value_name(branch->condition) << ", "
               << block_name(branch->when_true) << ", " << block_name(branch->when_false);
    }
    output << '\n';
}

std::filesystem::path sibling_temp_directory(const std::filesystem::path &output,
                                             std::error_code &error)
{
    static std::atomic<unsigned long> serial{0};
    const std::filesystem::path parent = output.parent_path().empty() ?
        std::filesystem::path(".") : output.parent_path();
    const std::string base = "." + output.filename().string() + ".ir-tmp-";
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
    std::filesystem::remove(directory / "output.ir", ignored);
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
    return status.type() == std::filesystem::file_type::not_found ||
           status.type() == std::filesystem::file_type::regular;
}

} // namespace

IRPrintResult IRPrinter::print(const ir::Module &module) const
{
    const ir::VerificationResult verified = ir::verify_module(module);
    if (!verified.valid)
    {
        return failure(IRPrintStatus::InvalidIR, verified.reason);
    }

    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << "module {\n";
    for (const ir::Storage &storage : module.storages)
    {
        output << "  storage " << storage_name(storage.id) << ' '
               << storage_kind_name(storage.kind) << " owner "
               << function_name(storage.owner) << " symbol("
               << storage.symbol.scope_id << ", " << escaped(storage.symbol.name)
               << ") : " << shape_name(storage.type) << '\n';
    }
    if (!module.storages.empty())
    {
        output << '\n';
    }

    for (std::size_t function_index = 0; function_index < module.functions.size();
         function_index++)
    {
        const ir::Function &function = module.functions[function_index];
        const bool external = function.kind == ir::FunctionKind::ExternalBuiltin;
        output << "  " << (external ? "declare " : "define ")
               << function_name(function.id) << ' ' << function_kind_name(function.kind)
               << " symbol(" << function.symbol.scope_id << ", "
               << escaped(function.symbol.name) << ") name " << escaped(function.name) << " (";
        for (std::size_t parameter = 0; parameter < function.parameter_types.size(); parameter++)
        {
            if (parameter != 0)
            {
                output << ", ";
            }
            if (!external)
            {
                output << storage_name(function.parameters[parameter]) << " : ";
            }
            output << shape_name(function.parameter_types[parameter]);
        }
        output << ") -> " << shape_name(function.return_type);
        if (external)
        {
            output << '\n';
        }
        else
        {
            output << " {\n";
            for (const ir::BasicBlock &block : function.blocks)
            {
                output << "    block " << block_name(block.id) << ":\n";
                for (const ir::Instruction &instruction : block.instructions)
                {
                    print_instruction(output, function, instruction);
                }
                print_terminator(output, block);
            }
            output << "  }\n";
        }
        if (function_index + 1U != module.functions.size())
        {
            output << '\n';
        }
    }
    output << "}\n";

    IRPrintResult result;
    result.status = IRPrintStatus::Success;
    result.text = output.str();
    return result;
}

IRPrintResult IRPrinter::print_to_file(const ir::Module &module,
                                       const std::filesystem::path &output) const
{
    IRPrintResult rendered = print(module);
    if (!rendered.succeeded())
    {
        return rendered;
    }
    if (output.empty())
    {
        return failure(IRPrintStatus::IoError, "output path is empty");
    }

    std::error_code error;
    const std::filesystem::path parent = output.parent_path().empty() ?
        std::filesystem::path(".") : output.parent_path();
    if (!std::filesystem::exists(parent, error) || error ||
        !std::filesystem::is_directory(parent, error) || error)
    {
        return failure(IRPrintStatus::IoError, "output parent directory is unavailable");
    }
    if (!is_direct_regular_or_missing(output, error))
    {
        return failure(IRPrintStatus::IoError, error ?
            "cannot inspect output path" : "output path is not a direct regular file");
    }

    const std::filesystem::path temporary_directory = sibling_temp_directory(output, error);
    if (error || temporary_directory.empty())
    {
        return failure(IRPrintStatus::IoError, "cannot reserve a sibling temporary output");
    }
    std::filesystem::permissions(temporary_directory, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::replace, error);
    if (error)
    {
        cleanup_temp_directory(temporary_directory);
        return failure(IRPrintStatus::IoError, "cannot restrict temporary output directory");
    }
    const std::filesystem::path temporary = temporary_directory / "output.ir";
    const std::filesystem::file_status temporary_status =
        std::filesystem::symlink_status(temporary, error);
    if (error == std::errc::no_such_file_or_directory)
    {
        error.clear();
    }
    if (error || temporary_status.type() != std::filesystem::file_type::not_found)
    {
        cleanup_temp_directory(temporary_directory);
        return failure(IRPrintStatus::IoError, "temporary output file is not safely empty");
    }
    {
        std::ofstream stream(temporary, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!stream.is_open())
        {
            cleanup_temp_directory(temporary_directory);
            return failure(IRPrintStatus::IoError, "cannot open temporary output");
        }
        stream << rendered.text;
        stream.close();
        if (!stream)
        {
            cleanup_temp_directory(temporary_directory);
            return failure(IRPrintStatus::IoError, "cannot write temporary output");
        }
    }
    const std::filesystem::file_status written_status =
        std::filesystem::symlink_status(temporary, error);
    if (error || written_status.type() != std::filesystem::file_type::regular)
    {
        cleanup_temp_directory(temporary_directory);
        return failure(IRPrintStatus::IoError, "temporary output file is not a regular file");
    }
    if (!is_direct_regular_or_missing(output, error))
    {
        cleanup_temp_directory(temporary_directory);
        return failure(IRPrintStatus::IoError, error ?
            "cannot inspect output path" : "output path is not a direct regular file");
    }
    std::filesystem::rename(temporary, output, error);
    if (error)
    {
        cleanup_temp_directory(temporary_directory);
        return failure(IRPrintStatus::IoError, "cannot replace output atomically");
    }
    std::error_code cleanup_error;
    std::filesystem::remove(temporary_directory, cleanup_error);
    return rendered;
}
