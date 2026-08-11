#include "RestrictedCEmitter.h"

#include "BuiltinCatalog.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <system_error>
#include <variant>

namespace
{

RestrictedCResult failure(RestrictedCStatus status, const std::string &diagnostic)
{
    RestrictedCResult result;
    result.status = status;
    result.diagnostic = diagnostic;
    return result;
}

bool supported_shape(const value_shape &shape)
{
    return !shape.is_array && shape.array_upper_bound == -1 &&
           (shape.element_type == TYPE_INT || shape.element_type == TYPE_BOOL ||
            shape.element_type == TYPE_FLOAT || shape.element_type == TYPE_STRING);
}

bool supported_unary(ir::UnaryOp operation, data_types type)
{
    return (operation == ir::UnaryOp::Negate && (type == TYPE_INT || type == TYPE_FLOAT)) ||
           (operation == ir::UnaryOp::Not && (type == TYPE_INT || type == TYPE_BOOL));
}

bool supported_binary(ir::BinaryOp operation, data_types type)
{
    switch (operation)
    {
    case ir::BinaryOp::Add:
    case ir::BinaryOp::Subtract:
    case ir::BinaryOp::Multiply:
    case ir::BinaryOp::Divide:
        return type == TYPE_INT || type == TYPE_FLOAT;
    case ir::BinaryOp::And:
    case ir::BinaryOp::Or:
        return type == TYPE_INT || type == TYPE_BOOL;
    case ir::BinaryOp::Less:
    case ir::BinaryOp::LessEqual:
    case ir::BinaryOp::Greater:
    case ir::BinaryOp::GreaterEqual:
        return type == TYPE_INT || type == TYPE_BOOL || type == TYPE_FLOAT;
    case ir::BinaryOp::Equal:
    case ir::BinaryOp::NotEqual:
        return type == TYPE_INT || type == TYPE_BOOL || type == TYPE_FLOAT ||
               type == TYPE_STRING;
    }
    return false;
}

bool supported_cast(ir::CastOp operation)
{
    return operation == ir::CastOp::IntToBool || operation == ir::CastOp::BoolToInt ||
           operation == ir::CastOp::IntToFloat || operation == ir::CastOp::FloatToInt;
}

bool host_has_binary32_float()
{
    return sizeof(float) == sizeof(std::uint32_t) &&
           std::numeric_limits<float>::is_iec559 &&
           std::numeric_limits<float>::radix == 2 &&
           std::numeric_limits<float>::digits == 24 &&
           std::numeric_limits<float>::max_exponent == 128;
}

bool float_word(float value, std::uint32_t &word)
{
    if (!host_has_binary32_float() || !std::isfinite(value))
    {
        return false;
    }
    std::array<unsigned char,
               (sizeof(float) > sizeof(std::uint32_t) ? sizeof(float) : sizeof(std::uint32_t))>
        bytes{};
    std::memcpy(bytes.data(), &value, sizeof(value));
    std::memcpy(&word, bytes.data(), sizeof(word));
    return true;
}

std::string float_literal(float value)
{
    std::uint32_t word = 0;
    return float_word(value, word) ?
        "I32_FROM_U32(UINT32_C(" + std::to_string(word) + "))" : std::string();
}

const ir::Value *value_for(const ir::Function &function, ir::ValueId id)
{
    if (!id.valid() || id.function != function.id || id.index >= function.values.size())
    {
        return NULL;
    }
    const ir::Value &value = function.values[id.index];
    return value.id == id ? &value : NULL;
}

const ir::Storage *storage_for(const ir::Module &module, ir::StorageId id)
{
    if (!id.valid() || id.index >= module.storages.size())
    {
        return NULL;
    }
    const ir::Storage &storage = module.storages[id.index];
    return storage.id == id ? &storage : NULL;
}

const ir::Function *function_for(const ir::Module &module, ir::FunctionId id)
{
    if (!id.valid() || id.index >= module.functions.size())
    {
        return NULL;
    }
    const ir::Function &function = module.functions[id.index];
    return function.id == id ? &function : NULL;
}

std::string value_register(ir::ValueId id)
{
    return "Reg[" + std::to_string(static_cast<std::uint64_t>(id.index) + 2U) + "u]";
}

std::string storage_word(ir::StorageId id)
{
    return "MM[" + std::to_string(id.index) + "u]";
}

std::string register_slot(std::size_t index)
{
    return "Reg[" + std::to_string(index) + "u]";
}

struct RegisterLayout
{
    std::size_t count = 0;
    std::size_t exit = 0;
    std::size_t division_zero = 0;
    std::size_t division_overflow = 0;
    bool has_division = false;
};

struct RuntimeRequirements
{
    std::array<bool, static_cast<std::size_t>(BuiltinId::Sqrt) + 1U> builtin_used{};
    bool procedures = false;
    bool procedure_mode = false;
    std::vector<bool> reachable_functions;
    bool float_words = false;
    bool float_decode = false;
    bool float_encode = false;
    bool float_to_int = false;
    bool string_words = false;
    bool string_equality = false;
    std::vector<std::string> string_literals;

    void require(BuiltinId id)
    {
        builtin_used[static_cast<std::size_t>(id)] = true;
    }

    bool uses(BuiltinId id) const
    {
        return builtin_used[static_cast<std::size_t>(id)];
    }

    bool uses_runtime_io() const
    {
        return uses(BuiltinId::GetBool) || uses(BuiltinId::GetInteger) ||
               uses(BuiltinId::GetFloat) || uses(BuiltinId::PutBool) ||
               uses(BuiltinId::PutInteger) || uses(BuiltinId::PutFloat) ||
               uses(BuiltinId::GetString) || uses(BuiltinId::PutString);
    }

    bool uses_token_input() const
    {
        return uses(BuiltinId::GetBool) || uses(BuiltinId::GetInteger) ||
               uses(BuiltinId::GetFloat);
    }

    bool uses_float_helpers() const
    {
        return float_decode || float_encode;
    }

    bool uses_word_to_float() const
    {
        return float_decode;
    }

    bool uses_float_to_word() const
    {
        return float_encode;
    }
};

bool supported_external_builtin(BuiltinId id)
{
    return id == BuiltinId::GetBool || id == BuiltinId::GetInteger ||
           id == BuiltinId::GetFloat || id == BuiltinId::PutBool ||
           id == BuiltinId::GetString || id == BuiltinId::PutInteger ||
           id == BuiltinId::PutFloat || id == BuiltinId::PutString ||
           id == BuiltinId::Sqrt;
}

RestrictedCResult validate_external_builtin_call(const ir::Module &module,
                                                 const ir::Function &caller,
                                                 const ir::Call &call,
                                                 RuntimeRequirements &runtime)
{
    const ir::Function *callee = function_for(module, call.callee);
    const ir::Value *result = value_for(caller, call.result);
    if (callee == NULL || result == NULL)
    {
        return failure(RestrictedCStatus::InvalidIR,
                       "restricted C external call has an invalid callee or result");
    }

    const BuiltinSpec *builtin = find_builtin(callee->name);
    if (callee->kind != ir::FunctionKind::ExternalBuiltin || builtin == NULL)
    {
        return failure(RestrictedCStatus::Unsupported,
                       "restricted C only lowers canonical scalar external calls");
    }

    const SymbolRef expected_reference{0, builtin->spelling};
    if (callee->symbol != expected_reference || callee->return_type != builtin->return_shape ||
        callee->parameter_types != builtin->parameter_shapes)
    {
        return failure(RestrictedCStatus::Unsupported,
                       "restricted C external call does not match the builtin catalog");
    }

    if (result->type != builtin->return_shape ||
        call.arguments.size() != builtin->parameter_shapes.size())
    {
        return failure(RestrictedCStatus::InvalidIR,
                       "restricted C external call has an invalid result or arity");
    }
    for (std::size_t argument_index = 0; argument_index < call.arguments.size(); argument_index++)
    {
        const ir::Value *argument = value_for(caller, call.arguments[argument_index]);
        if (argument == NULL || argument->type != builtin->parameter_shapes[argument_index])
        {
            return failure(RestrictedCStatus::InvalidIR,
                           "restricted C external call has an invalid argument");
        }
    }

    if (!supported_external_builtin(builtin->id))
    {
        return failure(RestrictedCStatus::Unsupported,
                       "restricted C does not yet lower this canonical external builtin");
    }

    runtime.require(builtin->id);
    runtime.float_words = runtime.float_words || builtin->id == BuiltinId::GetFloat ||
                          builtin->id == BuiltinId::PutFloat || builtin->id == BuiltinId::Sqrt;
    runtime.float_decode = runtime.float_decode || builtin->id == BuiltinId::PutFloat;
    runtime.float_encode = runtime.float_encode || builtin->id == BuiltinId::GetFloat ||
                           builtin->id == BuiltinId::Sqrt;
    runtime.string_words = runtime.string_words || builtin->id == BuiltinId::GetString ||
                           builtin->id == BuiltinId::PutString;
    RestrictedCResult result_status;
    result_status.status = RestrictedCStatus::Success;
    return result_status;
}

struct ProcedureFrame
{
    std::size_t words = 0;
    std::vector<std::size_t> parameter_offsets;
    std::vector<std::size_t> local_offsets;
    std::vector<std::size_t> value_offsets;
};

struct ProcedureRegisterLayout
{
    std::size_t count = 0;
    std::size_t exit = 0;
    std::size_t temporary_a = 0;
    std::size_t temporary_b = 0;
    std::size_t temporary_c = 0;
    std::size_t division_zero = 0;
    std::size_t division_overflow = 0;
    std::size_t return_value = 0;
    std::size_t return_site = 0;
    std::size_t string_heap = 0;
};

bool make_register_layout(const ir::Function &program, RegisterLayout &layout)
{
    for (const ir::BasicBlock &block : program.blocks)
    {
        for (const ir::Instruction &instruction : block.instructions)
        {
            const ir::Binary *binary = std::get_if<ir::Binary>(&instruction);
            layout.has_division = layout.has_division ||
                                  (binary != NULL && binary->operation == ir::BinaryOp::Divide);
        }
    }
    const std::size_t extra = layout.has_division ? 3U : 1U;
    const std::size_t reserved = 2U;
    const std::size_t maximum = static_cast<std::size_t>(
        std::numeric_limits<std::uint32_t>::max());
    if (program.values.size() > maximum ||
        program.values.size() > maximum - reserved - extra)
    {
        return false;
    }
    layout.exit = program.values.size() + reserved;
    layout.division_zero = layout.exit + 1U;
    layout.division_overflow = layout.exit + 2U;
    layout.count = program.values.size() + reserved + extra;
    return layout.count != 0U;
}

std::string int32_literal(int value)
{
    const std::int64_t widened = value;
    if (widened < static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min()) ||
        widened > static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max()))
    {
        return std::string();
    }
    if (widened == static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min()))
    {
        return "INT32_MIN";
    }
    if (widened < 0)
    {
        return "(-INT32_C(" + std::to_string(-widened) + "))";
    }
    return "INT32_C(" + std::to_string(value) + ")";
}

