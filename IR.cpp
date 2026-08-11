#include "IR.h"

#include "BuiltinCatalog.h"

#include <set>
#include <unordered_set>

namespace ir
{
namespace
{

VerificationResult failure(const std::string &reason)
{
    VerificationResult result;
    result.reason = reason;
    return result;
}

const Function *find_function(const Module &module, FunctionId id)
{
    if (!id.valid() || id.index >= module.functions.size())
    {
        return NULL;
    }
    const Function &function = module.functions[id.index];
    return function.id == id ? &function : NULL;
}

const Storage *find_storage(const Module &module, StorageId id)
{
    if (!id.valid() || id.index >= module.storages.size())
    {
        return NULL;
    }
    const Storage &storage = module.storages[id.index];
    return storage.id == id ? &storage : NULL;
}

bool is_visible_storage(const Storage &storage, FunctionId function, FunctionId program)
{
    return storage.kind == StorageKind::Global ? storage.owner == program :
                                                  storage.owner == function;
}

bool matches_cast(CastOp operation, const value_shape &source, const value_shape &target)
{
    if (source.is_array || target.is_array)
    {
        return false;
    }
    return (operation == CastOp::IntToFloat && source.element_type == TYPE_INT &&
            target.element_type == TYPE_FLOAT) ||
           (operation == CastOp::FloatToInt && source.element_type == TYPE_FLOAT &&
            target.element_type == TYPE_INT) ||
           (operation == CastOp::BoolToInt && source.element_type == TYPE_BOOL &&
            target.element_type == TYPE_INT) ||
           (operation == CastOp::IntToBool && source.element_type == TYPE_INT &&
            target.element_type == TYPE_BOOL);
}

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

} // namespace

bool is_ready_type(const value_shape &shape)
{
    return shape.element_type != TYPE_NONE && !shape.is_array &&
           shape.array_upper_bound == -1;
}

bool is_program_result_shape(const value_shape &shape)
{
    return shape.element_type == TYPE_NONE && !shape.is_array &&
           shape.array_upper_bound == -1;
}

VerificationResult verify_module(const Module &module)
{
    std::size_t program_count = 0;
    FunctionId program_id;
    std::set<std::string> external_names;
    std::unordered_set<SymbolRef, SymbolRefHash> declaration_symbols;

    for (std::size_t function_index = 0; function_index < module.functions.size(); function_index++)
    {
        const Function &function = module.functions[function_index];
        if (function.id != FunctionId(static_cast<std::uint32_t>(function_index)))
        {
            return failure("function id does not match module order");
        }
        if (function.symbol.scope_id < 0 || function.symbol.name.empty() ||
            function.name != function.symbol.name ||
            !declaration_symbols.emplace(function.symbol).second)
        {
            return failure("function symbol is missing or duplicated");
        }
        if (function.kind == FunctionKind::Program)
        {
            if (!is_program_result_shape(function.return_type) || function.symbol.scope_id != 0 ||
                !function.parameter_types.empty() || !function.parameters.empty() ||
                !function.locals.empty())
            {
                return failure("program function must have no result or local signature metadata");
            }
        }
        else if (!is_ready_type(function.return_type))
        {
            return failure("function has unresolved or array return type");
        }
        for (const value_shape &parameter : function.parameter_types)
        {
            if (function.kind == FunctionKind::Program || !is_ready_type(parameter))
            {
                return failure("function has unresolved or array parameter type");
            }
        }
        if (function.kind == FunctionKind::Program)
        {
            program_count++;
            program_id = function.id;
        }
        if (function.kind == FunctionKind::ExternalBuiltin)
        {
            if (!function.blocks.empty() || !external_names.insert(function.name).second)
            {
                return failure("external builtin has a body or duplicate name");
            }
            const BuiltinSpec *spec = find_builtin(function.name);
            if (spec == NULL || spec->return_shape != function.return_type ||
                spec->parameter_shapes != function.parameter_types ||
                function.symbol != SymbolRef{0, function.name} ||
                !function.parameters.empty() || !function.locals.empty() ||
                !function.values.empty())
            {
                return failure("external builtin does not match catalog");
            }
        }
        else
        {
            if (function.blocks.size() != 1 || function.blocks[0].id != BlockId(function.id, 0))
            {
                return failure("defined function does not have exactly one owned block");
            }
        }
    }
    if (program_count != 1)
    {
        return failure("module must contain exactly one program function");
    }
    if (external_names.size() != builtin_catalog().size())
    {
        return failure("module external builtin catalog is incomplete");
    }

    for (std::size_t storage_index = 0; storage_index < module.storages.size(); storage_index++)
    {
        const Storage &storage = module.storages[storage_index];
        if (storage.id != StorageId(static_cast<std::uint32_t>(storage_index)) ||
            !is_ready_type(storage.type) || storage.symbol.name.empty() ||
            storage.symbol.scope_id < 0 ||
            (storage.kind == StorageKind::Global && storage.symbol.scope_id != 0) ||
            (storage.kind != StorageKind::Global && storage.symbol.scope_id == 0) ||
            !declaration_symbols.emplace(storage.symbol).second)
        {
            return failure("storage id or type is invalid");
        }
        const Function *owner = find_function(module, storage.owner);
        if (owner == NULL || owner->kind == FunctionKind::ExternalBuiltin)
        {
            return failure("storage has invalid owner");
        }
        if ((storage.kind == StorageKind::Global && storage.owner != program_id) ||
            (storage.kind != StorageKind::Global && storage.owner == program_id) ||
            (storage.kind != StorageKind::Global && owner->kind != FunctionKind::Procedure))
        {
            return failure("storage kind and owner disagree");
        }
    }

    std::vector<unsigned int> storage_membership(module.storages.size(), 0);
    for (const Function &function : module.functions)
    {
        std::set<std::uint32_t> listed_storage_ids;
        for (std::size_t parameter_index = 0; parameter_index < function.parameters.size();
             parameter_index++)
        {
            const StorageId parameter_id = function.parameters[parameter_index];
            if (!parameter_id.valid() || parameter_id.index >= storage_membership.size() ||
                !listed_storage_ids.insert(parameter_id.index).second)
            {
                return failure("function parameter storage list is duplicated or stale");
            }
            const Storage *parameter = find_storage(module, parameter_id);
            if (parameter == NULL || parameter->owner != function.id ||
                parameter->kind != StorageKind::Parameter ||
                parameter_index >= function.parameter_types.size() ||
                parameter->type != function.parameter_types[parameter_index])
            {
                return failure("function parameter storage does not match signature");
            }
            storage_membership[parameter_id.index]++;
        }
        if (function.kind == FunctionKind::Procedure &&
            function.parameters.size() != function.parameter_types.size())
        {
            return failure("function parameter count does not match signature");
        }
        for (StorageId local_id : function.locals)
        {
            if (!local_id.valid() || local_id.index >= storage_membership.size() ||
                !listed_storage_ids.insert(local_id.index).second)
            {
                return failure("function local storage list is duplicated or stale");
            }
            const Storage *local = find_storage(module, local_id);
            if (local == NULL || local->owner != function.id ||
                local->kind != StorageKind::Local)
            {
                return failure("function local storage has wrong ownership");
            }
            storage_membership[local_id.index]++;
        }
        if (function.kind == FunctionKind::ExternalBuiltin)
        {
            continue;
        }

        const BasicBlock &block = function.blocks[0];
        std::vector<bool> defined(function.values.size(), false);
        const auto value_for = [&function, &defined](ValueId id) -> const Value * {
            if (!id.valid() || id.function != function.id || id.index >= function.values.size() ||
                !defined[id.index])
            {
                return NULL;
            }
            const Value &value = function.values[id.index];
            return value.id == id ? &value : NULL;
        };
        const auto define = [&function, &defined](ValueId id, ValueLocation location) -> bool {
            if (!id.valid() || id.function != function.id || id.index >= function.values.size() ||
                defined[id.index])
            {
                return false;
            }
            const Value &value = function.values[id.index];
            if (value.id != id || value.location != location || !is_ready_type(value.type))
            {
                return false;
            }
            defined[id.index] = true;
            return true;
        };

        for (const Instruction &instruction : block.instructions)
        {
            if (const Constant *constant = std::get_if<Constant>(&instruction))
            {
                if (!define(constant->result, ValueLocation::Constant))
                {
                    return failure("invalid constant result");
                }
                const value_shape type = function.values[constant->result.index].type;
                if ((type.element_type == TYPE_INT && !std::holds_alternative<int>(constant->payload)) ||
                    (type.element_type == TYPE_FLOAT && !std::holds_alternative<float>(constant->payload)) ||
                    (type.element_type == TYPE_BOOL && !std::holds_alternative<bool>(constant->payload)) ||
                    (type.element_type == TYPE_STRING && !std::holds_alternative<std::string>(constant->payload)))
                {
                    return failure("constant payload does not match value type");
                }
            }
            else if (const Load *load = std::get_if<Load>(&instruction))
            {
                const Storage *storage = find_storage(module, load->source);
                if (!define(load->result, ValueLocation::Load) || storage == NULL ||
                    !is_visible_storage(*storage, function.id, program_id) ||
                    function.values[load->result.index].type != storage->type)
                {
                    return failure("invalid load");
                }
            }
            else if (const Store *store = std::get_if<Store>(&instruction))
            {
                const Storage *storage = find_storage(module, store->destination);
                const Value *value = value_for(store->value);
                if (storage == NULL || value == NULL ||
                    !is_visible_storage(*storage, function.id, program_id) ||
                    storage->type != value->type)
                {
                    return failure("invalid store");
                }
            }
            else if (const Unary *unary = std::get_if<Unary>(&instruction))
            {
                const Value *operand = value_for(unary->operand);
                if (!define(unary->result, ValueLocation::Unary) || operand == NULL ||
                    function.values[unary->result.index].type != operand->type ||
                    !valid_unary(unary->operation, operand->type.element_type))
                {
                    return failure("invalid unary instruction");
                }
            }
            else if (const Binary *binary = std::get_if<Binary>(&instruction))
            {
                const Value *left = value_for(binary->left);
                const Value *right = value_for(binary->right);
                const value_shape expected_result = left == NULL ? value_shape() :
                    (binary_returns_bool(binary->operation) ?
                         value_shape{TYPE_BOOL, false, -1} : left->type);
                if (!define(binary->result, ValueLocation::Binary) || left == NULL || right == NULL ||
                    left->type != right->type ||
                    function.values[binary->result.index].type != expected_result ||
                    !valid_binary(binary->operation, left->type.element_type))
                {
                    return failure("invalid binary instruction");
                }
            }
            else if (const Cast *cast = std::get_if<Cast>(&instruction))
            {
                const Value *operand = value_for(cast->operand);
                if (!define(cast->result, ValueLocation::Cast) || operand == NULL ||
                    !matches_cast(cast->operation, operand->type,
                                  function.values[cast->result.index].type))
                {
                    return failure("invalid cast instruction");
                }
            }
            else if (const Call *call = std::get_if<Call>(&instruction))
            {
                const Function *callee = find_function(module, call->callee);
                if (!define(call->result, ValueLocation::Call) || callee == NULL ||
                    (callee->kind != FunctionKind::Procedure &&
                     callee->kind != FunctionKind::ExternalBuiltin) ||
                    callee->parameter_types.size() != call->arguments.size() ||
                    function.values[call->result.index].type != callee->return_type)
                {
                    return failure("invalid call result or signature");
                }
                for (std::size_t i = 0; i < call->arguments.size(); i++)
                {
                    const Value *argument = value_for(call->arguments[i]);
                    if (argument == NULL || argument->type != callee->parameter_types[i])
                    {
                        return failure("invalid call argument");
                    }
                }
            }
        }

        for (bool was_defined : defined)
        {
            if (!was_defined)
            {
                return failure("value table contains an undefined value");
            }
        }

        if (std::holds_alternative<std::monostate>(block.terminator))
        {
            return failure("defined function has no terminator");
        }
        const Terminator &terminator = std::get<Terminator>(block.terminator);
        if (function.kind == FunctionKind::Program)
        {
            if (!std::holds_alternative<HaltTerminator>(terminator))
            {
                return failure("program must halt");
            }
        }
        else
        {
            const ReturnTerminator *returned = std::get_if<ReturnTerminator>(&terminator);
            const Value *value = returned == NULL ? NULL : value_for(returned->value);
            if (value == NULL || value->type != function.return_type)
            {
                return failure("procedure must return an exact typed value");
            }
        }
    }

    for (std::size_t storage_index = 0; storage_index < module.storages.size(); storage_index++)
    {
        const Storage &storage = module.storages[storage_index];
        if ((storage.kind == StorageKind::Global && storage_membership[storage_index] != 0) ||
            (storage.kind != StorageKind::Global && storage_membership[storage_index] != 1))
        {
            return failure("storage metadata is not an exact owner-list bijection");
        }
    }

    VerificationResult success;
    success.valid = true;
    return success;
}

} // namespace ir
