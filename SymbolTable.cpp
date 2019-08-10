#include "SymbolTable.h"

SymbolTable::SymbolTable()
{
    init_reserved_words();
    init_reserved_chars();
}

bool SymbolTable::init_reserved_words()
{
    //Inserts keywords into the symbol table with the string value and the key
    insert_stringValue("program", T_PROGRAM);
    insert_stringValue("is", T_IS);
    insert_stringValue("begin", T_BEGIN);
    insert_stringValue("end", T_END);
    insert_stringValue("global", T_GLOBAL);
    insert_stringValue("procedure", T_PROCEDURE);
    insert_stringValue("variable", T_VARIABLE);
    insert_stringValue("type", T_TYPE);
    insert_stringValue("integer", T_INTEGER_TYPE);
    insert_stringValue("float", T_FLOAT_TYPE);
    insert_stringValue("string", T_STRING_TYPE);
    insert_stringValue("bool", T_BOOL_TYPE);
    insert_stringValue("enum", T_ENUM);
    insert_stringValue("if", T_IF);
    insert_stringValue("then", T_THEN);
    insert_stringValue("else", T_ELSE);
    insert_stringValue("for", T_FOR);
    insert_stringValue("return", T_RETURN);
    insert_stringValue("not", T_NOT);
    insert_stringValue("true", T_TRUE);
    insert_stringValue("false", T_FALSE);

    return 1;
}

bool SymbolTable::insert_stringValue(std::string stringValue, token_type type_of_token)
{
    //stores
    token *new_token;
    new_token = new token;
    new_token->type = type_of_token;
    new_token->stringValue = stringValue;
    insert_string_token(*new_token);

    return 1;
}

bool SymbolTable::insert_string_token(token new_token)
{
    std::string key_value = new_token.stringValue;
    map[key_value] = new_token;

    return 1;
}

bool SymbolTable::is_in_table(std::string test_string)
{
    if (map.find(test_string) == map.end())
    {
        return 0;
    }
    else
    {
        return 1;
    }
}

bool SymbolTable::init_reserved_chars()
{
    insert_char_table('(', T_LPARAM);
    insert_char_table(')', T_RPARAM);
    insert_char_table('[', T_LBRACKET);
    insert_char_table(']', T_RBRACKET);
    insert_char_table(',', T_COMMA);
    insert_char_table('/', T_SLASH);
    insert_char_table('{', T_LBRACE);
    insert_char_table('}', T_RBRACE);
    insert_char_table('=', T_ASSIGN);
    insert_char_table('+', T_PLUS);
    insert_char_table('_', T_UNDERSCORE);
    insert_char_table('.', T_PERIOD);
    insert_char_table('>', T_GREATER);
    insert_char_table('<', T_LESS);
    insert_char_table('*', T_MULT);
    insert_char_table('"', T_QUOTE);
    insert_char_table('!', T_EXCLAM);
    insert_char_table(';', T_SEMICOLON);
    insert_char_table(':', T_COLON);
    insert_char_table('|', T_VERTICAL_BAR);
    insert_char_table('&', T_AMPERSAND);
    insert_char_table('-', T_MINUS);

    return 1;
}

bool SymbolTable::insert_char_table(char reserved_char, token_type type_of_token)
{
    reserved_chars[reserved_char] = type_of_token;
    return 1;
}

bool SymbolTable::is_reserved_char(char test_char)
{
    if (reserved_chars.find(test_char) == reserved_chars.end())
    {
        return 0;
    }
    else
        return 1;
}

bool SymbolTable::make_token_global(token global_token)
{
    bool return_value;
    //used as the temporary hold value
    token temp_token;
    //finds the token value using the string key
    temp_token = map[global_token.stringValue];
    //changes the value to global
    temp_token.global_scope = true;
    //puts the modified value back in the map
    map[temp_token.stringValue] = temp_token;
    return true;
}

bool SymbolTable::is_global_token(token token_to_check)
{
    bool is_global;
    is_global = token_to_check.global_scope;
    return is_global;
}