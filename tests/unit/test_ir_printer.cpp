#include "../vendor/doctest.h"
#include "../../IRBuilder.h"
#include "../../IRPrinter.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{

value_shape scalar(data_types type)
{
    return value_shape{type, false, -1};
}

value_shape array(data_types type, int upper_bound)
{
    return value_shape{type, true, upper_bound};
}

ir::Module printable_module()
{
    ir::IRBuilder builder;
    REQUIRE(builder.register_program(SymbolRef{0, "printer"}, "printer").valid());
    builder.seed_external_builtins();
    const ir::StorageId value = builder.register_storage(
        SymbolRef{0, "value"}, scalar(TYPE_INT), ir::StorageKind::Global);
    REQUIRE(value.valid());
    const ir::ValueId one = builder.emit_constant(scalar(TYPE_INT), 1);
    REQUIRE(one.valid());
    REQUIRE(builder.emit_store(value, one));
    const ir::ValueId loaded = builder.emit_load(value);
    const ir::ValueId two = builder.emit_constant(scalar(TYPE_INT), 2);
    const ir::ValueId sum = builder.emit_binary(ir::BinaryOp::Add, loaded, two);
    REQUIRE(loaded.valid());
    REQUIRE(two.valid());
    REQUIRE(sum.valid());
    REQUIRE(builder.emit_store(value, sum));
    REQUIRE(builder.emit_halt());
    builder.finalize(true);
    REQUIRE(builder.status() == ir::ModuleStatus::Ready);
    return builder.module();
}

ir::Module vocabulary_module()
{
    ir::IRBuilder builder;
    REQUIRE(builder.register_program(SymbolRef{0, "vocabulary"}, "vocabulary").valid());
    builder.seed_external_builtins();
    const ir::FunctionId identity = builder.register_procedure(
        SymbolRef{0, "identity"}, "identity", scalar(TYPE_INT),
        {{SymbolRef{1, "input"}, scalar(TYPE_INT)}});
    REQUIRE(identity.valid());
    REQUIRE(builder.enter_function(identity));
    const ir::StorageId local = builder.register_storage(
        SymbolRef{1, "local"}, scalar(TYPE_INT), ir::StorageKind::Local);
    const ir::StorageId parameter = builder.storage_for(SymbolRef{1, "input"});
    REQUIRE(local.valid());
    REQUIRE(parameter.valid());
    const ir::ValueId parameter_value = builder.emit_load(parameter);
    REQUIRE(parameter_value.valid());
    REQUIRE(builder.emit_store(local, parameter_value));
    const ir::ValueId local_value = builder.emit_load(local);
    REQUIRE(local_value.valid());
    REQUIRE(builder.emit_return(local_value));
    REQUIRE(builder.leave_function());

    const ir::StorageId integers = builder.register_storage(
        SymbolRef{0, "integers"}, array(TYPE_INT, 1), ir::StorageKind::Global);
    const ir::StorageId floats = builder.register_storage(
        SymbolRef{0, "floats"}, array(TYPE_FLOAT, 1), ir::StorageKind::Global);
    REQUIRE(integers.valid());
    REQUIRE(floats.valid());
    const ir::ValueId zero = builder.emit_constant(scalar(TYPE_INT), 0);
    const ir::ValueId checked_read = builder.emit_check_index(integers, zero);
    const ir::ValueId element = builder.emit_element_load(integers, checked_read);
    const ir::ValueId one = builder.emit_constant(scalar(TYPE_INT), 1);
    const ir::ValueId checked_write = builder.emit_check_index(integers, one);
    REQUIRE(zero.valid());
    REQUIRE(checked_read.valid());
    REQUIRE(element.valid());
    REQUIRE(one.valid());
    REQUIRE(checked_write.valid());
    REQUIRE(builder.emit_element_store(integers, checked_write, element));

    const ir::ValueId snapshot = builder.emit_load(integers);
    const ir::ValueId negated = builder.emit_unary(ir::UnaryOp::Negate, snapshot);
    const ir::ValueId added = builder.emit_binary(ir::BinaryOp::Add, negated, one);
    const ir::ValueId converted = builder.emit_cast(ir::CastOp::IntToFloat, added);
    REQUIRE(snapshot.valid());
    REQUIRE(negated.valid());
    REQUIRE(added.valid());
    REQUIRE(converted.valid());
    REQUIRE(builder.emit_store(floats, converted));

    const ir::ValueId truth = builder.emit_constant(scalar(TYPE_BOOL), true);
    const ir::ValueId floating = builder.emit_constant(scalar(TYPE_FLOAT), 1.5F);
    const ir::ValueId string_value = builder.emit_constant(
        scalar(TYPE_STRING), std::string("a\n\"\\\x01", 5));
    REQUIRE(truth.valid());
    REQUIRE(floating.valid());
    REQUIRE(string_value.valid());
    REQUIRE(builder.emit_unary(ir::UnaryOp::Not, truth).valid());
    REQUIRE(builder.emit_cast(ir::CastOp::FloatToInt, floating).valid());
    REQUIRE(builder.emit_cast(ir::CastOp::BoolToInt, truth).valid());
    REQUIRE(builder.emit_cast(ir::CastOp::IntToBool, one).valid());
    REQUIRE(builder.emit_binary(ir::BinaryOp::Equal, string_value, string_value).valid());
    const ir::ValueId called = builder.emit_call(identity, {one});
    REQUIRE(called.valid());
    REQUIRE(builder.emit_halt());
    builder.finalize(true);
    REQUIRE(builder.status() == ir::ModuleStatus::Ready);
    return builder.module();
}

