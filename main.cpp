#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include "scanner.h"
#include "token.h"
#include "SymbolTable.h"
#include "parser.h"

int main(int argc, char *argv[])
{
    //Makes sure that a file is passed to the compiler
    if (argc == 1)
    {
        std::cout << "Error!\nUsage: " << argv[0] << " <file to compile>\n";
        return 1;
    }
    else
    {
        //converts the argument passed to a string for ease of use
        std::string string_arg = argv[1];

        parser *file_parser;
        file_parser = new parser(string_arg);

        //scanner *first_scan;
        //first_scan = new scanner(string_arg);
        //first_scan->test();
        return 0;
    }
}