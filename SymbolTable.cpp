#include "SymbolTable.h"

#include <unordered_set>

namespace
{

token builtin_procedure(const std::string &name, data_types return_type,
                        const std::vector<data_types> &parameters)
{
    token builtin;
    builtin.type = T_IDENTIFIER;
    builtin.stringValue = name;
    builtin.global_scope = true;
    builtin.scope_id = 0;
    builtin.identifer_type = I_PROCEDURE;
    builtin.identifier_data_type = return_type;
    builtin.procedure_params = parameters;
    return builtin;
}

} // namespace

SymbolTable::SymbolTable()
{
    init_reserved_words();
    init_reserved_chars();
    create_scope(0, -1, false);

    //The parser defers argument checking, but calls must still resolve in the
    //same declaration model as source procedures.
    const std::vector<token> builtins = {
        builtin_procedure("getbool", TYPE_BOOL, {}),
        builtin_procedure("getinteger", TYPE_INT, {}),
        builtin_procedure("getfloat", TYPE_FLOAT, {}),
        builtin_procedure("getstring", TYPE_STRING, {}),
        builtin_procedure("putbool", TYPE_BOOL, {TYPE_BOOL}),
        builtin_procedure("putinteger", TYPE_BOOL, {TYPE_INT}),
        builtin_procedure("putfloat", TYPE_BOOL, {TYPE_FLOAT}),
        builtin_procedure("putstring", TYPE_BOOL, {TYPE_STRING}),
        builtin_procedure("sqrt", TYPE_FLOAT, {TYPE_INT})};
    declare_all(0, builtins);
}

bool SymbolTable::init_reserved_words()
{
    const std::pair<const char *, token_type> words[] = {
        {"program", T_PROGRAM}, {"is", T_IS},       {"begin", T_BEGIN},
        {"end", T_END},         {"global", T_GLOBAL}, {"procedure", T_PROCEDURE},
        {"variable", T_VARIABLE}, {"type", T_TYPE}, {"integer", T_INTEGER_TYPE},
        {"float", T_FLOAT_TYPE}, {"string", T_STRING_TYPE}, {"bool", T_BOOL_TYPE},
        {"enum", T_ENUM},       {"if", T_IF},       {"then", T_THEN},
        {"else", T_ELSE},       {"for", T_FOR},     {"return", T_RETURN},
        {"not", T_NOT},         {"true", T_TRUE},   {"false", T_FALSE}};
    for (const std::pair<const char *, token_type> &word : words)
    {
        insert_stringValue(word.first, word.second);
    }
    return true;
}

bool SymbolTable::insert_stringValue(const std::string &stringValue, token_type type_of_token)
{
    token new_token;
    new_token.type = type_of_token;
    new_token.stringValue = stringValue;
    return insert_string_token(new_token);
}

bool SymbolTable::insert_string_token(const token &new_token)
{
    return map.emplace(new_token.stringValue, new_token).second;
}

bool SymbolTable::is_in_table(const std::string &test_string) const
{
    return map.find(test_string) != map.end();
}

bool SymbolTable::lookup_lexeme(const std::string &lexeme, token &out) const
{
    std::unordered_map<std::string, token>::const_iterator found = map.find(lexeme);
    if (found == map.end())
    {
        return false;
    }
    out = found->second;
    return true;
}

bool SymbolTable::init_reserved_chars()
{
    const std::pair<char, token_type> chars[] = {
        {'(', T_LPARAM}, {')', T_RPARAM}, {'[', T_LBRACKET}, {']', T_RBRACKET},
        {',', T_COMMA}, {'/', T_SLASH}, {'{', T_LBRACE}, {'}', T_RBRACE},
        {'=', T_ASSIGN}, {'+', T_PLUS}, {'_', T_UNDERSCORE}, {'.', T_PERIOD},
        {'>', T_GREATER}, {'<', T_LESS}, {'*', T_MULT}, {'"', T_QUOTE},
        {'!', T_EXCLAM}, {';', T_SEMICOLON}, {':', T_COLON}, {'|', T_VERTICAL_BAR},
        {'&', T_AMPERSAND}, {'-', T_MINUS}};
    for (const std::pair<char, token_type> &entry : chars)
    {
        insert_char_table(entry.first, entry.second);
    }
    return true;
}

bool SymbolTable::insert_char_table(char reserved_char, token_type type_of_token)
{
    return reserved_chars.emplace(reserved_char, type_of_token).second;
}

bool SymbolTable::is_reserved_char(char test_char) const
{
    return reserved_chars.find(test_char) != reserved_chars.end();
}

const ScopeTable *SymbolTable::find_scope(int scope_id) const
{
    std::unordered_map<int, ScopeTable>::const_iterator found = scope_table.find(scope_id);
    return found == scope_table.end() ? NULL : &found->second;
}