std::string external_call_expression(const ir::Function &callee, const std::string &argument)
{
    const BuiltinSpec *builtin = find_builtin(callee.name);
    if (builtin == NULL)
    {
        return std::string();
    }

    switch (builtin->id)
    {
    case BuiltinId::GetBool:
        return "R_get_b1()";
    case BuiltinId::GetInteger:
        return "R_get_i32()";
    case BuiltinId::GetFloat:
        return "R_get_f32()";
    case BuiltinId::GetString:
        return "R_get_str(Reg[0u])";
    case BuiltinId::PutBool:
        return "R_put_b1(" + argument + ")";
    case BuiltinId::PutInteger:
        return "R_put_i32(" + argument + ")";
    case BuiltinId::PutFloat:
        return "R_put_f32(" + argument + ")";
    case BuiltinId::PutString:
        return "R_put_str(" + argument + ")";
    case BuiltinId::Sqrt:
        return "R_sqrt_i32(" + argument + ")";
    default:
        return std::string();
    }
}

void emit_runtime_headers(std::ostringstream &output, const RuntimeRequirements &runtime)
{
    if (runtime.float_words)
    {
        output << "#include <float.h>\n";
    }
    if (runtime.uses_token_input())
    {
        output << "#include <ctype.h>\n";
    }
    if (runtime.uses(BuiltinId::PutInteger))
    {
        output << "#include <inttypes.h>\n";
    }
    if (runtime.uses_runtime_io())
    {
        output << "#include <stdio.h>\n";
    }
    if (runtime.uses(BuiltinId::GetFloat))
    {
        output << "#include <stdlib.h>\n";
    }
    if (runtime.uses_float_helpers())
    {
        output << "#include <string.h>\n";
    }
    if (runtime.uses(BuiltinId::GetFloat) || runtime.uses(BuiltinId::PutFloat) ||
        runtime.uses(BuiltinId::Sqrt) || runtime.float_to_int)
    {
        output << "#include <math.h>\n";
    }
}

void emit_float_guard(std::ostringstream &output, const RuntimeRequirements &runtime)
{
    if (!runtime.float_words)
    {
        return;
    }
    output << "#if !defined(__STDC_IEC_559__) || __STDC_IEC_559__ != 1\n"
           << "#error \"restricted C requires IEC 60559 floating point\"\n"
           << "#endif\n"
           << "#if FLT_RADIX != 2 || FLT_MANT_DIG != 24 || FLT_MAX_EXP != 128\n"
           << "#error \"restricted C requires IEEE binary32 float\"\n"
           << "#endif\n"
           << "_Static_assert(sizeof(float) == 4, \"restricted C requires 32-bit float\");\n\n";
}

void emit_runtime_support(std::ostringstream &output, const RuntimeRequirements &runtime)
{
    if (runtime.string_equality)
    {
        output << "static int32_t R_str_eq(int32_t r0, int32_t r1)\n{\n"
               << "    uint32_t r2;\n"
               << "    uint32_t r3;\n"
               << "    if (r0 < INT32_C(0) || r1 < INT32_C(0)) return INT32_C(0);\n"
               << "    r2 = (uint32_t)r0;\n"
               << "    r3 = (uint32_t)r1;\n"
               << "    while (r2 < MM_WORDS && r3 < MM_WORDS)\n"
               << "    {\n"
               << "        const int32_t r4 = MM[r2];\n"
               << "        const int32_t r5 = MM[r3];\n"
               << "        if (r4 < INT32_C(0) || r4 > INT32_C(255) ||\n"
               << "            r5 < INT32_C(0) || r5 > INT32_C(255)) return INT32_C(0);\n"
               << "        if (r4 != r5) return INT32_C(0);\n"
               << "        if (r4 == INT32_C(0)) return INT32_C(1);\n"
               << "        ++r2;\n"
               << "        ++r3;\n"
               << "    }\n"
               << "    return INT32_C(0);\n}\n\n";
    }
    if (runtime.uses(BuiltinId::GetString))
    {
        output << "static int32_t R_get_str(int32_t r0)\n{\n"
               << "    const uint32_t r1 = (uint32_t)Reg[STRING_HEAP_REGISTER];\n"
               << "    uint32_t r2 = r1;\n"
               << "    int r3 = getchar();\n"
               << "    int r4 = INT32_C(0);\n"
               << "    int r5 = INT32_C(0);\n"
               << "    if (r3 == EOF || r3 == '\\n') return STRING_EMPTY_HANDLE;\n"
               << "    if (r0 < INT32_C(0)) r4 = INT32_C(1);\n"
               << "    while (r3 != EOF && r3 != '\\n')\n"
               << "    {\n"
               << "        if (r3 == 0) r4 = INT32_C(1);\n"
               << "        if (r4 == INT32_C(0) && r5 == INT32_C(0))\n"
               << "        {\n"
               << "            if (r2 <= (uint32_t)r0) r4 = INT32_C(1);\n"
               << "            else { --r2; MM[r2] = INT32_C(0); r5 = INT32_C(1); }\n"
               << "        }\n"
               << "        if (r4 == INT32_C(0))\n"
               << "        {\n"
               << "            if (r2 <= (uint32_t)r0) r4 = INT32_C(1);\n"
               << "            else { --r2; MM[r2] = (int32_t)(unsigned char)r3; }\n"
               << "        }\n"
               << "        r3 = getchar();\n"
               << "    }\n"
               << "    if (r4 != INT32_C(0)) return STRING_EMPTY_HANDLE;\n"
               << "    {\n"
               << "        uint32_t r6 = r2;\n"
               << "        uint32_t r7 = r1 - UINT32_C(2);\n"
               << "        while (r6 < r7)\n"
               << "        {\n"
               << "            const int32_t r8 = MM[r6];\n"
               << "            MM[r6] = MM[r7];\n"
               << "            MM[r7] = r8;\n"
               << "            ++r6;\n"
               << "            --r7;\n"
               << "        }\n"
               << "    }\n"
               << "    Reg[STRING_HEAP_REGISTER] = (int32_t)r2;\n"
               << "    return (int32_t)r2;\n}\n\n";
    }
    if (runtime.uses(BuiltinId::PutString))
    {
        output << "static int32_t R_put_str(int32_t r0)\n{\n"
               << "    uint32_t r1;\n"
               << "    if (r0 < INT32_C(0)) return INT32_C(0);\n"
               << "    r1 = (uint32_t)r0;\n"
               << "    while (r1 < MM_WORDS)\n"
               << "    {\n"
               << "        const int32_t r2 = MM[r1++];\n"
               << "        if (r2 == INT32_C(0))\n"
               << "            return putchar('\\n') == EOF ? INT32_C(0) : INT32_C(1);\n"
               << "        if (r2 < INT32_C(0) || r2 > INT32_C(255) || putchar((unsigned char)r2) == EOF)\n"
               << "            return INT32_C(0);\n"
               << "    }\n"
               << "    return INT32_C(0);\n}\n\n";
    }
    if (runtime.uses_word_to_float())
    {
        output << "static float R_word_f32(int32_t r0)\n{\n"
               << "    const uint32_t r1 = (uint32_t)r0;\n"
               << "    float r2;\n"
               << "    memcpy(&r2, &r1, sizeof(r2));\n"
               << "    return r2;\n}\n\n";
    }
    if (runtime.uses_float_to_word())
    {
        output << "static int32_t R_f32_word(float r0)\n{\n"
               << "    uint32_t r1;\n"
               << "    memcpy(&r1, &r0, sizeof(r1));\n"
               << "    return I32_FROM_U32(r1);\n}\n\n";
    }
    if (runtime.float_to_int)
    {
        output << "static int32_t R_f32_i32(int32_t r0)\n{\n"
               << "    const float r1 = R_word_f32(r0);\n"
               << "    if (isnan(r1)) return INT32_C(0);\n"
               << "    if (r1 >= 2147483648.0f) return INT32_MAX;\n"
               << "    if (r1 <= -2147483648.0f) return INT32_MIN;\n"
               << "    return (int32_t)r1;\n}\n\n";
    }
    if (runtime.uses_token_input())
    {
        output << "static int R_next_token_char(void)\n{\n"
               << "    int r0;\n"
               << "    do\n"
               << "    {\n"
               << "        r0 = getchar();\n"
               << "    } while (r0 != EOF && isspace((unsigned char)r0));\n"
               << "    return r0;\n}\n\n";
    }
    if (runtime.uses(BuiltinId::GetBool) || runtime.uses(BuiltinId::GetInteger))
    {
        output << "static int R_decimal_i32(int r0, int32_t *r1)\n{\n"
               << "    int r2 = INT32_C(1);\n"
               << "    int r3 = INT32_C(0);\n"
               << "    int r4 = INT32_C(0);\n"
               << "    uint32_t r5 = UINT32_C(0);\n"
               << "    uint32_t r6;\n"
               << "    if (r0 == '+' || r0 == '-')\n"
               << "    {\n"
               << "        r2 = r0 == '-' ? -INT32_C(1) : INT32_C(1);\n"
               << "        r0 = getchar();\n"
               << "    }\n"
               << "    r6 = r2 < INT32_C(0) ? UINT32_C(2147483648) : UINT32_C(2147483647);\n"
               << "    while (r0 != EOF && !isspace((unsigned char)r0))\n"
               << "    {\n"
               << "        if (r0 < '0' || r0 > '9')\n"
               << "        {\n"
               << "            r4 = INT32_C(1);\n"
               << "        }\n"
               << "        else\n"
               << "        {\n"
               << "            const uint32_t r7 = (uint32_t)(r0 - '0');\n"
               << "            r3 = INT32_C(1);\n"
               << "            if (r5 > r6 / UINT32_C(10) ||\n"
               << "                (r5 == r6 / UINT32_C(10) && r7 > r6 % UINT32_C(10)))\n"
               << "            {\n"
               << "                r4 = INT32_C(1);\n"
               << "            }\n"
               << "            else if (r4 == INT32_C(0))\n"
               << "            {\n"
               << "                r5 = r5 * UINT32_C(10) + r7;\n"
               << "            }\n"
               << "        }\n"
               << "        r0 = getchar();\n"
               << "    }\n"
               << "    if (r3 == INT32_C(0) || r4 != INT32_C(0))\n"
               << "    {\n"
               << "        return INT32_C(0);\n"
               << "    }\n"
               << "    if (r2 < INT32_C(0))\n"
               << "    {\n"
               << "        *r1 = r5 == UINT32_C(2147483648) ? INT32_MIN : -(int32_t)r5;\n"
               << "    }\n"
               << "    else\n"
               << "    {\n"
               << "        *r1 = (int32_t)r5;\n"
               << "    }\n"
               << "    return INT32_C(1);\n}\n\n";
    }
    if (runtime.uses(BuiltinId::GetBool))
    {
        output << "static void R_skip_token(int r0)\n{\n"
               << "    while (r0 != EOF && !isspace((unsigned char)r0))\n"
               << "    {\n"
               << "        r0 = getchar();\n"
               << "    }\n}\n\n"
               << "static int R_bool_word(int r0)\n{\n"
               << "    int r1;\n"
               << "    if (tolower((unsigned char)r0) == 't')\n"
               << "    {\n"
               << "        r1 = getchar();\n"
               << "        if (r1 == EOF || tolower((unsigned char)r1) != 'r')\n"
               << "        {\n"
               << "            R_skip_token(r1);\n"
               << "            return INT32_C(0);\n"
               << "        }\n"
               << "        r1 = getchar();\n"
               << "        if (r1 == EOF || tolower((unsigned char)r1) != 'u')\n"
               << "        {\n"
               << "            R_skip_token(r1);\n"
               << "            return INT32_C(0);\n"
               << "        }\n"
               << "        r1 = getchar();\n"
               << "        if (r1 == EOF || tolower((unsigned char)r1) != 'e')\n"
               << "        {\n"
               << "            R_skip_token(r1);\n"
               << "            return INT32_C(0);\n"
               << "        }\n"
               << "        r1 = getchar();\n"
               << "        if (r1 == EOF || isspace((unsigned char)r1))\n"
               << "        {\n"
               << "            return INT32_C(1);\n"
               << "        }\n"
               << "        R_skip_token(r1);\n"
               << "        return INT32_C(0);\n"
               << "    }\n"
               << "    if (tolower((unsigned char)r0) == 'f')\n"
               << "    {\n"
               << "        r1 = getchar();\n"
               << "        if (r1 == EOF || tolower((unsigned char)r1) != 'a')\n"
               << "        {\n"
               << "            R_skip_token(r1);\n"
               << "            return INT32_C(0);\n"
               << "        }\n"
               << "        r1 = getchar();\n"
               << "        if (r1 == EOF || tolower((unsigned char)r1) != 'l')\n"
               << "        {\n"
               << "            R_skip_token(r1);\n"
               << "            return INT32_C(0);\n"
               << "        }\n"
               << "        r1 = getchar();\n"
               << "        if (r1 == EOF || tolower((unsigned char)r1) != 's')\n"
               << "        {\n"
               << "            R_skip_token(r1);\n"
               << "            return INT32_C(0);\n"
               << "        }\n"
               << "        r1 = getchar();\n"
               << "        if (r1 == EOF || tolower((unsigned char)r1) != 'e')\n"
               << "        {\n"
               << "            R_skip_token(r1);\n"
               << "            return INT32_C(0);\n"
               << "        }\n"
               << "        r1 = getchar();\n"
               << "        if (r1 == EOF || isspace((unsigned char)r1))\n"
               << "        {\n"
               << "            return INT32_C(0);\n"
               << "        }\n"
               << "        R_skip_token(r1);\n"
               << "        return INT32_C(0);\n"
               << "    }\n"
               << "    R_skip_token(r0);\n"
               << "    return INT32_C(0);\n}\n\n";
    }
    if (runtime.uses(BuiltinId::GetInteger))
    {
        output << "static int32_t R_get_i32(void)\n{\n"
               << "    int32_t r0 = INT32_C(0);\n"
               << "    const int r1 = R_next_token_char();\n"
               << "    return r1 == EOF || !R_decimal_i32(r1, &r0) ? INT32_C(0) : r0;\n}\n\n";
    }
    if (runtime.uses(BuiltinId::GetBool))
    {
        output << "static int32_t R_get_b1(void)\n{\n"
               << "    int32_t r0 = INT32_C(0);\n"
               << "    const int r1 = R_next_token_char();\n"
               << "    if (r1 == EOF)\n"
               << "    {\n"
               << "        return INT32_C(0);\n"
               << "    }\n"
               << "    if (r1 == '+' || r1 == '-' || (r1 >= '0' && r1 <= '9'))\n"
               << "    {\n"
               << "        return R_decimal_i32(r1, &r0) && r0 != INT32_C(0) ? INT32_C(1) : INT32_C(0);\n"
               << "    }\n"
               << "    return R_bool_word(r1) ? INT32_C(1) : INT32_C(0);\n}\n\n";
    }
    if (runtime.uses(BuiltinId::GetFloat))
    {
        output << "static int R_decimal_f32_token(const char *r0)\n{\n"
               << "    size_t r1 = 0u;\n"
               << "    int r2 = INT32_C(0);\n"
               << "    if (r0[r1] == '+' || r0[r1] == '-') ++r1;\n"
               << "    while (r0[r1] >= '0' && r0[r1] <= '9') { r2 = INT32_C(1); ++r1; }\n"
               << "    if (r0[r1] == '.')\n"
               << "    {\n"
               << "        ++r1;\n"
               << "        while (r0[r1] >= '0' && r0[r1] <= '9') { r2 = INT32_C(1); ++r1; }\n"
               << "    }\n"
               << "    if (r2 == INT32_C(0)) return INT32_C(0);\n"
               << "    if (r0[r1] == 'e' || r0[r1] == 'E')\n"
               << "    {\n"
               << "        int r3 = INT32_C(0);\n"
               << "        ++r1;\n"
               << "        if (r0[r1] == '+' || r0[r1] == '-') ++r1;\n"
               << "        while (r0[r1] >= '0' && r0[r1] <= '9') { r3 = INT32_C(1); ++r1; }\n"
               << "        if (r3 == INT32_C(0)) return INT32_C(0);\n"
               << "    }\n"
               << "    return r0[r1] == '\\0' ? INT32_C(1) : INT32_C(0);\n}\n\n"
               << "static int32_t R_get_f32(void)\n{\n"
               << "    size_t r0 = 64u;\n"
               << "    size_t r1 = 0u;\n"
               << "    int r2 = R_next_token_char();\n"
               << "    int r3 = INT32_C(0);\n"
               << "    char *r4;\n"
               << "    char *r5;\n"
               << "    char *r6;\n"
               << "    float r7;\n"
               << "    if (r2 == EOF) return INT32_C(0);\n"
               << "    r4 = (char *)malloc(r0);\n"
               << "    while (r2 != EOF && !isspace((unsigned char)r2))\n"
               << "    {\n"
               << "        if (r4 != NULL && r1 + 1u >= r0)\n"
               << "        {\n"
               << "            const size_t r8 = r0 <= ((size_t)-1) / 2u ? r0 * 2u : 0u;\n"
               << "            r5 = r8 == 0u ? NULL : (char *)realloc(r4, r8);\n"
               << "            if (r5 == NULL) { free(r4); r4 = NULL; }\n"
               << "            else { r4 = r5; r0 = r8; }\n"
               << "        }\n"
               << "        if (r4 != NULL) r4[r1++] = (char)r2;\n"
               << "        r2 = getchar();\n"
               << "    }\n"
               << "    if (r4 == NULL) return INT32_C(0);\n"
               << "    r4[r1] = '\\0';\n"
               << "    if (!R_decimal_f32_token(r4)) { free(r4); return INT32_C(0); }\n"
               << "    r7 = strtof(r4, &r6);\n"
               << "    if (r6 != r4 + r1 || !isfinite(r7)) r3 = INT32_C(1);\n"
               << "    free(r4);\n"
               << "    return r3 ? INT32_C(0) : R_f32_word(r7);\n}\n\n";
    }
    if (runtime.uses(BuiltinId::PutInteger))
    {
        output << "static int32_t R_put_i32(int32_t r0)\n{\n"
               << "    return printf(\"%\" PRId32 \"\\n\", r0) < 0 ? INT32_C(0) : INT32_C(1);\n}\n\n";
    }
    if (runtime.uses(BuiltinId::PutBool))
    {
        output << "static int32_t R_put_b1(int32_t r0)\n{\n"
               << "    return printf(\"%s\\n\", r0 == INT32_C(0) ? \"false\" : \"true\") < 0 ? "
               << "INT32_C(0) : INT32_C(1);\n}\n\n";
    }
    if (runtime.uses(BuiltinId::PutFloat))
    {
        output << "static int32_t R_put_f32(int32_t r0)\n{\n"
               << "    const float r1 = R_word_f32(r0);\n"
               << "    int r2;\n"
               << "    if (isnan(r1)) r2 = printf(\"nan\\n\");\n"
               << "    else if (isinf(r1)) r2 = printf(signbit(r1) ? \"-inf\\n\" : \"inf\\n\");\n"
               << "    else r2 = printf(\"%.*g\\n\", FLT_DECIMAL_DIG, (double)r1);\n"
               << "    return r2 < 0 ? INT32_C(0) : INT32_C(1);\n}\n\n";
    }
    if (runtime.uses(BuiltinId::Sqrt))
    {
        output << "static int32_t R_sqrt_i32(int32_t r0)\n{\n"
               << "    if (r0 < INT32_C(0)) return I32_FROM_U32(UINT32_C(2143289344));\n"
               << "    return R_f32_word(sqrtf((float)r0));\n}\n\n";
    }
}

