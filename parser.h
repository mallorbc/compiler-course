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
    void start_parser(token first_token);
    bool detect_token();


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

    bool parse_program_keyword();
    bool parse_identifier();
    bool parse_IS();


};


#endif // !PARSER_H