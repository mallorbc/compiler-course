#include "../vendor/doctest.h"
#include "../../IRBuilder.h"
#include "../../RestrictedCEmitter.h"

#include <limits>
#include <string>
#include <variant>

namespace
{

value_shape scalar(data_types type)
{
    value_shape shape;
    shape.element_type = type;
    return shape;
}

ir::Module integer_and_bool_module()
{
    ir::IRBuilder builder;
    REQUIRE(builder.register_program(SymbolRef{0, "hidden_program"}, "hidden_program").valid());
    builder.seed_external_builtins();
    const ir::StorageId integer = builder.register_storage(
        SymbolRef{0, "hidden_integer"}, scalar(TYPE_INT), ir::StorageKind::Global);
    const ir::StorageId boolean = builder.register_storage(
        SymbolRef{0, "hidden_boolean"}, scalar(TYPE_BOOL), ir::StorageKind::Global);
    REQUIRE(integer.valid());
    REQUIRE(boolean.valid());

    const ir::ValueId minus_five = builder.emit_constant(scalar(TYPE_INT), -5);
    const ir::ValueId three = builder.emit_constant(scalar(TYPE_INT), 3);
    const ir::ValueId minimum = builder.emit_constant(
        scalar(TYPE_INT), std::numeric_limits<int>::min());
    const ir::ValueId minus_one = builder.emit_constant(scalar(TYPE_INT), -1);
    const ir::ValueId truth = builder.emit_constant(scalar(TYPE_BOOL), true);
    const ir::ValueId lie = builder.emit_constant(scalar(TYPE_BOOL), false);
    REQUIRE(minus_five.valid());
    REQUIRE(three.valid());
    REQUIRE(minimum.valid());
    REQUIRE(minus_one.valid());
    REQUIRE(truth.valid());
    REQUIRE(lie.valid());
    REQUIRE(builder.emit_store(integer, minus_five));
    REQUIRE(builder.emit_store(boolean, truth));
    const ir::ValueId loaded = builder.emit_load(integer);
    REQUIRE(loaded.valid());
    REQUIRE(builder.emit_binary(ir::BinaryOp::Add, loaded, three).valid());
    REQUIRE(builder.emit_binary(ir::BinaryOp::Subtract, loaded, three).valid());
    REQUIRE(builder.emit_binary(ir::BinaryOp::Multiply, loaded, three).valid());
    REQUIRE(builder.emit_binary(ir::BinaryOp::Divide, loaded, three).valid());
    REQUIRE(builder.emit_binary(ir::BinaryOp::Divide, minimum, minus_one).valid());
    REQUIRE(builder.emit_unary(ir::UnaryOp::Negate, loaded).valid());
    REQUIRE(builder.emit_unary(ir::UnaryOp::Not, loaded).valid());
    REQUIRE(builder.emit_binary(ir::BinaryOp::Less, loaded, three).valid());
    REQUIRE(builder.emit_binary(ir::BinaryOp::LessEqual, loaded, three).valid());
    REQUIRE(builder.emit_binary(ir::BinaryOp::Greater, loaded, three).valid());
    REQUIRE(builder.emit_binary(ir::BinaryOp::GreaterEqual, loaded, three).valid());
    REQUIRE(builder.emit_binary(ir::BinaryOp::Equal, loaded, three).valid());
    REQUIRE(builder.emit_binary(ir::BinaryOp::NotEqual, loaded, three).valid());
    REQUIRE(builder.emit_binary(ir::BinaryOp::And, truth, lie).valid());
    REQUIRE(builder.emit_binary(ir::BinaryOp::Or, truth, lie).valid());
    REQUIRE(builder.emit_binary(ir::BinaryOp::And, loaded, three).valid());
    REQUIRE(builder.emit_binary(ir::BinaryOp::Or, loaded, three).valid());
    const ir::ValueId not_truth = builder.emit_unary(ir::UnaryOp::Not, truth);
    REQUIRE(not_truth.valid());
    const ir::ValueId as_boolean = builder.emit_cast(ir::CastOp::IntToBool, loaded);
    REQUIRE(as_boolean.valid());
    REQUIRE(builder.emit_cast(ir::CastOp::BoolToInt, as_boolean).valid());
    REQUIRE(builder.emit_store(boolean, not_truth));
    REQUIRE(builder.emit_halt());
    builder.finalize(true);
    REQUIRE(builder.status() == ir::ModuleStatus::Ready);
    return builder.module();
}

ir::Module procedure_module()
{
    ir::IRBuilder builder;
    REQUIRE(builder.register_program(SymbolRef{0, "procedure_program"}, "procedure_program").valid());
    builder.seed_external_builtins();
    const ir::FunctionId procedure = builder.register_procedure(
        SymbolRef{0, "identity"}, "identity", scalar(TYPE_INT), {});
    REQUIRE(procedure.valid());
    REQUIRE(builder.enter_function(procedure));
    const ir::ValueId one = builder.emit_constant(scalar(TYPE_INT), 1);
    REQUIRE(one.valid());
    REQUIRE(builder.emit_return(one));
    REQUIRE(builder.leave_function());
    REQUIRE(builder.emit_call(procedure, std::vector<ir::ValueId>{}).valid());
    REQUIRE(builder.emit_halt());
    builder.finalize(true);
    REQUIRE(builder.status() == ir::ModuleStatus::Ready);
    return builder.module();
}

ir::Module put_integer_module()
{
    ir::IRBuilder builder;
    REQUIRE(builder.register_program(SymbolRef{0, "runtime_program"}, "runtime_program").valid());
    builder.seed_external_builtins();
    const ir::StorageId answer = builder.register_storage(
        SymbolRef{0, "runtime_answer"}, scalar(TYPE_INT), ir::StorageKind::Global);
    const ir::StorageId printed = builder.register_storage(
        SymbolRef{0, "runtime_printed"}, scalar(TYPE_BOOL), ir::StorageKind::Global);
    REQUIRE(answer.valid());
    REQUIRE(printed.valid());
    const ir::ValueId six = builder.emit_constant(scalar(TYPE_INT), 6);
    const ir::ValueId seven = builder.emit_constant(scalar(TYPE_INT), 7);
    REQUIRE(six.valid());
    REQUIRE(seven.valid());
    const ir::ValueId answer_value = builder.emit_binary(ir::BinaryOp::Multiply, six, seven);
    REQUIRE(answer_value.valid());
    REQUIRE(builder.emit_store(answer, answer_value));
    const ir::ValueId loaded_answer = builder.emit_load(answer);
    REQUIRE(loaded_answer.valid());
    const ir::FunctionId put_integer = builder.function_for(SymbolRef{0, "putinteger"});
    REQUIRE(put_integer.valid());
    const ir::ValueId output_result = builder.emit_call(
        put_integer, std::vector<ir::ValueId>{loaded_answer});
    REQUIRE(output_result.valid());
    REQUIRE(builder.emit_store(printed, output_result));
    REQUIRE(builder.emit_halt());
    builder.finalize(true);
    REQUIRE(builder.status() == ir::ModuleStatus::Ready);
    return builder.module();
}

ir::Module branch_module()
{
    ir::IRBuilder builder;
    REQUIRE(builder.register_program(SymbolRef{0, "branch_program"}, "branch_program").valid());
    builder.seed_external_builtins();
    const ir::StorageId answer = builder.register_storage(
        SymbolRef{0, "branch_answer"}, scalar(TYPE_INT), ir::StorageKind::Global);
    REQUIRE(answer.valid());
    const ir::ValueId condition = builder.emit_constant(scalar(TYPE_BOOL), true);
    const ir::BlockId then_block = builder.create_block();
    const ir::BlockId else_block = builder.create_block();
    const ir::BlockId join_block = builder.create_block();
    REQUIRE(builder.emit_branch(condition, then_block, else_block));
    REQUIRE(builder.select_block(then_block));
    const ir::ValueId forty_two = builder.emit_constant(scalar(TYPE_INT), 42);
    REQUIRE(builder.emit_store(answer, forty_two));
    REQUIRE(builder.emit_jump(join_block));
    REQUIRE(builder.select_block(else_block));
    const ir::ValueId zero = builder.emit_constant(scalar(TYPE_INT), 0);
    REQUIRE(builder.emit_store(answer, zero));
    REQUIRE(builder.emit_jump(join_block));
    REQUIRE(builder.select_block(join_block));
    REQUIRE(builder.emit_halt());
    builder.finalize(true);
    REQUIRE(builder.status() == ir::ModuleStatus::Ready);
    return builder.module();
}

ir::Module cyclic_branch_module()
{
    ir::IRBuilder builder;
    REQUIRE(builder.register_program(SymbolRef{0, "cycle_program"}, "cycle_program").valid());
    builder.seed_external_builtins();
    const ir::ValueId condition = builder.emit_constant(scalar(TYPE_BOOL), true);
    const ir::BlockId loop_block = builder.create_block();
    const ir::BlockId halt_block = builder.create_block();
    REQUIRE(builder.emit_branch(condition, loop_block, halt_block));
    REQUIRE(builder.select_block(loop_block));
    REQUIRE(builder.emit_branch(condition, loop_block, halt_block));
    REQUIRE(builder.select_block(halt_block));
    REQUIRE(builder.emit_halt());
    builder.finalize(true);
    REQUIRE(builder.status() == ir::ModuleStatus::Ready);
    return builder.module();
}

ir::Module branch_local_float_module()
{
    ir::IRBuilder builder;
    REQUIRE(builder.register_program(SymbolRef{0, "branch_float"}, "branch_float").valid());
    builder.seed_external_builtins();
    const ir::ValueId condition = builder.emit_constant(scalar(TYPE_BOOL), true);
    const ir::BlockId then_block = builder.create_block();
    const ir::BlockId else_block = builder.create_block();
    const ir::BlockId join_block = builder.create_block();
    REQUIRE(builder.emit_branch(condition, then_block, else_block));
    REQUIRE(builder.select_block(then_block));
    REQUIRE(builder.emit_constant(scalar(TYPE_FLOAT), 1.0F).valid());
    REQUIRE(builder.emit_jump(join_block));
    REQUIRE(builder.select_block(else_block));
    REQUIRE(builder.emit_jump(join_block));
    REQUIRE(builder.select_block(join_block));
    REQUIRE(builder.emit_halt());
    builder.finalize(true);
    REQUIRE(builder.status() == ir::ModuleStatus::Ready);
    return builder.module();
}

} // namespace

