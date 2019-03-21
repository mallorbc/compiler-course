#ifndef PARSER_H
#define PARSER_H
#include "token.h"
#include <iostream>
#include <string>
#include <vector>
#include "scanner.h"


class parser{
    public:
    //constructors for the parser
    parser(std::string parse_file);

    //data structures and methods for current tokens and look ahead tokens
    token Current_parse_token;
    int Current_parse_token_type;
    //These values will store Look_ahead_tokens[0] for ease of access
    token Next_parse_token;
    int Next_parse_token_type;
    //vector that could be used to build up a queue of tokens
    std::vector<token> Look_ahead_tokens;
    token Get_Valid_Token();

    //Lexer object and the file that will be lexed by it
    scanner *Lexer;
    std::string parse_file;

    //data strucutres and methods for generating error reports
    std::vector<std::string> error_reports;
    void add_error_report(std::string error_report);
    void generate_error_report(std::string error_message);
    void print_errors();

    //parsing parts of the program
    bool parse_program();
    bool parse_program_header();
    bool parse_program_body();

    //methods for parsing declarations
    bool parse_base_declaration();
    bool parse_variable_declaration();
    bool parse_type_declaration();

    bool parse_bound();
    bool parse_number();


    bool parse_procedure_header();
    bool parse_procedure_body();

    bool parse_type_mark();

    bool parse_parameter_list();
    bool parse_parameter();
    
    bool parse_statement();


};


#endif // !PARSER_H