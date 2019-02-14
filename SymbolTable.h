#ifndef SYMBOLTABLE_H
#define SYMBOLTABLE_H
#include "token.h"
#include <unordered_map>

class SymbolTable{
public:
//hash map with a integer key and a string combo
//std::unordered_map<int,std::string> map;

//hash map with an integer key and a token for the data
std::unordered_map<int,token> map;

};




#endif // !SYMBOLTABLE_H