TEST_CASE("Stage 4B restricted C emits deterministic numeric-only straight-line C")
{
    const ir::Module module = integer_and_bool_module();
    RestrictedCEmitter emitter;
    const RestrictedCResult first = emitter.emit(module);
    const RestrictedCResult second = emitter.emit(module);

    REQUIRE(first.succeeded());
    CHECK(second.succeeded());
    CHECK(first.text == second.text);
    CHECK(first.text.find("#include <stdint.h>") != std::string::npos);
    CHECK(first.text.find("#define MM_BYTES (67108864u)") != std::string::npos);
    CHECK(first.text.find("int32_t MM[MM_BYTES / sizeof(int32_t)];") != std::string::npos);
    CHECK(first.text.find("int32_t Reg[REGISTER_COUNT];") != std::string::npos);
    CHECK(first.text.find("goto L_f0_b0;") != std::string::npos);
    CHECK(first.text.find("L_f0_b0:") != std::string::npos);
    CHECK(first.text.find("MM[0u]") != std::string::npos);
    CHECK(first.text.find("MM[1u]") != std::string::npos);
    CHECK(first.text.find("(-INT32_C(5))") != std::string::npos);
    CHECK(first.text.find("Reg[4u] = INT32_MIN;") != std::string::npos);
    CHECK(first.text.find("INT32_C(-") == std::string::npos);
    CHECK(first.text.find("(uint32_t)") != std::string::npos);
    CHECK(first.text.find("~(uint32_t)Reg[") != std::string::npos);
    CHECK(first.text.find(" & (uint32_t)Reg[") != std::string::npos);
    CHECK(first.text.find(" | (uint32_t)Reg[") != std::string::npos);
    CHECK(first.text.find("==") != std::string::npos);
    CHECK(first.text.find("&&") != std::string::npos);
    CHECK(first.text.find("||") != std::string::npos);
    const std::size_t expected_register_count = module.functions[0].values.size() + 5U;
    CHECK(first.text.find("#define REGISTER_COUNT " + std::to_string(expected_register_count) + "u") !=
          std::string::npos);
    CHECK(first.text.find("if (Reg[") != std::string::npos);
    CHECK(first.text.find("goto L_f0_d0_0;") != std::string::npos);
    CHECK(first.text.find("L_f0_d0_0:") != std::string::npos);
    CHECK(first.text.find("L_f0_d0_1:") != std::string::npos);
    CHECK(first.text.find("L_f0_d0_2:") != std::string::npos);
    CHECK(first.text.find("goto L_f0_x0;") != std::string::npos);
    CHECK(first.text.find("(-INT32_C(1))") != std::string::npos);
    CHECK(first.text.find("Reg[12u] = Reg[8u] / Reg[3u];") != std::string::npos);
    const std::size_t exit_register = module.functions[0].values.size() + 2U;
    CHECK(first.text.find("Reg[" + std::to_string(exit_register) +
                          "u] = INT32_C(1);") != std::string::npos);
    CHECK(first.text.find("Reg[13u] = INT32_MIN;") != std::string::npos);
    CHECK(first.text.find("else") == std::string::npos);
    CHECK(first.text.find("return 1;") == std::string::npos);
    CHECK(first.text.find("return Reg[") != std::string::npos);
    CHECK(first.text.find("hidden_program") == std::string::npos);
    CHECK(first.text.find("hidden_integer") == std::string::npos);
    CHECK(first.text.find("hidden_boolean") == std::string::npos);
    CHECK(first.text.find("R_put_i32") == std::string::npos);
    CHECK(first.text.find("<stdio.h>") == std::string::npos);
}

