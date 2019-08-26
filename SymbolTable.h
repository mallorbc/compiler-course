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

    bool scope_map_exists(int scope_id);
    bool create_new_scope_table(int scope_id);
    bool resync_tables(int scope_id, token token_to_sync);
    bool remove_scope(int scope_id);

    bool add_procedure_valid_inputs(std::string procedure_name, data_types input_data_type, int scope_id);
    bool update_token_scope_id(token token_to_update, int scope_id);
    bool update_identifier_type(token token_to_update, int scope_id);
    bool update_identifier_data_type(std::string identifier_name, data_types data_type, int scope_id);
    bool update_procedure_return_type(std::string procedure_name, data_types return_type, int scope_id);

    bool token_is_in_scope_table(std::string token_string, int scope_id);
    bool token_is_in_global_scope(token token_to_test, int scope_id);
    token get_globabl_token(token token_to_get);
    //bool update_variable_data_type(std::string var_name, data_types data_type, int scope_id)
};

#endif // !SYMBOLTABLE_H