//This deliberately lives entirely in the restricted-C backend.  The IR knows
//only normal calls/returns and canonical storage ownership; frame offsets and
//the flat-C continuation ABI are not frontend or IR concepts.
RestrictedCResult preflight_with_procedures(const ir::Module &module,
                                            const ir::Function *&program,
                                            RuntimeRequirements &runtime)
{
    if (module.functions.empty() || module.functions[0].kind != ir::FunctionKind::Program ||
        module.functions[0].id != ir::FunctionId(0))
    {
        return failure(RestrictedCStatus::Unsupported,
                       "restricted C requires a verified Program function f0");
    }
    program = &module.functions[0];
    std::size_t global_count = 0;
    for (const ir::Storage &storage : module.storages)
    {
        global_count += storage.kind == ir::StorageKind::Global ? 1U : 0U;
    }
    if (!RestrictedCEmitter::storage_count_fits_memory(global_count))
    {
        return failure(RestrictedCStatus::Unsupported,
                       "restricted C global storage exceeds the fixed memory capacity");
    }
    runtime.reachable_functions.assign(module.functions.size(), false);
    runtime.reachable_functions[0] = true;
    std::vector<std::size_t> pending{0};
    while (!pending.empty())
    {
        const std::size_t function_index = pending.back();
        pending.pop_back();
        const ir::Function &function = module.functions[function_index];
        for (const ir::BasicBlock &block : function.blocks)
        {
            for (const ir::Instruction &instruction : block.instructions)
            {
                const ir::Call *call = std::get_if<ir::Call>(&instruction);
                if (call == NULL)
                {
                    continue;
                }
                const ir::Function *callee = function_for(module, call->callee);
                if (callee == NULL)
                {
                    return failure(RestrictedCStatus::InvalidIR,
                                   "restricted C call has no canonical callee");
                }
                if (callee->kind == ir::FunctionKind::Procedure &&
                    !runtime.reachable_functions[callee->id.index])
                {
                    runtime.reachable_functions[callee->id.index] = true;
                    pending.push_back(callee->id.index);
                }
            }
        }
    }
    for (const ir::Function &function : module.functions)
    {
        runtime.procedure_mode = runtime.procedure_mode ||
                                 function.kind == ir::FunctionKind::Procedure;
        if (function.kind == ir::FunctionKind::ExternalBuiltin ||
            !runtime.reachable_functions[function.id.index])
        {
            continue;
        }
        if (function.kind != ir::FunctionKind::Program &&
            function.kind != ir::FunctionKind::Procedure)
        {
            return failure(RestrictedCStatus::Unsupported,
                           "restricted C encountered an unknown defined function");
        }
        if (function.kind == ir::FunctionKind::Procedure)
        {
            runtime.procedures = true;
            if (!supported_shape(function.return_type))
            {
                return failure(RestrictedCStatus::Unsupported,
                               "restricted C supports scalar procedure returns only");
            }
            runtime.float_words = runtime.float_words ||
                                  function.return_type.element_type == TYPE_FLOAT;
            runtime.string_words = runtime.string_words ||
                                   function.return_type.element_type == TYPE_STRING;
        }
        for (const value_shape &shape : function.parameter_types)
        {
            if (!supported_shape(shape))
            {
                return failure(RestrictedCStatus::Unsupported,
                               "restricted C supports scalar parameters only");
            }
            runtime.float_words = runtime.float_words || shape.element_type == TYPE_FLOAT;
            runtime.string_words = runtime.string_words || shape.element_type == TYPE_STRING;
        }
        for (const ir::Value &value : function.values)
        {
            if (!supported_shape(value.type))
            {
                return failure(RestrictedCStatus::Unsupported,
                               "restricted C supports scalar values only");
            }
            runtime.float_words = runtime.float_words || value.type.element_type == TYPE_FLOAT;
            runtime.string_words = runtime.string_words || value.type.element_type == TYPE_STRING;
        }
        for (const ir::BasicBlock &block : function.blocks)
        {
            for (const ir::Instruction &instruction : block.instructions)
            {
                if (const ir::Constant *constant = std::get_if<ir::Constant>(&instruction))
                {
                    const ir::Value *value = value_for(function, constant->result);
                    if (value != NULL && value->type.element_type == TYPE_FLOAT)
                    {
                        if (!std::holds_alternative<float>(constant->payload) ||
                            !std::isfinite(std::get<float>(constant->payload)))
                        {
                            return failure(RestrictedCStatus::InvalidIR,
                                           "restricted C Float constant must be finite binary32");
                        }
                        runtime.float_words = true;
                    }
                    else if (value != NULL && value->type.element_type == TYPE_STRING)
                    {
                        const std::string &payload = std::get<std::string>(constant->payload);
                        if (payload.find('\0') != std::string::npos)
                        {
                            return failure(RestrictedCStatus::Unsupported,
                                           "restricted C String constants may not contain NUL");
                        }
                        if (!payload.empty() &&
                            std::find(runtime.string_literals.begin(),
                                      runtime.string_literals.end(), payload) ==
                                runtime.string_literals.end())
                        {
                            runtime.string_literals.push_back(payload);
                        }
                    }
                }
                else if (const ir::Unary *unary = std::get_if<ir::Unary>(&instruction))
                {
                    const ir::Value *operand = value_for(function, unary->operand);
                    const ir::Value *result = value_for(function, unary->result);
                    runtime.float_decode = runtime.float_decode ||
                        (operand != NULL && operand->type.element_type == TYPE_FLOAT);
                    runtime.float_encode = runtime.float_encode ||
                        (result != NULL && result->type.element_type == TYPE_FLOAT);
                }
                else if (const ir::Binary *binary = std::get_if<ir::Binary>(&instruction))
                {
                    const ir::Value *left = value_for(function, binary->left);
                    const ir::Value *result = value_for(function, binary->result);
                    runtime.float_decode = runtime.float_decode ||
                        (left != NULL && left->type.element_type == TYPE_FLOAT);
                    runtime.float_encode = runtime.float_encode ||
                        (result != NULL && result->type.element_type == TYPE_FLOAT);
                    runtime.string_equality = runtime.string_equality ||
                        (left != NULL && left->type.element_type == TYPE_STRING);
                }
                else if (const ir::Cast *cast = std::get_if<ir::Cast>(&instruction))
                {
                    runtime.float_to_int = runtime.float_to_int ||
                                           cast->operation == ir::CastOp::FloatToInt;
                    runtime.float_encode = runtime.float_encode ||
                                           cast->operation == ir::CastOp::IntToFloat;
                    runtime.float_decode = runtime.float_decode ||
                                           cast->operation == ir::CastOp::FloatToInt;
                }
                if (const ir::Call *call = std::get_if<ir::Call>(&instruction))
                {
                    const ir::Function *callee = function_for(module, call->callee);
                    if (callee == NULL)
                    {
                        return failure(RestrictedCStatus::InvalidIR,
                                       "restricted C call has no canonical callee");
                    }
                    if (callee->kind == ir::FunctionKind::Procedure)
                    {
                        continue;
                    }
                    RestrictedCResult checked =
                        validate_external_builtin_call(module, function, *call, runtime);
                    if (!checked.succeeded())
                    {
                        return checked;
                    }
                }
            }
        }
    }
    for (const ir::Storage &storage : module.storages)
    {
        if ((storage.kind == ir::StorageKind::Global ||
             runtime.reachable_functions[storage.owner.index]) &&
            !supported_shape(storage.type))
        {
            return failure(RestrictedCStatus::Unsupported,
                           "restricted C supports scalar storage only");
        }
        if (storage.kind == ir::StorageKind::Global ||
            runtime.reachable_functions[storage.owner.index])
        {
            runtime.float_words = runtime.float_words || storage.type.element_type == TYPE_FLOAT;
            runtime.string_words = runtime.string_words || storage.type.element_type == TYPE_STRING;
        }
    }
    runtime.procedure_mode = runtime.procedure_mode || runtime.string_words;
    RestrictedCResult result;
    result.status = RestrictedCStatus::Success;
    return result;
}