TEST_CASE("Stage 5A restricted C emits flat numeric branch blocks")
{
    RestrictedCEmitter emitter;
    const RestrictedCResult result = emitter.emit(branch_module());
    REQUIRE(result.succeeded());
    CHECK(result.text.find("L_f0_b0:") != std::string::npos);
    CHECK(result.text.find("L_f0_b1:") != std::string::npos);
    CHECK(result.text.find("L_f0_b2:") != std::string::npos);
    CHECK(result.text.find("L_f0_b3:") != std::string::npos);
    CHECK(result.text.find("if (Reg[") != std::string::npos);
    CHECK(result.text.find("goto L_f0_b1;") != std::string::npos);
    CHECK(result.text.find("goto L_f0_b2;") != std::string::npos);
    CHECK(result.text.find("goto L_f0_b3;") != std::string::npos);
    CHECK(result.text.find("L_f0_x0:") != std::string::npos);
    CHECK(result.text.find("else") == std::string::npos);
    CHECK(result.text.find("branch_program") == std::string::npos);
    CHECK(result.text.find("branch_answer") == std::string::npos);
}

TEST_CASE("Stage 5B restricted C emits verifier-valid cyclic Program flow")
{
    const ir::Module module = cyclic_branch_module();
    CHECK(ir::verify_module(module).valid);
    RestrictedCEmitter emitter;
    const RestrictedCResult result = emitter.emit(module);
    REQUIRE(result.succeeded());
    CHECK(result.text.find("L_f0_b1:") != std::string::npos);
    CHECK(result.text.find("goto L_f0_b1;") != std::string::npos);
    CHECK(result.text.find("while") == std::string::npos);
    CHECK(result.text.find("for (") == std::string::npos);

    ir::Module no_exit = module;
    no_exit.functions[0].blocks[1].terminator = ir::Terminator(
        ir::JumpTerminator{ir::BlockId(ir::FunctionId(0), 1)});
    CHECK_FALSE(ir::verify_module(no_exit).valid);
    const RestrictedCResult invalid = emitter.emit(no_exit);
    CHECK(invalid.status == RestrictedCStatus::InvalidIR);
    CHECK(invalid.text.empty());
}

