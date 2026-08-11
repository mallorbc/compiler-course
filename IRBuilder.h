#ifndef IR_BUILDER_H
#define IR_BUILDER_H

#include "BuiltinCatalog.h"
#include "IR.h"

#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace ir
{

class IRBuilder
{
public:
    IRBuilder();

    void mark_frontend_error();
    void mark_unsupported(const std::string &reason);
    void mark_invalid(const std::string &reason);
    bool emission_enabled() const noexcept;

    FunctionId register_program(const SymbolRef &symbol, const std::string &name);
    void seed_external_builtins();
    FunctionId register_procedure(const SymbolRef &symbol, const std::string &name,
                                  const value_shape &return_type,
                                  const std::vector<std::pair<SymbolRef, value_shape>> &parameters);
    bool enter_function(FunctionId function);
    bool leave_function();
    FunctionId current_function() const noexcept;
    FunctionId program_function() const noexcept;
    FunctionId function_for(const SymbolRef &symbol) const;

    BlockId create_block();
    bool select_block(BlockId block);
    BlockId current_block() const noexcept;
    //Parsing remains structural after a terminator.  These scoped guards make
    //IR lowering inert for an unreachable source statement without changing
    //frontend diagnostics or disturbing the function/block context stacks.
    bool current_block_is_open() const noexcept;
    bool begin_unreachable_statement();
    bool end_unreachable_statement();
    bool emit_jump(BlockId target);
    bool emit_branch(ValueId condition, BlockId when_true, BlockId when_false);

    StorageId register_storage(const SymbolRef &symbol, const value_shape &type,
                               StorageKind kind);
    StorageId storage_for(const SymbolRef &symbol) const;

    ValueId emit_constant(const value_shape &type,
                          const std::variant<int, float, bool, std::string> &payload);
    ValueId emit_load(StorageId source);
    bool emit_store(StorageId destination, ValueId value);
    ValueId emit_check_index(StorageId storage, ValueId raw_index);
    ValueId emit_element_load(StorageId storage, ValueId checked_index);
    bool emit_element_store(StorageId storage, ValueId checked_index, ValueId value);
    ValueId emit_unary(UnaryOp operation, ValueId operand);
    ValueId emit_binary(BinaryOp operation, ValueId left, ValueId right);
    ValueId emit_cast(CastOp operation, ValueId operand);
    ValueId emit_call(FunctionId callee, const std::vector<ValueId> &arguments);
    bool emit_return(ValueId value);
    bool emit_halt();

    void finalize(bool top_level_parse_success);
    ModuleStatus status() const noexcept;
    const std::string &reason() const noexcept;
    const Module &module() const noexcept;

private:
    Function *current_function_mut();
    const Function *function_for_id(FunctionId id) const;
    const Storage *storage_for_id(StorageId id) const;
    const Value *value_for_id(ValueId id) const;
    ValueId append_value(const value_shape &type, ValueLocation location);
    BasicBlock *current_block_mut();
    bool can_emit_value(const value_shape &type);
    void discard_if_not_ready();

    Module scratch_module;
    Module final_module;
    ModuleStatus final_status = ModuleStatus::Unfinalized;
    std::string final_reason;
    bool saw_frontend_error = false;
    bool saw_unsupported = false;
    bool saw_invalid = false;
    unsigned int unreachable_statement_depth = 0;
    std::string unsupported_reason;
    std::string invalid_reason;
    FunctionId program_id;
    std::vector<FunctionId> function_context;
    std::vector<BlockId> block_context;
    std::unordered_map<SymbolRef, FunctionId, SymbolRefHash> functions_by_symbol;
    std::unordered_map<SymbolRef, StorageId, SymbolRefHash> storages_by_symbol;
};

} // namespace ir

#endif // IR_BUILDER_H