RestrictedCResult preflight(const ir::Module &module, const ir::Function *&program,
                            RuntimeRequirements &runtime)
{
    const ir::VerificationResult verified = ir::verify_module(module);
    if (!verified.valid)
    {
        return failure(RestrictedCStatus::InvalidIR, verified.reason);
    }
    RestrictedCResult procedure_checked = preflight_with_procedures(module, program, runtime);
    if (!procedure_checked.succeeded() || runtime.procedure_mode)
    {
        return procedure_checked;
    }
    //A non-String Program with no procedure declarations retains the original
    //straight-line preflight and byte-for-byte output path below.
    if (module.functions.empty() || module.functions[0].kind != ir::FunctionKind::Program ||
        module.functions[0].id != ir::FunctionId(0) || module.functions[0].blocks.empty())
    {
        return failure(RestrictedCStatus::Unsupported,
                       "restricted C requires a verified Program function f0");
    }
    program = &module.functions[0];
    if (!RestrictedCEmitter::storage_count_fits_memory(module.storages.size()))
    {
        return failure(RestrictedCStatus::Unsupported,
                       "restricted C global storage exceeds the fixed memory capacity");
    }
    for (const ir::Function &function : module.functions)
    {
        if (function.kind == ir::FunctionKind::ExternalBuiltin)
        {
            continue; //Declarations are harmless until a Call uses one.
        }
        if (function.kind == ir::FunctionKind::Procedure)
        {
            continue;
        }
        if (function.kind != ir::FunctionKind::Program)
        {
            return failure(RestrictedCStatus::Unsupported,
                           "restricted C does not yet lower procedures");
        }
        if (function.id != program->id || !function.parameters.empty() || !function.locals.empty() ||
            !function.parameter_types.empty())
        {
            return failure(RestrictedCStatus::Unsupported,
                           "restricted C only supports a parameterless Program");
        }
    }
    for (const ir::Storage &storage : module.storages)
    {
        if (storage.kind != ir::StorageKind::Global)
        {
            continue;
        }
        if (storage.kind != ir::StorageKind::Global || storage.owner != program->id ||
            !supported_shape(storage.type))
        {
            return failure(RestrictedCStatus::Unsupported,
                           "restricted C supports scalar numeric and Bool globals only");
        }
        runtime.float_words = runtime.float_words || storage.type.element_type == TYPE_FLOAT;
    }
    for (const ir::Value &value : program->values)
    {
        if (!supported_shape(value.type))
        {
            return failure(RestrictedCStatus::Unsupported,
                           "restricted C supports scalar numeric and Bool values only");
        }
        runtime.float_words = runtime.float_words || value.type.element_type == TYPE_FLOAT;
    }
    for (const ir::BasicBlock &block : program->blocks)
    {
        for (const ir::Instruction &instruction : block.instructions)
        {
        if (const ir::Constant *constant = std::get_if<ir::Constant>(&instruction))
        {
            const ir::Value *result = value_for(*program, constant->result);
            if (result == NULL ||
                (result->type.element_type == TYPE_INT &&
                 (!std::holds_alternative<int>(constant->payload) ||
                  int32_literal(std::get<int>(constant->payload)).empty())) ||
                (result->type.element_type == TYPE_BOOL &&
                 !std::holds_alternative<bool>(constant->payload)) ||
                (result->type.element_type == TYPE_FLOAT &&
                 (!std::holds_alternative<float>(constant->payload) ||
                  !std::isfinite(std::get<float>(constant->payload)))))
            {
                return failure(RestrictedCStatus::InvalidIR,
                               "restricted C constant does not match its result");
            }
        }
        else if (const ir::Load *load = std::get_if<ir::Load>(&instruction))
        {
            const ir::Value *result = value_for(*program, load->result);
            const ir::Storage *source = storage_for(module, load->source);
            if (result == NULL || source == NULL || result->type != source->type ||
                source->kind != ir::StorageKind::Global)
            {
                return failure(RestrictedCStatus::InvalidIR,
                               "restricted C load is not a typed global load");
            }
        }
        else if (const ir::Store *store = std::get_if<ir::Store>(&instruction))
        {
            const ir::Storage *destination = storage_for(module, store->destination);
            const ir::Value *value = value_for(*program, store->value);
            if (destination == NULL || value == NULL || destination->kind != ir::StorageKind::Global ||
                destination->type != value->type)
            {
                return failure(RestrictedCStatus::InvalidIR,
                               "restricted C store is not a typed global store");
            }
        }
        else if (const ir::Unary *unary = std::get_if<ir::Unary>(&instruction))
        {
            const ir::Value *result = value_for(*program, unary->result);
            const ir::Value *operand = value_for(*program, unary->operand);
            if (result == NULL || operand == NULL || result->type != operand->type ||
                !supported_unary(unary->operation, operand->type.element_type))
            {
                return failure(RestrictedCStatus::InvalidIR,
                               "restricted C unary instruction is invalid");
            }
            runtime.float_decode = runtime.float_decode ||
                                   operand->type.element_type == TYPE_FLOAT;
            runtime.float_encode = runtime.float_encode ||
                                   result->type.element_type == TYPE_FLOAT;
        }
        else if (const ir::Binary *binary = std::get_if<ir::Binary>(&instruction))
        {
            const ir::Value *result = value_for(*program, binary->result);
            const ir::Value *left = value_for(*program, binary->left);
            const ir::Value *right = value_for(*program, binary->right);
            if (result == NULL || left == NULL || right == NULL || left->type != right->type ||
                !supported_binary(binary->operation, left->type.element_type))
            {
                return failure(RestrictedCStatus::InvalidIR,
                               "restricted C binary instruction is invalid");
            }
            const data_types expected =
                binary->operation == ir::BinaryOp::Less ||
                        binary->operation == ir::BinaryOp::LessEqual ||
                        binary->operation == ir::BinaryOp::Greater ||
                        binary->operation == ir::BinaryOp::GreaterEqual ||
                        binary->operation == ir::BinaryOp::Equal ||
                        binary->operation == ir::BinaryOp::NotEqual
                    ? TYPE_BOOL
                    : left->type.element_type;
            if (result->type.element_type != expected)
            {
                return failure(RestrictedCStatus::InvalidIR,
                               "restricted C binary result has the wrong type");
            }
            runtime.float_decode = runtime.float_decode ||
                                   left->type.element_type == TYPE_FLOAT;
            runtime.float_encode = runtime.float_encode ||
                                   result->type.element_type == TYPE_FLOAT;
        }
        else if (const ir::Cast *cast = std::get_if<ir::Cast>(&instruction))
        {
            const ir::Value *result = value_for(*program, cast->result);
            const ir::Value *operand = value_for(*program, cast->operand);
            if (result == NULL || operand == NULL || !supported_cast(cast->operation) ||
                (cast->operation == ir::CastOp::IntToBool &&
                 (operand->type.element_type != TYPE_INT || result->type.element_type != TYPE_BOOL)) ||
                (cast->operation == ir::CastOp::BoolToInt &&
                 (operand->type.element_type != TYPE_BOOL || result->type.element_type != TYPE_INT)) ||
                (cast->operation == ir::CastOp::IntToFloat &&
                 (operand->type.element_type != TYPE_INT || result->type.element_type != TYPE_FLOAT)) ||
                (cast->operation == ir::CastOp::FloatToInt &&
                 (operand->type.element_type != TYPE_FLOAT || result->type.element_type != TYPE_INT)))
            {
                return failure(RestrictedCStatus::InvalidIR,
                               "restricted C cast is invalid");
            }
            runtime.float_to_int = runtime.float_to_int ||
                                   cast->operation == ir::CastOp::FloatToInt;
            runtime.float_encode = runtime.float_encode ||
                                   cast->operation == ir::CastOp::IntToFloat;
            runtime.float_decode = runtime.float_decode ||
                                   cast->operation == ir::CastOp::FloatToInt;
        }
        else if (const ir::Call *call = std::get_if<ir::Call>(&instruction))
        {
            RestrictedCResult checked =
                validate_external_builtin_call(module, *program, *call, runtime);
            if (!checked.succeeded())
            {
                return checked;
            }
        }
        else
        {
            return failure(RestrictedCStatus::Unsupported,
                           "restricted C encountered an unknown instruction");
        }
        }
    }
    RegisterLayout layout;
    if (!make_register_layout(*program, layout))
    {
        return failure(RestrictedCStatus::Unsupported,
                       "restricted C register model cannot represent this many values");
    }
    RestrictedCResult success;
    success.status = RestrictedCStatus::Success;
    return success;
}

