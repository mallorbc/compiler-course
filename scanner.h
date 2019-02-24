#ifndef SCANNER_H
#define SCANNER_H
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include "token.h"
#include <ctype.h>
#include <algorithm>
#include "SymbolTable.h"
#include "CustomFunctions.h"
#include <cstdio>

// struct Line_struct{
//     std::string line_string;
//     int line_number;
// };
enum char_type{
    alpha_char = 1,
    number_char = 2,
    reserved_char = 3,
    invalid_char = 4
};

class scanner{
    public:
    //used to hold the symbol table and character table
    SymbolTable symbol_table;
    //two constructors for the class, first one is unused
    scanner();
    scanner(std::string file);
    void ReadFile();
    void InitScanner(std::string file);
    bool Is_Reserved_Word(std::string test_word);
    void Get_token();
    bool Is_Reserved_Char(char test_char);
    void test();
    bool is_first_char();
    int what_is_char(char test_char);
    bool is_end_token();
    void build_char_token();
    void build_number_token();
    void build_string_token();
    void invalid_char_test();
    int current_line;

    std::string FileName;
    //Creates an text input stream
    std::ifstream source;
    //used to track whether or not the character is the first character in the token
    bool first_char;
    //checks whether or not the first char is a number; if so, can't be an identifier
    bool first_char_is_num;
    //used to track whtether the first character is an alpha
    bool first_char_is_alpha;
    //used to rack whether or not the first character is a reserved character
    bool first_char_is_reserved_char;
    //used to track if the character is an invalid character
    bool first_char_is_invalid;
    //used to check the next char; so to make sure that it isn't the end of the line, a space, of the EOF
    char next_char;
    //used to store the current character
    char current_char;
    //used to build a string from the characters after checking them.  Ease of use
    std::string build_string;
    token Current_token;
    //Line_struct Line_data;


    int char_status;

    bool quote_status = 0;


};


#endif // !SCANNER_H
