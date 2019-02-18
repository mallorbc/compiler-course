#ifndef SYMBOLTABLE_H
#define SYMBOLTABLE_H
#include "token.h"
#include <unordered_map>
#include <string>
#include <vector>
#include <utility>

class SymbolTable{
public:
//hash map with a integer key and a string combo
//std::unordered_map<int,std::string> map;

//hash map with an integer key and a token for the data
std::unordered_map<std::string,token> map;
bool insert_stringValue(std::string stringValue,token_type type_of_token);
bool init_reserved_words();
bool insert_string_token(token new_token);
bool is_in_table(std::string test_string);
//std::vector<std::string> Reserved_Words;
SymbolTable();
};




#endif // !SYMBOLTABLE_H