#ifndef IR_H
#define IR_H

#include "SemanticTypes.h"

#include <cstdint>
#include <limits>
#include <string>
#include <variant>
#include <vector>

//IR is intentionally frontend-independent: no scanner/parser/token/typechecker
//headers may be included here.  IDs encode their ownership domain and default
//to an invalid sentinel so unrelated domains cannot accidentally interconvert.
namespace ir
{

constexpr std::uint32_t invalid_index = std::numeric_limits<std::uint32_t>::max();

struct FunctionId
{
    std::uint32_t index = invalid_index;
    constexpr FunctionId() = default;
    explicit constexpr FunctionId(std::uint32_t value) : index(value) {}
    constexpr bool valid() const noexcept { return index != invalid_index; }
};

struct StorageId
{
    std::uint32_t index = invalid_index;
    constexpr StorageId() = default;
    explicit constexpr StorageId(std::uint32_t value) : index(value) {}
    constexpr bool valid() const noexcept { return index != invalid_index; }
};

struct BlockId
{
    FunctionId function;
    std::uint32_t index = invalid_index;
    constexpr BlockId() = default;
    constexpr BlockId(FunctionId owner, std::uint32_t value) : function(owner), index(value) {}
    constexpr bool valid() const noexcept { return function.valid() && index != invalid_index; }
};

struct ValueId
{
    FunctionId function;
    std::uint32_t index = invalid_index;
    constexpr ValueId() = default;
    constexpr ValueId(FunctionId owner, std::uint32_t value) : function(owner), index(value) {}
    constexpr bool valid() const noexcept { return function.valid() && index != invalid_index; }
};

inline constexpr bool operator==(FunctionId left, FunctionId right)
{
    return left.index == right.index;
}
inline constexpr bool operator!=(FunctionId left, FunctionId right) { return !(left == right); }
inline constexpr bool operator==(StorageId left, StorageId right)
{
    return left.index == right.index;
}
inline constexpr bool operator!=(StorageId left, StorageId right) { return !(left == right); }
inline constexpr bool operator==(BlockId left, BlockId right)
{
    return left.function == right.function && left.index == right.index;
}
inline constexpr bool operator!=(BlockId left, BlockId right) { return !(left == right); }
inline constexpr bool operator==(ValueId left, ValueId right)
{
    return left.function == right.function && left.index == right.index;
}
inline constexpr bool operator!=(ValueId left, ValueId right) { return !(left == right); }

enum class ModuleStatus
{
    Unfinalized,
    Ready,
    FrontendError,
    InvalidIR,
    Unsupported
};

enum class FunctionKind
{
    Program,
    Procedure,
    ExternalBuiltin
};

enum class StorageKind
{
    Global,
    Parameter,
    Local
};

enum class ValueLocation
{
    Constant,
    Load,
    Unary,
    Binary,
    Cast,
    Call
};

enum class UnaryOp
{
    Negate,
    Not
};

enum class BinaryOp
{
    Add,
    Subtract,
    Multiply,
    Divide,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Equal,
    NotEqual,
    And,
    Or
};

enum class CastOp
{
    IntToFloat,
    FloatToInt,
    BoolToInt,
    IntToBool
};

struct Value
{
    ValueId id;
    value_shape type;
    ValueLocation location = ValueLocation::Constant;
};

struct Storage
{
    StorageId id;
    SymbolRef symbol;
    value_shape type;
    StorageKind kind = StorageKind::Local;
    FunctionId owner;
};

struct Constant
{
    ValueId result;
    //A String payload contains semantic bytes only; source delimiters are a
    //frontend concern and are never retained in finalized IR.
    std::variant<int, float, bool, std::string> payload;
};

struct Load
{
    ValueId result;
    StorageId source;
};

struct Store
{
    StorageId destination;
    ValueId value;
};

struct Unary
{
    ValueId result;
    UnaryOp operation = UnaryOp::Negate;
    ValueId operand;
};

struct Binary
{
    ValueId result;
    BinaryOp operation = BinaryOp::Add;
    ValueId left;
    ValueId right;
};

struct Cast
{
    ValueId result;
    CastOp operation = CastOp::IntToFloat;
    ValueId operand;
};

struct Call
{
    ValueId result;
    FunctionId callee;
    std::vector<ValueId> arguments;
};

using Instruction = std::variant<Constant, Load, Store, Unary, Binary, Cast, Call>;

struct ReturnTerminator
{
    ValueId value;
};

struct HaltTerminator
{
};

struct JumpTerminator
{
    BlockId target;
};

struct BranchTerminator
{
    ValueId condition;
    BlockId when_true;
    BlockId when_false;
};

using Terminator = std::variant<ReturnTerminator, HaltTerminator, JumpTerminator,
                                BranchTerminator>;

struct BasicBlock
{
    BlockId id;
    std::vector<Instruction> instructions;
    std::variant<std::monostate, Terminator> terminator;
};

struct Function
{
    FunctionId id;
    FunctionKind kind = FunctionKind::Procedure;
    std::string name;
    SymbolRef symbol;
    //Program has no result and retains the default TYPE_NONE scalar shape.
    //Procedures and external builtins must use a fully resolved scalar shape.
    value_shape return_type;
    std::vector<value_shape> parameter_types;
    std::vector<StorageId> parameters;
    std::vector<StorageId> locals;
    std::vector<Value> values;
    std::vector<BasicBlock> blocks;
};

struct Module
{
    std::vector<Function> functions;
    std::vector<Storage> storages;
};

struct VerificationResult
{
    bool valid = false;
    std::string reason;
};

bool is_ready_type(const value_shape &shape);
VerificationResult verify_module(const Module &module);

} // namespace ir

#endif // IR_H
