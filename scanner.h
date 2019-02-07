#ifndef SCANNER_H
#define SCANNER_H
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include "token.h"
#include <ctype.h>

struct Line_struct{
    std::string line_string;
    int line_number;
};

class scanner{
    public:
    scanner();
    scanner(std::string file);
    void ReadFile();
    void InitScanner(std::string file);
    void test();
    int current_line = 1;
    Line_struct Line_data;
    std::string FileName;

};


#endif // !SCANNER_H
