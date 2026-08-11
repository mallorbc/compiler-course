#include "../vendor/doctest.h"
#include "../../IRBuilder.h"
#include "../../RestrictedCEmitter.h"

#include <filesystem>
#include <fstream>
#include <iterator>
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

ir::Module mutual_procedure_module()
{
    ir::IRBuilder builder;
    REQUIRE(builder.register_program(SymbolRef{0, "mutual_program"}, "mutual_program").valid());
    builder.seed_external_builtins();
    const ir::FunctionId alpha = builder.register_procedure(
        SymbolRef{0, "alpha_codegen"}, "alpha_codegen", scalar(TYPE_INT), {});
    const ir::FunctionId beta = builder.register_procedure(
        SymbolRef{0, "beta_codegen"}, "beta_codegen", scalar(TYPE_INT), {});
    REQUIRE(alpha.valid());
    REQUIRE(beta.valid());
    REQUIRE(builder.enter_function(alpha));
    const ir::ValueId beta_value = builder.emit_call(beta, {});
    REQUIRE(beta_value.valid());
    REQUIRE(builder.emit_return(beta_value));
    REQUIRE(builder.leave_function());
    REQUIRE(builder.enter_function(beta));
    const ir::ValueId alpha_value = builder.emit_call(alpha, {});
    REQUIRE(alpha_value.valid());
    REQUIRE(builder.emit_return(alpha_value));
    REQUIRE(builder.leave_function());
    REQUIRE(builder.emit_call(alpha, {}).valid());
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

ir::Module integer_bool_runtime_module()
{
    ir::IRBuilder builder;
    REQUIRE(builder.register_program(SymbolRef{0, "runtime_io_program"}, "runtime_io_program").valid());
    builder.seed_external_builtins();

    const ir::FunctionId get_integer = builder.function_for(SymbolRef{0, "getinteger"});
    const ir::FunctionId get_bool = builder.function_for(SymbolRef{0, "getbool"});
    const ir::FunctionId put_integer = builder.function_for(SymbolRef{0, "putinteger"});
    const ir::FunctionId put_bool = builder.function_for(SymbolRef{0, "putbool"});
    REQUIRE(get_integer.valid());
    REQUIRE(get_bool.valid());
    REQUIRE(put_integer.valid());
    REQUIRE(put_bool.valid());

    const ir::ValueId integer = builder.emit_call(get_integer, {});
    const ir::ValueId boolean = builder.emit_call(get_bool, {});
    REQUIRE(integer.valid());
    REQUIRE(boolean.valid());
    REQUIRE(builder.emit_call(put_integer, {integer}).valid());
    REQUIRE(builder.emit_call(put_bool, {boolean}).valid());
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

TEST_CASE("Stage 6B restricted C preflights Float branches atomically")
{
    RestrictedCEmitter emitter;
    const ir::Module float_in_then = branch_local_float_module();
    REQUIRE(ir::verify_module(float_in_then).valid);
    REQUIRE(float_in_then.functions[0].blocks.size() == 4);
    REQUIRE(float_in_then.functions[0].blocks[0].instructions.size() == 1);
    const RestrictedCResult supported = emitter.emit(float_in_then);
    CHECK(supported.status == RestrictedCStatus::Success);
    CHECK(supported.text.find("restricted C requires IEEE binary32 float") != std::string::npos);

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

TEST_CASE("Stage 6A restricted C lowers catalog-canonical Integer and Bool runtime calls")
{
    RestrictedCEmitter emitter;
    const ir::Module module = integer_bool_runtime_module();
    const RestrictedCResult result = emitter.emit(module);
    REQUIRE(result.succeeded());
    CHECK(result.text.find("#include <ctype.h>") != std::string::npos);
    CHECK(result.text.find("#include <inttypes.h>") != std::string::npos);
    CHECK(result.text.find("#include <stdio.h>") != std::string::npos);
    CHECK(result.text.find("static int32_t R_get_i32(void)") != std::string::npos);
    CHECK(result.text.find("static int32_t R_get_b1(void)") != std::string::npos);
    CHECK(result.text.find("static int32_t R_put_i32(int32_t r0)") != std::string::npos);
    CHECK(result.text.find("static int32_t R_put_b1(int32_t r0)") != std::string::npos);
    CHECK(result.text.find("R_decimal_i32") != std::string::npos);
    CHECK(result.text.find("runtime_io_program") == std::string::npos);

    ir::Module impostor = module;
    const ir::FunctionId get_integer = ir::FunctionId(2);
    REQUIRE(impostor.functions[get_integer.index].name == "getinteger");
    impostor.functions[get_integer.index].symbol = SymbolRef{0, "not_getinteger"};
    const RestrictedCResult noncanonical = emitter.emit(impostor);
    CHECK(noncanonical.status == RestrictedCStatus::InvalidIR);
    CHECK(noncanonical.text.empty());

    ir::Module wrong_arity = module;
    for (ir::Instruction &instruction : wrong_arity.functions[0].blocks[0].instructions)
    {
        if (ir::Call *call = std::get_if<ir::Call>(&instruction))
        {
            if (call->callee == get_integer)
            {
                call->arguments.push_back(ir::ValueId(ir::FunctionId(0), 0));
                break;
            }
        }
    }
    const RestrictedCResult malformed_arity = emitter.emit(wrong_arity);
    CHECK(malformed_arity.status == RestrictedCStatus::InvalidIR);
    CHECK(malformed_arity.text.empty());

    ir::Module wrong_type = module;
    const ir::FunctionId put_bool = ir::FunctionId(5);
    for (ir::Instruction &instruction : wrong_type.functions[0].blocks[0].instructions)
    {
        if (ir::Call *call = std::get_if<ir::Call>(&instruction))
        {
            if (call->callee == put_bool)
            {
                call->arguments[0] = ir::ValueId(ir::FunctionId(0), 0);
                break;
            }
        }
    }
    const RestrictedCResult malformed_type = emitter.emit(wrong_type);
    CHECK(malformed_type.status == RestrictedCStatus::InvalidIR);
    CHECK(malformed_type.text.empty());

    ir::IRBuilder unsupported_builder;
    REQUIRE(unsupported_builder.register_program(SymbolRef{0, "sqrt_program"}, "sqrt_program").valid());
    unsupported_builder.seed_external_builtins();
    const ir::ValueId input = unsupported_builder.emit_constant(scalar(TYPE_INT), 1);
    REQUIRE(input.valid());
    const ir::FunctionId sqrt = unsupported_builder.function_for(SymbolRef{0, "sqrt"});
    REQUIRE(sqrt.valid());
    REQUIRE(unsupported_builder.emit_call(sqrt, {input}).valid());
    REQUIRE(unsupported_builder.emit_halt());
    unsupported_builder.finalize(true);
    REQUIRE(unsupported_builder.status() == ir::ModuleStatus::Ready);
    const RestrictedCResult unsupported = emitter.emit(unsupported_builder.module());
    CHECK(unsupported.status == RestrictedCStatus::Success);
    REQUIRE(unsupported.links.size() == 1);
    CHECK(unsupported.links[0] == RestrictedCLink::Math);
    CHECK(unsupported.text.find("R_sqrt_i32") != std::string::npos);

    ir::IRBuilder dead_unsupported_builder;
    REQUIRE(dead_unsupported_builder.register_program(SymbolRef{0, "dead_runtime"},
                                                       "dead_runtime").valid());
    dead_unsupported_builder.seed_external_builtins();
    const ir::FunctionId hidden = dead_unsupported_builder.register_procedure(
        SymbolRef{0, "hidden_runtime"}, "hidden_runtime", scalar(TYPE_INT), {});
    REQUIRE(hidden.valid());
    REQUIRE(dead_unsupported_builder.enter_function(hidden));
    const ir::ValueId hidden_input = dead_unsupported_builder.emit_constant(scalar(TYPE_INT), 4);
    REQUIRE(hidden_input.valid());
    const ir::FunctionId hidden_sqrt =
        dead_unsupported_builder.function_for(SymbolRef{0, "sqrt"});
    REQUIRE(hidden_sqrt.valid());
    REQUIRE(dead_unsupported_builder.emit_call(hidden_sqrt, {hidden_input}).valid());
    const ir::ValueId hidden_result =
        dead_unsupported_builder.emit_constant(scalar(TYPE_INT), 1);
    REQUIRE(dead_unsupported_builder.emit_return(hidden_result));
    REQUIRE(dead_unsupported_builder.leave_function());
    REQUIRE(dead_unsupported_builder.emit_halt());
    dead_unsupported_builder.finalize(true);
    REQUIRE(dead_unsupported_builder.status() == ir::ModuleStatus::Ready);
    const RestrictedCResult dead_unsupported = emitter.emit(dead_unsupported_builder.module());
    CHECK(dead_unsupported.status == RestrictedCStatus::Success);
    CHECK(dead_unsupported.links.empty());
    CHECK(dead_unsupported.text.find("L_f10_") == std::string::npos);
    CHECK(dead_unsupported.text.find("R_sqrt") == std::string::npos);
}

TEST_CASE("Stage 4B fixed memory capacity is an emitter invariant")
{
    const std::size_t capacity = RestrictedCEmitter::memory_word_capacity();
    CHECK(RestrictedCEmitter::memory_byte_capacity() == 64U * 1024U * 1024U);
    CHECK(capacity == RestrictedCEmitter::memory_byte_capacity() / sizeof(std::int32_t));
    CHECK(RestrictedCEmitter::storage_count_fits_memory(capacity));
    CHECK_FALSE(RestrictedCEmitter::storage_count_fits_memory(capacity + 1U));
}

TEST_CASE("Stage 5C restricted C lowers scalar procedures and rejects unsupported modules atomically")
{
    RestrictedCEmitter emitter;

    const RestrictedCResult procedure = emitter.emit(procedure_module());
    CHECK(procedure.status == RestrictedCStatus::Success);
    CHECK_FALSE(procedure.text.empty());

    const RestrictedCResult mutual = emitter.emit(mutual_procedure_module());
    CHECK(mutual.status == RestrictedCStatus::Success);
    CHECK(mutual.text.find("L_f10_b0:") != std::string::npos);
    CHECK(mutual.text.find("L_f11_b0:") != std::string::npos);

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
    CHECK(call.status == RestrictedCStatus::Success);
    REQUIRE(call.links.size() == 1);
    CHECK(call.links[0] == RestrictedCLink::Math);

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
    CHECK(put_bool_call.status == RestrictedCStatus::Success);
    CHECK(put_bool_call.text.find("R_put_b1") != std::string::npos);
    CHECK(put_bool_call.text.find("R_get_") == std::string::npos);
    CHECK(put_bool_call.text.find("#include <ctype.h>") == std::string::npos);
    CHECK(put_bool_call.text.find("#include <inttypes.h>") == std::string::npos);

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
    CHECK(get_integer_call.status == RestrictedCStatus::Success);
    CHECK(get_integer_call.text.find("R_get_i32") != std::string::npos);
    CHECK(get_integer_call.text.find("R_put_") == std::string::npos);
    CHECK(get_integer_call.text.find("#include <inttypes.h>") == std::string::npos);

    ir::Module invalid;
    const RestrictedCResult malformed = emitter.emit(invalid);
    CHECK(malformed.status == RestrictedCStatus::InvalidIR);
    CHECK(malformed.text.empty());
}

TEST_CASE("Stage 6B Float words, helpers, casts, and link metadata are exact")
{
    ir::IRBuilder builder;
    REQUIRE(builder.register_program(SymbolRef{0, "float_words"}, "float_words").valid());
    builder.seed_external_builtins();
    const ir::ValueId positive_zero = builder.emit_constant(scalar(TYPE_FLOAT), 0.0f);
    const ir::ValueId negative_zero = builder.emit_constant(scalar(TYPE_FLOAT), -0.0f);
    const ir::ValueId one_and_half = builder.emit_constant(scalar(TYPE_FLOAT), 1.5f);
    const ir::ValueId one_tenth = builder.emit_constant(scalar(TYPE_FLOAT), 0.1f);
    const ir::ValueId subnormal = builder.emit_constant(
        scalar(TYPE_FLOAT), std::numeric_limits<float>::denorm_min());
    const ir::ValueId maximum = builder.emit_constant(
        scalar(TYPE_FLOAT), std::numeric_limits<float>::max());
    const ir::ValueId integer = builder.emit_constant(scalar(TYPE_INT), 9);
    REQUIRE(positive_zero.valid());
    REQUIRE(negative_zero.valid());
    REQUIRE(one_and_half.valid());
    REQUIRE(one_tenth.valid());
    REQUIRE(subnormal.valid());
    REQUIRE(maximum.valid());
    REQUIRE(integer.valid());
    REQUIRE(builder.emit_binary(ir::BinaryOp::Divide, one_and_half, negative_zero).valid());
    const ir::ValueId promoted = builder.emit_cast(ir::CastOp::IntToFloat, integer);
    REQUIRE(promoted.valid());
    REQUIRE(builder.emit_cast(ir::CastOp::FloatToInt, promoted).valid());
    const ir::FunctionId sqrt = builder.function_for(SymbolRef{0, "sqrt"});
    REQUIRE(sqrt.valid());
    REQUIRE(builder.emit_call(sqrt, {integer}).valid());
    REQUIRE(builder.emit_halt());
    builder.finalize(true);
    REQUIRE(builder.status() == ir::ModuleStatus::Ready);

    RestrictedCEmitter emitter;
    const RestrictedCResult result = emitter.emit(builder.module());
    REQUIRE(result.succeeded());
    REQUIRE(result.links.size() == 1);
    CHECK(result.links[0] == RestrictedCLink::Math);
    CHECK(result.text.find("I32_FROM_U32(UINT32_C(0))") != std::string::npos);
    CHECK(result.text.find("I32_FROM_U32(UINT32_C(2147483648))") != std::string::npos);
    CHECK(result.text.find("I32_FROM_U32(UINT32_C(1069547520))") != std::string::npos);
    CHECK(result.text.find("I32_FROM_U32(UINT32_C(1036831949))") != std::string::npos);
    CHECK(result.text.find("I32_FROM_U32(UINT32_C(1))") != std::string::npos);
    CHECK(result.text.find("I32_FROM_U32(UINT32_C(2139095039))") != std::string::npos);
    CHECK(result.text.find("memcpy(&r2, &r1, sizeof(r2))") != std::string::npos);
    CHECK(result.text.find("R_f32_i32") != std::string::npos);
    CHECK(result.text.find("R_word_f32") != std::string::npos);
    CHECK(result.text.find("union") == std::string::npos);
    CHECK(result.text.find("goto L_f0_d") == std::string::npos);

    const std::filesystem::path output =
        std::filesystem::temp_directory_path() / "compiler-stage6b-link-metadata.c";
    std::error_code cleanup_error;
    std::filesystem::remove(output, cleanup_error);
    const RestrictedCResult published = emitter.emit_to_file(builder.module(), output);
    REQUIRE(published.succeeded());
    REQUIRE(published.links.size() == 1);
    CHECK(published.links[0] == RestrictedCLink::Math);
    const RestrictedCResult publication_failure =
        emitter.emit_to_file(builder.module(), std::filesystem::path());
    CHECK(publication_failure.status == RestrictedCStatus::IoError);
    CHECK(publication_failure.text.empty());
    CHECK(publication_failure.links.empty());
    cleanup_error.clear();
    std::filesystem::remove(output, cleanup_error);

    ir::Module nonfinite = builder.module();
    bool replaced = false;
    for (ir::Instruction &instruction : nonfinite.functions[0].blocks[0].instructions)
    {
        ir::Constant *constant = std::get_if<ir::Constant>(&instruction);
        if (constant != NULL && constant->result == one_and_half)
        {
            constant->payload = std::numeric_limits<float>::infinity();
            replaced = true;
            break;
        }
    }
    REQUIRE(replaced);
    const RestrictedCResult invalid = emitter.emit(nonfinite);
    CHECK(invalid.status == RestrictedCStatus::InvalidIR);
    CHECK(invalid.text.empty());
    CHECK(invalid.links.empty());

    ir::Module nan_constant = builder.module();
    replaced = false;
    for (ir::Instruction &instruction : nan_constant.functions[0].blocks[0].instructions)
    {
        ir::Constant *constant = std::get_if<ir::Constant>(&instruction);
        if (constant != NULL && constant->result == one_and_half)
        {
            constant->payload = std::numeric_limits<float>::quiet_NaN();
            replaced = true;
            break;
        }
    }
    REQUIRE(replaced);
    const RestrictedCResult invalid_nan = emitter.emit(nan_constant);
    CHECK(invalid_nan.status == RestrictedCStatus::InvalidIR);
    CHECK(invalid_nan.text.empty());
    CHECK(invalid_nan.links.empty());
}

TEST_CASE("Stage 6C String pools are semantic, deduplicated, and atomic")
{
    ir::IRBuilder builder;
    REQUIRE(builder.register_program(SymbolRef{0, "string_pool"}, "string_pool").valid());
    builder.seed_external_builtins();
    const ir::StorageId global = builder.register_storage(
        SymbolRef{0, "value"}, scalar(TYPE_STRING), ir::StorageKind::Global);
    REQUIRE(global.valid());
    const ir::ValueId empty = builder.emit_constant(scalar(TYPE_STRING), std::string());
    const ir::ValueId first = builder.emit_constant(scalar(TYPE_STRING), std::string("MiXeD"));
    const ir::ValueId duplicate = builder.emit_constant(scalar(TYPE_STRING), std::string("MiXeD"));
    const ir::ValueId distinct = builder.emit_constant(scalar(TYPE_STRING), std::string("mixed"));
    const std::string high_bytes{static_cast<char>(0xff), '\n'};
    const ir::ValueId high = builder.emit_constant(scalar(TYPE_STRING), high_bytes);
    REQUIRE(empty.valid());
    REQUIRE(first.valid());
    REQUIRE(duplicate.valid());
    REQUIRE(distinct.valid());
    REQUIRE(high.valid());
    REQUIRE(builder.emit_store(global, first));
    REQUIRE(builder.emit_halt());
    builder.finalize(true);
    REQUIRE(builder.status() == ir::ModuleStatus::Ready);

    RestrictedCEmitter emitter;
    const RestrictedCResult result = emitter.emit(builder.module());
    REQUIRE(result.succeeded());
    CHECK(result.links.empty());
    CHECK(result.text.find("#include <stdio.h>") == std::string::npos);
    CHECK(result.text.find("R_str_eq") == std::string::npos);
    CHECK(result.text.find("INT32_C(255)") != std::string::npos);
    CHECK(result.text.find("INT32_C(10)") != std::string::npos);
    CHECK(result.text.find("MiXeD") == std::string::npos);
    CHECK(result.text.find("mixed") == std::string::npos);
    CHECK(result.text.find("STRING_EMPTY_HANDLE") != std::string::npos);
    std::size_t duplicate_handle_uses = 0;
    std::size_t position = 0;
    const std::string handle_assignment = " = INT32_C(2);\n";
    while ((position = result.text.find(handle_assignment, position)) != std::string::npos)
    {
        duplicate_handle_uses++;
        position += handle_assignment.size();
    }
    CHECK(duplicate_handle_uses == 2);

    ir::Module embedded_nul = builder.module();
    bool replaced = false;
    for (ir::Instruction &instruction : embedded_nul.functions[0].blocks[0].instructions)
    {
        ir::Constant *constant = std::get_if<ir::Constant>(&instruction);
        if (constant != NULL && constant->result == first)
        {
            constant->payload = std::string("a\0b", 3U);
            replaced = true;
            break;
        }
    }
    REQUIRE(replaced);
    const RestrictedCResult nul = emitter.emit(embedded_nul);
    CHECK(nul.status == RestrictedCStatus::Unsupported);
    CHECK(nul.text.empty());
    CHECK(nul.links.empty());
    const std::filesystem::path sentinel_path =
        std::filesystem::temp_directory_path() / "compiler-stage6c-string-sentinel.c";
    {
        std::ofstream sentinel(sentinel_path, std::ios::binary | std::ios::trunc);
        REQUIRE(sentinel.is_open());
        sentinel << "preserve\n";
    }
    const RestrictedCResult nul_file = emitter.emit_to_file(embedded_nul, sentinel_path);
    CHECK(nul_file.status == RestrictedCStatus::Unsupported);
    std::ifstream preserved_nul(sentinel_path, std::ios::binary);
    REQUIRE(preserved_nul.is_open());
    CHECK(std::string(std::istreambuf_iterator<char>(preserved_nul),
                      std::istreambuf_iterator<char>()) == "preserve\n");

    ir::IRBuilder oversized_builder;
    REQUIRE(oversized_builder.register_program(SymbolRef{0, "oversized"}, "oversized").valid());
    oversized_builder.seed_external_builtins();
    const std::string oversized_literal(RestrictedCEmitter::memory_word_capacity(), 'x');
    REQUIRE(oversized_builder.emit_constant(scalar(TYPE_STRING), oversized_literal).valid());
    REQUIRE(oversized_builder.emit_halt());
    oversized_builder.finalize(true);
    REQUIRE(oversized_builder.status() == ir::ModuleStatus::Ready);
    const RestrictedCResult oversized = emitter.emit(oversized_builder.module());
    CHECK(oversized.status == RestrictedCStatus::Unsupported);
    CHECK(oversized.text.empty());
    CHECK(oversized.links.empty());
    const RestrictedCResult oversized_file =
        emitter.emit_to_file(oversized_builder.module(), sentinel_path);
    CHECK(oversized_file.status == RestrictedCStatus::Unsupported);
    std::ifstream preserved_oversized(sentinel_path, std::ios::binary);
    REQUIRE(preserved_oversized.is_open());
    CHECK(std::string(std::istreambuf_iterator<char>(preserved_oversized),
                      std::istreambuf_iterator<char>()) == "preserve\n");
    std::error_code cleanup_error;
    std::filesystem::remove(sentinel_path, cleanup_error);
}

TEST_CASE("Stage 6C String static layout is category ordered and exact deduplicated")
{
    ir::IRBuilder builder;
    REQUIRE(builder.register_program(SymbolRef{0, "string_layout"}, "string_layout").valid());
    builder.seed_external_builtins();
    REQUIRE(builder.register_storage(SymbolRef{0, "result"}, scalar(TYPE_BOOL),
                                     ir::StorageKind::Global).valid());
    const ir::FunctionId procedure = builder.register_procedure(
        SymbolRef{0, "procedure_literal"}, "procedure_literal", scalar(TYPE_STRING), {});
    REQUIRE(procedure.valid());
    REQUIRE(builder.enter_function(procedure));
    const ir::ValueId first_q = builder.emit_constant(scalar(TYPE_STRING), std::string("Q"));
    const ir::ValueId duplicate_q = builder.emit_constant(scalar(TYPE_STRING), std::string("Q"));
    REQUIRE(first_q.valid());
    REQUIRE(duplicate_q.valid());
    REQUIRE(builder.emit_return(duplicate_q));
    REQUIRE(builder.leave_function());

    const ir::ValueId first_p = builder.emit_constant(scalar(TYPE_STRING), std::string("P"));
    const ir::ValueId duplicate_p = builder.emit_constant(scalar(TYPE_STRING), std::string("P"));
    REQUIRE(first_p.valid());
    REQUIRE(duplicate_p.valid());
    REQUIRE(builder.emit_call(procedure, {}).valid());
    REQUIRE(builder.emit_halt());
    builder.finalize(true);
    REQUIRE(builder.status() == ir::ModuleStatus::Ready);

    const RestrictedCResult emitted = RestrictedCEmitter().emit(builder.module());
    REQUIRE(emitted.succeeded());
    CHECK(emitted.links.empty());
    CHECK(emitted.text.find("#define STRING_EMPTY_HANDLE INT32_C(2)\n") != std::string::npos);
    CHECK(emitted.text.find("    Reg[0u] = INT32_C(7);\n") != std::string::npos);
    CHECK(emitted.text.find("    MM[2u] = INT32_C(0);\n") != std::string::npos);
    CHECK(emitted.text.find("    MM[3u] = INT32_C(80);\n") != std::string::npos);
    CHECK(emitted.text.find("    MM[4u] = INT32_C(0);\n") != std::string::npos);
    CHECK(emitted.text.find("    MM[5u] = INT32_C(81);\n") != std::string::npos);
    CHECK(emitted.text.find("    MM[6u] = INT32_C(0);\n") != std::string::npos);
    CHECK(emitted.text.find("    Reg[2u] = INT32_C(3);\n") != std::string::npos);
    CHECK(emitted.text.find("    Reg[3u] = INT32_C(3);\n") != std::string::npos);
    CHECK(emitted.text.find("    Reg[8u] = 1u;\n") != std::string::npos);

    const std::string p_word = "    MM[3u] = INT32_C(80);\n";
    const std::string q_word = "    MM[5u] = INT32_C(81);\n";
    const std::size_t p_position = emitted.text.find(p_word);
    const std::size_t q_position = emitted.text.find(q_word);
    REQUIRE(p_position != std::string::npos);
    REQUIRE(q_position != std::string::npos);
    CHECK(emitted.text.find(p_word, p_position + p_word.size()) == std::string::npos);
    CHECK(emitted.text.find(q_word, q_position + q_word.size()) == std::string::npos);
}