std::string binary_expression_text(ir::BinaryOp operation, data_types type,
                                   const std::string &left, const std::string &right)
{
    if (type == TYPE_STRING)
    {
        const std::string equal = "R_str_eq(" + left + ", " + right + ")";
        return operation == ir::BinaryOp::Equal ? equal :
            "(" + equal + " == INT32_C(0)) ? INT32_C(1) : INT32_C(0)";
    }
    if (type == TYPE_FLOAT)
    {
        const std::string decoded_left = "R_word_f32(" + left + ")";
        const std::string decoded_right = "R_word_f32(" + right + ")";
        switch (operation)
        {
        case ir::BinaryOp::Add:
            return "R_f32_word(" + decoded_left + " + " + decoded_right + ")";
        case ir::BinaryOp::Subtract:
            return "R_f32_word(" + decoded_left + " - " + decoded_right + ")";
        case ir::BinaryOp::Multiply:
            return "R_f32_word(" + decoded_left + " * " + decoded_right + ")";
        case ir::BinaryOp::Divide:
            return "R_f32_word(" + decoded_left + " / " + decoded_right + ")";
        case ir::BinaryOp::Less:
            return "(" + decoded_left + " < " + decoded_right + ") ? INT32_C(1) : INT32_C(0)";
        case ir::BinaryOp::LessEqual:
            return "(" + decoded_left + " <= " + decoded_right + ") ? INT32_C(1) : INT32_C(0)";
        case ir::BinaryOp::Greater:
            return "(" + decoded_left + " > " + decoded_right + ") ? INT32_C(1) : INT32_C(0)";
        case ir::BinaryOp::GreaterEqual:
            return "(" + decoded_left + " >= " + decoded_right + ") ? INT32_C(1) : INT32_C(0)";
        case ir::BinaryOp::Equal:
            return "(" + decoded_left + " == " + decoded_right + ") ? INT32_C(1) : INT32_C(0)";
        case ir::BinaryOp::NotEqual:
            return "(" + decoded_left + " != " + decoded_right + ") ? INT32_C(1) : INT32_C(0)";
        case ir::BinaryOp::And:
        case ir::BinaryOp::Or:
            return std::string();
        }
    }
    switch (operation)
    {
    case ir::BinaryOp::Add:
        return "I32_FROM_U32((uint32_t)" + left + " + (uint32_t)" + right + ")";
    case ir::BinaryOp::Subtract:
        return "I32_FROM_U32((uint32_t)" + left + " - (uint32_t)" + right + ")";
    case ir::BinaryOp::Multiply:
        return "I32_FROM_U32((uint32_t)" + left + " * (uint32_t)" + right + ")";
    case ir::BinaryOp::Divide:
        return left + " / " + right;
    case ir::BinaryOp::Less:
        return "(" + left + " < " + right + ") ? INT32_C(1) : INT32_C(0)";
    case ir::BinaryOp::LessEqual:
        return "(" + left + " <= " + right + ") ? INT32_C(1) : INT32_C(0)";
    case ir::BinaryOp::Greater:
        return "(" + left + " > " + right + ") ? INT32_C(1) : INT32_C(0)";
    case ir::BinaryOp::GreaterEqual:
        return "(" + left + " >= " + right + ") ? INT32_C(1) : INT32_C(0)";
    case ir::BinaryOp::Equal:
        return "(" + left + " == " + right + ") ? INT32_C(1) : INT32_C(0)";
    case ir::BinaryOp::NotEqual:
        return "(" + left + " != " + right + ") ? INT32_C(1) : INT32_C(0)";
    case ir::BinaryOp::And:
        if (type == TYPE_BOOL)
        {
            return "(" + left + " != INT32_C(0) && " + right +
                   " != INT32_C(0)) ? INT32_C(1) : INT32_C(0)";
        }
        return "I32_FROM_U32((uint32_t)" + left + " & (uint32_t)" + right + ")";
    case ir::BinaryOp::Or:
        if (type == TYPE_BOOL)
        {
            return "(" + left + " != INT32_C(0) || " + right +
                   " != INT32_C(0)) ? INT32_C(1) : INT32_C(0)";
        }
        return "I32_FROM_U32((uint32_t)" + left + " | (uint32_t)" + right + ")";
    }
    return std::string();
}

std::string binary_expression(const ir::Binary &binary, data_types type)
{
    return binary_expression_text(binary.operation, type, value_register(binary.left),
                                  value_register(binary.right));
}

std::filesystem::path sibling_temp_directory(const std::filesystem::path &output,
                                             std::error_code &error)
{
    static std::atomic<unsigned long> serial{0};
    const std::filesystem::path parent = output.parent_path().empty() ?
                                               std::filesystem::path(".") : output.parent_path();
    const std::string base = "." + output.filename().string() + ".restricted-c-tmp-";
    for (unsigned int attempt = 0; attempt < 1024U; attempt++)
    {
        const std::filesystem::path candidate = parent /
            (base + std::to_string(serial.fetch_add(1, std::memory_order_relaxed)));
        if (std::filesystem::create_directory(candidate, error))
        {
            return candidate;
        }
        if (error == std::errc::file_exists)
        {
            error.clear();
            continue;
        }
        if (error)
        {
            return std::filesystem::path();
        }
    }
    error = std::make_error_code(std::errc::file_exists);
    return std::filesystem::path();
}

void cleanup_temp_directory(const std::filesystem::path &directory)
{
    std::error_code ignored;
    const std::filesystem::path file = directory / "output.c";
    std::filesystem::remove(file, ignored);
    ignored.clear();
    std::filesystem::remove(directory, ignored);
}

bool is_direct_regular_or_missing(const std::filesystem::path &path, std::error_code &error)
{
    const std::filesystem::file_status status = std::filesystem::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory)
    {
        error.clear();
        return true;
    }
    if (error)
    {
        return false;
    }
    if (status.type() == std::filesystem::file_type::not_found)
    {
        return true;
    }
    return status.type() == std::filesystem::file_type::regular;
}

bool make_procedure_frames(const ir::Module &module, const std::vector<bool> &reachable,
                           std::vector<ProcedureFrame> &frames, std::size_t static_words)
{
    frames.assign(module.functions.size(), ProcedureFrame{});
    if (static_words > RestrictedCEmitter::memory_word_capacity())
    {
        return false;
    }
    for (const ir::Function &function : module.functions)
    {
        if (function.kind != ir::FunctionKind::Procedure || !reachable[function.id.index])
        {
            continue;
        }
        ProcedureFrame &frame = frames[function.id.index];
        frame.words = 4U; //old SP, old FP, continuation, result-word address
        for (ir::StorageId ignored : function.parameters)
        {
            (void)ignored;
            frame.parameter_offsets.push_back(frame.words++);
        }
        for (ir::StorageId ignored : function.locals)
        {
            (void)ignored;
            frame.local_offsets.push_back(frame.words++);
        }
        for (std::size_t value = 0; value < function.values.size(); value++)
        {
            frame.value_offsets.push_back(frame.words++);
        }
        if (frame.words > RestrictedCEmitter::memory_word_capacity() - static_words)
        {
            return false;
        }
    }
    return true;
}

bool make_procedure_register_layout(const ir::Function &program, bool needs_string_heap,
                                    ProcedureRegisterLayout &layout)
{
    const std::size_t reserved = 2U;
    const std::size_t extra = needs_string_heap ? 9U : 8U;
    const std::size_t maximum = static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max());
    if (program.values.size() > maximum || program.values.size() > maximum - reserved - extra)
    {
        return false;
    }
    layout.exit = program.values.size() + reserved;
    layout.temporary_a = layout.exit + 1U;
    layout.temporary_b = layout.exit + 2U;
    layout.temporary_c = layout.exit + 3U;
    layout.division_zero = layout.exit + 4U;
    layout.division_overflow = layout.exit + 5U;
    layout.return_value = layout.exit + 6U;
    layout.return_site = layout.exit + 7U;
    layout.string_heap = layout.exit + 8U;
    layout.count = layout.exit + extra;
    return true;
}

std::size_t frame_storage_offset(const ir::Function &function, const ProcedureFrame &frame,
                                 ir::StorageId storage)
{
    for (std::size_t index = 0; index < function.parameters.size(); index++)
    {
        if (function.parameters[index] == storage)
        {
            return frame.parameter_offsets[index];
        }
    }
    for (std::size_t index = 0; index < function.locals.size(); index++)
    {
        if (function.locals[index] == storage)
        {
            return frame.local_offsets[index];
        }
    }
    return std::numeric_limits<std::size_t>::max();
}

