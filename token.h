#ifndef TOKEN_H
#define TOKEN_H
#include<string>

enum  token_type
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
 T_BOOL_VALUE = 282

};








class token{
    public:
    //token(){};
    
    //cleans up token object
    ~token(){
        this->type = 0;
        this->line_found = 0;
        this->column_found = 0;
        this->global_scope = false;

        this->intValue = 0;
        this->stringValue = "";
        this->floatValue = 0;
        this->charValue = '\0';
        this->boolValue = false;


    };
    int type;
    int line_found;
    int column_found;
    bool global_scope;

    int intValue;
    std::string stringValue;
    bool boolValue;
    float floatValue;
    char charValue; 
    // union value{
    //     int intValue;
    //     std::string stringValue;
    //     bool boolValue;
    //     float floatValue;
    //     char charValue;  
    // };



};
#endif // !TOKEN_H