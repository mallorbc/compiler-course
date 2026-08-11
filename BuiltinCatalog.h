#ifndef BUILTIN_CATALOG_H
#define BUILTIN_CATALOG_H

#include "SemanticTypes.h"

#include <array>
#include <string>
#include <vector>

enum class BuiltinId
{
    GetBool,
    GetInteger,
    GetFloat,
    GetString,
    PutBool,
    PutInteger,
    PutFloat,
    PutString,
    Sqrt
};

struct BuiltinSpec
{
    BuiltinId id;
    const char *spelling;
    value_shape return_shape;
    std::vector<value_shape> parameter_shapes;
};

const std::array<BuiltinSpec, 9> &builtin_catalog();
const BuiltinSpec *find_builtin(BuiltinId id);
const BuiltinSpec *find_builtin(const std::string &spelling);

#endif // BUILTIN_CATALOG_H
