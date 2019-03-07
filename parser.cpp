#include "parser.h"

parser::parser(std::string file_to_parse){
    parse_file = file_to_parse;
    Lexer = new scanner(parse_file);
    while(!Lexer->end_of_file){
        //Current_parse_token = Get_Valid_Token();
        Current_parse_token = Get_Valid_Token();
        //should only happen at the end of the file now
        if(Current_parse_token.type == 0){
            continue;
        }
        std::cout<<"PARSE: starting to parse "<<Current_parse_token.type<<std::endl;
        no_errors = detect_token();
        //Current_parse_token = Lexer->Current_token;
    }
    print_errors();

}

token parser::Get_Valid_Token(){
    Current_parse_token = Lexer->Get_token();
    Current_parse_token_type = Current_parse_token.type;
    if(Current_parse_token.type == 0){
        std::cout<<"Token type 0 detected"<<std::endl;
    }

    return Current_parse_token;
}


void parser::start_parser(token first_token){
    if(first_token.type!=T_PROGRAM){
        no_errors = false;
        std::cout<<"Program doesn't start with keyword: program"<<std::endl;
    }
    Current_parse_token = first_token;
    started_parsing = true;
    detect_token();
}


bool parser::detect_token(){
    switch(Current_parse_token_type){
        case T_PROGRAM:
        no_errors = parse_program_keyword();
        break;
    }
    return no_errors;

}

bool parser::parse_program_keyword(){
    if(Current_parse_token_type!=T_PROGRAM){
        generate_error_report("Expected keyword \"Program\" not found");
        return false;
    }
    Current_parse_token = Get_Valid_Token();

    switch(Current_parse_token_type){
        case T_IDENTIFIER:
        no_errors = parse_identifier();

        break;

        default:

        break;

    }

    std::cout<<Current_parse_token.type<<std::endl;

    std::cout<<"Started parsing program_header"<<std::endl;
    return no_errors;

}

bool parser::parse_identifier(){
    if(Current_parse_token_type!=T_IDENTIFIER){
        generate_error_report("Expected identifier not found");
        return false;
    }
    Current_parse_token = Get_Valid_Token();

    switch(Current_parse_token_type){
        case T_IS:

        break;

        case T_COLON:

        break;

        default:

        break;


    }


    return no_errors;
}

void parser::add_error_report(std::string error_report){
    error_reports.push_back(error_report);
}

void parser::generate_error_report(std::string error_message){
    std::string full_error_message = "";
    full_error_message = "Error on line " + Current_parse_token.line_found + ':';
    full_error_message = full_error_message + error_message;
    add_error_report(full_error_message);
}

void parser::print_errors(){
    for(int i = 0; i < error_reports.size(); i++){
        std::cout<<error_reports[i]<<std::endl;
    }
}

bool parser::parse_IS(){
    if(Current_parse_token_type!=T_IS){
        generate_error_report("Expected keyword is not found");
        return false;
    }
    // Current_parse_token = Get_Valid_Token()
    // switch(Current_parse_token_type){
    //     case
    // }
    return no_errors;

}