#include "TypeChecker.h"

TypeChecker::TypeChecker(std::unordered_map<std::string, token> starter_SymbolTable_map)
{
    copy_table(starter_SymbolTable_map);
}

bool TypeChecker::check_tokens(token first_token, token second_token)
{
    bool same_type;

    return same_type;
}

//may not be needed since we will be copying the symbol table over
bool TypeChecker::insert_variable(token declared_variable)
{
    bool sucessful_insert;

    return sucessful_insert;
}

bool TypeChecker::copy_table(std::unordered_map<std::string, token> map_to_copy)
{
    bool sucessful_copy;
    token_type_map = map_to_copy;
    sucessful_copy = true;
    return sucessful_copy;
}

std::unordered_map<std::string, token> TypeChecker::set_token_global(token token_to_set)
{
    return token_type_map;
}