RestrictedCResult emit_with_procedures(const ir::Module &module, const ir::Function &program,
                                       const RuntimeRequirements &runtime)
{
    std::vector<std::size_t> global_words(module.storages.size(),
                                           std::numeric_limits<std::size_t>::max());
    std::size_t static_words = 0;
    for (const ir::Storage &storage : module.storages)
    {
        if (storage.kind == ir::StorageKind::Global)
        {
            if (static_words == RestrictedCEmitter::memory_word_capacity())
            {
                return failure(RestrictedCStatus::Unsupported,
                               "restricted C globals exceed fixed memory capacity");
            }
            global_words[storage.id.index] = static_words++;
        }
    }
    std::vector<std::size_t> program_spills(program.values.size(),
                                            std::numeric_limits<std::size_t>::max());
    for (const ir::BasicBlock &block : program.blocks)
    {
        for (const ir::Instruction &instruction : block.instructions)
        {
            const ir::Call *call = std::get_if<ir::Call>(&instruction);
            if (call != NULL && module.functions[call->callee.index].kind ==
                                    ir::FunctionKind::Procedure)
            {
                if (static_words == RestrictedCEmitter::memory_word_capacity())
                {
                    return failure(RestrictedCStatus::Unsupported,
                                   "restricted C Program call spills exceed fixed memory capacity");
                }
                program_spills[call->result.index] = static_words++;
            }
        }
    }
    std::size_t empty_string_handle = std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> literal_handles;
    if (runtime.string_words)
    {
        if (static_words == RestrictedCEmitter::memory_word_capacity())
        {
            return failure(RestrictedCStatus::Unsupported,
                           "restricted C String pool exceeds fixed memory capacity");
        }
        empty_string_handle = static_words++;
        for (const std::string &literal : runtime.string_literals)
        {
            const std::size_t available =
                RestrictedCEmitter::memory_word_capacity() - static_words;
            if (literal.size() >= available)
            {
                return failure(RestrictedCStatus::Unsupported,
                               "restricted C String pool exceeds fixed memory capacity");
            }
            literal_handles.push_back(static_words);
            static_words += literal.size() + 1U;
        }
    }
    std::vector<ProcedureFrame> frames;
    if (!make_procedure_frames(module, runtime.reachable_functions, frames, static_words))
    {
        return failure(RestrictedCStatus::Unsupported,
                       "restricted C static/frame layout exceeds fixed memory capacity");
    }
    ProcedureRegisterLayout layout;
    if (!make_procedure_register_layout(program, runtime.uses(BuiltinId::GetString), layout))
    {
        return failure(RestrictedCStatus::Unsupported,
                       "restricted C register model cannot represent this many values");
    }

    const auto word = [](std::size_t index) {
        return std::string("MM[") + std::to_string(index) + "u]";
    };
    const auto frame_word = [&word](std::size_t offset) {
        return std::string("MM[(uint32_t)Reg[1u] + ") + std::to_string(offset) + "u]";
    };
    const auto value_read = [&frames, &frame_word](const ir::Function &function, ir::ValueId value) {
        if (function.kind == ir::FunctionKind::Program)
        {
            return value_register(value);
        }
        return frame_word(frames[function.id.index].value_offsets[value.index]);
    };
    const auto storage_read = [&module, &frames, &global_words, &word, &frame_word](const ir::Function &function,
                                                                        ir::StorageId storage) {
        const ir::Storage &entry = module.storages[storage.index];
        if (entry.kind == ir::StorageKind::Global)
        {
            return word(global_words[storage.index]);
        }
        return frame_word(frame_storage_offset(function, frames[function.id.index], storage));
    };
    const auto value_write = [&frames, &frame_word](const ir::Function &function, ir::ValueId value) {
        if (function.kind == ir::FunctionKind::Program)
        {
            return value_register(value);
        }
        return frame_word(frames[function.id.index].value_offsets[value.index]);
    };
    const auto result_address = [&frames, &program_spills](const ir::Function &function,
                                                             ir::ValueId value) {
        if (function.kind == ir::FunctionKind::Program)
        {
            return std::to_string(program_spills[value.index]) + "u";
        }
        return std::string("((uint32_t)Reg[1u] + ") +
               std::to_string(frames[function.id.index].value_offsets[value.index]) + "u)";
    };
    const auto string_handle = [&runtime, &literal_handles, empty_string_handle](
                                   const std::string &literal) {
        if (literal.empty())
        {
            return empty_string_handle;
        }
        const std::vector<std::string>::const_iterator found =
            std::find(runtime.string_literals.begin(), runtime.string_literals.end(), literal);
        return found == runtime.string_literals.end() ?
            std::numeric_limits<std::size_t>::max() :
            literal_handles[static_cast<std::size_t>(found - runtime.string_literals.begin())];
    };

    std::ostringstream output;
    output << "#include <stdint.h>\n";
    emit_runtime_headers(output, runtime);
    emit_float_guard(output, runtime);
    output << "\n#define MM_BYTES (" << RestrictedCEmitter::memory_byte_capacity() << "u)\n";
    output << "#define MM_WORDS (MM_BYTES / sizeof(int32_t))\n";
    output << "#define REGISTER_COUNT " << layout.count << "u\n\n";
    output << "#define I32_FROM_U32(value) ((value) <= UINT32_C(2147483647) ? "
           << "(int32_t)(value) : INT32_MIN + (int32_t)((uint32_t)(value) - "
           << "UINT32_C(2147483648)))\n\n";
    if (runtime.string_words)
    {
        output << "#define STRING_EMPTY_HANDLE INT32_C(" << empty_string_handle << ")\n";
    }
    if (runtime.uses(BuiltinId::GetString))
    {
        output << "#define STRING_HEAP_REGISTER " << layout.string_heap << "u\n";
    }
    if (runtime.string_words)
    {
        output << "\n";
    }
    output << "int32_t MM[MM_WORDS];\nint32_t Reg[REGISTER_COUNT];\n\n";
    emit_runtime_support(output, runtime);
    output << "int main(void)\n{\n";
    output << "    " << register_slot(layout.exit) << " = INT32_C(0);\n";
    output << "    Reg[0u] = INT32_C(" << static_words << ");\n";
    output << "    Reg[1u] = INT32_C(0);\n";
    if (runtime.uses(BuiltinId::GetString))
    {
        output << "    " << register_slot(layout.string_heap) << " = INT32_C("
               << RestrictedCEmitter::memory_word_capacity() << ");\n";
    }
    if (runtime.string_words)
    {
        for (const ir::Storage &storage : module.storages)
        {
            if (storage.kind == ir::StorageKind::Global &&
                storage.type.element_type == TYPE_STRING)
            {
                output << "    " << word(global_words[storage.id.index]) << " = INT32_C("
                       << empty_string_handle << ");\n";
            }
        }
        output << "    " << word(empty_string_handle) << " = INT32_C(0);\n";
        for (std::size_t literal_index = 0; literal_index < runtime.string_literals.size();
             literal_index++)
        {
            const std::string &literal = runtime.string_literals[literal_index];
            const std::size_t handle = literal_handles[literal_index];
            for (std::size_t byte = 0; byte < literal.size(); byte++)
            {
                output << "    " << word(handle + byte) << " = INT32_C("
                       << static_cast<unsigned int>(
                              static_cast<unsigned char>(literal[byte])) << ");\n";
            }
            output << "    " << word(handle + literal.size()) << " = INT32_C(0);\n";
        }
    }
    output << "    goto L_f0_b0;\n";

    std::size_t division_number = 0;
    std::size_t continuation_number = 0;
    std::vector<std::string> continuations;
    for (const ir::Function &function : module.functions)
    {
        if (function.kind == ir::FunctionKind::ExternalBuiltin ||
            !runtime.reachable_functions[function.id.index])
        {
            continue;
        }
        for (const ir::BasicBlock &block : function.blocks)
        {
            output << "L_f" << function.id.index << "_b" << block.id.index << ":\n";
            const bool procedure_function = function.kind == ir::FunctionKind::Procedure;
            for (const ir::Instruction &instruction : block.instructions)
            {
                if (const ir::Constant *constant = std::get_if<ir::Constant>(&instruction))
                {
                    const data_types type = function.values[constant->result.index].type.element_type;
                    const std::string literal = type == TYPE_INT ? int32_literal(std::get<int>(constant->payload)) :
                        (type == TYPE_FLOAT ? float_literal(std::get<float>(constant->payload)) :
                         (type == TYPE_STRING ?
                              "INT32_C(" + std::to_string(string_handle(
                                  std::get<std::string>(constant->payload))) + ")" :
                          (std::get<bool>(constant->payload) ? "INT32_C(1)" : "INT32_C(0)")));
                    if (procedure_function)
                    {
                        output << "    " << register_slot(layout.temporary_a) << " = " << literal << ";\n";
                        output << "    " << value_write(function, constant->result) << " = "
                               << register_slot(layout.temporary_a) << ";\n";
                    }
                    else
                    {
                        output << "    " << value_write(function, constant->result) << " = " << literal << ";\n";
                    }
                }
                else if (const ir::Load *load = std::get_if<ir::Load>(&instruction))
                {
                    if (procedure_function)
                    {
                        output << "    " << register_slot(layout.temporary_a) << " = "
                               << storage_read(function, load->source) << ";\n";
                        output << "    " << value_write(function, load->result) << " = "
                               << register_slot(layout.temporary_a) << ";\n";
                    }
                    else
                    {
                        output << "    " << value_write(function, load->result) << " = "
                               << storage_read(function, load->source) << ";\n";
                    }
                }
                else if (const ir::Store *store = std::get_if<ir::Store>(&instruction))
                {
                    if (procedure_function)
                    {
                        output << "    " << register_slot(layout.temporary_a) << " = "
                               << value_read(function, store->value) << ";\n";
                        output << "    " << storage_read(function, store->destination) << " = "
                               << register_slot(layout.temporary_a) << ";\n";
                    }
                    else
                    {
                        output << "    " << storage_read(function, store->destination) << " = "
                               << value_read(function, store->value) << ";\n";
                    }
                }
                else if (const ir::Unary *unary = std::get_if<ir::Unary>(&instruction))
                {
                    const std::string operand = value_read(function, unary->operand);
                    const data_types type = function.values[unary->operand.index].type.element_type;
                    const std::string result = procedure_function ? register_slot(layout.temporary_b) :
                                                                    value_write(function, unary->result);
                    if (procedure_function)
                    {
                        output << "    " << register_slot(layout.temporary_a) << " = " << operand << ";\n";
                    }
                    const std::string staged_operand = procedure_function ? register_slot(layout.temporary_a) : operand;
                    output << "    " << result << " = ";
                    if (unary->operation == ir::UnaryOp::Negate)
                    {
                        if (type == TYPE_FLOAT)
                        {
                            output << "R_f32_word(-R_word_f32(" << staged_operand << "))";
                        }
                        else
                        {
                            output << "I32_FROM_U32(UINT32_C(0) - (uint32_t)" << staged_operand << ")";
                        }
                    }
                    else if (type == TYPE_BOOL)
                    {
                        output << "(" << staged_operand << " == INT32_C(0)) ? INT32_C(1) : INT32_C(0)";
                    }
                    else
                    {
                        output << "I32_FROM_U32(~(uint32_t)" << staged_operand << ")";
                    }
                    output << ";\n";
                    if (procedure_function)
                    {
                        output << "    " << value_write(function, unary->result) << " = "
                               << register_slot(layout.temporary_b) << ";\n";
                    }
                }
                else if (const ir::Binary *binary = std::get_if<ir::Binary>(&instruction))
                {
                    const std::string left = procedure_function ? register_slot(layout.temporary_a) :
                                                                   value_read(function, binary->left);
                    const std::string right = procedure_function ? register_slot(layout.temporary_b) :
                                                                    value_read(function, binary->right);
                    const std::string result = procedure_function ? register_slot(layout.temporary_c) :
                                                                     value_write(function, binary->result);
                    const data_types type = function.values[binary->left.index].type.element_type;
                    if (procedure_function)
                    {
                        output << "    " << left << " = " << value_read(function, binary->left) << ";\n";
                        output << "    " << right << " = " << value_read(function, binary->right) << ";\n";
                    }
                    if (binary->operation == ir::BinaryOp::Divide && type == TYPE_INT)
                    {
                        const std::string prefix = "L_f" + std::to_string(function.id.index) + "_d" +
                                                   std::to_string(division_number++) + "_";
                        output << "    " << register_slot(layout.division_zero) << " = (" << right
                               << " == INT32_C(0)) ? INT32_C(1) : INT32_C(0);\n";
                        output << "    if (" << register_slot(layout.division_zero) << ") goto "
                               << prefix << "0;\n";
                        output << "    " << register_slot(layout.division_overflow) << " = (" << left
                               << " == INT32_MIN && " << right
                               << " == (-INT32_C(1))) ? INT32_C(1) : INT32_C(0);\n";
                        output << "    if (" << register_slot(layout.division_overflow) << ") goto "
                               << prefix << "1;\n";
                        output << "    " << result << " = "
                               << binary_expression_text(binary->operation, type, left, right) << ";\n";
                        output << "    goto " << prefix << "2;\n";
                        output << prefix << "0:\n    " << register_slot(layout.exit)
                               << " = INT32_C(1);\n    goto L_f0_x0;\n";
                        output << prefix << "1:\n    " << result << " = INT32_MIN;\n";
                        output << prefix << "2:\n";
                    }
                    else
                    {
                        output << "    " << result << " = "
                               << binary_expression_text(binary->operation, type, left, right) << ";\n";
                    }
                    if (procedure_function)
                    {
                        output << "    " << value_write(function, binary->result) << " = "
                               << register_slot(layout.temporary_c) << ";\n";
                    }
                }
                else if (const ir::Cast *cast = std::get_if<ir::Cast>(&instruction))
                {
                    const std::string operand = procedure_function ? register_slot(layout.temporary_a) :
                                                                      value_read(function, cast->operand);
                    const std::string result = procedure_function ? register_slot(layout.temporary_b) :
                                                                     value_write(function, cast->result);
                    if (procedure_function)
                    {
                        output << "    " << operand << " = " << value_read(function, cast->operand) << ";\n";
                    }
                    output << "    " << result << " = ";
                    if (cast->operation == ir::CastOp::IntToBool)
                    {
                        output << "(" << operand
                               << " != INT32_C(0)) ? INT32_C(1) : INT32_C(0)";
                    }
                    else if (cast->operation == ir::CastOp::IntToFloat)
                    {
                        output << "R_f32_word((float)" << operand << ")";
                    }
                    else if (cast->operation == ir::CastOp::FloatToInt)
                    {
                        output << "R_f32_i32(" << operand << ")";
                    }
                    else
                    {
                        output << operand;
                    }
                    output << ";\n";
                    if (procedure_function)
                    {
                        output << "    " << value_write(function, cast->result) << " = "
                               << register_slot(layout.temporary_b) << ";\n";
                    }
                }
                else if (const ir::Call *call = std::get_if<ir::Call>(&instruction))
                {
                    const ir::Function &callee = module.functions[call->callee.index];
                    if (callee.kind == ir::FunctionKind::ExternalBuiltin)
                    {
                        if (procedure_function)
                        {
                            std::string argument;
                            if (!call->arguments.empty())
                            {
                                argument = register_slot(layout.temporary_a);
                                output << "    " << argument << " = "
                                       << value_read(function, call->arguments[0]) << ";\n";
                            }
                            output << "    " << register_slot(layout.temporary_b) << " = "
                                   << external_call_expression(callee, argument) << ";\n";
                            output << "    " << value_write(function, call->result) << " = "
                                   << register_slot(layout.temporary_b) << ";\n";
                        }
                        else
                        {
                            const std::string argument = call->arguments.empty()
                                ? std::string()
                                : value_read(function, call->arguments[0]);
                            output << "    " << value_write(function, call->result) << " = "
                                   << external_call_expression(callee, argument) << ";\n";
                        }
                    }
                    else
                    {
                        const ProcedureFrame &callee_frame = frames[callee.id.index];
                        const std::size_t continuation = continuation_number++;
                        continuations.push_back("L_f" + std::to_string(function.id.index) + "_c" +
                                                std::to_string(continuation));
                        if (runtime.uses(BuiltinId::GetString))
                        {
                            output << "    " << register_slot(layout.temporary_a)
                                   << " = ((uint32_t)Reg[0u] > (uint32_t)"
                                   << register_slot(layout.string_heap) << " || "
                                   << callee_frame.words << "u > (uint32_t)"
                                   << register_slot(layout.string_heap)
                                   << " - (uint32_t)Reg[0u]) ? INT32_C(1) : INT32_C(0);\n";
                        }
                        else
                        {
                            output << "    " << register_slot(layout.temporary_a) << " = ((uint32_t)Reg[0u] > "
                                   << "MM_WORDS - " << callee_frame.words
                                   << "u) ? INT32_C(1) : INT32_C(0);\n";
                        }
                        output << "    if (" << register_slot(layout.temporary_a) << ") goto L_f0_s0;\n";
                        for (std::size_t argument = 0; argument < call->arguments.size(); argument++)
                        {
                            output << "    " << register_slot(layout.temporary_b) << " = "
                                   << value_read(function, call->arguments[argument]) << ";\n";
                            output << "    MM[(uint32_t)Reg[0u] + "
                                   << callee_frame.parameter_offsets[argument] << "u] = "
                                   << register_slot(layout.temporary_b) << ";\n";
                        }
                        output << "    MM[(uint32_t)Reg[0u]] = Reg[0u];\n";
                        output << "    MM[(uint32_t)Reg[0u] + 1u] = Reg[1u];\n";
                        output << "    MM[(uint32_t)Reg[0u] + 2u] = INT32_C(" << continuation << ");\n";
                        output << "    " << register_slot(layout.temporary_c) << " = "
                               << result_address(function, call->result) << ";\n";
                        output << "    MM[(uint32_t)Reg[0u] + 3u] = "
                               << register_slot(layout.temporary_c) << ";\n";
                        for (std::size_t offset = 4U + callee_frame.parameter_offsets.size();
                             offset < callee_frame.words; offset++)
                        {
                            output << "    MM[(uint32_t)Reg[0u] + " << offset << "u] = INT32_C(0);\n";
                        }
                        for (std::size_t local_index = 0; local_index < callee.locals.size();
                             local_index++)
                        {
                            const ir::Storage &local = module.storages[callee.locals[local_index].index];
                            if (local.type.element_type == TYPE_STRING)
                            {
                                output << "    MM[(uint32_t)Reg[0u] + "
                                       << callee_frame.local_offsets[local_index]
                                       << "u] = INT32_C(" << empty_string_handle << ");\n";
                            }
                        }
                        output << "    Reg[1u] = Reg[0u];\n";
                        output << "    Reg[0u] = I32_FROM_U32((uint32_t)Reg[0u] + "
                               << callee_frame.words << "u);\n";
                        output << "    goto L_f" << callee.id.index << "_b0;\n";
                        output << continuations.back() << ":\n";
                        if (function.kind == ir::FunctionKind::Program)
                        {
                            output << "    " << register_slot(layout.temporary_b) << " = "
                                   << word(program_spills[call->result.index])
                                   << ";\n";
                            output << "    " << value_write(function, call->result) << " = "
                                   << register_slot(layout.temporary_b) << ";\n";
                        }
                    }
                }
            }
            const ir::Terminator &terminator = std::get<ir::Terminator>(block.terminator);
            if (const ir::JumpTerminator *jump = std::get_if<ir::JumpTerminator>(&terminator))
            {
                output << "    goto L_f" << function.id.index << "_b" << jump->target.index << ";\n";
            }
            else if (const ir::BranchTerminator *branch = std::get_if<ir::BranchTerminator>(&terminator))
            {
                if (procedure_function)
                {
                    output << "    " << register_slot(layout.temporary_a) << " = "
                           << value_read(function, branch->condition) << ";\n";
                }
                output << "    if (" << (procedure_function ? register_slot(layout.temporary_a) :
                                                           value_read(function, branch->condition)) << ") goto L_f"
                       << function.id.index << "_b" << branch->when_true.index << ";\n";
                output << "    goto L_f" << function.id.index << "_b" << branch->when_false.index << ";\n";
            }
            else if (const ir::ReturnTerminator *returned = std::get_if<ir::ReturnTerminator>(&terminator))
            {
                output << "    " << register_slot(layout.temporary_a) << " = "
                       << value_read(function, returned->value) << ";\n";
                output << "    " << register_slot(layout.return_value) << " = "
                       << register_slot(layout.temporary_a) << ";\n";
                output << "    " << register_slot(layout.return_site) << " = " << frame_word(2U) << ";\n";
                output << "    " << register_slot(layout.temporary_c) << " = " << frame_word(3U) << ";\n";
                output << "    MM[(uint32_t)" << register_slot(layout.temporary_c) << "] = "
                       << register_slot(layout.return_value) << ";\n";
                output << "    " << register_slot(layout.temporary_a) << " = " << frame_word(0U) << ";\n";
                output << "    " << register_slot(layout.temporary_b) << " = " << frame_word(1U) << ";\n";
                output << "    Reg[0u] = " << register_slot(layout.temporary_a) << ";\n";
                output << "    Reg[1u] = " << register_slot(layout.temporary_b) << ";\n";
                output << "    goto L_f0_r0;\n";
            }
            else
            {
                output << "    goto L_f0_x0;\n";
            }
        }
    }
    if (runtime.procedures)
    {
        output << "L_f0_r0:\n";
        for (std::size_t continuation = 0; continuation < continuations.size(); continuation++)
        {
            output << "    " << register_slot(layout.temporary_a) << " = ("
                   << register_slot(layout.return_site) << " == INT32_C(" << continuation
                   << ")) ? INT32_C(1) : INT32_C(0);\n";
            output << "    if (" << register_slot(layout.temporary_a) << ") goto "
                   << continuations[continuation] << ";\n";
        }
        output << "    " << register_slot(layout.exit) << " = INT32_C(1);\n    goto L_f0_x0;\n";
        output << "L_f0_s0:\n    " << register_slot(layout.exit)
               << " = INT32_C(1);\n    goto L_f0_x0;\n";
    }
    output << "L_f0_x0:\n    return " << register_slot(layout.exit) << ";\n}\n";
    RestrictedCResult result;
    result.status = RestrictedCStatus::Success;
    result.text = output.str();
    if (runtime.uses(BuiltinId::Sqrt))
    {
        result.links.push_back(RestrictedCLink::Math);
    }
    return result;
}

} // namespace

