#include "../vendor/doctest.h"
#include "../../BuiltinCatalog.h"
#include "../../IR.h"

#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

//The finalizer deliberately has no public mutation hook.  This translation
//unit exposes its scratch module only to pin priority for corrupt internal CFG
//state that the public builder correctly prevents callers from constructing.
#define private public
#include "../../IRBuilder.h"
#undef private

#include <type_traits>

namespace
{

value_shape scalar(data_types type)
{
    value_shape shape;
    shape.element_type = type;
    return shape;
}

ir::Module ready_cfg_module()
{
    ir::IRBuilder builder;
    REQUIRE(builder.register_program(SymbolRef{0, "cfg_fixture"}, "cfg_fixture").valid());
    builder.seed_external_builtins();
    const ir::StorageId storage = builder.register_storage(SymbolRef{0, "value"}, scalar(TYPE_INT),
                                                           ir::StorageKind::Global);
    REQUIRE(storage.valid());
    const ir::ValueId condition = builder.emit_constant(scalar(TYPE_BOOL), true);
    const ir::ValueId value = builder.emit_constant(scalar(TYPE_INT), 1);
    const ir::BlockId then_block = builder.create_block();
    const ir::BlockId else_block = builder.create_block();
    const ir::BlockId join_block = builder.create_block();
    REQUIRE(builder.emit_branch(condition, then_block, else_block));
    REQUIRE(builder.select_block(then_block));
    REQUIRE(builder.emit_store(storage, value));
    REQUIRE(builder.emit_jump(join_block));
    REQUIRE(builder.select_block(else_block));
    REQUIRE(builder.emit_store(storage, value));
    REQUIRE(builder.emit_jump(join_block));
    REQUIRE(builder.select_block(join_block));
    REQUIRE(builder.emit_load(storage).valid());
    REQUIRE(builder.emit_halt());
    builder.finalize(true);
    REQUIRE(builder.status() == ir::ModuleStatus::Ready);
    return builder.module();
}

ir::Module ready_loop_module()
{
    ir::IRBuilder builder;
    REQUIRE(builder.register_program(SymbolRef{0, "loop_fixture"}, "loop_fixture").valid());
    builder.seed_external_builtins();
    const ir::StorageId counter = builder.register_storage(
        SymbolRef{0, "counter"}, scalar(TYPE_INT), ir::StorageKind::Global);
    REQUIRE(counter.valid());
    const ir::ValueId zero = builder.emit_constant(scalar(TYPE_INT), 0);
    REQUIRE(zero.valid());
    REQUIRE(builder.emit_store(counter, zero));
    const ir::BlockId condition = builder.create_block();
    const ir::BlockId body = builder.create_block();
    const ir::BlockId exit = builder.create_block();
    REQUIRE(builder.emit_jump(condition));
    REQUIRE(builder.select_block(condition));
    const ir::ValueId loaded_counter = builder.emit_load(counter);
    const ir::ValueId limit = builder.emit_constant(scalar(TYPE_INT), 3);
    const ir::ValueId is_less = builder.emit_binary(ir::BinaryOp::Less, loaded_counter, limit);
    REQUIRE(loaded_counter.valid());
    REQUIRE(limit.valid());
    REQUIRE(is_less.valid());
    REQUIRE(builder.emit_branch(is_less, body, exit));
    REQUIRE(builder.select_block(body));
    const ir::ValueId body_counter = builder.emit_load(counter);
    const ir::ValueId one = builder.emit_constant(scalar(TYPE_INT), 1);
    const ir::ValueId incremented = builder.emit_binary(ir::BinaryOp::Add, body_counter, one);
    REQUIRE(body_counter.valid());
    REQUIRE(one.valid());
    REQUIRE(incremented.valid());
    REQUIRE(builder.emit_store(counter, incremented));
    REQUIRE(builder.emit_jump(condition));
    REQUIRE(builder.select_block(exit));
    REQUIRE(builder.emit_halt());
    builder.finalize(true);
    REQUIRE(builder.status() == ir::ModuleStatus::Ready);
    return builder.module();
}

} // namespace

TEST_CASE("Stage 4A IDs and builtin catalog are strongly separated")
{
    static_assert(!std::is_convertible<ir::FunctionId, ir::StorageId>::value,
                  "IR identity domains must not interconvert");
    static_assert(!std::is_convertible<ir::StorageId, ir::FunctionId>::value,
                  "IR identity domains must not interconvert");
    CHECK_FALSE(ir::FunctionId().valid());
    CHECK_FALSE(ir::StorageId().valid());
    CHECK_FALSE(ir::ValueId().valid());
    CHECK(builtin_catalog().size() == 9);
    const BuiltinSpec *put_integer = find_builtin(BuiltinId::PutInteger);
    REQUIRE(put_integer != NULL);
    CHECK(put_integer->return_shape == scalar(TYPE_BOOL));
    REQUIRE(put_integer->parameter_shapes.size() == 1);
    CHECK(put_integer->parameter_shapes[0] == scalar(TYPE_INT));
}

TEST_CASE("Stage 5A builder forms deterministic Program branch blocks")
{
    ir::IRBuilder builder;
    REQUIRE(builder.register_program(SymbolRef{0, "cfg_root"}, "cfg_root").valid());
    builder.seed_external_builtins();
    const ir::ValueId condition = builder.emit_constant(scalar(TYPE_BOOL), true);
    REQUIRE(condition.valid());
    const ir::BlockId then_block = builder.create_block();
    const ir::BlockId else_block = builder.create_block();
    const ir::BlockId join_block = builder.create_block();
    CHECK(then_block == ir::BlockId(builder.program_function(), 1));
    CHECK(else_block == ir::BlockId(builder.program_function(), 2));
    CHECK(join_block == ir::BlockId(builder.program_function(), 3));
    REQUIRE(builder.emit_branch(condition, then_block, else_block));
    CHECK(builder.select_block(then_block));
    REQUIRE(builder.emit_jump(join_block));
    CHECK(builder.select_block(else_block));
    REQUIRE(builder.emit_jump(join_block));
    CHECK(builder.select_block(join_block));
    REQUIRE(builder.emit_halt());
    builder.finalize(true);

    REQUIRE(builder.status() == ir::ModuleStatus::Ready);
    const ir::Function &program = builder.module().functions[0];
    REQUIRE(program.blocks.size() == 4);
    const ir::BranchTerminator *branch =
        std::get_if<ir::BranchTerminator>(&std::get<ir::Terminator>(program.blocks[0].terminator));
    REQUIRE(branch != NULL);
    CHECK(branch->condition == condition);
    CHECK(branch->when_true == then_block);
    CHECK(branch->when_false == else_block);
    CHECK(std::holds_alternative<ir::JumpTerminator>(
        std::get<ir::Terminator>(program.blocks[1].terminator)));
    CHECK(std::holds_alternative<ir::HaltTerminator>(
        std::get<ir::Terminator>(program.blocks[3].terminator)));
    CHECK(ir::verify_module(builder.module()).valid);
}

