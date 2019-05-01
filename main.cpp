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
    if(argc==1){
        std::cout<<"Error!\nUsage: "<<argv[0]<<" <file to compile>\n";
        return 1;
    }
    else{
        //converts the argument passed to a string for ease of use
        std::string string_arg = argv[1];

        parser *file_parser;
        file_parser = new parser(string_arg);

        //Creates a pointer of object scanner
        scanner *first_scan;
        //Creates a new object scanner and passes in the file, the file is then read
        //first_scan = new scanner(string_arg);
        //first_scan->test();
        //first_scan->test();
        //first_scan->ReadFile();
        
        return 0;
    }
    return 1;
}