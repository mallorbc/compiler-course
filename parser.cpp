#include "parser.h"

parser::parser(std::string file_to_parse){
    //initalized the Current_parse_token type to 9999 as a flag to say that nothing has been loaded yet
    Current_parse_token.type = 9999;
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
    token first_token;
    //if first token in the list;  may need a similar condition for the last token
    if(Current_parse_token.type == 9999){
        first_token = Lexer->Get_token();
        Current_parse_token = Lexer->Get_token();
        Look_ahead_tokens.push_back(Current_parse_token);
        Current_parse_token = first_token;
        Current_parse_token_type = Current_parse_token.type;
        Next_parse_token = Look_ahead_tokens[0];
        Next_parse_token_type = Next_parse_token.type;
        
    }
    //if not the first token
    else{
        Current_parse_token = Look_ahead_tokens[0];
        Current_parse_token_type = Current_parse_token.type;
        Look_ahead_tokens.erase(Look_ahead_tokens.begin());
        Look_ahead_tokens.push_back(Lexer->Get_token());
        Next_parse_token = Look_ahead_tokens[0];
        Next_parse_token_type = Next_parse_token.type;
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
//not done
bool parser::parse_program_body(){
    bool valid_parse;
    //Current_parse_token = Get_Valid_Token();
    if(Current_parse_token_type!=T_BEGIN){
        while(Current_parse_token_type!=T_BEGIN){
            //keeps parsing until keyword begin is found
            valid_parse = parse_base_declaration();
            //Current_parse_token = Get_Valid_Token();
            //after any declaration type you need a semicolon
            if(Current_parse_token_type!=T_SEMICOLON){
                generate_error_report("Missing \";\" to complete declaration");
                return false;
            }
            if(!valid_parse){
                break;
            }
        }
    }
    else{
        Current_parse_token = Get_Valid_Token();
        valid_parse = parse_base_statement();
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
        //If the procedure has no parameters
        if(Next_parse_token_type == T_RPARAM){
            //sets valid_parse to true to allow parsing of procedure body
            valid_parse = true;
            Current_parse_token = Get_Valid_Token();
        }
        //else it is not a RPARAM, meaning there are parameters, meaning they need to be parsed. Or errors which will be detected later
        else{
        Current_parse_token = Get_Valid_Token();
        valid_parse = parse_parameter_list();
        //this if statement may not be needed
        if(valid_parse){
            Current_parse_token = Get_Valid_Token();
            valid_parse = parse_procedure_body();
        }
        }

    }
    else{
        generate_error_report("Missing \"(\" needed to for procedure declaration");
        return false;
    }
    //Current_parse_token = Get_Valid_Token();

    
    return valid_parse;

}

//likely will need debugging due to while loop
bool parser::parse_procedure_body(){
    bool valid_parse;
    //must be able to parse declarations until T_BEGIN is found
    while(Current_parse_token_type!=T_BEGIN){
        valid_parse = parse_base_declaration();
        //after every declaration there needds to be a semicolon
        if(Current_parse_token_type!=T_SEMICOLON){
            generate_error_report("Missing \";\" needed to end a declaration");
            return false;
        }
        else{
            Current_parse_token = Get_Valid_Token();
            if(Current_parse_token_type == T_BEGIN){
                Current_parse_token = Get_Valid_Token();
                break;
            }
        }

    }
    valid_parse = parse_statement();


    return valid_parse;
}

bool parser::parse_type_mark(){
    bool valid_parse;
    //May need to do something once the type is determined
    if(Current_parse_token_type == T_INTEGER_TYPE){
        Current_parse_token = Get_Valid_Token();
        valid_parse = true;
    }
    else if(Current_parse_token_type == T_FLOAT_TYPE){
        Current_parse_token = Get_Valid_Token();
        valid_parse = true;
    }
    else if(Current_parse_token_type == T_STRING_TYPE){
        Current_parse_token = Get_Valid_Token();
        valid_parse = true;
    }
    else if(Current_parse_token_type == T_BOOL_TYPE){
        Current_parse_token = Get_Valid_Token();
        valid_parse = true;
    }
    //I think this means that the this will be the same type as the identifier
    else if(Current_parse_token_type == T_IDENTIFIER){
        Current_parse_token = Get_Valid_Token();
        valid_parse = true;
    }
    else if(Current_parse_token_type == T_ENUM){
        Current_parse_token = Get_Valid_Token();
        if(Current_parse_token_type == T_LBRACE){
            Current_parse_token = Get_Valid_Token();
            if(Current_parse_token_type == T_LBRACE){
                Current_parse_token = Get_Valid_Token();
                if(Current_parse_token_type!=T_IDENTIFIER){
                    generate_error_report("Expected identifier as part of Enum");
                    return false;
                }
                else{
                    Current_parse_token = Get_Valid_Token();
                    //end of enumeration, contains one identifier
                    if(Current_parse_token_type == T_RBRACE){
                        valid_parse = true;
                    }
                    //else at least 2 identifiers exist in the enum
                    else{
                        //the first token has to be a comma
                        if(Current_parse_token_type == T_COMMA){
                            //The next token after a comma has to be an identifier
                            if(Next_parse_token_type == T_IDENTIFIER){
                                while(true){
                                    Current_parse_token = Get_Valid_Token();
                                    //if the next token after the identifer is a RBRACE, it is valid and end of the parse 
                                    if(Next_parse_token_type == T_RBRACE){
                                        Current_parse_token = Get_Valid_Token();
                                        return true;
                                    }
                                    //else it better be a comma
                                    else if(Next_parse_token_type == T_COMMA){
                                        Current_parse_token = Get_Valid_Token();
                                        //The current token is now a comma, so the next token needs to be an identifier
                                        if(Next_parse_token_type!=T_IDENTIFIER){
                                            generate_error_report("Missing expected identifier after comma in enumeration list");
                                            return false;

                                        }
                                    }
                                    //else it contains some other invalid token
                                    else{
                                        generate_error_report("Enumeration list must be either a comma or a identifier");
                                        return false;

                                    }

                                }
                            }

                        }
                        else{
                            generate_error_report("Missing expected comma for list of enumerations");
                            return false;
                        }
                    }
                }
            }
        }
    }
    return valid_parse;
}

//likely won't work on the first try
bool parser::parse_parameter_list(){
    bool valid_parse;

    valid_parse = parse_parameter();
    //if no errors from parsing the parameter
    if(valid_parse){
        Current_parse_token = Get_Valid_Token();
        ///meaning that there are more parameters
        if(Current_parse_token_type == T_COMMA){
            Current_parse_token = Get_Valid_Token();
            valid_parse = parse_parameter_list();

        }
        //there are no more parameters to parse
        else if(Current_parse_token_type == T_RPARAM){
            Current_parse_token = Get_Valid_Token();
            valid_parse = true;

        }
        //an invalid token was detected
        else{
            generate_error_report("Missing expected comma or parameter");
            return false;

        }
        

    }
        // bool parameter_needed = false;
        // while(true){
        //     valid_parse = parse_parameter();
        //     Current_parse_token = Get_Valid_Token();
        //     if(Current_parse_token_type == T_RPARAM){
        //         if(!parameter_needed){
        //             return true;
        //         }
        //         //trailing comma
        //         else{
        //             generate_error_report("Extra trailing comma occuring in parameter list");
        //             return false;
        //         }
        //     }

        // }
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


    return valid_parse;

}

bool parser::parse_variable_declaration(){
    bool valid_parse;
    if(Current_parse_token_type == T_IDENTIFIER){
        Current_parse_token = Get_Valid_Token();
    }
    else{
        generate_error_report("Missing identifier for variable declaration");
        return false;
    }
    if(Current_parse_token_type == T_COLON){
        Current_parse_token = Get_Valid_Token();
        valid_parse = parse_type_mark();
        if(valid_parse){
            Current_parse_token = Get_Valid_Token();
            //checks for optional bracket to declare an array
            if(Current_parse_token_type == T_LBRACKET){
                Current_parse_token = Get_Valid_Token();
                valid_parse = parse_bound();
            }
        }
    }
    else{
        generate_error_report("Missing colon for delcaration of variable type");
        return false;
    }


    return valid_parse;

}

//definitely ask professor about this 
bool parser::parse_bound(){
    bool valid_parse;
    //minus is an optional token
    if(Current_parse_token_type == T_MINUS){
        Current_parse_token = Get_Valid_Token();
    }
    valid_parse = parse_number();

    return valid_parse;
}

bool parser::parse_type_declaration(){
    bool valid_parse;
    //T_TYPE has already been parsed;  May need changed in the future
    if(Current_parse_token_type == T_IDENTIFIER){
        Current_parse_token = Get_Valid_Token();
    }
    else{
        generate_error_report("Missing required identifier for type declaration");
        return false;
    }
    if(Current_parse_token_type == T_IS){
        Current_parse_token = Get_Valid_Token();
    }
    else{
        generate_error_report("Missing required \"is\" for type declaration");
        return false;
    }
    valid_parse = parse_type_mark();

    return valid_parse;

}

bool parser::parse_base_statement(){
    bool valid_parse;

    return valid_parse;
}

bool parser::parse_parameter(){
    bool valid_parse;
    valid_parse = parse_variable_declaration();
    return valid_parse;
}

bool parser::parse_number(){
    bool valid_parse;
    //the token will be either an integer or a float, or and error
    if(Current_parse_token_type == T_INTEGER_TYPE){
        valid_parse = true;
        Current_parse_token = Get_Valid_Token();
    }
    else if(Current_parse_token_type == T_FLOAT_TYPE){
        valid_parse = true;
        Current_parse_token = Get_Valid_Token();
    }
    //not an integer, not a float, so is an error
    else{
        generate_error_report("Missing expected float or integer");
        return false;

    }
}