TEST_CASE("Stage 5B builder verifies an exiting Program loop CFG")
{
    const ir::Module module = ready_loop_module();
    REQUIRE(ir::verify_module(module).valid);
    const ir::Function &program = module.functions[0];
    REQUIRE(program.blocks.size() == 4);
    const ir::JumpTerminator *preheader =
        std::get_if<ir::JumpTerminator>(&std::get<ir::Terminator>(program.blocks[0].terminator));
    const ir::BranchTerminator *condition =
        std::get_if<ir::BranchTerminator>(&std::get<ir::Terminator>(program.blocks[1].terminator));
    const ir::JumpTerminator *backedge =
        std::get_if<ir::JumpTerminator>(&std::get<ir::Terminator>(program.blocks[2].terminator));
    REQUIRE(preheader != NULL);
    REQUIRE(condition != NULL);
    REQUIRE(backedge != NULL);
    CHECK(preheader->target == ir::BlockId(program.id, 1));
    CHECK(condition->when_true == ir::BlockId(program.id, 2));
    CHECK(condition->when_false == ir::BlockId(program.id, 3));
    CHECK(backedge->target == ir::BlockId(program.id, 1));
    CHECK(std::holds_alternative<ir::HaltTerminator>(
        std::get<ir::Terminator>(program.blocks[3].terminator)));

    ir::Module no_exit = module;
    no_exit.functions[0].blocks[2].terminator = ir::Terminator(
        ir::JumpTerminator{ir::BlockId(ir::FunctionId(0), 2)});
    const ir::VerificationResult no_exit_result = ir::verify_module(no_exit);
    CAPTURE(no_exit_result.reason);
    CHECK_FALSE(no_exit_result.valid);
    CHECK(no_exit_result.reason.find("cannot reach halt") != std::string::npos);

    ir::Module body_into_condition = module;
    const ir::ValueId body_value = std::get<ir::Load>(
        body_into_condition.functions[0].blocks[2].instructions[0]).result;
    ir::BranchTerminator &invalid_branch = std::get<ir::BranchTerminator>(
        std::get<ir::Terminator>(body_into_condition.functions[0].blocks[1].terminator));
    invalid_branch.condition = body_value;
    CHECK_FALSE(ir::verify_module(body_into_condition).valid);

    ir::Module body_into_exit = module;
    const ir::ValueId exit_leak = std::get<ir::Load>(
        body_into_exit.functions[0].blocks[2].instructions[0]).result;
    body_into_exit.functions[0].blocks[3].instructions.insert(
        body_into_exit.functions[0].blocks[3].instructions.begin(),
        ir::Store{ir::StorageId(0), exit_leak});
    CHECK_FALSE(ir::verify_module(body_into_exit).valid);
}

TEST_CASE("Stage 5A verifier enforces branch visibility while allowing dominating values")
{
    ir::IRBuilder valid;
    REQUIRE(valid.register_program(SymbolRef{0, "dominates"}, "dominates").valid());
    valid.seed_external_builtins();
    const ir::StorageId global = valid.register_storage(SymbolRef{0, "value"}, scalar(TYPE_INT),
                                                        ir::StorageKind::Global);
    REQUIRE(global.valid());
    const ir::ValueId condition = valid.emit_constant(scalar(TYPE_BOOL), true);
    const ir::ValueId entry_value = valid.emit_constant(scalar(TYPE_INT), 9);
    const ir::BlockId then_block = valid.create_block();
    const ir::BlockId else_block = valid.create_block();
    const ir::BlockId join_block = valid.create_block();
    REQUIRE(valid.emit_branch(condition, then_block, else_block));
    REQUIRE(valid.select_block(then_block));
    REQUIRE(valid.emit_store(global, entry_value));
    REQUIRE(valid.emit_jump(join_block));
    REQUIRE(valid.select_block(else_block));
    REQUIRE(valid.emit_store(global, entry_value));
    REQUIRE(valid.emit_jump(join_block));
    REQUIRE(valid.select_block(join_block));
    CHECK(valid.emit_load(global).valid());
    REQUIRE(valid.emit_halt());
    valid.finalize(true);
    REQUIRE(valid.status() == ir::ModuleStatus::Ready);
    CHECK(ir::verify_module(valid.module()).valid);

    ir::Module sibling_leak = valid.module();
    sibling_leak.functions[0].blocks[2].instructions.push_back(
        ir::Store{global, entry_value});
    //The added Store is after the valid one in a sibling branch but remains
    //valid because entry_value dominates both arms.  Use a then-only value
    //instead to pin the actual cross-branch rejection.
    sibling_leak.functions[0].values.push_back(
        ir::Value{ir::ValueId(ir::FunctionId(0),
                              static_cast<std::uint32_t>(sibling_leak.functions[0].values.size())),
                  scalar(TYPE_INT), ir::ValueLocation::Constant});
    const ir::ValueId then_only = sibling_leak.functions[0].values.back().id;
    sibling_leak.functions[0].blocks[1].instructions.insert(
        sibling_leak.functions[0].blocks[1].instructions.begin(),
        ir::Constant{then_only, 3});
    sibling_leak.functions[0].blocks[2].instructions.back() = ir::Store{global, then_only};
    CHECK_FALSE(ir::verify_module(sibling_leak).valid);
}

TEST_CASE("Stage 5A CFG APIs and verifier reject malformed control flow")
{
    ir::IRBuilder non_bool;
    REQUIRE(non_bool.register_program(SymbolRef{0, "non_bool"}, "non_bool").valid());
    non_bool.seed_external_builtins();
    const ir::ValueId integer = non_bool.emit_constant(scalar(TYPE_INT), 1);
    const ir::BlockId true_block = non_bool.create_block();
    const ir::BlockId false_block = non_bool.create_block();
    CHECK_FALSE(non_bool.emit_branch(integer, true_block, false_block));
    non_bool.finalize(true);
    CHECK(non_bool.status() == ir::ModuleStatus::InvalidIR);

    ir::IRBuilder unsealed;
    REQUIRE(unsealed.register_program(SymbolRef{0, "unsealed"}, "unsealed").valid());
    unsealed.seed_external_builtins();
    const ir::BlockId alternate = unsealed.create_block();
    CHECK_FALSE(unsealed.select_block(alternate));
    unsealed.finalize(true);
    CHECK(unsealed.status() == ir::ModuleStatus::InvalidIR);

    ir::IRBuilder cross_target;
    REQUIRE(cross_target.register_program(SymbolRef{0, "cross_target"}, "cross_target").valid());
    cross_target.seed_external_builtins();
    const ir::ValueId truth = cross_target.emit_constant(scalar(TYPE_BOOL), true);
    const ir::BlockId local = cross_target.create_block();
    CHECK_FALSE(cross_target.emit_branch(truth, local, ir::BlockId(ir::FunctionId(99), 0)));
    cross_target.finalize(true);
    CHECK(cross_target.status() == ir::ModuleStatus::InvalidIR);

    ir::IRBuilder program_return;
    REQUIRE(program_return.register_program(SymbolRef{0, "program_return"}, "program_return").valid());
    program_return.seed_external_builtins();
    const ir::ValueId result = program_return.emit_constant(scalar(TYPE_INT), 1);
    REQUIRE(program_return.emit_halt());
    program_return.finalize(true);
    REQUIRE(program_return.status() == ir::ModuleStatus::Ready);
    ir::Module return_module = program_return.module();
    return_module.functions[0].blocks[0].terminator = ir::Terminator(ir::ReturnTerminator{result});
    CHECK_FALSE(ir::verify_module(return_module).valid);

    ir::Module missing_halt = program_return.module();
    missing_halt.functions[0].blocks[0].terminator =
        ir::Terminator(ir::JumpTerminator{ir::BlockId(ir::FunctionId(0), 0)});
    CHECK_FALSE(ir::verify_module(missing_halt).valid);
}