TEST_CASE("Stage 5A restricted C preflights every branch atomically")
{
    RestrictedCEmitter emitter;
    const ir::Module float_in_then = branch_local_float_module();
    REQUIRE(ir::verify_module(float_in_then).valid);
    REQUIRE(float_in_then.functions[0].blocks.size() == 4);
    REQUIRE(float_in_then.functions[0].blocks[0].instructions.size() == 1);
    const RestrictedCResult unsupported = emitter.emit(float_in_then);
    CHECK(unsupported.status == RestrictedCStatus::Unsupported);
    CHECK(unsupported.text.empty());
    CHECK(unsupported.diagnostic.find("Integer and Bool") != std::string::npos);

    ir::Module malformed = branch_module();
    ir::BranchTerminator &branch = std::get<ir::BranchTerminator>(
        std::get<ir::Terminator>(malformed.functions[0].blocks[0].terminator));
    branch.when_false = ir::BlockId(ir::FunctionId(0), 99);
    CHECK_FALSE(ir::verify_module(malformed).valid);
    const RestrictedCResult invalid = emitter.emit(malformed);
    CHECK(invalid.status == RestrictedCStatus::InvalidIR);
    CHECK(invalid.text.empty());
}

TEST_CASE("Stage 4C restricted C lowers only canonical putInteger calls")
{
    const ir::Module module = put_integer_module();
    RestrictedCEmitter emitter;
    const RestrictedCResult result = emitter.emit(module);
    REQUIRE(result.succeeded());
    CHECK(result.text.find("#include <inttypes.h>") != std::string::npos);
    CHECK(result.text.find("#include <stdio.h>") != std::string::npos);
    CHECK(result.text.find("static int32_t R_put_i32(int32_t r0)") != std::string::npos);
    CHECK(result.text.find("printf(\"%\" PRId32 \"\\n\", r0)") != std::string::npos);
    CHECK(result.text.find(
              "return printf(\"%\" PRId32 \"\\n\", r0) < 0 ? "
              "INT32_C(0) : INT32_C(1);") != std::string::npos);
    const ir::Call *call = NULL;
    for (const ir::Instruction &instruction : module.functions[0].blocks[0].instructions)
    {
        if (const ir::Call *candidate = std::get_if<ir::Call>(&instruction))
        {
            call = candidate;
            break;
        }
    }
    REQUIRE(call != NULL);
    const std::string expected = "Reg[" + std::to_string(call->result.index + 2U) +
        "u] = R_put_i32(Reg[" + std::to_string(call->arguments[0].index + 2U) + "u]);";
    CHECK(result.text.find(expected) != std::string::npos);
    CHECK(result.text.find("runtime_program") == std::string::npos);
    CHECK(result.text.find("runtime_answer") == std::string::npos);
    CHECK(result.text.find("runtime_printed") == std::string::npos);

    ir::Module invalid_call = module;
    for (ir::Instruction &instruction : invalid_call.functions[0].blocks[0].instructions)
    {
        if (ir::Call *candidate = std::get_if<ir::Call>(&instruction))
        {
            candidate->arguments[0] = ir::ValueId(invalid_call.functions[0].id, 999U);
            break;
        }
    }
    const RestrictedCResult malformed = emitter.emit(invalid_call);
    CHECK(malformed.status == RestrictedCStatus::InvalidIR);
    CHECK(malformed.text.empty());
}