ScopeTable *SymbolTable::find_scope_mut(int scope_id)
{
    std::unordered_map<int, ScopeTable>::iterator found = scope_table.find(scope_id);
    return found == scope_table.end() ? NULL : &found->second;
}

bool SymbolTable::create_scope(int scope_id, int parent_scope_id, bool has_parent)
{
    return scope_table.emplace(scope_id, ScopeTable(scope_id, parent_scope_id, has_parent)).second;
}

bool SymbolTable::has_scope(int scope_id) const
{
    return find_scope(scope_id) != NULL;
}

bool SymbolTable::set_scope_owner(int scope_id, const SymbolRef &owner)
{
    ScopeTable *scope = find_scope_mut(scope_id);
    if (scope == NULL || !has_declared(owner.scope_id, owner.name))
    {
        return false;
    }
    scope->owner_procedure = owner;
    scope->has_owner_procedure = true;
    return true;
}

bool SymbolTable::lookup_scope_owner(int scope_id, token &out) const
{
    const ScopeTable *scope = find_scope(scope_id);
    if (scope == NULL || !scope->has_owner_procedure)
    {
        return false;
    }
    return lookup_declared(scope->owner_procedure, out);
}

bool SymbolTable::declare_symbol(int scope_id, const token &new_token)
{
    ScopeTable *scope = find_scope_mut(scope_id);
    if (scope == NULL || new_token.stringValue.empty())
    {
        return false;
    }
    token canonical = new_token;
    canonical.scope_id = scope_id;
    canonical.global_scope = scope_id == 0;
    return scope->declare_token(canonical);
}

bool SymbolTable::can_declare_all(int scope_id, const std::vector<token> &symbols) const
{
    const ScopeTable *scope = find_scope(scope_id);
    if (scope == NULL)
    {
        return false;
    }
    std::unordered_set<std::string> names;
    for (const token &symbol : symbols)
    {
        if (symbol.stringValue.empty() || scope->is_in_table(symbol.stringValue) ||
            !names.emplace(symbol.stringValue).second)
        {
            return false;
        }
    }
    return true;
}

bool SymbolTable::declare_all(int scope_id, const std::vector<token> &symbols)
{
    if (!can_declare_all(scope_id, symbols))
    {
        return false;
    }
    for (const token &symbol : symbols)
    {
        if (!declare_symbol(scope_id, symbol))
        {
            return false;
        }
    }
    return true;
}

bool SymbolTable::lookup_declared(const SymbolRef &reference, token &out) const
{
    const ScopeTable *scope = find_scope(reference.scope_id);
    if (scope == NULL)
    {
        return false;
    }
    const token *found = scope->find_token(reference.name);
    if (found == NULL)
    {
        return false;
    }
    out = *found;
    return true;
}

bool SymbolTable::replace_declared(const SymbolRef &reference, const token &replacement)
{
    ScopeTable *scope = find_scope_mut(reference.scope_id);
    if (scope == NULL)
    {
        return false;
    }
    token *existing = scope->find_token_mut(reference.name);
    if (existing == NULL)
    {
        return false;
    }
    token canonical = replacement;
    canonical.stringValue = reference.name;
    canonical.scope_id = reference.scope_id;
    canonical.global_scope = reference.scope_id == 0;
    *existing = canonical;
    return true;
}

bool SymbolTable::append_procedure_parameter(const SymbolRef &reference, data_types parameter_type)
{
    ScopeTable *scope = find_scope_mut(reference.scope_id);
    if (scope == NULL)
    {
        return false;
    }
    token *procedure = scope->find_token_mut(reference.name);
    if (procedure == NULL || procedure->identifer_type != I_PROCEDURE)
    {
        return false;
    }
    procedure->procedure_params.push_back(parameter_type);
    return true;
}

bool SymbolTable::has_declared(int scope_id, const std::string &name) const
{
    const ScopeTable *scope = find_scope(scope_id);
    return scope != NULL && scope->is_in_table(name);
}

bool SymbolTable::resolve_name(const std::string &name, int current_scope_id, token &out) const
{
    const ScopeTable *current = find_scope(current_scope_id);
    if (current != NULL)
    {
        const token *local = current->find_token(name);
        if (local != NULL)
        {
            out = *local;
            return true;
        }
        if (current->has_owner_procedure &&
            current->owner_procedure.name == name &&
            lookup_declared(current->owner_procedure, out))
        {
            return true;
        }
    }
    if (current_scope_id != 0)
    {
        const ScopeTable *global = find_scope(0);
        const token *global_symbol = global == NULL ? NULL : global->find_token(name);
        if (global_symbol != NULL)
        {
            out = *global_symbol;
            return true;
        }
    }
    return false;
}

bool SymbolTable::resolve_procedure(const std::string &name, int current_scope_id, token &out) const
{
    return resolve_name(name, current_scope_id, out) && out.identifer_type == I_PROCEDURE;
}