TEST_CASE("Stage 5A verifier independently rejects malformed CFG invariants")
{
    const ir::Module ready = ready_cfg_module();
    REQUIRE(ir::verify_module(ready).valid);

    ir::Module unreachable = ready;
    ir::BasicBlock orphan;
    orphan.id = ir::BlockId(ir::FunctionId(0), 4);
    orphan.terminator = ir::Terminator(ir::JumpTerminator{ir::BlockId(ir::FunctionId(0), 3)});
    unreachable.functions[0].blocks.push_back(orphan);
    const ir::VerificationResult unreachable_result = ir::verify_module(unreachable);
    CAPTURE(unreachable_result.reason);
    CHECK_FALSE(unreachable_result.valid);
    CHECK(unreachable_result.reason.find("unreachable") != std::string::npos);

    ir::Module two_halts = ready;
    two_halts.functions[0].blocks[1].terminator = ir::Terminator(ir::HaltTerminator{});
    const ir::VerificationResult two_halts_result = ir::verify_module(two_halts);
    CAPTURE(two_halts_result.reason);
    CHECK_FALSE(two_halts_result.valid);
    CHECK(two_halts_result.reason.find("exactly one halt") != std::string::npos);

    ir::Module cycle_without_exit = ready;
    cycle_without_exit.functions[0].blocks[1].terminator =
        ir::Terminator(ir::JumpTerminator{ir::BlockId(ir::FunctionId(0), 1)});
    const ir::VerificationResult cycle_without_exit_result = ir::verify_module(cycle_without_exit);
    CAPTURE(cycle_without_exit_result.reason);
    CHECK_FALSE(cycle_without_exit_result.valid);
    CHECK(cycle_without_exit_result.reason.find("cannot reach halt") != std::string::npos);

    ir::Module use_before_definition;
    {
        ir::IRBuilder builder;
        REQUIRE(builder.register_program(SymbolRef{0, "use_before"}, "use_before").valid());
        builder.seed_external_builtins();
        const ir::StorageId storage = builder.register_storage(SymbolRef{0, "value"}, scalar(TYPE_INT),
                                                               ir::StorageKind::Global);
        const ir::ValueId value = builder.emit_constant(scalar(TYPE_INT), 1);
        REQUIRE(builder.emit_store(storage, value));
        REQUIRE(builder.emit_halt());
        builder.finalize(true);
        REQUIRE(builder.status() == ir::ModuleStatus::Ready);
        use_before_definition = builder.module();
    }
    std::swap(use_before_definition.functions[0].blocks[0].instructions[0],
              use_before_definition.functions[0].blocks[0].instructions[1]);
    const ir::VerificationResult use_before_definition_result =
        ir::verify_module(use_before_definition);
    CAPTURE(use_before_definition_result.reason);
    CHECK_FALSE(use_before_definition_result.valid);
    CHECK(use_before_definition_result.reason.find("invalid store") != std::string::npos);

    const auto add_branch_local_and_use_at_join = [&ready](std::size_t source_block) {
        ir::Module module = ready;
        ir::Function &program = module.functions[0];
        const ir::ValueId local(program.id, static_cast<std::uint32_t>(program.values.size()));
        program.values.push_back(ir::Value{local, scalar(TYPE_INT), ir::ValueLocation::Constant});
        program.blocks[source_block].instructions.push_back(ir::Constant{local, 7});
        program.blocks[3].instructions.insert(program.blocks[3].instructions.begin(),
                                              ir::Store{ir::StorageId(0), local});
        return module;
    };
    const ir::VerificationResult then_merge_result =
        ir::verify_module(add_branch_local_and_use_at_join(1));
    CAPTURE(then_merge_result.reason);
    CHECK_FALSE(then_merge_result.valid);
    CHECK(then_merge_result.reason.find("invalid store") != std::string::npos);
    const ir::VerificationResult sibling_merge_result =
        ir::verify_module(add_branch_local_and_use_at_join(2));
    CAPTURE(sibling_merge_result.reason);
    CHECK_FALSE(sibling_merge_result.valid);
    CHECK(sibling_merge_result.reason.find("invalid store") != std::string::npos);

    ir::Module out_of_range_target = ready;
    ir::BranchTerminator &out_of_range_branch =
        std::get<ir::BranchTerminator>(
            std::get<ir::Terminator>(out_of_range_target.functions[0].blocks[0].terminator));
    out_of_range_branch.when_true = ir::BlockId(ir::FunctionId(0), 99);
    const ir::VerificationResult out_of_range_result = ir::verify_module(out_of_range_target);
    CAPTURE(out_of_range_result.reason);
    CHECK_FALSE(out_of_range_result.valid);
    CHECK(out_of_range_result.reason.find("invalid targets") != std::string::npos);

    ir::Module wrong_function_target = ready;
    ir::BranchTerminator &wrong_function_branch =
        std::get<ir::BranchTerminator>(
            std::get<ir::Terminator>(wrong_function_target.functions[0].blocks[0].terminator));
    wrong_function_branch.when_false = ir::BlockId(ir::FunctionId(9), 0);
    const ir::VerificationResult wrong_function_result = ir::verify_module(wrong_function_target);
    CAPTURE(wrong_function_result.reason);
    CHECK_FALSE(wrong_function_result.valid);
    CHECK(wrong_function_result.reason.find("invalid targets") != std::string::npos);

    ir::IRBuilder procedure_builder;
    REQUIRE(procedure_builder.register_program(SymbolRef{0, "procedure_blocks"},
                                               "procedure_blocks").valid());
    procedure_builder.seed_external_builtins();
    const ir::FunctionId procedure = procedure_builder.register_procedure(
        SymbolRef{0, "identity"}, "identity", scalar(TYPE_INT), {});
    REQUIRE(procedure.valid());
    REQUIRE(procedure_builder.enter_function(procedure));
    const ir::ValueId returned = procedure_builder.emit_constant(scalar(TYPE_INT), 1);
    REQUIRE(procedure_builder.emit_return(returned));
    REQUIRE(procedure_builder.leave_function());
    REQUIRE(procedure_builder.emit_halt());
    procedure_builder.finalize(true);
    REQUIRE(procedure_builder.status() == ir::ModuleStatus::Ready);
    ir::Module multi_block_procedure = procedure_builder.module();
    ir::BasicBlock extra_procedure_block;
    extra_procedure_block.id = ir::BlockId(procedure, 1);
    extra_procedure_block.terminator = ir::Terminator(ir::ReturnTerminator{returned});
    multi_block_procedure.functions[procedure.index].blocks.push_back(extra_procedure_block);
    const ir::VerificationResult procedure_blocks_result = ir::verify_module(multi_block_procedure);
    CAPTURE(procedure_blocks_result.reason);
    CHECK_FALSE(procedure_blocks_result.valid);
    CHECK(procedure_blocks_result.reason.find("unreachable") != std::string::npos);
}

TEST_CASE("Stage 5A builder rejects unfinished and procedure multi-block states atomically")
{
    ir::IRBuilder post_terminator;
    REQUIRE(post_terminator.register_program(SymbolRef{0, "post_terminator"},
                                             "post_terminator").valid());
    post_terminator.seed_external_builtins();
    REQUIRE(post_terminator.emit_halt());
    CHECK_FALSE(post_terminator.emit_constant(scalar(TYPE_INT), 1).valid());
    post_terminator.finalize(true);
    CHECK(post_terminator.status() == ir::ModuleStatus::Unsupported);
    CHECK(post_terminator.module().functions.empty());

    ir::IRBuilder orphan_block;
    REQUIRE(orphan_block.register_program(SymbolRef{0, "orphan_block"}, "orphan_block").valid());
    orphan_block.seed_external_builtins();
    REQUIRE(orphan_block.create_block().valid());
    REQUIRE(orphan_block.emit_halt());
    orphan_block.finalize(true);
    CHECK(orphan_block.status() == ir::ModuleStatus::InvalidIR);
    CHECK(orphan_block.module().functions.empty());

    ir::IRBuilder duplicate_halt;
    REQUIRE(duplicate_halt.register_program(SymbolRef{0, "duplicate_halt"},
                                            "duplicate_halt").valid());
    duplicate_halt.seed_external_builtins();
    const ir::BlockId second_halt_block = duplicate_halt.create_block();
    REQUIRE(duplicate_halt.emit_halt());
    REQUIRE(duplicate_halt.select_block(second_halt_block));
    REQUIRE(duplicate_halt.emit_halt());
    duplicate_halt.finalize(true);
    CHECK(duplicate_halt.status() == ir::ModuleStatus::InvalidIR);
    CHECK(duplicate_halt.module().functions.empty());

    ir::IRBuilder procedure_blocks;
    REQUIRE(procedure_blocks.register_program(SymbolRef{0, "builder_procedure"},
                                              "builder_procedure").valid());
    procedure_blocks.seed_external_builtins();
    const ir::FunctionId procedure = procedure_blocks.register_procedure(
        SymbolRef{0, "procedure"}, "procedure", scalar(TYPE_INT), {});
    REQUIRE(procedure_blocks.enter_function(procedure));
    REQUIRE(procedure_blocks.create_block().valid());
    const ir::ValueId returned = procedure_blocks.emit_constant(scalar(TYPE_INT), 1);
    REQUIRE(procedure_blocks.emit_return(returned));
    REQUIRE(procedure_blocks.leave_function());
    REQUIRE(procedure_blocks.emit_halt());
    procedure_blocks.finalize(true);
    CHECK(procedure_blocks.status() == ir::ModuleStatus::InvalidIR);
    CHECK(procedure_blocks.module().functions.empty());
}

