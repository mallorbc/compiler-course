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
    CHECK(first.text.find("goto L_f0_b2;") != std::string::npos);
    CHECK(first.text.find("L_f0_b2:") != std::string::npos);
    CHECK(first.text.find("L_f0_b3:") != std::string::npos);
    CHECK(first.text.find("L_f0_b4:") != std::string::npos);
    CHECK(first.text.find("L_f0_b5:") != std::string::npos);
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
