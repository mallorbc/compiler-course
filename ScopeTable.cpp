#include "ScopeTable.h"

ScopeTable::ScopeTable()
{
}

ScopeTable::ScopeTable(int scope_id, int parent_id, bool has_parent_scope)
    : table_scope_id(scope_id), parent_scope_id(parent_id), has_parent(has_parent_scope)
{
}

bool ScopeTable::declare_token(const token &new_token)
{
    return scope_map.emplace(new_token.stringValue, new_token).second;
}

bool ScopeTable::is_in_table(const std::string &test_string) const
{
    return scope_map.find(test_string) != scope_map.end();
}

const token *ScopeTable::find_token(const std::string &name) const
{
    std::unordered_map<std::string, token>::const_iterator found = scope_map.find(name);
    return found == scope_map.end() ? NULL : &found->second;
}

token *ScopeTable::find_token_mut(const std::string &name)
{
    std::unordered_map<std::string, token>::iterator found = scope_map.find(name);
    return found == scope_map.end() ? NULL : &found->second;
}