TEST_CASE("Stage 4A builder publishes one typed scalar straight-line module")
{
    ir::IRBuilder builder;
    const SymbolRef program{0, "ir_program"};
    REQUIRE(builder.register_program(program, "ir_program").valid());
    builder.seed_external_builtins();

    const SymbolRef global{0, "global_value"};
    const ir::StorageId global_storage = builder.register_storage(
        global, scalar(TYPE_INT), ir::StorageKind::Global);
    REQUIRE(global_storage.valid());
    const ir::ValueId one = builder.emit_constant(scalar(TYPE_INT), 1);
    REQUIRE(one.valid());
    REQUIRE(builder.emit_store(global_storage, one));
    const ir::ValueId loaded = builder.emit_load(global_storage);
    REQUIRE(loaded.valid());
    const ir::ValueId negated = builder.emit_unary(ir::UnaryOp::Negate, loaded);
    REQUIRE(negated.valid());
    const ir::ValueId sum = builder.emit_binary(ir::BinaryOp::Add, loaded, negated);
    REQUIRE(sum.valid());
    const ir::ValueId float_sum = builder.emit_cast(ir::CastOp::IntToFloat, sum);
    REQUIRE(float_sum.valid());
    const ir::FunctionId sqrt = builder.function_for(SymbolRef{0, "sqrt"});
    REQUIRE(sqrt.valid());
    const ir::ValueId sqrt_result = builder.emit_call(sqrt, std::vector<ir::ValueId>{sum});
    REQUIRE(sqrt_result.valid());
    REQUIRE(builder.emit_halt());
    builder.finalize(true);

    CAPTURE(builder.reason());
    CHECK(builder.status() == ir::ModuleStatus::Ready);
    CHECK(builder.module().functions.size() == 10);
    CHECK(builder.module().storages.size() == 1);
    CHECK(ir::verify_module(builder.module()).valid);
}

TEST_CASE("Stage 4A builder rejects invalid work atomically and prioritizes frontend errors")
{
    ir::IRBuilder unsupported;
    REQUIRE(unsupported.register_program(SymbolRef{0, "unsupported"}, "unsupported").valid());
    unsupported.seed_external_builtins();
    unsupported.mark_unsupported("array slice");
    unsupported.finalize(true);
    CHECK(unsupported.status() == ir::ModuleStatus::Unsupported);
    CHECK(unsupported.module().functions.empty());
    CHECK(unsupported.module().storages.empty());

    ir::IRBuilder frontend;
    REQUIRE(frontend.register_program(SymbolRef{0, "frontend"}, "frontend").valid());
    frontend.seed_external_builtins();
    frontend.mark_unsupported("array slice");
    frontend.mark_frontend_error();
    frontend.finalize(true);
    CHECK(frontend.status() == ir::ModuleStatus::FrontendError);
    CHECK(frontend.module().functions.empty());

    ir::IRBuilder invalid;
    REQUIRE(invalid.register_program(SymbolRef{0, "invalid"}, "invalid").valid());
    invalid.seed_external_builtins();
    const ir::ValueId string_value = invalid.emit_constant(scalar(TYPE_STRING), std::string("x"));
    CHECK_FALSE(invalid.emit_unary(ir::UnaryOp::Negate, string_value).valid());
    invalid.finalize(true);
    CHECK(invalid.status() == ir::ModuleStatus::InvalidIR);
    CHECK(invalid.module().functions.empty());

    ir::IRBuilder invalid_over_unsupported;
    REQUIRE(invalid_over_unsupported.register_program(SymbolRef{0, "priority"}, "priority").valid());
    invalid_over_unsupported.seed_external_builtins();
    const ir::ValueId invalid_string = invalid_over_unsupported.emit_constant(
        scalar(TYPE_STRING), std::string("x"));
    CHECK_FALSE(invalid_over_unsupported.emit_unary(ir::UnaryOp::Negate,
                                                    invalid_string).valid());
    invalid_over_unsupported.mark_unsupported("later unsupported feature");
    invalid_over_unsupported.finalize(true);
    CHECK(invalid_over_unsupported.status() == ir::ModuleStatus::InvalidIR);
}

TEST_CASE("Stage 4A builder registers procedure parameters, returns, and exact calls")
{
    ir::IRBuilder builder;
    REQUIRE(builder.register_program(SymbolRef{0, "procedure_program"}, "procedure_program").valid());
    builder.seed_external_builtins();
    const SymbolRef procedure{0, "identity"};
    const SymbolRef parameter{1, "value"};
    const ir::FunctionId identity = builder.register_procedure(
        procedure, "identity", scalar(TYPE_INT),
        std::vector<std::pair<SymbolRef, value_shape>>{{parameter, scalar(TYPE_INT)}});
    REQUIRE(identity.valid());
    REQUIRE(builder.enter_function(identity));
    const ir::StorageId parameter_storage = builder.storage_for(parameter);
    REQUIRE(parameter_storage.valid());
    const ir::ValueId loaded = builder.emit_load(parameter_storage);
    REQUIRE(loaded.valid());
    REQUIRE(builder.emit_return(loaded));
    REQUIRE(builder.leave_function());
    const ir::ValueId argument = builder.emit_constant(scalar(TYPE_INT), 4);
    REQUIRE(argument.valid());
    CHECK(builder.emit_call(identity, std::vector<ir::ValueId>{argument}).valid());
    REQUIRE(builder.emit_halt());
    builder.finalize(true);
    CHECK(builder.status() == ir::ModuleStatus::Ready);
    CHECK(ir::verify_module(builder.module()).valid);

    ir::Module duplicate_parameter = builder.module();
    duplicate_parameter.functions[identity.index].parameters.push_back(
        duplicate_parameter.functions[identity.index].parameters[0]);
    CHECK_FALSE(ir::verify_module(duplicate_parameter).valid);

    ir::Module orphan_local = builder.module();
    ir::Storage orphan;
    orphan.id = ir::StorageId(static_cast<std::uint32_t>(orphan_local.storages.size()));
    orphan.symbol = SymbolRef{1, "orphan"};
    orphan.type = scalar(TYPE_INT);
    orphan.kind = ir::StorageKind::Local;
    orphan.owner = identity;
    orphan_local.storages.push_back(orphan);
    CHECK_FALSE(ir::verify_module(orphan_local).valid);

    ir::Module mismatched_name = builder.module();
    mismatched_name.functions[identity.index].name = "different";
    CHECK_FALSE(ir::verify_module(mismatched_name).valid);
}

