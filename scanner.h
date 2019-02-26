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
    //reads the file
    void ReadFile();
    //initializes the scanner
    void InitScanner(std::string file);
    //checks to see whether token is an identifier or reserved word
    bool Is_Reserved_Word(std::string test_word);
    //stores the next token in Current_token
    void Get_token();
    //checks to see if is a reserved char
    bool Is_Reserved_Char(char test_char);
    //soley for quick tests
    void test();
    //detects whether or not the current char is the first char in the token
    bool is_first_char();
    //start of the lexer; detects what the beggining char is
    int what_is_char(char test_char);
    //builds char tokesn
    void build_char_token();
    //builds floats and integer tokens
    void build_number_token();
    //builds indentifiers and reserved words
    void build_string_token();
    //tracks spaces, end lines and invalid characters
    void invalid_char_test();
    //builds the string stored in quotes
    void string_value_builder();


    //tracks the current line; increments at the end of the line
    int current_line;
    //used to store the file name
    std::string FileName;
    //Creates an text input stream
    std::ifstream source;
    //used to check the next char; so to make sure that it isn't the end of the line, a space, of the EOF
    char next_char;
    //used to store the current character
    char current_char;
    //used to build a string from the characters after checking them.  Ease of use
    std::string build_string;
    //pointer for current token
    token *Current_token;
    //used for lexer to switch case on based on char
    int char_status;
    //tracks whether the program has an open quoation
    bool quote_status = false;
    //tracks whether an error has occured
    bool error_detected = false;


    //used to trigger debug statements
    bool debug = false;


};


#endif // !SCANNER_H
