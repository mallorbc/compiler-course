#ifndef TOKEN_H
#define TOKEN_H
#include "SemanticTypes.h"
#include <string>
#include <vector>

//we need to return both a token and a parse status; this struct will help with that

enum token_type
{
    //should ! be its own token?
    T_SEMICOLON = ';',
    T_LPARAM = '(',
    T_RPARAM = ')',
    T_ASSIGN = '=',
    T_SLASH = '/',
    T_LBRACE = '{',
    T_RBRACE = '}',
    T_LBRACKET = '[',
    T_RBRACKET = ']',
    T_COMMA = ',',
    T_PLUS = '+',
    T_MINUS = '-',
    T_UNDERSCORE = '_',
    T_PERIOD = '.',
    T_GREATER = '>',
    T_LESS = '<',
    T_MULT = '*',
    T_QUOTE = '"',
    T_EXCLAM = '!',
    T_COLON = ':',
    T_VERTICAL_BAR = '|',
    T_AMPERSAND = '&',

    //finish adding reserved words
    T_FOR = 257,
    T_IF = 258,
    T_RETURN = 259,
    T_PROGRAM = 260,
    T_IS = 261,
    T_BEGIN = 262,
    T_END = 263,
    T_GLOBAL = 264,
    T_PROCEDURE = 265,
    T_VARIABLE = 266,
    T_TYPE = 267,
    T_INTEGER_TYPE = 268,
    T_FLOAT_TYPE = 269,
    T_STRING_TYPE = 270,
    T_BOOL_TYPE = 271,
    T_ENUM = 272,
    T_THEN = 273,
    T_ELSE = 274,
    T_NOT = 275,
    T_TRUE = 276,
    T_FALSE = 277,
    T_IDENTIFIER = 278,
    T_INTEGER_VALUE = 279,
    T_FLOAT_VALUE = 280,
    T_STRING_VALUE = 281,
    T_BOOL_VALUE = 282,
    T_INVALID = 283,
    T_NULL = 999

};

enum identifier_types
{
    I_NONE = 0,
    I_PROCEDURE = 1,
    I_VARIABLE = 2,
    I_TYPE = 3,
    I_PROGRAM_NAME = 4
};

class token
{
public:
    // token(){
    //     this->
    // };

    //cleans up token object
    ~token()
    {
        this->type = 0;
        this->line_found = 0;
        this->column_found = 0;
        this->global_scope = false;
        this->scope_id = 0;

        this->intValue = 0;
        this->stringValue = "";
        this->floatValue = 0;
        this->charValue = '\0';
        this->boolValue = false;
        this->first_token_on_line = false;
        this->array_upper_bound = -1;
    };
    int type = 0;
    int line_found = 0;
    int column_found = 0;
    bool global_scope = false;
    //scope id of 0 is the outermost scope, the other scopes will increment by 1
    int scope_id = 0;
    int intValue = 0;
    std::string stringValue = "";
    bool boolValue = false;
    float floatValue = 0.0;
    char charValue = 0;

    bool first_token_on_line = false;

    //an identifer can be either associated with procedure(1), variable(2), type(3), or program name(4)
    identifier_types identifer_type = I_NONE;
    //Procedure signatures use full value shapes.  Parameter names remain in
    //the procedure body scope; a call only needs ordered type/array/bound
    //information.
    std::vector<value_shape> procedure_params;
    //a variable can be of type string, bool, int, float, or none
    data_types identifier_data_type = TYPE_NONE;

    bool is_array = false;
    //Inclusive upper bound for a declared or synthesized array; -1 for a
    //scalar or an otherwise unresolved shape.
    int array_upper_bound = -1;
    // union value{
    //     int intValue;
    //     std::string stringValue;
    //     bool boolValue;
    //     float floatValue;
    //     char charValue;
    // };
};

inline value_shape shape_of(const token &value)
{
    value_shape shape;
    shape.element_type = value.identifier_data_type;
    shape.is_array = value.is_array;
    shape.array_upper_bound = value.is_array ? value.array_upper_bound : -1;
    return shape;
}

inline void apply_shape(token &value, const value_shape &shape)
{
    value.identifier_data_type = shape.element_type;
    value.is_array = shape.is_array;
    value.array_upper_bound = shape.is_array ? shape.array_upper_bound : -1;
}

inline bool same_shape(const value_shape &left, const value_shape &right)
{
    return left == right;
}

inline std::string shape_name(const value_shape &shape)
{
    std::string element_name = "unknown";
    switch (shape.element_type)
    {
    case TYPE_INT:
        element_name = "integer";
        break;
    case TYPE_FLOAT:
        element_name = "float";
        break;
    case TYPE_STRING:
        element_name = "string";
        break;
    case TYPE_BOOL:
        element_name = "bool";
        break;
    case TYPE_NONE:
        break;
    }
    if (!shape.is_array)
    {
        return element_name;
    }
    return element_name + "[" + std::to_string(shape.array_upper_bound) + "]";
}

struct token_and_status
{
    bool valid_parse = true;
    //Parsing and semantic analysis deliberately have separate outcomes.  A
    //well-formed expression can be semantically invalid, and callers must
    //still consume the rest of its grammar production for recovery.
    bool semantic_valid = false;
    token resolved_token;
};
#endif // !TOKEN_H
