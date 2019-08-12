#include "ScopeTable.h"

ScopeTable::ScopeTable()
{
}
ScopeTable::ScopeTable(int scope_id)
{
    table_scope_id = scope_id;
}

bool ScopeTable::insert_stringValue(std::string stringValue, token_type type_of_token)
{
    //stores
    token *new_token;
    new_token = new token;
    new_token->type = type_of_token;
    new_token->stringValue = stringValue;
    insert_string_token(*new_token);

    return 1;
}

bool ScopeTable::insert_string_token(token new_token)
{
    //adds only identifers as we only car about them
    if (new_token.type == T_IDENTIFIER)
    {
        std::string key_value = new_token.stringValue;
        scope_map[key_value] = new_token;
    }

    return 1;
}

bool ScopeTable::is_in_table(std::string test_string)
{
    if (scope_map.find(test_string) == scope_map.end())
    {
        return 0;
    }
    else
    {
        return 1;
    }
}
