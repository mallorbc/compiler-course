#ifndef SEMANTIC_TYPES_H
#define SEMANTIC_TYPES_H

#include <cstddef>
#include <functional>
#include <string>

//These are deliberately independent of scanner/parser tokens.  They are the
//small semantic vocabulary shared by declarations, type checking, and IR.
enum data_types
{
    TYPE_NONE = 0,
    TYPE_INT = 1,
    TYPE_FLOAT = 2,
    TYPE_STRING = 3,
    TYPE_BOOL = 4
};

struct value_shape
{
    //Array bounds are inclusive: [N] denotes legal source indices 0..N.
    data_types element_type = TYPE_NONE;
    bool is_array = false;
    int array_upper_bound = -1;
};

inline bool operator==(const value_shape &left, const value_shape &right)
{
    return left.element_type == right.element_type &&
           left.is_array == right.is_array &&
           left.array_upper_bound == right.array_upper_bound;
}

inline bool operator!=(const value_shape &left, const value_shape &right)
{
    return !(left == right);
}

//A declaration is owned by exactly one retained lexical scope.  It is a
//stable canonical key, never a pointer into an unordered map.
struct SymbolRef
{
    int scope_id = 0;
    std::string name;
};

inline bool operator==(const SymbolRef &left, const SymbolRef &right)
{
    return left.scope_id == right.scope_id && left.name == right.name;
}

inline bool operator!=(const SymbolRef &left, const SymbolRef &right)
{
    return !(left == right);
}

struct SymbolRefHash
{
    std::size_t operator()(const SymbolRef &reference) const noexcept
    {
        const std::size_t name_hash = std::hash<std::string>{}(reference.name);
        const std::size_t scope_hash = std::hash<int>{}(reference.scope_id);
        return name_hash ^ (scope_hash + 0x9e3779b9U + (name_hash << 6U) +
                            (name_hash >> 2U));
    }
};

#endif // SEMANTIC_TYPES_H
