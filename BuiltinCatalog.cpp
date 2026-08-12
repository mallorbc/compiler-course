#include "BuiltinCatalog.h"

namespace
{

value_shape scalar(data_types type)
{
    value_shape shape;
    shape.element_type = type;
    return shape;
}

} // namespace

const std::array<BuiltinSpec, 9> &builtin_catalog()
{
    static const std::array<BuiltinSpec, 9> catalog = {{
        {BuiltinId::GetBool, "getbool", scalar(TYPE_BOOL), {}},
        {BuiltinId::GetInteger, "getinteger", scalar(TYPE_INT), {}},
        {BuiltinId::GetFloat, "getfloat", scalar(TYPE_FLOAT), {}},
        {BuiltinId::GetString, "getstring", scalar(TYPE_STRING), {}},
        {BuiltinId::PutBool, "putbool", scalar(TYPE_BOOL), {scalar(TYPE_BOOL)}},
        {BuiltinId::PutInteger, "putinteger", scalar(TYPE_BOOL), {scalar(TYPE_INT)}},
        {BuiltinId::PutFloat, "putfloat", scalar(TYPE_BOOL), {scalar(TYPE_FLOAT)}},
        {BuiltinId::PutString, "putstring", scalar(TYPE_BOOL), {scalar(TYPE_STRING)}},
        {BuiltinId::Sqrt, "sqrt", scalar(TYPE_FLOAT), {scalar(TYPE_INT)}}
    }};
    return catalog;
}

const BuiltinSpec *find_builtin(BuiltinId id)
{
    const std::array<BuiltinSpec, 9> &catalog = builtin_catalog();
    for (const BuiltinSpec &spec : catalog)
    {
        if (spec.id == id)
        {
            return &spec;
        }
    }
    return NULL;
}

const BuiltinSpec *find_builtin(const std::string &spelling)
{
    const std::array<BuiltinSpec, 9> &catalog = builtin_catalog();
    for (const BuiltinSpec &spec : catalog)
    {
        if (spelling == spec.spelling)
        {
            return &spec;
        }
    }
    return NULL;
}
