#include "IRBuilder.h"

#include <unordered_set>

namespace ir
{
namespace
{

bool binary_returns_bool(BinaryOp operation)
{
    return operation == BinaryOp::Less || operation == BinaryOp::LessEqual ||
           operation == BinaryOp::Greater || operation == BinaryOp::GreaterEqual ||
           operation == BinaryOp::Equal || operation == BinaryOp::NotEqual;
}

bool valid_unary(UnaryOp operation, data_types type)
{
    return (operation == UnaryOp::Negate && (type == TYPE_INT || type == TYPE_FLOAT)) ||
           (operation == UnaryOp::Not && (type == TYPE_INT || type == TYPE_BOOL));
}

bool valid_binary(BinaryOp operation, data_types type)
{
    switch (operation)
    {
    case BinaryOp::Add:
    case BinaryOp::Subtract:
    case BinaryOp::Multiply:
    case BinaryOp::Divide:
        return type == TYPE_INT || type == TYPE_FLOAT;
    case BinaryOp::And:
    case BinaryOp::Or:
        return type == TYPE_INT || type == TYPE_BOOL;
    case BinaryOp::Less:
    case BinaryOp::LessEqual:
    case BinaryOp::Greater:
    case BinaryOp::GreaterEqual:
        return type == TYPE_INT || type == TYPE_FLOAT || type == TYPE_BOOL;
    case BinaryOp::Equal:
    case BinaryOp::NotEqual:
        return type != TYPE_NONE;
    }
    return false;
}

bool cast_matches(CastOp operation, const value_shape &source, value_shape &target)
{
    if (!is_ready_type(source))
    {
        return false;
    }
    target = source;
    if (operation == CastOp::IntToFloat && source.element_type == TYPE_INT)
    {
        target.element_type = TYPE_FLOAT;
        return true;
    }
    if (operation == CastOp::FloatToInt && source.element_type == TYPE_FLOAT)
    {
        target.element_type = TYPE_INT;
        return true;
    }
    if (operation == CastOp::BoolToInt && source.element_type == TYPE_BOOL)
    {
        target.element_type = TYPE_INT;
        return true;
    }
    if (operation == CastOp::IntToBool && source.element_type == TYPE_INT)
    {
        target.element_type = TYPE_BOOL;
        return true;
    }
    return false;
}

} // namespace

IRBuilder::IRBuilder()
{
}

void IRBuilder::mark_frontend_error()
{
    saw_frontend_error = true;
}

void IRBuilder::mark_unsupported(const std::string &reason_text)
{
    if (!saw_unsupported)
    {
        unsupported_reason = reason_text;
    }
    saw_unsupported = true;
}

void IRBuilder::mark_invalid(const std::string &reason_text)
{
    if (!saw_invalid)
    {
        invalid_reason = reason_text;
    }
    saw_invalid = true;
}

bool IRBuilder::emission_enabled() const noexcept
{
    return !saw_frontend_error && !saw_unsupported && !saw_invalid &&
           final_status == ModuleStatus::Unfinalized;
}

FunctionId IRBuilder::register_program(const SymbolRef &symbol, const std::string &name)
{
    if (!emission_enabled())
    {
        return FunctionId();
    }
    if (symbol.scope_id != 0 || symbol.name.empty() || name != symbol.name ||
        program_id.valid() || functions_by_symbol.find(symbol) != functions_by_symbol.end() ||
        storages_by_symbol.find(symbol) != storages_by_symbol.end())
    {
        mark_invalid("program registration is not unique");
        return FunctionId();
    }
    Function function;
    function.id = FunctionId(static_cast<std::uint32_t>(scratch_module.functions.size()));
    function.kind = FunctionKind::Program;
    function.name = name;
    function.symbol = symbol;
    //The program is a terminator-only root, not a synthetic Bool procedure.
    function.return_type = value_shape();
    BasicBlock block;
    block.id = BlockId(function.id, 0);
    function.blocks.push_back(block);
    scratch_module.functions.push_back(function);
    program_id = function.id;
    functions_by_symbol.emplace(symbol, function.id);
    function_context.push_back(function.id);
    return function.id;
}

void IRBuilder::seed_external_builtins()
{
    if (!emission_enabled())
    {
        return;
    }
    for (const BuiltinSpec &spec : builtin_catalog())
    {
        const SymbolRef reference{0, spec.spelling};
        if (functions_by_symbol.find(reference) != functions_by_symbol.end() ||
            storages_by_symbol.find(reference) != storages_by_symbol.end())
        {
            mark_invalid("builtin function registration is duplicated");
            return;
        }
        Function function;
        function.id = FunctionId(static_cast<std::uint32_t>(scratch_module.functions.size()));
        function.kind = FunctionKind::ExternalBuiltin;
        function.name = spec.spelling;
        function.symbol = reference;
        function.return_type = spec.return_shape;
        function.parameter_types = spec.parameter_shapes;
        scratch_module.functions.push_back(function);
        functions_by_symbol.emplace(reference, function.id);
    }
}

FunctionId IRBuilder::register_procedure(
    const SymbolRef &symbol, const std::string &name, const value_shape &return_type,
    const std::vector<std::pair<SymbolRef, value_shape>> &parameters)
{
    if (!emission_enabled())
    {
        return FunctionId();
    }
    if (symbol.scope_id < 0 || symbol.name.empty() || name != symbol.name ||
        functions_by_symbol.find(symbol) != functions_by_symbol.end() ||
        storages_by_symbol.find(symbol) != storages_by_symbol.end())
    {
        mark_invalid("procedure registration conflicts with canonical identity");
        return FunctionId();
    }
    if (!is_ready_type(return_type))
    {
        mark_unsupported("procedure needs a supported scalar return type");
        return FunctionId();
    }
    std::unordered_set<SymbolRef, SymbolRefHash> parameter_symbols;
    for (const std::pair<SymbolRef, value_shape> &parameter : parameters)
    {
        if (parameter.first.scope_id <= 0 || parameter.first.name.empty() ||
            parameter.first == symbol ||
            !parameter_symbols.emplace(parameter.first).second ||
            functions_by_symbol.find(parameter.first) != functions_by_symbol.end() ||
            storages_by_symbol.find(parameter.first) != storages_by_symbol.end())
        {
            mark_invalid("procedure parameter conflicts with canonical identity");
            return FunctionId();
        }
        if (!is_ready_type(parameter.second))
        {
            mark_unsupported("procedure needs supported scalar parameters");
            return FunctionId();
        }
    }
    Function function;
    function.id = FunctionId(static_cast<std::uint32_t>(scratch_module.functions.size()));
    function.kind = FunctionKind::Procedure;
    function.name = name;
    function.symbol = symbol;
    function.return_type = return_type;
    BasicBlock block;
    block.id = BlockId(function.id, 0);
    function.blocks.push_back(block);
    for (const std::pair<SymbolRef, value_shape> &parameter : parameters)
    {
        function.parameter_types.push_back(parameter.second);
    }
    scratch_module.functions.push_back(function);
    functions_by_symbol.emplace(symbol, function.id);
    for (const std::pair<SymbolRef, value_shape> &parameter : parameters)
    {
        Storage storage;
        storage.id = StorageId(static_cast<std::uint32_t>(scratch_module.storages.size()));
        storage.symbol = parameter.first;
        storage.type = parameter.second;
        storage.kind = StorageKind::Parameter;
        storage.owner = function.id;
        scratch_module.storages.push_back(storage);
        scratch_module.functions[function.id.index].parameters.push_back(storage.id);
        storages_by_symbol.emplace(parameter.first, storage.id);
    }
    return function.id;
}

bool IRBuilder::enter_function(FunctionId function)
{
    if (!emission_enabled())
    {
        return false;
    }
    const Function *found = function_for_id(function);
    if (found == NULL || found->kind != FunctionKind::Procedure)
    {
        mark_invalid("can only enter a defined procedure function");
        return false;
    }
    function_context.push_back(function);
    return true;
}

bool IRBuilder::leave_function()
{
    if (!emission_enabled())
    {
        //Context restoration is parser structural bookkeeping rather than IR
        //emission.  Keep nesting balanced even after the scratch module has
        //been disabled; finalization will discard it atomically.
        if (function_context.size() > 1)
        {
            function_context.pop_back();
            return true;
        }
        return false;
    }
    if (function_context.size() <= 1)
    {
        mark_invalid("cannot leave program function context");
        return false;
    }
    function_context.pop_back();
    return true;
}

FunctionId IRBuilder::current_function() const noexcept
{
    return function_context.empty() ? FunctionId() : function_context.back();
}

FunctionId IRBuilder::program_function() const noexcept
{
    return program_id;
}

FunctionId IRBuilder::function_for(const SymbolRef &symbol) const
{
    std::unordered_map<SymbolRef, FunctionId, SymbolRefHash>::const_iterator found =
        functions_by_symbol.find(symbol);
    return found == functions_by_symbol.end() ? FunctionId() : found->second;
}

StorageId IRBuilder::register_storage(const SymbolRef &symbol, const value_shape &type,
                                      StorageKind kind)
{
    if (!emission_enabled())
    {
        return StorageId();
    }
    if (kind == StorageKind::Parameter)
    {
        mark_invalid("procedure parameters are registered with their signature only");
        return StorageId();
    }
    const FunctionId owner = kind == StorageKind::Global ? program_id : current_function();
    if (symbol.name.empty() || symbol.scope_id < 0 ||
        (kind == StorageKind::Global && symbol.scope_id != 0) ||
        (kind == StorageKind::Local && symbol.scope_id == 0) ||
        functions_by_symbol.find(symbol) != functions_by_symbol.end())
    {
        mark_invalid("storage registration conflicts with canonical identity");
        return StorageId();
    }
    std::unordered_map<SymbolRef, StorageId, SymbolRefHash>::const_iterator existing =
        storages_by_symbol.find(symbol);
    if (existing != storages_by_symbol.end())
    {
        const Storage *registered = storage_for_id(existing->second);
        if (registered != NULL && registered->type == type && registered->kind == kind &&
            registered->owner == owner)
        {
            return existing->second;
        }
        mark_invalid("storage registration conflicts with canonical storage");
        return StorageId();
    }
    Function *owner_function = current_function_mut();
    if (!is_ready_type(type))
    {
        mark_unsupported("storage needs a supported scalar type");
        return StorageId();
    }
    if (!owner.valid() || (kind == StorageKind::Global && owner != program_id) ||
        (kind == StorageKind::Local &&
         (owner_function == NULL || owner_function->kind != FunctionKind::Procedure)))
    {
        mark_invalid("storage has no valid canonical owner");
        return StorageId();
    }
    Storage storage;
    storage.id = StorageId(static_cast<std::uint32_t>(scratch_module.storages.size()));
    storage.symbol = symbol;
    storage.type = type;
    storage.kind = kind;
    storage.owner = owner;
    if (kind == StorageKind::Local)
    {
        owner_function->locals.push_back(storage.id);
    }
    scratch_module.storages.push_back(storage);
    storages_by_symbol.emplace(symbol, storage.id);
    return storage.id;
}

StorageId IRBuilder::storage_for(const SymbolRef &symbol) const
{
    std::unordered_map<SymbolRef, StorageId, SymbolRefHash>::const_iterator found =
        storages_by_symbol.find(symbol);
    return found == storages_by_symbol.end() ? StorageId() : found->second;
}

ValueId IRBuilder::emit_constant(const value_shape &type,
                                 const std::variant<int, float, bool, std::string> &payload)
{
    if (!can_emit_value(type))
    {
        return ValueId();
    }
    BasicBlock *block = current_block_mut();
    if (block == NULL || !std::holds_alternative<std::monostate>(block->terminator))
    {
        mark_unsupported("post-return statements need control-flow lowering");
        return ValueId();
    }
    ValueId result = append_value(type, ValueLocation::Constant);
    if (!result.valid())
    {
        return ValueId();
    }
    block->instructions.push_back(Constant{result, payload});
    return result;
}

ValueId IRBuilder::emit_load(StorageId source)
{
    if (!emission_enabled())
    {
        return ValueId();
    }
    const Storage *storage = storage_for_id(source);
    if (storage == NULL)
    {
        mark_invalid("load references invalid storage");
        return ValueId();
    }
    if (!can_emit_value(storage->type))
    {
        return ValueId();
    }
    if (storage->kind != StorageKind::Global && storage->owner != current_function())
    {
        mark_invalid("load references invisible storage");
        return ValueId();
    }
    BasicBlock *block = current_block_mut();
    if (block == NULL || !std::holds_alternative<std::monostate>(block->terminator))
    {
        mark_unsupported("post-return statements need control-flow lowering");
        return ValueId();
    }
    ValueId result = append_value(storage->type, ValueLocation::Load);
    if (!result.valid())
    {
        return ValueId();
    }
    block->instructions.push_back(Load{result, source});
    return result;
}

bool IRBuilder::emit_store(StorageId destination, ValueId value)
{
    if (!emission_enabled())
    {
        return false;
    }
    const Storage *storage = storage_for_id(destination);
    const Value *source = value_for_id(value);
    BasicBlock *block = current_block_mut();
    if (storage == NULL || source == NULL || block == NULL ||
        storage->type != source->type ||
        (storage->kind != StorageKind::Global && storage->owner != current_function()))
    {
        mark_invalid("store is ill typed or out of scope");
        return false;
    }
    if (!std::holds_alternative<std::monostate>(block->terminator))
    {
        mark_unsupported("post-return statements need control-flow lowering");
        return false;
    }
    block->instructions.push_back(Store{destination, value});
    return true;
}

ValueId IRBuilder::emit_unary(UnaryOp operation, ValueId operand)
{
    if (!emission_enabled())
    {
        return ValueId();
    }
    const Value *source = value_for_id(operand);
    if (source == NULL || !is_ready_type(source->type) ||
        !valid_unary(operation, source->type.element_type))
    {
        mark_invalid("unary operand is invalid");
        return ValueId();
    }
    BasicBlock *block = current_block_mut();
    if (block == NULL || !std::holds_alternative<std::monostate>(block->terminator))
    {
        mark_unsupported("post-return statements need control-flow lowering");
        return ValueId();
    }
    ValueId result = append_value(source->type, ValueLocation::Unary);
    if (!result.valid())
    {
        return ValueId();
    }
    block->instructions.push_back(Unary{result, operation, operand});
    return result;
}

ValueId IRBuilder::emit_binary(BinaryOp operation, ValueId left, ValueId right)
{
    if (!emission_enabled())
    {
        return ValueId();
    }
    const Value *left_value = value_for_id(left);
    const Value *right_value = value_for_id(right);
    if (left_value == NULL || right_value == NULL ||
        left_value->type != right_value->type ||
        !valid_binary(operation, left_value->type.element_type))
    {
        mark_invalid("binary operands are invalid or mixed");
        return ValueId();
    }
    value_shape result_type = left_value->type;
    if (binary_returns_bool(operation))
    {
        result_type.element_type = TYPE_BOOL;
    }
    BasicBlock *block = current_block_mut();
    if (block == NULL || !std::holds_alternative<std::monostate>(block->terminator))
    {
        mark_unsupported("post-return statements need control-flow lowering");
        return ValueId();
    }
    ValueId result = append_value(result_type, ValueLocation::Binary);
    if (!result.valid())
    {
        return ValueId();
    }
    block->instructions.push_back(Binary{result, operation, left, right});
    return result;
}

ValueId IRBuilder::emit_cast(CastOp operation, ValueId operand)
{
    if (!emission_enabled())
    {
        return ValueId();
    }
    const Value *source = value_for_id(operand);
    value_shape target;
    if (source == NULL || !cast_matches(operation, source->type, target))
    {
        mark_invalid("cast is invalid");
        return ValueId();
    }
    BasicBlock *block = current_block_mut();
    if (block == NULL || !std::holds_alternative<std::monostate>(block->terminator))
    {
        mark_unsupported("post-return statements need control-flow lowering");
        return ValueId();
    }
    ValueId result = append_value(target, ValueLocation::Cast);
    if (!result.valid())
    {
        return ValueId();
    }
    block->instructions.push_back(Cast{result, operation, operand});
    return result;
}

ValueId IRBuilder::emit_call(FunctionId callee, const std::vector<ValueId> &arguments)
{
    if (!emission_enabled())
    {
        return ValueId();
    }
    const Function *function = function_for_id(callee);
    if (function == NULL ||
        (function->kind != FunctionKind::Procedure && function->kind != FunctionKind::ExternalBuiltin) ||
        function->parameter_types.size() != arguments.size())
    {
        mark_invalid("call has invalid callee or arity");
        return ValueId();
    }
    if (!can_emit_value(function->return_type))
    {
        return ValueId();
    }
    for (std::size_t i = 0; i < arguments.size(); i++)
    {
        const Value *argument = value_for_id(arguments[i]);
        if (argument == NULL || argument->type != function->parameter_types[i])
        {
            mark_invalid("call argument type is invalid");
            return ValueId();
        }
    }
    BasicBlock *block = current_block_mut();
    if (block == NULL || !std::holds_alternative<std::monostate>(block->terminator))
    {
        mark_unsupported("post-return statements need control-flow lowering");
        return ValueId();
    }
    ValueId result = append_value(function->return_type, ValueLocation::Call);
    if (!result.valid())
    {
        return ValueId();
    }
    block->instructions.push_back(Call{result, callee, arguments});
    return result;
}

bool IRBuilder::emit_return(ValueId value)
{
    if (!emission_enabled())
    {
        return false;
    }
    Function *function = current_function_mut();
    const Value *returned = value_for_id(value);
    BasicBlock *block = current_block_mut();
    if (function == NULL || function->kind != FunctionKind::Procedure || returned == NULL ||
        returned->id.function != function->id || block == NULL ||
        returned->type != function->return_type)
    {
        mark_invalid("procedure return has invalid context, value, or type");
        return false;
    }
    if (!std::holds_alternative<std::monostate>(block->terminator))
    {
        mark_unsupported("post-return statements need control-flow lowering");
        return false;
    }
    block->terminator = Terminator(ReturnTerminator{value});
    return true;
}

bool IRBuilder::emit_halt()
{
    if (!emission_enabled())
    {
        return false;
    }
    Function *function = current_function_mut();
    BasicBlock *block = current_block_mut();
    if (function == NULL || function->kind != FunctionKind::Program ||
        function->id != program_id || block == NULL ||
        !std::holds_alternative<std::monostate>(block->terminator))
    {
        mark_invalid("program halt is invalid");
        return false;
    }
    block->terminator = Terminator(HaltTerminator{});
    return true;
}

void IRBuilder::finalize(bool top_level_parse_success)
{
    if (final_status != ModuleStatus::Unfinalized)
    {
        return;
    }
    if (!top_level_parse_success)
    {
        saw_frontend_error = true;
    }
    if (saw_frontend_error)
    {
        final_status = ModuleStatus::FrontendError;
        final_reason = "frontend diagnostics or parse failure";
        discard_if_not_ready();
        return;
    }
    //Frontend failures always win.  An internal failure must win over every
    //unsupported feature, including fallthrough discovered below.
    if (saw_invalid)
    {
        final_status = ModuleStatus::InvalidIR;
        final_reason = invalid_reason;
        discard_if_not_ready();
        return;
    }
    for (const Function &function : scratch_module.functions)
    {
        if (function.kind == FunctionKind::Procedure &&
            (function.blocks.size() != 1 ||
             std::holds_alternative<std::monostate>(function.blocks[0].terminator)))
        {
            mark_unsupported("procedure fallthrough requires control-flow lowering");
            break;
        }
    }
    if (function_context.size() != 1 || function_context.front() != program_id)
    {
        mark_invalid("IR function context is not restored to the program root");
    }
    if (saw_invalid)
    {
        final_status = ModuleStatus::InvalidIR;
        final_reason = invalid_reason;
        discard_if_not_ready();
        return;
    }
    if (saw_unsupported)
    {
        final_status = ModuleStatus::Unsupported;
        final_reason = unsupported_reason;
        discard_if_not_ready();
        return;
    }
    const VerificationResult verified = verify_module(scratch_module);
    if (!verified.valid)
    {
        final_status = ModuleStatus::InvalidIR;
        final_reason = verified.reason;
        discard_if_not_ready();
        return;
    }
    final_status = ModuleStatus::Ready;
    final_module = scratch_module;
}

ModuleStatus IRBuilder::status() const noexcept
{
    return final_status;
}

const std::string &IRBuilder::reason() const noexcept
{
    return final_reason;
}

const Module &IRBuilder::module() const noexcept
{
    return final_module;
}

Function *IRBuilder::current_function_mut()
{
    if (function_context.empty())
    {
        return NULL;
    }
    const FunctionId id = function_context.back();
    if (!id.valid() || id.index >= scratch_module.functions.size())
    {
        return NULL;
    }
    return &scratch_module.functions[id.index];
}

const Function *IRBuilder::function_for_id(FunctionId id) const
{
    if (!id.valid() || id.index >= scratch_module.functions.size())
    {
        return NULL;
    }
    const Function &function = scratch_module.functions[id.index];
    return function.id == id ? &function : NULL;
}

const Storage *IRBuilder::storage_for_id(StorageId id) const
{
    if (!id.valid() || id.index >= scratch_module.storages.size())
    {
        return NULL;
    }
    const Storage &storage = scratch_module.storages[id.index];
    return storage.id == id ? &storage : NULL;
}

const Value *IRBuilder::value_for_id(ValueId id) const
{
    const Function *function = function_for_id(id.function);
    if (function == NULL || !id.valid() || id.index >= function->values.size())
    {
        return NULL;
    }
    const Value &value = function->values[id.index];
    return value.id == id ? &value : NULL;
}

ValueId IRBuilder::append_value(const value_shape &type, ValueLocation location)
{
    Function *function = current_function_mut();
    BasicBlock *block = current_block_mut();
    if (function == NULL || block == NULL || !is_ready_type(type))
    {
        mark_invalid("value has no supported owner or type");
        return ValueId();
    }
    if (!std::holds_alternative<std::monostate>(block->terminator))
    {
        mark_unsupported("post-return statements need control-flow lowering");
        return ValueId();
    }
    const ValueId id(function->id, static_cast<std::uint32_t>(function->values.size()));
    function->values.push_back(Value{id, type, location});
    return id;
}

BasicBlock *IRBuilder::current_block_mut()
{
    Function *function = current_function_mut();
    if (function == NULL || function->blocks.size() != 1)
    {
        return NULL;
    }
    return &function->blocks[0];
}

bool IRBuilder::can_emit_value(const value_shape &type)
{
    if (!emission_enabled() || !is_ready_type(type) || current_function_mut() == NULL ||
        current_block_mut() == NULL)
    {
        mark_unsupported("value needs a supported scalar straight-line context");
        return false;
    }
    BasicBlock *block = current_block_mut();
    if (block == NULL || !std::holds_alternative<std::monostate>(block->terminator))
    {
        mark_unsupported("post-return statements need control-flow lowering");
        return false;
    }
    return true;
}

void IRBuilder::discard_if_not_ready()
{
    scratch_module = Module();
    final_module = Module();
    function_context.clear();
    functions_by_symbol.clear();
    storages_by_symbol.clear();
}

} // namespace ir
