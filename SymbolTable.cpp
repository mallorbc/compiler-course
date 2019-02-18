#include "SymbolTable.h"

SymbolTable::SymbolTable(){
    init_reserved_words();
}

bool SymbolTable::init_reserved_words(){
    //Inserts keywords into the symbol table with the string value and the key
    insert_stringValue("program",T_PROGRAM);
    insert_stringValue("is",T_IS);
    insert_stringValue("begin",T_BEGIN);
    insert_stringValue("end",T_END);
    insert_stringValue("global",T_GLOBAL);
    insert_stringValue("procedure",T_PROCEDUIRE);
    insert_stringValue("variable",T_VARIABLE);
    insert_stringValue("type",T_TYPE);
    insert_stringValue("integer",T_INTEGER);
    insert_stringValue("float",T_FLOAT);
    insert_stringValue("string",T_STRING);
    insert_stringValue("bool",T_BOOL);
    insert_stringValue("enum",T_ENUM);
    insert_stringValue("if",T_IF);
    insert_stringValue("then",T_THEN);
    insert_stringValue("else",T_ELSE);
    insert_stringValue("for",T_FOR);
    insert_stringValue("return",T_RETURN);
    insert_stringValue("not",T_NOT);
    insert_stringValue("true",T_TRUE);
    insert_stringValue("false",T_FALSE);
    
    return 1;

}

bool SymbolTable::insert_stringValue(std::string stringValue,token_type type_of_token){
    //stores
    token *new_token;
    new_token = new token;
    new_token->type = type_of_token;
    new_token->stringValue = stringValue;
    insert_string_token(*new_token);

    return 1;
    

}

bool SymbolTable::insert_string_token(token new_token){
    std::string key_value = new_token.stringValue;
    map[key_value] = new_token;
  
    return 1;
}

bool SymbolTable::is_in_table(std::string test_string){
    if(map.find(test_string) == map.end()){
        return 0;
    }
    else{
        return 1;
    }
}