TEST_CASE("Stage 4B fixed memory capacity is an emitter invariant")
{
    const std::size_t capacity = RestrictedCEmitter::memory_word_capacity();
    CHECK(RestrictedCEmitter::memory_byte_capacity() == 64U * 1024U * 1024U);
    CHECK(capacity == RestrictedCEmitter::memory_byte_capacity() / sizeof(std::int32_t));
    CHECK(RestrictedCEmitter::storage_count_fits_memory(capacity));
    CHECK_FALSE(RestrictedCEmitter::storage_count_fits_memory(capacity + 1U));
}

TEST_CASE("Stage 4B restricted C rejects unlowered and invalid modules atomically")
{
    RestrictedCEmitter emitter;

    const RestrictedCResult procedure = emitter.emit(procedure_module());
    CHECK(procedure.status == RestrictedCStatus::Unsupported);
    CHECK(procedure.text.empty());

    ir::IRBuilder call_builder;
    REQUIRE(call_builder.register_program(SymbolRef{0, "call_program"}, "call_program").valid());
    call_builder.seed_external_builtins();
    const ir::ValueId input = call_builder.emit_constant(scalar(TYPE_INT), 1);
    REQUIRE(input.valid());
    const ir::FunctionId sqrt = call_builder.function_for(SymbolRef{0, "sqrt"});
    REQUIRE(sqrt.valid());
    REQUIRE(call_builder.emit_call(sqrt, std::vector<ir::ValueId>{input}).valid());
    REQUIRE(call_builder.emit_halt());
    call_builder.finalize(true);
    REQUIRE(call_builder.status() == ir::ModuleStatus::Ready);
    const RestrictedCResult call = emitter.emit(call_builder.module());
    CHECK(call.status == RestrictedCStatus::Unsupported);
    CHECK(call.text.empty());

    ir::IRBuilder put_bool_builder;
    REQUIRE(put_bool_builder.register_program(SymbolRef{0, "put_bool_program"}, "put_bool_program").valid());
    put_bool_builder.seed_external_builtins();
    const ir::ValueId boolean = put_bool_builder.emit_constant(scalar(TYPE_BOOL), true);
    REQUIRE(boolean.valid());
    const ir::FunctionId put_bool = put_bool_builder.function_for(SymbolRef{0, "putbool"});
    REQUIRE(put_bool.valid());
    REQUIRE(put_bool_builder.emit_call(put_bool, std::vector<ir::ValueId>{boolean}).valid());
    REQUIRE(put_bool_builder.emit_halt());
    put_bool_builder.finalize(true);
    REQUIRE(put_bool_builder.status() == ir::ModuleStatus::Ready);
    const RestrictedCResult put_bool_call = emitter.emit(put_bool_builder.module());
    CHECK(put_bool_call.status == RestrictedCStatus::Unsupported);
    CHECK(put_bool_call.text.empty());

    ir::IRBuilder get_integer_builder;
    REQUIRE(get_integer_builder.register_program(SymbolRef{0, "get_integer_program"}, "get_integer_program").valid());
    get_integer_builder.seed_external_builtins();
    const ir::FunctionId get_integer =
        get_integer_builder.function_for(SymbolRef{0, "getinteger"});
    REQUIRE(get_integer.valid());
    REQUIRE(get_integer_builder.emit_call(get_integer, std::vector<ir::ValueId>{}).valid());
    REQUIRE(get_integer_builder.emit_halt());
    get_integer_builder.finalize(true);
    REQUIRE(get_integer_builder.status() == ir::ModuleStatus::Ready);
    const RestrictedCResult get_integer_call = emitter.emit(get_integer_builder.module());
    CHECK(get_integer_call.status == RestrictedCStatus::Unsupported);
    CHECK(get_integer_call.text.empty());

    for (data_types unsupported_type : {TYPE_FLOAT, TYPE_STRING})
    {
        ir::IRBuilder scalar_builder;
        REQUIRE(scalar_builder.register_program(SymbolRef{0, "scalar_program"}, "scalar_program").valid());
        scalar_builder.seed_external_builtins();
        if (unsupported_type == TYPE_FLOAT)
        {
            REQUIRE(scalar_builder.emit_constant(scalar(TYPE_FLOAT), 1.5f).valid());
        }
        else
        {
            REQUIRE(scalar_builder.emit_constant(scalar(TYPE_STRING), std::string("value")).valid());
        }
        REQUIRE(scalar_builder.emit_halt());
        scalar_builder.finalize(true);
        REQUIRE(scalar_builder.status() == ir::ModuleStatus::Ready);
        const RestrictedCResult unsupported = emitter.emit(scalar_builder.module());
        CHECK(unsupported.status == RestrictedCStatus::Unsupported);
        CHECK(unsupported.text.empty());
    }

    ir::Module invalid;
    const RestrictedCResult malformed = emitter.emit(invalid);
    CHECK(malformed.status == RestrictedCStatus::InvalidIR);
    CHECK(malformed.text.empty());
}