TEST_CASE("Stage 4A program is result-free and cannot be called")
{
    ir::IRBuilder builder;
    const ir::FunctionId program = builder.register_program(SymbolRef{0, "root"}, "root");
    REQUIRE(program.valid());
    builder.seed_external_builtins();
    CHECK_FALSE(builder.emit_call(program, {}).valid());
    builder.finalize(true);
    CHECK(builder.status() == ir::ModuleStatus::InvalidIR);
    CHECK(builder.module().functions.empty());

    ir::IRBuilder ready;
    REQUIRE(ready.register_program(SymbolRef{0, "root_ready"}, "root_ready").valid());
    ready.seed_external_builtins();
    REQUIRE(ready.emit_halt());
    ready.finalize(true);
    REQUIRE(ready.status() == ir::ModuleStatus::Ready);
    REQUIRE(ready.module().functions[0].return_type.element_type == TYPE_NONE);
    CHECK_FALSE(ir::is_ready_type(ready.module().functions[0].return_type));
    ir::Module program_call = ready.module();
    program_call.functions[0].values.push_back(
        ir::Value{ir::ValueId(ir::FunctionId(0), 0), scalar(TYPE_INT), ir::ValueLocation::Call});
    program_call.functions[0].blocks[0].instructions.push_back(
        ir::Call{ir::ValueId(ir::FunctionId(0), 0), ir::FunctionId(0), {}});
    CHECK_FALSE(ir::verify_module(program_call).valid);
}

TEST_CASE("Stage 4A storage registration and procedure context are exact")
{
    ir::IRBuilder identical;
    REQUIRE(identical.register_program(SymbolRef{0, "storage_root"}, "storage_root").valid());
    identical.seed_external_builtins();
    const SymbolRef global{0, "g"};
    const ir::StorageId first = identical.register_storage(global, scalar(TYPE_INT),
                                                            ir::StorageKind::Global);
    REQUIRE(first.valid());
    CHECK(identical.register_storage(global, scalar(TYPE_INT), ir::StorageKind::Global) == first);
    REQUIRE(identical.emit_halt());
    identical.finalize(true);
    CHECK(identical.status() == ir::ModuleStatus::Ready);

    ir::IRBuilder ad_hoc_parameter;
    REQUIRE(ad_hoc_parameter.register_program(SymbolRef{0, "parameter_root"},
                                               "parameter_root").valid());
    ad_hoc_parameter.seed_external_builtins();
    CHECK_FALSE(ad_hoc_parameter.register_storage(SymbolRef{0, "not_a_param"}, scalar(TYPE_INT),
                                                  ir::StorageKind::Parameter).valid());
    ad_hoc_parameter.finalize(true);
    CHECK(ad_hoc_parameter.status() == ir::ModuleStatus::InvalidIR);

    ir::IRBuilder conflicting;
    REQUIRE(conflicting.register_program(SymbolRef{0, "conflict_root"}, "conflict_root").valid());
    conflicting.seed_external_builtins();
    REQUIRE(conflicting.register_storage(global, scalar(TYPE_INT), ir::StorageKind::Global).valid());
    CHECK_FALSE(conflicting.register_storage(global, scalar(TYPE_FLOAT),
                                             ir::StorageKind::Global).valid());
    conflicting.finalize(true);
    CHECK(conflicting.status() == ir::ModuleStatus::InvalidIR);

    ir::IRBuilder unbalanced;
    REQUIRE(unbalanced.register_program(SymbolRef{0, "context_root"}, "context_root").valid());
    unbalanced.seed_external_builtins();
    const ir::FunctionId procedure = unbalanced.register_procedure(
        SymbolRef{0, "done"}, "done", scalar(TYPE_INT), {});
    REQUIRE(procedure.valid());
    REQUIRE(unbalanced.enter_function(procedure));
    const ir::ValueId value = unbalanced.emit_constant(scalar(TYPE_INT), 1);
    REQUIRE(value.valid());
    REQUIRE(unbalanced.emit_return(value));
    unbalanced.finalize(true); // Deliberately omit leave_function().
    CHECK(unbalanced.status() == ir::ModuleStatus::InvalidIR);
}

TEST_CASE("Stage 4A canonical identities and invalid builder inputs are rejected")
{
    ir::IRBuilder bad_procedure;
    REQUIRE(bad_procedure.register_program(SymbolRef{0, "procedure_root"},
                                           "procedure_root").valid());
    bad_procedure.seed_external_builtins();
    CHECK_FALSE(bad_procedure.register_procedure(SymbolRef{-1, "bad"}, "bad",
                                                  scalar(TYPE_INT), {}).valid());
    bad_procedure.finalize(true);
    CHECK(bad_procedure.status() == ir::ModuleStatus::InvalidIR);

    ir::IRBuilder wrong_name;
    REQUIRE(wrong_name.register_program(SymbolRef{0, "name_root"}, "name_root").valid());
    wrong_name.seed_external_builtins();
    CHECK_FALSE(wrong_name.register_procedure(SymbolRef{0, "declared"}, "different",
                                               scalar(TYPE_INT), {}).valid());
    wrong_name.finalize(true);
    CHECK(wrong_name.status() == ir::ModuleStatus::InvalidIR);

    ir::IRBuilder duplicate;
    REQUIRE(duplicate.register_program(SymbolRef{0, "duplicate_root"},
                                       "duplicate_root").valid());
    duplicate.seed_external_builtins();
    REQUIRE(duplicate.register_procedure(SymbolRef{0, "same"}, "same",
                                         scalar(TYPE_INT), {}).valid());
    CHECK_FALSE(duplicate.register_procedure(SymbolRef{0, "same"}, "same",
                                              scalar(TYPE_INT), {}).valid());
    duplicate.finalize(true);
    CHECK(duplicate.status() == ir::ModuleStatus::InvalidIR);

    ir::IRBuilder bad_global;
    REQUIRE(bad_global.register_program(SymbolRef{0, "global_root"}, "global_root").valid());
    bad_global.seed_external_builtins();
    CHECK_FALSE(bad_global.register_storage(SymbolRef{42, "g"}, scalar(TYPE_INT),
                                             ir::StorageKind::Global).valid());
    bad_global.finalize(true);
    CHECK(bad_global.status() == ir::ModuleStatus::InvalidIR);

    ir::IRBuilder bad_local;
    REQUIRE(bad_local.register_program(SymbolRef{0, "local_root"}, "local_root").valid());
    bad_local.seed_external_builtins();
    CHECK_FALSE(bad_local.register_storage(SymbolRef{1, "local"}, scalar(TYPE_INT),
                                            ir::StorageKind::Local).valid());
    bad_local.finalize(true);
    CHECK(bad_local.status() == ir::ModuleStatus::InvalidIR);

    ir::IRBuilder cross_kind;
    REQUIRE(cross_kind.register_program(SymbolRef{0, "cross_root"}, "cross_root").valid());
    cross_kind.seed_external_builtins();
    CHECK_FALSE(cross_kind.register_storage(SymbolRef{0, "getinteger"}, scalar(TYPE_INT),
                                             ir::StorageKind::Global).valid());
    cross_kind.finalize(true);
    CHECK(cross_kind.status() == ir::ModuleStatus::InvalidIR);

    ir::IRBuilder unsupported_shape;
    REQUIRE(unsupported_shape.register_program(SymbolRef{0, "shape_root"},
                                               "shape_root").valid());
    unsupported_shape.seed_external_builtins();
    value_shape array_type{TYPE_INT, true, 2};
    CHECK_FALSE(unsupported_shape.register_storage(SymbolRef{0, "array"}, array_type,
                                                    ir::StorageKind::Global).valid());
    unsupported_shape.finalize(true);
    CHECK(unsupported_shape.status() == ir::ModuleStatus::Unsupported);

    ir::IRBuilder wrong_return;
    REQUIRE(wrong_return.register_program(SymbolRef{0, "return_root"}, "return_root").valid());
    wrong_return.seed_external_builtins();
    const ir::FunctionId procedure = wrong_return.register_procedure(
        SymbolRef{0, "returns_int"}, "returns_int", scalar(TYPE_INT), {});
    REQUIRE(procedure.valid());
    REQUIRE(wrong_return.enter_function(procedure));
    const ir::ValueId value = wrong_return.emit_constant(scalar(TYPE_FLOAT), 1.0F);
    REQUIRE(value.valid());
    CHECK_FALSE(wrong_return.emit_return(value));
    REQUIRE(wrong_return.leave_function());
    wrong_return.finalize(true);
    CHECK(wrong_return.status() == ir::ModuleStatus::InvalidIR);
}

