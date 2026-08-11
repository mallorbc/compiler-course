#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <system_error>
#include "scanner.h"
#include "token.h"
#include "SymbolTable.h"
#include "parser.h"

int main(int argc, char *argv[])
{
    //Makes sure that a file is passed to the compiler
    if (argc != 2)
    {
        std::cout << "Error!\nUsage: " << argv[0] << " <file to compile>\n";
        return 1;
    }
    else
    {
        //converts the argument passed to a string for ease of use
        std::string string_arg = argv[1];

        std::error_code path_error;
        if (!std::filesystem::is_regular_file(string_arg, path_error))
        {
            std::cout << "Error!\nUnable to open source file: " << string_arg << "\n";
            return 1;
        }

        std::ifstream source_file(string_arg);
        if (!source_file.is_open())
        {
            std::cout << "Error!\nUnable to open source file: " << string_arg << "\n";
            return 1;
        }

        parser *file_parser;
        file_parser = new parser(string_arg);

        //scanner *first_scan;
        //first_scan = new scanner(string_arg);
        //first_scan->test();
        if (file_parser->error_count() > 0)
        {
            return 1;
        }
        return 0;
    }
}