RestrictedCResult RestrictedCEmitter::emit(const ir::Module &module) const
{
    const ir::Function *program = NULL;
    RuntimeRequirements runtime;
    RestrictedCResult checked = preflight(module, program, runtime);
    if (!checked.succeeded())
    {
        return checked;
    }
    if (runtime.float_words && !host_has_binary32_float())
    {
        return failure(RestrictedCStatus::Unsupported,
                       "restricted C Float lowering requires host IEEE binary32");
    }
    if (runtime.uses(BuiltinId::Sqrt))
    {
        checked.links.push_back(RestrictedCLink::Math);
    }

    if (runtime.procedure_mode)
    {
        return emit_with_procedures(module, *program, runtime);
    }

    RegisterLayout layout;
    if (!make_register_layout(*program, layout))
    {
        return failure(RestrictedCStatus::Unsupported,
                       "restricted C register model cannot represent this many values");
    }
    std::ostringstream output;
    output << "#include <stdint.h>\n";
    emit_runtime_headers(output, runtime);
    emit_float_guard(output, runtime);
    output << "\n";
    output << "#define MM_BYTES (" << RestrictedCEmitter::memory_byte_capacity() << "u)\n";
    output << "#define REGISTER_COUNT " << layout.count << "u\n\n";
    output << "#define I32_FROM_U32(value) ((value) <= UINT32_C(2147483647) ? "
           << "(int32_t)(value) : INT32_MIN + (int32_t)((uint32_t)(value) - "
           << "UINT32_C(2147483648)))\n\n";
    output << "int32_t MM[MM_BYTES / sizeof(int32_t)];\n";
    output << "int32_t Reg[REGISTER_COUNT];\n\n";
    emit_runtime_support(output, runtime);
    output << "int main(void)\n{\n";
    output << "    " << register_slot(layout.exit) << " = INT32_C(0);\n";
    output << "    goto L_f0_b0;\n";

    std::size_t division_number = 0;
    for (const ir::BasicBlock &block : program->blocks)
    {
        output << "L_f0_b" << block.id.index << ":\n";
        for (const ir::Instruction &instruction : block.instructions)
        {
        if (const ir::Constant *constant = std::get_if<ir::Constant>(&instruction))
        {
            const ir::Value &result = program->values[constant->result.index];
            output << "    " << value_register(constant->result) << " = ";
            if (result.type.element_type == TYPE_INT)
            {
                output << int32_literal(std::get<int>(constant->payload));
            }
            else if (result.type.element_type == TYPE_FLOAT)
            {
                output << float_literal(std::get<float>(constant->payload));
            }
            else
            {
                output << (std::get<bool>(constant->payload) ? "INT32_C(1)" : "INT32_C(0)");
            }
            output << ";\n";
        }
        else if (const ir::Load *load = std::get_if<ir::Load>(&instruction))
        {
            output << "    " << value_register(load->result) << " = "
                   << storage_word(load->source) << ";\n";
        }
        else if (const ir::Store *store = std::get_if<ir::Store>(&instruction))
        {
            output << "    " << storage_word(store->destination) << " = "
                   << value_register(store->value) << ";\n";
        }
        else if (const ir::Unary *unary = std::get_if<ir::Unary>(&instruction))
        {
            const data_types type = program->values[unary->operand.index].type.element_type;
            output << "    " << value_register(unary->result) << " = ";
            if (unary->operation == ir::UnaryOp::Negate)
            {
                if (type == TYPE_FLOAT)
                {
                    output << "R_f32_word(-R_word_f32(" << value_register(unary->operand) << "))";
                }
                else
                {
                    output << "I32_FROM_U32(UINT32_C(0) - (uint32_t)" << value_register(unary->operand)
                           << ")";
                }
            }
            else if (type == TYPE_BOOL)
            {
                output << "(" << value_register(unary->operand)
                       << " == INT32_C(0)) ? INT32_C(1) : INT32_C(0)";
            }
            else
            {
                output << "I32_FROM_U32(~(uint32_t)" << value_register(unary->operand) << ")";
            }
            output << ";\n";
        }
        else if (const ir::Binary *binary = std::get_if<ir::Binary>(&instruction))
        {
            const data_types type = program->values[binary->left.index].type.element_type;
            if (binary->operation == ir::BinaryOp::Divide && type == TYPE_INT)
            {
                const std::string label_prefix = "L_f0_d" + std::to_string(division_number) + "_";
                const std::string zero_label = label_prefix + "0";
                const std::string overflow_label = label_prefix + "1";
                const std::string done_label = label_prefix + "2";
                output << "    " << register_slot(layout.division_zero) << " = ("
                       << value_register(binary->right)
                       << " == INT32_C(0)) ? INT32_C(1) : INT32_C(0);\n";
                output << "    if (" << register_slot(layout.division_zero) << ") goto "
                       << zero_label << ";\n";
                output << "    " << register_slot(layout.division_overflow) << " = ("
                       << value_register(binary->left) << " == INT32_MIN && "
                       << value_register(binary->right)
                       << " == (-INT32_C(1))) ? INT32_C(1) : INT32_C(0);\n";
                output << "    if (" << register_slot(layout.division_overflow) << ") goto "
                       << overflow_label << ";\n";
                output << "    " << value_register(binary->result) << " = "
                       << binary_expression(*binary, type) << ";\n";
                output << "    goto " << done_label << ";\n";
                output << zero_label << ":\n";
                output << "    " << register_slot(layout.exit) << " = INT32_C(1);\n";
                output << "    goto L_f0_x0;\n";
                output << overflow_label << ":\n";
                output << "    " << value_register(binary->result) << " = INT32_MIN;\n";
                output << done_label << ":\n";
                division_number++;
            }
            else
            {
                output << "    " << value_register(binary->result) << " = "
                       << binary_expression(*binary, type) << ";\n";
            }
        }
        else if (const ir::Cast *cast = std::get_if<ir::Cast>(&instruction))
        {
            output << "    " << value_register(cast->result) << " = ";
            if (cast->operation == ir::CastOp::IntToBool)
            {
                output << "(" << value_register(cast->operand)
                       << " != INT32_C(0)) ? INT32_C(1) : INT32_C(0)";
            }
            else if (cast->operation == ir::CastOp::IntToFloat)
            {
                output << "R_f32_word((float)" << value_register(cast->operand) << ")";
            }
            else if (cast->operation == ir::CastOp::FloatToInt)
            {
                output << "R_f32_i32(" << value_register(cast->operand) << ")";
            }
            else
            {
                output << value_register(cast->operand);
            }
            output << ";\n";
        }
        else if (const ir::Call *call = std::get_if<ir::Call>(&instruction))
        {
            const ir::Function &callee = module.functions[call->callee.index];
            const std::string argument = call->arguments.empty()
                ? std::string()
                : value_register(call->arguments[0]);
            output << "    " << value_register(call->result) << " = "
                   << external_call_expression(callee, argument) << ";\n";
        }
        }
        const ir::Terminator &terminator = std::get<ir::Terminator>(block.terminator);
        if (const ir::JumpTerminator *jump = std::get_if<ir::JumpTerminator>(&terminator))
        {
            output << "    goto L_f0_b" << jump->target.index << ";\n";
        }
        else if (const ir::BranchTerminator *branch =
                     std::get_if<ir::BranchTerminator>(&terminator))
        {
            output << "    if (" << value_register(branch->condition) << ") goto L_f0_b"
                   << branch->when_true.index << ";\n";
            output << "    goto L_f0_b" << branch->when_false.index << ";\n";
        }
        else if (std::holds_alternative<ir::HaltTerminator>(terminator))
        {
            output << "    goto L_f0_x0;\n";
        }
    }
    output << "L_f0_x0:\n";
    output << "    return " << register_slot(layout.exit) << ";\n}\n";
    checked.text = output.str();
    return checked;
}

