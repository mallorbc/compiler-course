#ifndef SCOPETABLE_H
#define SCOPETABLE_H

#include "SemanticTypes.h"
#include "token.h"

#include <string>
#include <unordered_map>

class ScopeTable
{
public:
    int table_scope_id = 0;
    int parent_scope_id = -1;
    bool has_parent = false;
    bool has_owner_procedure = false;
    SymbolRef owner_procedure;

    //Kept public for the course project's existing inspection-oriented unit
    //tests.  Production lookups go through find_* below and never use [].
    std::unordered_map<std::string, token> scope_map;

    ScopeTable();
    ScopeTable(int scope_id, int parent_id, bool has_parent_scope);

    bool declare_token(const token &new_token);
    bool is_in_table(const std::string &test_string) const;
    const token *find_token(const std::string &name) const;
    token *find_token_mut(const std::string &name);
};

#endif // SCOPETABLE_H