std::string read_file(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

} // namespace

TEST_CASE("IR printer renders deterministic typed blocks and builtin declarations")
{
    const ir::Module module = printable_module();
    IRPrinter printer;
    const IRPrintResult first = printer.print(module);
    const IRPrintResult second = printer.print(module);
    REQUIRE(first.succeeded());
    REQUIRE(second.succeeded());
    CHECK(first.text == second.text);
    CHECK(first.diagnostic.empty());
    CHECK(first.text.find(
        "module {\n"
        "  storage @s0 global owner @f0 symbol(0, \"value\") : int\n\n"
        "  define @f0 program symbol(0, \"printer\") name \"printer\" () -> none {\n"
        "    block ^b0:\n"
        "      %v0 : int = constant 1\n"
        "      store @s0, %v0\n"
        "      %v1 : int = load @s0\n"
        "      %v2 : int = constant 2\n"
        "      %v3 : int = add %v1, %v2\n"
        "      store @s0, %v3\n"
        "      halt\n"
        "  }\n") == 0);
    CHECK(first.text.find(
        "declare @f6 external symbol(0, \"putinteger\") name \"putinteger\" (int) -> bool")
        != std::string::npos);
    CHECK(first.text.find(
        "declare @f9 external symbol(0, \"sqrt\") name \"sqrt\" (int) -> float")
        != std::string::npos);
    CHECK(first.text.size() >= 2);
    CHECK(first.text.substr(first.text.size() - 2) == "}\n");
}

TEST_CASE("IR printer rejects malformed modules and publishes atomically")
{
    IRPrinter printer;
    const IRPrintResult invalid = printer.print(ir::Module{});
    CHECK(invalid.status == IRPrintStatus::InvalidIR);
    CHECK_FALSE(invalid.diagnostic.empty());
    CHECK(invalid.text.empty());

    const std::filesystem::path output =
        std::filesystem::temp_directory_path() / "compiler-ir-printer-unit.ir";
    {
        std::ofstream sentinel(output, std::ios::binary | std::ios::trunc);
        REQUIRE(sentinel.is_open());
        sentinel << "preserve\n";
    }
    const IRPrintResult rejected = printer.print_to_file(ir::Module{}, output);
    CHECK(rejected.status == IRPrintStatus::InvalidIR);
    CHECK(read_file(output) == "preserve\n");

    const IRPrintResult written = printer.print_to_file(printable_module(), output);
    REQUIRE(written.succeeded());
    CHECK(read_file(output) == written.text);
    std::error_code cleanup_error;
    std::filesystem::remove(output, cleanup_error);
}

TEST_CASE("IR printer covers aggregate, indexed, cast, call, and return vocabulary")
{
    IRPrinter printer;
    const IRPrintResult printed = printer.print(vocabulary_module());
    REQUIRE(printed.succeeded());
    const std::vector<std::string> required = {
        "parameter owner @f10 symbol(1, \"input\") : int",
        "local owner @f10 symbol(1, \"local\") : int",
        "global owner @f0 symbol(0, \"integers\") : int[0..1]",
        "define @f10 procedure symbol(0, \"identity\") name \"identity\" (@s0 : int) -> int",
        "return %v1",
        "check_index @s2",
        "element_load @s2",
        "element_store @s2",
        ": int[0..1] = negate",
        ": int[0..1] = add",
        ": float[0..1] = int_to_float",
        ": bool = not",
        ": int = float_to_int",
        ": int = bool_to_int",
        ": bool = int_to_bool",
        ": bool = equal",
        "constant 0x1.8p+0",
        "constant \"a\\n\\\"\\\\\\x01\"",
        "call @f10(",
    };
    for (const std::string &fragment : required)
    {
        CHECK_MESSAGE(printed.text.find(fragment) != std::string::npos,
                      "missing IR fragment: ", fragment);
    }
}