TEST_CASE("Stage 4A verifier enforces canonical SymbolRef scope and cross-kind identity")
{
    ir::IRBuilder builder;
    REQUIRE(builder.register_program(SymbolRef{0, "identity_root"}, "identity_root").valid());
    builder.seed_external_builtins();
    const ir::StorageId global = builder.register_storage(SymbolRef{0, "global"},
                                                           scalar(TYPE_INT),
                                                           ir::StorageKind::Global);
    REQUIRE(global.valid());
    const ir::FunctionId procedure = builder.register_procedure(
        SymbolRef{0, "identity"}, "identity", scalar(TYPE_INT),
        {{SymbolRef{1, "parameter"}, scalar(TYPE_INT)}});
    REQUIRE(procedure.valid());
    REQUIRE(builder.enter_function(procedure));
    const ir::StorageId local = builder.register_storage(SymbolRef{1, "local"},
                                                          scalar(TYPE_INT),
                                                          ir::StorageKind::Local);
    REQUIRE(local.valid());
    const ir::ValueId loaded = builder.emit_load(builder.storage_for(SymbolRef{1, "parameter"}));
    REQUIRE(loaded.valid());
    REQUIRE(builder.emit_return(loaded));
    REQUIRE(builder.leave_function());
    REQUIRE(builder.emit_halt());
    builder.finalize(true);
    REQUIRE(builder.status() == ir::ModuleStatus::Ready);

    ir::Module negative_function = builder.module();
    negative_function.functions[procedure.index].symbol.scope_id = -1;
    CHECK_FALSE(ir::verify_module(negative_function).valid);

    ir::Module wrong_global_scope = builder.module();
    wrong_global_scope.storages[global.index].symbol.scope_id = 42;
    CHECK_FALSE(ir::verify_module(wrong_global_scope).valid);

    ir::Module root_parameter = builder.module();
    const ir::StorageId parameter = root_parameter.functions[procedure.index].parameters[0];
    root_parameter.storages[parameter.index].symbol.scope_id = 0;
    CHECK_FALSE(ir::verify_module(root_parameter).valid);

    ir::Module root_local = builder.module();
    root_local.storages[local.index].symbol.scope_id = 0;
    CHECK_FALSE(ir::verify_module(root_local).valid);

    ir::Module cross_kind = builder.module();
    cross_kind.storages[global.index].symbol = cross_kind.functions[procedure.index].symbol;
    CHECK_FALSE(ir::verify_module(cross_kind).valid);
}

TEST_CASE("Stage 4A finalization keeps invalid priority and preserves first unsupported reason")
{
    ir::IRBuilder priority;
    REQUIRE(priority.register_program(SymbolRef{0, "priority_root"}, "priority_root").valid());
    priority.seed_external_builtins();
    priority.mark_unsupported("first unsupported");
    priority.mark_unsupported("second unsupported");
    priority.mark_invalid("manual invariant failure");
    priority.finalize(true);
    CHECK(priority.status() == ir::ModuleStatus::InvalidIR);
    CHECK(priority.module().functions.empty());

    ir::IRBuilder unsupported;
    REQUIRE(unsupported.register_program(SymbolRef{0, "unsupported_root"}, "unsupported_root").valid());
    unsupported.seed_external_builtins();
    unsupported.mark_unsupported("first unsupported");
    unsupported.mark_unsupported("second unsupported");
    unsupported.finalize(true);
    CHECK(unsupported.status() == ir::ModuleStatus::Unsupported);
    CHECK(unsupported.reason() == "first unsupported");
    CHECK(unsupported.module().functions.empty());

    ir::IRBuilder invalid_fallthrough;
    REQUIRE(invalid_fallthrough.register_program(SymbolRef{0, "fallthrough_root"},
                                                 "fallthrough_root").valid());
    invalid_fallthrough.seed_external_builtins();
    REQUIRE(invalid_fallthrough.register_procedure(SymbolRef{0, "falls"}, "falls",
                                                    scalar(TYPE_INT), {}).valid());
    invalid_fallthrough.mark_invalid("preexisting internal failure");
    invalid_fallthrough.finalize(true);
    CHECK(invalid_fallthrough.status() == ir::ModuleStatus::InvalidIR);
    CHECK(invalid_fallthrough.reason() == "preexisting internal failure");
    CHECK(invalid_fallthrough.module().functions.empty());
}

TEST_CASE("Stage 4A closed procedure blocks reject every subsequent emitter")
{
    for (int emitter = 0; emitter < 7; emitter++)
    {
        CAPTURE(emitter);
        ir::IRBuilder builder;
        REQUIRE(builder.register_program(SymbolRef{0, "closed_root"}, "closed_root").valid());
        builder.seed_external_builtins();
        const SymbolRef parameter{1, "parameter"};
        const ir::FunctionId procedure = builder.register_procedure(
            SymbolRef{0, "closed"}, "closed", scalar(TYPE_INT),
            std::vector<std::pair<SymbolRef, value_shape>>{{parameter, scalar(TYPE_INT)}});
        REQUIRE(procedure.valid());
        REQUIRE(builder.enter_function(procedure));
        const ir::StorageId parameter_storage = builder.storage_for(parameter);
        const ir::StorageId local_storage = builder.register_storage(
            SymbolRef{1, "local"}, scalar(TYPE_INT), ir::StorageKind::Local);
        REQUIRE(parameter_storage.valid());
        REQUIRE(local_storage.valid());
        const ir::ValueId one = builder.emit_constant(scalar(TYPE_INT), 1);
        REQUIRE(one.valid());
        REQUIRE(builder.emit_return(one));
        bool accepted = false;
        switch (emitter)
        {
        case 0: accepted = builder.emit_constant(scalar(TYPE_INT), 2).valid(); break;
        case 1: accepted = builder.emit_load(parameter_storage).valid(); break;
        case 2: accepted = builder.emit_store(local_storage, one); break;
        case 3: accepted = builder.emit_unary(ir::UnaryOp::Negate, one).valid(); break;
        case 4: accepted = builder.emit_binary(ir::BinaryOp::Add, one, one).valid(); break;
        case 5: accepted = builder.emit_cast(ir::CastOp::IntToFloat, one).valid(); break;
        case 6:
            accepted = builder.emit_call(builder.function_for(SymbolRef{0, "sqrt"}), {one}).valid();
            break;
        }
        CHECK_FALSE(accepted);
        REQUIRE(builder.leave_function());
        builder.finalize(true);
        CHECK(builder.status() == ir::ModuleStatus::Unsupported);
        CHECK(builder.module().functions.empty());
    }
}

