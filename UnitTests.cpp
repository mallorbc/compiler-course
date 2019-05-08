#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include "scanner.h"
#include "token.h"
#include "SymbolTable.h"
#include "parser.h"
#include <unistd.h>

unsigned int second = 1000000;

bool test_type_declaration(std::string filename);


int main(int argc, char *argv[])
{
    bool test_passed;
    int counter = 0;
    int num_of_tests = 0;

    // while(true){
    //     std::cout<<counter<<std::endl;
    //     usleep(second);
    //     counter++;
    // }
    test_passed = test_type_declaration("type_declaration");
    if(test_passed){
        std::cout<<"The test passed"<<std::endl;
    }
    else{
        std::cout<<"The test failed"<<std::endl;
    }

}

bool test_type_declaration(std::string filename){
    bool test_result;
    parser *file_parser;
    file_parser = new parser(filename);
    while(file_parser->Current_parse_token_type!=T_TYPE){
        file_parser->Get_Valid_Token();
    }
    test_result = file_parser->parse_type_declaration();
    return test_result; 
}