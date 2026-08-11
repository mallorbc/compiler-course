#include "../vendor/doctest.h"
#include "../../BuiltinCatalog.h"
#include "../../IRBuilder.h"

#include <type_traits>

namespace
{

value_shape scalar(data_types type)
{
    value_shape shape;
    shape.element_type = type;
    return shape;
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
