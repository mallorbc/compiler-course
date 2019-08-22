#ifndef TOKEN_H
#define TOKEN_H
#include <string>
#include <vector>

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
    T_INVALID = 283

};

enum identifier_types
{
    I_NONE = 0,
    I_PROCEDURE = 1,
    I_VARIABLE = 2,
    I_TYPE = 3,
    I_PROGRAM_NAME = 4
};

enum data_types
{
    TYPE_NONE = 0,
    TYPE_INT = 1,
    TYPE_FLOAT = 2,
    TYPE_STRING = 3,
    TYPE_BOOL = 4
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
    //this will need to be added to the procedure identifiers
    std::vector<data_types> procedure_params;
    //a variable can be of type string, bool, int, float, or none
    data_types identifier_data_type;
    // union value{
    //     int intValue;
    //     std::string stringValue;
    //     bool boolValue;
    //     float floatValue;
    //     char charValue;
    // };
};
#endif // !TOKEN_H