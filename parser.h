#ifndef PARSER_H
#define PARSER_H
#include "token.h"
#include <iostream>


class parser{
    public:
    parser();
    // parser(token parse_toke);
    // void init_parser(token parse_token);
    void start_parser(token first_token);
    void parse_next_token(token next_token);
    void detect_token();
    void parse_program_header();

    token Current_parse_token;
    token Previous_parse_token;
    int Current_parse_token_type;
    bool error_detected = false;
    bool started_parsing = false;


};


#endif // !PARSER_H