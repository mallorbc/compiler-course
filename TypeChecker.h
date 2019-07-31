#ifndef TYPECHECKER_H
#define TYPECHECKER_H
#include "token.h"
#include <unordered_map>
#include <string>
#include <vector>
#include "SymbolTable.h"

class TypeChecker
{
public:
    TypeChecker(std::unordered_map<std::string, token> starter_table_map);
    //will hold the symbol table and will be used for type checking
    std::unordered_map<std::string, token> token_type_map;

    //variable that will hold the first token for the assignment
    token token_one;

    //variable that will hold the second token for the assignment
    token token_two;

    //this will be called when variables are declared
    bool insert_variable(token declared_variable);

    //this will be called in statements
    bool check_tokens(token first_token, token second_token);

    //takes an map input and copies it over to the variable token_type_map
    bool copy_table(std::unordered_map<std::string, token> map_to_copy);
};

#endif // !TYPECHECKER_H