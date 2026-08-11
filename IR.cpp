#include "IR.h"

#include "BuiltinCatalog.h"

#include <algorithm>
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
    if (!is_resolved_value_shape(source) || !is_resolved_value_shape(target) ||
        source.is_array != target.is_array ||
        (source.is_array && source.array_upper_bound != target.array_upper_bound))
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
    return (shape.element_type == TYPE_INT || shape.element_type == TYPE_FLOAT ||
            shape.element_type == TYPE_STRING || shape.element_type == TYPE_BOOL) &&
           !shape.is_array &&
           shape.array_upper_bound == -1;
}

bool is_resolved_value_shape(const value_shape &shape)
{
    return (shape.element_type == TYPE_INT || shape.element_type == TYPE_FLOAT ||
           shape.element_type == TYPE_STRING || shape.element_type == TYPE_BOOL) &&
           ((!shape.is_array && shape.array_upper_bound == -1) ||
            (shape.is_array && shape.array_upper_bound >= 0));
}

bool infer_unary_result_shape(UnaryOp operation, const value_shape &operand,
                              value_shape &result)
{
    if (!is_resolved_value_shape(operand) ||
        !valid_unary(operation, operand.element_type))
    {
        return false;
    }
    result = operand;
    return true;
}

bool infer_binary_result_shape(BinaryOp operation, const value_shape &left,
                               const value_shape &right, value_shape &result)
{
    if (!is_resolved_value_shape(left) || !is_resolved_value_shape(right) ||
        left.element_type != right.element_type ||
        (left.is_array && right.is_array &&
         left.array_upper_bound != right.array_upper_bound) ||
        !valid_binary(operation, left.element_type))
    {
        return false;
    }
    result = left.is_array ? left : right;
    if (binary_returns_bool(operation))
    {
        result.element_type = TYPE_BOOL;
    }
    return true;
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
            if (function.kind == FunctionKind::Program ||
                (function.kind == FunctionKind::ExternalBuiltin ? !is_ready_type(parameter) :
                                                                  !is_resolved_value_shape(parameter)))
            {
                return failure("function has an invalid parameter type");
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
            if (function.blocks.empty())
            {
                return failure("defined function has no entry block");
            }
            for (std::size_t block_index = 0; block_index < function.blocks.size(); block_index++)
            {
                if (function.blocks[block_index].id !=
                    BlockId(function.id, static_cast<std::uint32_t>(block_index)))
                {
                    return failure("defined block id does not match function order");
                }
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
            !is_resolved_value_shape(storage.type) || storage.symbol.name.empty() ||
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

        const std::size_t block_count = function.blocks.size();
        std::vector<std::vector<std::size_t>> predecessors(block_count);
        std::vector<std::vector<std::size_t>> successors(block_count);
        std::size_t halt_count = 0;
        std::vector<std::size_t> completion_blocks;
        const auto add_target = [&function, &successors, &predecessors](std::size_t source,
                                                                          BlockId target) -> bool {
            if (!target.valid() || target.function != function.id ||
                target.index >= function.blocks.size() ||
                function.blocks[target.index].id != target)
            {
                return false;
            }
            successors[source].push_back(target.index);
            predecessors[target.index].push_back(source);
            return true;
        };

        for (std::size_t block_index = 0; block_index < block_count; block_index++)
        {
            const BasicBlock &block = function.blocks[block_index];
            if (std::holds_alternative<std::monostate>(block.terminator))
            {
                return failure("defined function has no terminator");
            }
            const Terminator &terminator = std::get<Terminator>(block.terminator);
            if (function.kind == FunctionKind::Program)
            {
                if (std::holds_alternative<HaltTerminator>(terminator))
                {
                    halt_count++;
                    completion_blocks.push_back(block_index);
                }
                else if (const JumpTerminator *jump = std::get_if<JumpTerminator>(&terminator))
                {
                    if (!add_target(block_index, jump->target))
                    {
                        return failure("program jump has an invalid target");
                    }
                }
                else if (const BranchTerminator *branch =
                             std::get_if<BranchTerminator>(&terminator))
                {
                    if (branch->when_true == branch->when_false ||
                        !add_target(block_index, branch->when_true) ||
                        !add_target(block_index, branch->when_false))
                    {
                        return failure("program branch has invalid targets");
                    }
                }
                else
                {
                    return failure("program may not return a value");
                }
            }
            else // Procedure
            {
                if (std::holds_alternative<ReturnTerminator>(terminator))
                {
                    completion_blocks.push_back(block_index);
                }
                else if (const JumpTerminator *jump = std::get_if<JumpTerminator>(&terminator))
                {
                    if (!add_target(block_index, jump->target))
                    {
                        return failure("procedure jump has an invalid target");
                    }
                }
                else if (const BranchTerminator *branch =
                             std::get_if<BranchTerminator>(&terminator))
                {
                    if (branch->when_true == branch->when_false ||
                        !add_target(block_index, branch->when_true) ||
                        !add_target(block_index, branch->when_false))
                    {
                        return failure("procedure branch has invalid targets");
                    }
                }
                else
                {
                    return failure("procedure may not halt");
                }
            }
        }
        if (function.kind == FunctionKind::Program && halt_count != 1)
        {
            return failure("program must have exactly one halt block");
        }

        std::vector<bool> reachable(block_count, false);
        std::vector<std::size_t> work;
        work.push_back(0);
        reachable[0] = true;
        while (!work.empty())
        {
            const std::size_t current = work.back();
            work.pop_back();
            for (std::size_t target : successors[current])
            {
                if (!reachable[target])
                {
                    reachable[target] = true;
                    work.push_back(target);
                }
            }
        }
        if (std::find(reachable.begin(), reachable.end(), false) != reachable.end())
        {
            return failure("defined function has an unreachable block");
        }
        if (function.kind == FunctionKind::Program)
        {
            std::vector<bool> reaches_halt(block_count, false);
            for (std::size_t halt_block : completion_blocks)
            {
                work.push_back(halt_block);
                reaches_halt[halt_block] = true;
            }
            while (!work.empty())
            {
                const std::size_t current = work.back();
                work.pop_back();
                for (std::size_t predecessor : predecessors[current])
                {
                    if (!reaches_halt[predecessor])
                    {
                        reaches_halt[predecessor] = true;
                        work.push_back(predecessor);
                    }
                }
            }
            if (std::find(reaches_halt.begin(), reaches_halt.end(), false) != reaches_halt.end())
            {
                return failure("program block cannot reach halt");
            }
        }
        else
        {
            if (completion_blocks.empty())
            {
                return failure("procedure has no return block");
            }
            std::vector<bool> reaches_return(block_count, false);
            for (std::size_t return_block : completion_blocks)
            {
                work.push_back(return_block);
                reaches_return[return_block] = true;
            }
            while (!work.empty())
            {
                const std::size_t current = work.back();
                work.pop_back();
                for (std::size_t predecessor : predecessors[current])
                {
                    if (!reaches_return[predecessor])
                    {
                        reaches_return[predecessor] = true;
                        work.push_back(predecessor);
                    }
                }
            }
            if (std::find(reaches_return.begin(), reaches_return.end(), false) !=
                reaches_return.end())
            {
                return failure("procedure block cannot reach return");
            }
        }

        std::vector<std::vector<bool>> dominates(block_count,
                                                  std::vector<bool>(block_count, true));
        for (std::size_t index = 0; index < block_count; index++)
        {
            dominates[0][index] = false;
        }
        dominates[0][0] = true;
        bool changed = true;
        while (changed)
        {
            changed = false;
            for (std::size_t block_index = 1; block_index < block_count; block_index++)
            {
                std::vector<bool> next(block_count, true);
                for (std::size_t predecessor : predecessors[block_index])
                {
                    for (std::size_t index = 0; index < block_count; index++)
                    {
                        next[index] = next[index] && dominates[predecessor][index];
                    }
                }
                next[block_index] = true;
                if (next != dominates[block_index])
                {
                    dominates[block_index] = next;
                    changed = true;
                }
            }
        }

        struct Definition
        {
            bool present = false;
            std::size_t block = 0;
            std::size_t instruction = 0;
        };
        std::vector<Definition> definitions(function.values.size());
        const auto define = [&function, &definitions](ValueId id, ValueLocation location,
                                                       std::size_t block,
                                                       std::size_t instruction) -> bool {
            if (!id.valid() || id.function != function.id || id.index >= function.values.size() ||
                definitions[id.index].present)
            {
                return false;
            }
            const Value &value = function.values[id.index];
            if (value.id != id || value.location != location ||
                !is_resolved_value_shape(value.type))
            {
                return false;
            }
            definitions[id.index] = Definition{true, block, instruction};
            return true;
        };
        for (std::size_t block_index = 0; block_index < block_count; block_index++)
        {
            const std::vector<Instruction> &instructions = function.blocks[block_index].instructions;
            for (std::size_t instruction_index = 0; instruction_index < instructions.size();
                 instruction_index++)
            {
                const Instruction &instruction = instructions[instruction_index];
                bool result_valid = false;
                if (const Constant *constant = std::get_if<Constant>(&instruction))
                {
                    result_valid = define(constant->result, ValueLocation::Constant, block_index,
                                          instruction_index);
                }
                else if (const Load *load = std::get_if<Load>(&instruction))
                {
                    result_valid = define(load->result, ValueLocation::Load, block_index,
                                          instruction_index);
                }
                else if (const CheckIndex *check = std::get_if<CheckIndex>(&instruction))
                {
                    result_valid = define(check->result, ValueLocation::CheckedIndex, block_index,
                                          instruction_index);
                }
                else if (const ElementLoad *load = std::get_if<ElementLoad>(&instruction))
                {
                    result_valid = define(load->result, ValueLocation::ElementLoad, block_index,
                                          instruction_index);
                }
                else if (const Unary *unary = std::get_if<Unary>(&instruction))
                {
                    result_valid = define(unary->result, ValueLocation::Unary, block_index,
                                          instruction_index);
                }
                else if (const Binary *binary = std::get_if<Binary>(&instruction))
                {
                    result_valid = define(binary->result, ValueLocation::Binary, block_index,
                                          instruction_index);
                }
                else if (const Cast *cast = std::get_if<Cast>(&instruction))
                {
                    result_valid = define(cast->result, ValueLocation::Cast, block_index,
                                          instruction_index);
                }
                else if (const Call *call = std::get_if<Call>(&instruction))
                {
                    result_valid = define(call->result, ValueLocation::Call, block_index,
                                          instruction_index);
                }
                else
                {
                    result_valid = true; //Store and ElementStore have no result.
                }
                if (!result_valid)
                {
                    return failure("instruction result is missing, duplicated, or ill typed");
                }
            }
        }
        if (std::find_if(definitions.begin(), definitions.end(),
                         [](const Definition &definition) { return !definition.present; }) !=
            definitions.end())
        {
            return failure("value table contains an undefined value");
        }

        const auto value_for = [&function, &definitions, &dominates](ValueId id,
                                                                       std::size_t block,
                                                                       std::size_t instruction) -> const Value * {
            if (!id.valid() || id.function != function.id || id.index >= function.values.size() ||
                !definitions[id.index].present)
            {
                return NULL;
            }
            const Definition &definition = definitions[id.index];
            if ((definition.block == block && definition.instruction >= instruction) ||
                (definition.block != block && !dominates[block][definition.block]))
            {
                return NULL;
            }
            const Value &value = function.values[id.index];
            return value.id == id && value.location != ValueLocation::CheckedIndex ? &value : NULL;
        };

        std::vector<unsigned int> checked_index_consumers(function.values.size(), 0U);
        const auto checked_index_for = [&function, &definitions](ValueId id,
                                                                  std::size_t block,
                                                                  std::size_t instruction,
                                                                  StorageId storage) -> bool {
            if (!id.valid() || id.function != function.id || id.index >= function.values.size() ||
                !definitions[id.index].present)
            {
                return false;
            }
            const Definition &definition = definitions[id.index];
            if (definition.block != block || definition.instruction >= instruction)
            {
                return false;
            }
            const Value &value = function.values[id.index];
            if (value.id != id || value.location != ValueLocation::CheckedIndex ||
                value.type != value_shape{TYPE_INT, false, -1})
            {
                return false;
            }
            const CheckIndex *check = std::get_if<CheckIndex>(
                &function.blocks[block].instructions[definition.instruction]);
            return check != NULL && check->result == id && check->storage == storage;
        };

        for (std::size_t block_index = 0; block_index < block_count; block_index++)
        {
            const BasicBlock &block = function.blocks[block_index];
            for (std::size_t instruction_index = 0; instruction_index < block.instructions.size();
                 instruction_index++)
            {
                const Instruction &instruction = block.instructions[instruction_index];
                if (const Constant *constant = std::get_if<Constant>(&instruction))
                {
                    const value_shape type = function.values[constant->result.index].type;
                    if (!is_ready_type(type) ||
                        (type.element_type == TYPE_INT && !std::holds_alternative<int>(constant->payload)) ||
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
                    if (storage == NULL || !is_visible_storage(*storage, function.id, program_id) ||
                        function.values[load->result.index].type != storage->type)
                    {
                        return failure("invalid load");
                    }
                }
                else if (const Store *store = std::get_if<Store>(&instruction))
                {
                    const Storage *storage = find_storage(module, store->destination);
                    const Value *value = value_for(store->value, block_index, instruction_index);
                    if (storage == NULL || value == NULL ||
                        !is_visible_storage(*storage, function.id, program_id) ||
                        storage->type != value->type)
                    {
                        return failure("invalid store");
                    }
                }
                else if (const CheckIndex *check = std::get_if<CheckIndex>(&instruction))
                {
                    const Storage *storage = find_storage(module, check->storage);
                    const Value *raw_index = value_for(check->raw_index, block_index,
                                                       instruction_index);
                    if (storage == NULL || !storage->type.is_array ||
                        !is_visible_storage(*storage, function.id, program_id) ||
                        raw_index == NULL ||
                        raw_index->type != value_shape{TYPE_INT, false, -1} ||
                        function.values[check->result.index].type !=
                            value_shape{TYPE_INT, false, -1})
                    {
                        return failure("invalid checked index");
                    }
                }
                else if (const ElementLoad *load = std::get_if<ElementLoad>(&instruction))
                {
                    const Storage *storage = find_storage(module, load->storage);
                    value_shape element_type;
                    if (storage != NULL)
                    {
                        element_type.element_type = storage->type.element_type;
                    }
                    if (storage == NULL || !storage->type.is_array ||
                        !is_visible_storage(*storage, function.id, program_id) ||
                        !checked_index_for(load->checked_index, block_index, instruction_index,
                                           load->storage) ||
                        function.values[load->result.index].type != element_type)
                    {
                        return failure("invalid element load");
                    }
                    checked_index_consumers[load->checked_index.index]++;
                }
                else if (const ElementStore *store = std::get_if<ElementStore>(&instruction))
                {
                    const Storage *storage = find_storage(module, store->storage);
                    const Value *value = value_for(store->value, block_index, instruction_index);
                    value_shape element_type;
                    if (storage != NULL)
                    {
                        element_type.element_type = storage->type.element_type;
                    }
                    if (storage == NULL || !storage->type.is_array || value == NULL ||
                        !is_visible_storage(*storage, function.id, program_id) ||
                        !checked_index_for(store->checked_index, block_index, instruction_index,
                                           store->storage) || value->type != element_type)
                    {
                        return failure("invalid element store");
                    }
                    checked_index_consumers[store->checked_index.index]++;
                }
                else if (const Unary *unary = std::get_if<Unary>(&instruction))
                {
                    const Value *operand = value_for(unary->operand, block_index, instruction_index);
                    value_shape expected_result;
                    if (operand == NULL ||
                        !infer_unary_result_shape(unary->operation, operand->type,
                                                  expected_result) ||
                        function.values[unary->result.index].type != expected_result)
                    {
                        return failure("invalid unary instruction");
                    }
                }
                else if (const Binary *binary = std::get_if<Binary>(&instruction))
                {
                    const Value *left = value_for(binary->left, block_index, instruction_index);
                    const Value *right = value_for(binary->right, block_index, instruction_index);
                    value_shape expected_result;
                    if (left == NULL || right == NULL ||
                        !infer_binary_result_shape(binary->operation, left->type, right->type,
                                                   expected_result) ||
                        function.values[binary->result.index].type != expected_result)
                    {
                        return failure("invalid binary instruction");
                    }
                }
                else if (const Cast *cast = std::get_if<Cast>(&instruction))
                {
                    const Value *operand = value_for(cast->operand, block_index, instruction_index);
                    if (operand == NULL || !matches_cast(cast->operation, operand->type,
                                                         function.values[cast->result.index].type))
                    {
                        return failure("invalid cast instruction");
                    }
                }
                else if (const Call *call = std::get_if<Call>(&instruction))
                {
                    const Function *callee = find_function(module, call->callee);
                    if (callee == NULL ||
                        (callee->kind != FunctionKind::Procedure &&
                         callee->kind != FunctionKind::ExternalBuiltin) ||
                        callee->parameter_types.size() != call->arguments.size() ||
                        function.values[call->result.index].type != callee->return_type)
                    {
                        return failure("invalid call result or signature");
                    }
                    for (std::size_t i = 0; i < call->arguments.size(); i++)
                    {
                        const Value *argument = value_for(call->arguments[i], block_index,
                                                          instruction_index);
                        if (argument == NULL || argument->type != callee->parameter_types[i])
                        {
                            return failure("invalid call argument");
                        }
                    }
                }
            }
            const Terminator &terminator = std::get<Terminator>(block.terminator);
            if (const BranchTerminator *branch = std::get_if<BranchTerminator>(&terminator))
            {
                const Value *condition = value_for(branch->condition, block_index,
                                                   block.instructions.size());
                if (condition == NULL ||
                    condition->type != value_shape{TYPE_BOOL, false, -1})
                {
                    return failure("branch condition must be a visible scalar bool");
                }
            }
            if (const ReturnTerminator *returned = std::get_if<ReturnTerminator>(&terminator))
            {
                const Value *value = value_for(returned->value, block_index,
                                               block.instructions.size());
                if (function.kind != FunctionKind::Procedure || value == NULL ||
                    value->type != function.return_type)
                {
                    return failure("procedure must return an exact typed value");
                }
            }
        }
        for (std::size_t value_index = 0; value_index < function.values.size(); value_index++)
        {
            if (function.values[value_index].location == ValueLocation::CheckedIndex &&
                checked_index_consumers[value_index] != 1U)
            {
                return failure("checked index must have exactly one same-block element consumer");
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
