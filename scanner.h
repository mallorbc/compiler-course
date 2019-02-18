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

class scanner{
    public:
    SymbolTable symbol_table;
    scanner();
    scanner(std::string file);
    void ReadFile();
    void InitScanner(std::string file);
    //void Build_Reserved_Char_Table();
    bool Is_Reserved_Word(std::string test_word);
    void Get_token();
    //bool Is_Reserved_Char(char test_char);
    void test();
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
    //used to check the next variable; so to make sure that it isn't the end of the line, a space, of the EOF
    char next_char;
    //used to store the current character
    char current_char;
    //used to build a string from the characters after checking them.  Ease of use
    std::string build_string;
    token Current_token;
    //Line_struct Line_data;


};


#endif // !SCANNER_H
