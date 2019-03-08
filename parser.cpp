#include "parser.h"

parser::parser(std::string file_to_parse){
    bool valid_parse;
    parse_file = file_to_parse;
    Lexer = new scanner(parse_file);
    valid_parse = parse_program();
    while(!Lexer->end_of_file){
        Current_parse_token = Get_Valid_Token();
        //should only happen at the end of the file now
        if(Current_parse_token.type == 0){
            continue;
        }
        std::cout<<"PARSE: starting to parse "<<Current_parse_token.type<<std::endl;
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




void parser::add_error_report(std::string error_report){
    error_reports.push_back(error_report);
}

void parser::generate_error_report(std::string error_message){
    std::string full_error_message = "";
    full_error_message = "Error on line " + std::to_string(Current_parse_token.line_found) + ':';
    full_error_message = full_error_message + error_message;
    add_error_report(full_error_message);
}

void parser::print_errors(){
    for(int i = 0; i < error_reports.size(); i++){
        std::cout<<error_reports[i]<<std::endl;
    }
}



bool parser::parse_program(){
    bool valid_parse;
    valid_parse = parse_program_header();
    return valid_parse;
}

bool parser::parse_program_header(){
    bool valid_parse;
    Current_parse_token = Get_Valid_Token();
    if(Current_parse_token_type == T_PROGRAM){
        Current_parse_token = Get_Valid_Token();
    }
    else{
        generate_error_report("Expected keyword \"Program\" not found");
        return false;
    }
    if(Current_parse_token_type == T_IDENTIFIER){
        Current_parse_token = Get_Valid_Token();
    }
    else{
        generate_error_report("Expected \"identifier\" not found");
        return false;
    }  
    if(Current_parse_token_type == T_IS){
        Current_parse_token = Get_Valid_Token();
    }
    else{
        generate_error_report("Expected keyword \"is\" is not found");
        return false;
    }
    valid_parse = parse_program_body();
    return valid_parse;


}

bool parser::parse_program_body(){
    bool valid_parse;
    //Current_parse_token = Get_Valid_Token();
    if(Current_parse_token_type!=T_BEGIN){
        valid_parse = parse_base_declaration();
    }
    else{
        Current_parse_token = Get_Valid_Token();
        valid_parse = parse_statement();
    }
    return valid_parse;
}

bool parser::parse_base_declaration(){
    bool valid_parse;
    if(Current_parse_token_type == T_GLOBAL){
        //Do work for global declarations here
        Current_parse_token = Get_Valid_Token();
    }
    if(Current_parse_token_type == T_PROCEDUIRE){
        Current_parse_token = Get_Valid_Token();
        valid_parse = parse_program_header();

    }
    else if(Current_parse_token_type == T_VARIABLE){
        Current_parse_token = Get_Valid_Token();
        valid_parse = parse_variable_declaration();
    }
    else if(Current_parse_token_type == T_TYPE){
        Current_parse_token = Get_Valid_Token();
        valid_parse = parse_type_declaration();
    }
    else{
        generate_error_report("Expected keywords \"procedure\",\"variable\", or \"type\" not found");
        return false;
    }
    return valid_parse;

}

bool parser::parse_procedure_header(){
    bool valid_parse;

    return valid_parse;

}

bool parser::parse_variable_declaration(){
    bool valid_parse;

    return valid_parse;

}

bool parser::parse_type_declaration(){
    bool valid_parse;

    return valid_parse;

}

bool parser::parse_statement(){
    bool valid_parse;

    return valid_parse;
}