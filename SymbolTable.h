#ifndef SYMBOLTABLE_H
#define SYMBOLTABLE_H

#include "ScopeTable.h"

#include <string>
#include <unordered_map>
#include <vector>

class SymbolTable
{
public:
    //This remains the scanner's lexical interning cache for compatibility with
    //the original scanner tests.  It is intentionally not declaration state.
    std::unordered_map<std::string, token> map;
    std::unordered_map<char, int> reserved_chars;

    //Retained, stable scope graph.  Scope 0 is the canonical global scope.
    std::unordered_map<int, ScopeTable> scope_table;

    SymbolTable();

    bool insert_stringValue(const std::string &stringValue, token_type type_of_token);
    bool insert_string_token(const token &new_token);
    bool is_in_table(const std::string &test_string) const;
    bool lookup_lexeme(const std::string &lexeme, token &out) const;
    bool insert_char_table(char reserved_char, token_type type_of_token);
    bool is_reserved_char(char test_char) const;

    bool create_scope(int scope_id, int parent_scope_id, bool has_parent);
    bool has_scope(int scope_id) const;
    bool set_scope_owner(int scope_id, const SymbolRef &owner);
    bool lookup_scope_owner(int scope_id, token &out) const;

    bool declare_symbol(int scope_id, const token &new_token);
    bool can_declare_all(int scope_id, const std::vector<token> &symbols) const;
    bool declare_all(int scope_id, const std::vector<token> &symbols);
    bool lookup_declared(const SymbolRef &reference, token &out) const;
    bool replace_declared(const SymbolRef &reference, const token &replacement);
    bool append_procedure_parameter(const SymbolRef &reference, const value_shape &parameter_type);
    bool has_declared(int scope_id, const std::string &name) const;

    //Resolution deliberately does not walk parent scopes.  2024 course rules
    //guarantee current-local shadowing, self recursion, and source-ordered
    //globals; enclosing-procedure capture remains an explicit later decision.
    bool resolve_name(const std::string &name, int current_scope_id, token &out) const;
    bool resolve_procedure(const std::string &name, int current_scope_id, token &out) const;

private:
    bool init_reserved_words();
    bool init_reserved_chars();
    const ScopeTable *find_scope(int scope_id) const;
    ScopeTable *find_scope_mut(int scope_id);
};

#endif // SYMBOLTABLE_H
