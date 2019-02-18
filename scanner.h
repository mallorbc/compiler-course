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

// struct Line_struct{
//     std::string line_string;
//     int line_number;
// };

class scanner{
    public:
    scanner();
    scanner(std::string file);
    void ReadFile();
    void InitScanner(std::string file);
    //void Build_Reserved_Words_Table();
    //void Build_Reserved_Char_Table();
    //bool Is_Reserved_Word(std::string test_word);
    //bool Is_Reserved_Char(char test_char);
    std::string Tolower_string(std::string word_to_convert);
    void test();
    int current_line;
    std::string FileName;
    //std::vector<std::string> Reserved_Words;
    //std::vector<char> Reserved_Chars;
    //token Current_token;
    //Line_struct Line_data;


};


#endif // !SCANNER_H
