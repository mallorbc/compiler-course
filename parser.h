#ifndef PARSER_H
#define PARSER_H
#include "token.h"
#include <iostream>
#include <string>
#include <vector>
#include "scanner.h"


class parser{
    public:
    //parser();
    parser(std::string parse_file);
    token Get_Valid_Token();


    token Current_parse_token;
    //token Previous_parse_token;
    int Current_parse_token_type;
    bool no_errors = true;
    bool started_parsing = false;

    scanner *Lexer;

    std::string parse_file;

    std::vector<std::string> error_reports;
    void add_error_report(std::string error_report);
    void generate_error_report(std::string error_message);
    void print_errors();


    bool parse_program();
    bool parse_program_header();

    bool parse_program_body();

    bool parse_base_declaration();
    bool parse_procedure_header();
    bool parse_variable_declaration();
    
    bool parse_type_declaration();
    bool parse_type_mark();

    bool parse_parameter_list();
    bool parse_parameter();
    
    bool parse_statement();


};


#endif // !PARSER_H