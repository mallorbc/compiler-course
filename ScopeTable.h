#ifndef SCOPETABLE_H
#define SCOPETABLE_H

#include "token.h"
#include <unordered_map>
#include <string>

class ScopeTable
{
public:
    int table_scope_id;
    std::unordered_map<std::string, token> scope_map;

    ScopeTable(int scope_id);
    ScopeTable();
    bool insert_stringValue(std::string stringValue, token_type type_of_token);
    bool insert_string_token(token new_token);
    bool is_in_table(std::string test_string);
};

#endif // !SCOPETALBE_H