TEST_CASE("Stage 4A closed program blocks reject every subsequent emitter")
{
    for (int emitter = 0; emitter < 7; emitter++)
    {
        CAPTURE(emitter);
        ir::IRBuilder builder;
        REQUIRE(builder.register_program(SymbolRef{0, "halted_root"}, "halted_root").valid());
        builder.seed_external_builtins();
        const ir::StorageId global = builder.register_storage(SymbolRef{0, "g"}, scalar(TYPE_INT),
                                                               ir::StorageKind::Global);
        REQUIRE(global.valid());
        const ir::ValueId one = builder.emit_constant(scalar(TYPE_INT), 1);
        REQUIRE(one.valid());
        REQUIRE(builder.emit_halt());
        bool accepted = false;
        switch (emitter)
        {
        case 0: accepted = builder.emit_constant(scalar(TYPE_INT), 2).valid(); break;
        case 1: accepted = builder.emit_load(global).valid(); break;
        case 2: accepted = builder.emit_store(global, one); break;
        case 3: accepted = builder.emit_unary(ir::UnaryOp::Negate, one).valid(); break;
        case 4: accepted = builder.emit_binary(ir::BinaryOp::Add, one, one).valid(); break;
        case 5: accepted = builder.emit_cast(ir::CastOp::IntToFloat, one).valid(); break;
        case 6:
            accepted = builder.emit_call(builder.function_for(SymbolRef{0, "sqrt"}), {one}).valid();
            break;
        }
        CHECK_FALSE(accepted);
        builder.finalize(true);
        CHECK(builder.status() == ir::ModuleStatus::Unsupported);
        CHECK(builder.module().functions.empty());
    }
}

TEST_CASE("Stage 4A verifier catches stale identity and wrong instruction type")
{
    ir::Module malformed;
    ir::Function program;
    program.id = ir::FunctionId(0);
    program.kind = ir::FunctionKind::Program;
    program.name = "bad";
    program.symbol = SymbolRef{0, "bad"};
    program.return_type = scalar(TYPE_BOOL);
    program.blocks.push_back(ir::BasicBlock{ir::BlockId(program.id, 0), {},
                                            ir::Terminator(ir::HaltTerminator{})});
    malformed.functions.push_back(program);
    CHECK_FALSE(ir::verify_module(malformed).valid); // catalog externals are mandatory.

    ir::IRBuilder builder;
    REQUIRE(builder.register_program(SymbolRef{0, "verified"}, "verified").valid());
    builder.seed_external_builtins();
    REQUIRE(builder.emit_halt());
    builder.finalize(true);
    REQUIRE(builder.status() == ir::ModuleStatus::Ready);
    ir::Module stale = builder.module();
    stale.functions[0].blocks[0].instructions.push_back(
        ir::Load{ir::ValueId(ir::FunctionId(0), 99), ir::StorageId(99)});
    CHECK_FALSE(ir::verify_module(stale).valid);
}

TEST_CASE("Stage 5C procedure CFG permits branching returns and rejects fallthrough")
{
    ir::IRBuilder builder;
    REQUIRE(builder.register_program(SymbolRef{0, "procedure_cfg"}, "procedure_cfg").valid());
    builder.seed_external_builtins();
    const ir::FunctionId procedure = builder.register_procedure(
        SymbolRef{0, "choose"}, "choose", scalar(TYPE_INT), {});
    REQUIRE(procedure.valid());
    REQUIRE(builder.enter_function(procedure));
    const ir::ValueId condition = builder.emit_constant(scalar(TYPE_BOOL), true);
    const ir::BlockId then_block = builder.create_block();
    const ir::BlockId else_block = builder.create_block();
    REQUIRE(builder.emit_branch(condition, then_block, else_block));
    REQUIRE(builder.select_block(then_block));
    const ir::ValueId one = builder.emit_constant(scalar(TYPE_INT), 1);
    REQUIRE(builder.emit_return(one));
    REQUIRE(builder.select_block(else_block));
    const ir::ValueId zero = builder.emit_constant(scalar(TYPE_INT), 0);
    REQUIRE(builder.emit_return(zero));
    REQUIRE(builder.leave_function());
    REQUIRE(builder.emit_halt());
    builder.finalize(true);
    REQUIRE(builder.status() == ir::ModuleStatus::Ready);
    CHECK(ir::verify_module(builder.module()).valid);

    ir::IRBuilder fallthrough;
    REQUIRE(fallthrough.register_program(SymbolRef{0, "fallthrough"}, "fallthrough").valid());
    fallthrough.seed_external_builtins();
    const ir::FunctionId missing_return = fallthrough.register_procedure(
        SymbolRef{0, "missing"}, "missing", scalar(TYPE_INT), {});
    REQUIRE(missing_return.valid());
    REQUIRE(fallthrough.enter_function(missing_return));
    REQUIRE(fallthrough.leave_function());
    REQUIRE(fallthrough.emit_halt());
    fallthrough.finalize(true);
    CHECK(fallthrough.status() == ir::ModuleStatus::Unsupported);
    CHECK(fallthrough.module().functions.empty());
}

TEST_CASE("Stage 5C unreachable lowering suppression is balanced and inert")
{
    ir::IRBuilder builder;
    REQUIRE(builder.register_program(SymbolRef{0, "suppressed"}, "suppressed").valid());
    builder.seed_external_builtins();
    const ir::FunctionId procedure = builder.register_procedure(
        SymbolRef{0, "done"}, "done", scalar(TYPE_INT), {});
    REQUIRE(builder.enter_function(procedure));
    const ir::ValueId one = builder.emit_constant(scalar(TYPE_INT), 1);
    REQUIRE(builder.emit_return(one));
    REQUIRE(builder.begin_unreachable_statement());
    REQUIRE(builder.begin_unreachable_statement());
    CHECK_FALSE(builder.emit_constant(scalar(TYPE_FLOAT), 1.0F).valid());
    builder.mark_unsupported("dead unsupported operation");
    builder.mark_invalid("dead invalid lowering result");
    REQUIRE(builder.end_unreachable_statement());
    REQUIRE(builder.end_unreachable_statement());
    REQUIRE(builder.leave_function());
    REQUIRE(builder.emit_halt());
    builder.finalize(true);
    CHECK(builder.status() == ir::ModuleStatus::Ready);

    ir::IRBuilder underflow;
    REQUIRE(underflow.register_program(SymbolRef{0, "underflow"}, "underflow").valid());
    underflow.seed_external_builtins();
    CHECK_FALSE(underflow.end_unreachable_statement());
    underflow.finalize(true);
    CHECK(underflow.status() == ir::ModuleStatus::InvalidIR);

    ir::IRBuilder leaked;
    REQUIRE(leaked.register_program(SymbolRef{0, "leaked"}, "leaked").valid());
    leaked.seed_external_builtins();
    const ir::FunctionId leaked_procedure = leaked.register_procedure(
        SymbolRef{0, "leaked_done"}, "leaked_done", scalar(TYPE_INT), {});
    REQUIRE(leaked.enter_function(leaked_procedure));
    const ir::ValueId leaked_one = leaked.emit_constant(scalar(TYPE_INT), 1);
    REQUIRE(leaked.emit_return(leaked_one));
    REQUIRE(leaked.begin_unreachable_statement());
    REQUIRE(leaked.leave_function());
    leaked.finalize(true);
    CHECK(leaked.status() == ir::ModuleStatus::InvalidIR);

    ir::IRBuilder frontend;
    REQUIRE(frontend.register_program(SymbolRef{0, "dead_frontend"}, "dead_frontend").valid());
    frontend.seed_external_builtins();
    const ir::FunctionId frontend_procedure = frontend.register_procedure(
        SymbolRef{0, "dead_frontend_done"}, "dead_frontend_done", scalar(TYPE_INT), {});
    REQUIRE(frontend.enter_function(frontend_procedure));
    const ir::ValueId frontend_one = frontend.emit_constant(scalar(TYPE_INT), 1);
    REQUIRE(frontend.emit_return(frontend_one));
    REQUIRE(frontend.begin_unreachable_statement());
    frontend.mark_frontend_error();
    REQUIRE(frontend.end_unreachable_statement());
    REQUIRE(frontend.leave_function());
    CHECK_FALSE(frontend.emit_halt());
    frontend.finalize(false);
    CHECK(frontend.status() == ir::ModuleStatus::FrontendError);
    CHECK(frontend.module().functions.empty());
}

