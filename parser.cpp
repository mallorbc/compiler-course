#include "parser.h"

parser::parser(std::string file_to_parse){
    bool valid_parse;
    parse_file = file_to_parse;
    Lexer = new scanner(parse_file);
    valid_parse = parse_program();
    if(valid_parse){
        std::cout<<"The program parsed successfully"<<std::endl;
    }
    else{
        std::cout<<"The program had parsing errors"<<std::endl;
    }
    // while(!Lexer->end_of_file){
    //     Current_parse_token = Get_Valid_Token();
    //     //should only happen at the end of the file now
    //     if(Current_parse_token.type == 0){
    //         continue;
    //     }
    //     std::cout<<"PARSE: starting to parse "<<Current_parse_token.type<<std::endl;
    // }
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
        while(Current_parse_token_type!=T_BEGIN){
            //keeps parsing until keyword begin is found
            valid_parse = parse_base_declaration();
            if(!valid_parse){
                break;
            }
        }
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
    if(Current_parse_token_type == T_PROCEDURE){
        Current_parse_token = Get_Valid_Token();
        valid_parse = parse_procedure_header();

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
    if(Current_parse_token_type == T_IDENTIFIER){
        Current_parse_token = Get_Valid_Token();
    }
    else{
        generate_error_report("Procedure must be named a valid identifier");
        return false;
    }
    if(Current_parse_token_type == T_COLON){
        Current_parse_token = Get_Valid_Token();
        valid_parse = parse_type_mark();
    }
    else{
        generate_error_report("Expected \":\" before type mark declaration");
        return false;
    }
    Current_parse_token = Get_Valid_Token();
    if(Current_parse_token_type == T_LPARAM){
        Current_parse_token = Get_Valid_Token();
        valid_parse = parse_parameter_list();

    }
    else{
        generate_error_report("Missing \"(\" needed to for procedure declaration");
        return false;
    }
    Current_parse_token = Get_Valid_Token();
    if(Current_parse_token_type == T_RPARAM){

    }
    


    return valid_parse;

}

bool parser::parse_type_mark(){
    bool valid_parse;
    //May need to do something once the type is determined
    if(Current_parse_token_type == T_INTEGER_TYPE){
        
        valid_parse = true;
    }
    else if(Current_parse_token_type == T_FLOAT_TYPE){

        valid_parse = true;
    }
    else if(Current_parse_token_type == T_STRING_TYPE){

        valid_parse = true;
    }
    else if(Current_parse_token_type == T_BOOL_TYPE){

        valid_parse = true;
    }
    //I think this means that the this will be the same type as the identifier
    else if(Current_parse_token_type == T_IDENTIFIER){

        valid_parse = true;
    }
    else if(Current_parse_token_type == T_ENUM){
        Current_parse_token = Get_Valid_Token();
        if(Current_parse_token_type == T_LBRACE){
            Current_parse_token = Get_Valid_Token();
            if(Current_parse_token_type!=T_IDENTIFIER){
                generate_error_report("Expected identifier as part of Enum");
                return false;

            }
            else{
                Current_parse_token = Get_Valid_Token();
                if(Current_parse_token_type == T_COMMA){
                    bool identifier_needed = true;
                    while(true){
                        //expected an identifier and got an identifier, valid
                        if(Current_parse_token_type == T_IDENTIFIER && identifier_needed){
                            //generate code here?
                            identifier_needed = false;

                        }
                        //expected a comma, got an identifier, throw an error
                        else if(Current_parse_token_type == T_IDENTIFIER && !identifier_needed){
                            generate_error_report("Expected a \",\", instead recieved a identifier");
                            return false;
                            
                        }
                        //expected a comma, got a comma, valid
                        else if(Current_parse_token_type == T_COMMA && !identifier_needed){
                            //generate code here?
                            identifier_needed = true;
                        }
                        //expected an identifier, got a comma, throw an error
                        else if(Current_parse_token_type == T_COMMA && identifier_needed){
                            generate_error_report("Expected a \",\" or \"}\", recieved a repetead comma");
                            return false;
                        }
                        //some other odd token was given
                        else{
                            if(identifier_needed){
                                generate_error_report("Missing expected indentifier for entry in enumeration");
                                return false;
                            }
                            else{
                                generate_error_report("Expected \",\" for entry in enumeration");
                                return false;
                            }

                        }
                        Current_parse_token = Get_Valid_Token();
                        if(Current_parse_token_type == T_RBRACE){
                            //no trailing comma, right brace found, valid
                            if(!identifier_needed){
                                return true;
                            }
                            //trailing comma detected
                            else{
                                generate_error_report("Trailing comma detected before closing of enumeration");
                                return false;

                            }
                        }

                    }
                }
                //one identifier in the enuemeration, still valid
                else if(Current_parse_token_type == T_RBRACE){
                    valid_parse = true;

                }
                //invalid tokens inside an enumeration
                else{
                    return false;
                }
            }
        }
        else{
            generate_error_report("Missing expected \"{\" to start enumeration");
            return false;
        }
    }
    return valid_parse;
}

bool parser::parse_parameter_list(){
    bool valid_parse;
    //if parameter list is empty
    if(Current_parse_token_type == T_RPARAM){
        valid_parse = true;
    }
    // else if()
    //at least one parameter is passed
    else{
        bool parameter_needed = false;
        while(true){
            valid_parse = parse_parameter();
            Current_parse_token = Get_Valid_Token();
            if(Current_parse_token_type == T_RPARAM){
                if(!parameter_needed){
                    return true;
                }
                //trailing comma
                else{
                    generate_error_report("Extra trailing comma occuring in parameter list");
                    return false;
                }
            }

        }
        // valid_parse = parse_parameter();
        // Current_parse_token = Get_Valid_Token();
        // if(Current_parse_token_type == T_COMMA){
        //     while(true){
        //         valid_parse = parse_parameter();
        //         Current_parse_token = Get_Valid_Token();
        //         if(Current_parse_token_type == T_RPARAM){
        //             break;
        //         }
        //     }
        // }
    }


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

bool parser::parse_parameter(){
    bool valid_parse;
    valid_parse = parse_variable_declaration();
}