#include "parser.h"

// parser::parser(token parse_token){
//     init_parser();
// }
parser::parser(){

}

// void parser::init_parser(token parse_token){
//     Current_parse_token = parse_token;

// }

void parser::start_parser(token first_token){
    if(first_token.type!=T_PROGRAM){
        error_detected = true;
        std::cout<<"Program doesn't start with keyword: program"<<std::endl;
    }
    Current_parse_token = first_token;
    started_parsing = true;
    detect_token();
}

void parser::parse_next_token(token next_token){
    Previous_parse_token = Current_parse_token;
    Current_parse_token = next_token;
    detect_token();

}

void parser::detect_token(){
    Current_parse_token_type = Current_parse_token.type;

    switch(Current_parse_token_type){
        case T_PROGRAM:
        parse_program_header();
        break;
    }

}

void parser::parse_program_header(){
    token next_token;
    std::cout<<"Started parsing program_header"<<std::endl;

}