TEST_CASE("Stage 5C finalization prioritizes malformed/orphan procedure CFGs")
{
    ir::IRBuilder branch_fallthrough;
    REQUIRE(branch_fallthrough.register_program(SymbolRef{0, "branch_fallthrough"},
                                                 "branch_fallthrough").valid());
    branch_fallthrough.seed_external_builtins();
    const ir::FunctionId procedure = branch_fallthrough.register_procedure(
        SymbolRef{0, "branchy"}, "branchy", scalar(TYPE_INT), {});
    REQUIRE(branch_fallthrough.enter_function(procedure));
    const ir::ValueId condition = branch_fallthrough.emit_constant(scalar(TYPE_BOOL), true);
    const ir::BlockId returned_arm = branch_fallthrough.create_block();
    const ir::BlockId open_arm = branch_fallthrough.create_block();
    REQUIRE(branch_fallthrough.emit_branch(condition, returned_arm, open_arm));
    REQUIRE(branch_fallthrough.select_block(returned_arm));
    const ir::ValueId one = branch_fallthrough.emit_constant(scalar(TYPE_INT), 1);
    REQUIRE(branch_fallthrough.emit_return(one));
    REQUIRE(branch_fallthrough.leave_function());
    REQUIRE(branch_fallthrough.emit_halt());
    branch_fallthrough.finalize(true);
    CHECK(branch_fallthrough.status() == ir::ModuleStatus::Unsupported);

    ir::IRBuilder orphan;
    REQUIRE(orphan.register_program(SymbolRef{0, "sealed_orphan"}, "sealed_orphan").valid());
    orphan.seed_external_builtins();
    const ir::FunctionId orphan_procedure = orphan.register_procedure(
        SymbolRef{0, "sealed"}, "sealed", scalar(TYPE_INT), {});
    REQUIRE(orphan.enter_function(orphan_procedure));
    const ir::BlockId sealed_orphan = orphan.create_block();
    const ir::BlockId reachable_open = orphan.create_block();
    REQUIRE(orphan.emit_jump(reachable_open));
    REQUIRE(orphan.select_block(sealed_orphan));
    const ir::ValueId returned = orphan.emit_constant(scalar(TYPE_INT), 1);
    REQUIRE(orphan.emit_return(returned));
    REQUIRE(orphan.select_block(reachable_open));
    REQUIRE(orphan.leave_function());
    REQUIRE(orphan.emit_halt());
    orphan.finalize(true);
    CHECK(orphan.status() == ir::ModuleStatus::InvalidIR);
    CHECK(orphan.reason().find("unreachable") != std::string::npos);

    ir::IRBuilder cross_target;
    REQUIRE(cross_target.register_program(SymbolRef{0, "cross_target"}, "cross_target").valid());
    cross_target.seed_external_builtins();
    const ir::FunctionId cross_procedure = cross_target.register_procedure(
        SymbolRef{0, "cross"}, "cross", scalar(TYPE_INT), {});
    REQUIRE(cross_target.enter_function(cross_procedure));
    const ir::BlockId cross_open = cross_target.create_block();
    REQUIRE(cross_target.emit_jump(cross_open));
    REQUIRE(cross_target.select_block(cross_open));
    REQUIRE(cross_target.leave_function());
    REQUIRE(cross_target.emit_halt());
    ir::Function &corrupt_procedure =
        cross_target.scratch_module.functions[cross_procedure.index];
    corrupt_procedure.blocks[0].terminator = ir::Terminator(
        ir::JumpTerminator{ir::BlockId(cross_target.program_function(), 0)});
    cross_target.finalize(true);
    CHECK(cross_target.status() == ir::ModuleStatus::InvalidIR);
    CHECK(cross_target.reason().find("invalid block or target") != std::string::npos);
}

TEST_CASE("Stage 5C verifier rejects malformed procedure control flow")
{
    ir::IRBuilder builder;
    REQUIRE(builder.register_program(SymbolRef{0, "procedure_verify"}, "procedure_verify").valid());
    builder.seed_external_builtins();
    const ir::FunctionId procedure = builder.register_procedure(
        SymbolRef{0, "choose_verify"}, "choose_verify", scalar(TYPE_INT), {});
    REQUIRE(builder.enter_function(procedure));
    const ir::ValueId condition = builder.emit_constant(scalar(TYPE_BOOL), true);
    const ir::BlockId then_block = builder.create_block();
    const ir::BlockId else_block = builder.create_block();
    REQUIRE(builder.emit_branch(condition, then_block, else_block));
    REQUIRE(builder.select_block(then_block));
    const ir::ValueId then_value = builder.emit_constant(scalar(TYPE_INT), 1);
    REQUIRE(builder.emit_return(then_value));
    REQUIRE(builder.select_block(else_block));
    const ir::ValueId else_value = builder.emit_constant(scalar(TYPE_INT), 0);
    REQUIRE(builder.emit_return(else_value));
    REQUIRE(builder.leave_function());
    REQUIRE(builder.emit_call(procedure, {}).valid());
    REQUIRE(builder.emit_halt());
    builder.finalize(true);
    REQUIRE(builder.status() == ir::ModuleStatus::Ready);
    REQUIRE(ir::verify_module(builder.module()).valid);

    ir::Module halted = builder.module();
    halted.functions[procedure.index].blocks[0].terminator = ir::Terminator(ir::HaltTerminator{});
    CHECK_FALSE(ir::verify_module(halted).valid);

    ir::Module bad_target = builder.module();
    bad_target.functions[procedure.index].blocks[0].terminator = ir::Terminator(
        ir::JumpTerminator{ir::BlockId(builder.program_function(), 0)});
    CHECK_FALSE(ir::verify_module(bad_target).valid);

    ir::Module no_return_cycle = builder.module();
    ir::Function &cyclic = no_return_cycle.functions[procedure.index];
    cyclic.blocks.resize(1);
    cyclic.blocks[0].terminator = ir::Terminator(
        ir::JumpTerminator{ir::BlockId(procedure, 0)});
    CHECK_FALSE(ir::verify_module(no_return_cycle).valid);

    ir::Module dominance_leak = builder.module();
    ir::ReturnTerminator &else_return = std::get<ir::ReturnTerminator>(
        std::get<ir::Terminator>(dominance_leak.functions[procedure.index].blocks[2].terminator));
    else_return.value = then_value;
    CHECK_FALSE(ir::verify_module(dominance_leak).valid);
}

TEST_CASE("Stage 5C verifier accepts a reachable mutual-call SCC")
{
    ir::IRBuilder builder;
    REQUIRE(builder.register_program(SymbolRef{0, "mutual"}, "mutual").valid());
    builder.seed_external_builtins();
    const ir::FunctionId alpha = builder.register_procedure(
        SymbolRef{0, "alpha"}, "alpha", scalar(TYPE_INT), {});
    const ir::FunctionId beta = builder.register_procedure(
        SymbolRef{0, "beta"}, "beta", scalar(TYPE_INT), {});
    REQUIRE(alpha.valid());
    REQUIRE(beta.valid());

    REQUIRE(builder.enter_function(alpha));
    const ir::ValueId from_beta = builder.emit_call(beta, {});
    REQUIRE(from_beta.valid());
    REQUIRE(builder.emit_return(from_beta));
    REQUIRE(builder.leave_function());

    REQUIRE(builder.enter_function(beta));
    const ir::ValueId from_alpha = builder.emit_call(alpha, {});
    REQUIRE(from_alpha.valid());
    REQUIRE(builder.emit_return(from_alpha));
    REQUIRE(builder.leave_function());

    REQUIRE(builder.emit_call(alpha, {}).valid());
    REQUIRE(builder.emit_halt());
    builder.finalize(true);
    REQUIRE(builder.status() == ir::ModuleStatus::Ready);
    CHECK(ir::verify_module(builder.module()).valid);
}