RestrictedCResult RestrictedCEmitter::emit_to_file(const ir::Module &module,
                                                    const std::filesystem::path &output) const
{
    RestrictedCResult rendered = emit(module);
    if (!rendered.succeeded())
    {
        return rendered;
    }
    if (output.empty())
    {
        return failure(RestrictedCStatus::IoError, "output path is empty");
    }

    std::error_code error;
    const std::filesystem::path parent = output.parent_path().empty() ?
                                               std::filesystem::path(".") : output.parent_path();
    if (!std::filesystem::exists(parent, error) || error ||
        !std::filesystem::is_directory(parent, error) || error)
    {
        return failure(RestrictedCStatus::IoError, "output parent directory is unavailable");
    }
    if (!is_direct_regular_or_missing(output, error))
    {
        return failure(RestrictedCStatus::IoError, error ?
            "cannot inspect output path" : "output path is not a direct regular file");
    }

    const std::filesystem::path temporary_directory = sibling_temp_directory(output, error);
    if (error || temporary_directory.empty())
    {
        return failure(RestrictedCStatus::IoError, "cannot reserve a sibling temporary output");
    }
    std::filesystem::permissions(temporary_directory, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::replace, error);
    if (error)
    {
        cleanup_temp_directory(temporary_directory);
        return failure(RestrictedCStatus::IoError, "cannot restrict temporary output directory");
    }
    const std::filesystem::path temporary = temporary_directory / "output.c";
    const std::filesystem::file_status temporary_status =
        std::filesystem::symlink_status(temporary, error);
    if (error == std::errc::no_such_file_or_directory)
    {
        error.clear();
    }
    if (error || temporary_status.type() != std::filesystem::file_type::not_found)
    {
        cleanup_temp_directory(temporary_directory);
        return failure(RestrictedCStatus::IoError, "temporary output file is not safely empty");
    }
    {
        std::ofstream stream(temporary, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!stream.is_open())
        {
            cleanup_temp_directory(temporary_directory);
            return failure(RestrictedCStatus::IoError, "cannot open temporary output");
        }
        stream << rendered.text;
        stream.close();
        if (!stream)
        {
            cleanup_temp_directory(temporary_directory);
            return failure(RestrictedCStatus::IoError, "cannot write temporary output");
        }
    }
    const std::filesystem::file_status written_status =
        std::filesystem::symlink_status(temporary, error);
    if (error || written_status.type() != std::filesystem::file_type::regular)
    {
        cleanup_temp_directory(temporary_directory);
        return failure(RestrictedCStatus::IoError, "temporary output file is not a regular file");
    }
    if (!is_direct_regular_or_missing(output, error))
    {
        cleanup_temp_directory(temporary_directory);
        return failure(RestrictedCStatus::IoError, error ?
            "cannot inspect output path" : "output path is not a direct regular file");
    }
    std::filesystem::rename(temporary, output, error);
    if (error)
    {
        cleanup_temp_directory(temporary_directory);
        return failure(RestrictedCStatus::IoError, "cannot replace output atomically");
    }
    std::error_code cleanup_error;
    std::filesystem::remove(temporary_directory, cleanup_error);
    return rendered;
}
