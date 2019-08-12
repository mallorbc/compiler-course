#ifndef SYMBOLTABLE_H
#define SYMBOLTABLE_H
#include "token.h"
#include "ScopeTable.h"
#include <unordered_map>
#include <string>
#include <vector>
#include <utility>

class SymbolTable
{
public:
    std::unordered_map<int, ScopeTable> scope_table;
    //hash map with a integer key and a string combo
    //std::unordered_map<int,std::string> map;

    //hash map with an integer key and a token for the data
    std::unordered_map<std::string, token> map;
    std::unordered_map<char, int> reserved_chars;
    bool insert_stringValue(std::string stringValue, token_type type_of_token);
    bool init_reserved_words();
    bool init_reserved_chars();
    bool insert_string_token(token new_token);
    bool insert_char_table(char reserved_char, token_type type_of_token);
    bool is_in_table(std::string test_string);
    bool is_reserved_char(char test_char);
    //std::vector<std::string> Reserved_Words;
    SymbolTable();

    bool make_token_global(token global_token);
    bool is_global_token(token global_token);
    bool update_token_scope_id(token token_to_update, int scope_id);
    bool scope_map_exists(int scope_id);
    bool create_new_scope_table(int scope_id);
    bool resync_tables(int scope_id, token token_to_sync);
};

#endif // !SYMBOLTABLE_H