#include "parser.h"
//ready for testing
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
//ready for testing; May have issues at the end of the program
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



//ready for testing
void parser::add_error_report(std::string error_report){
    error_reports.push_back(error_report);
}

//ready for testing
void parser::generate_error_report(std::string error_message){
    std::string full_error_message = "";
    // if(Current_parse_token.first_token_on_line){
    //     full_error_message = "Error on line " + std::to_string(Current_parse_token.line_found-1) + ": ";
    // }
    // else{
        full_error_message = "Error on line " + std::to_string(Current_parse_token.line_found) + ": ";
    // }
    full_error_message = full_error_message + error_message;
    add_error_report(full_error_message);
}

//ready for testing
void parser::print_errors(){
    for(int i = 0; i < error_reports.size(); i++){
        std::cout<<error_reports[i]<<std::endl;
    }
}

//ready for testing
//refactored 1 time
bool parser::parse_program(){
    bool valid_parse;
    valid_parse = parse_program_header();
    valid_parse = parse_program_body();
    if(Current_parse_token_type == T_PERIOD){
        valid_parse = true;
    }
    else{
        generate_error_report("Missing \".\" to end the program");
        valid_parse = false;
    }
    if(debugging && !valid_parse){
        std::cout<<"parser failed on parse_proram()"<<std::endl;
    }
    return valid_parse;
}

//ready for testing
//refactored 1
bool parser::parse_program_header(){
    bool valid_parse;
    Current_parse_token = Get_Valid_Token();
    if(Current_parse_token_type == T_PROGRAM){
        valid_parse = true;
        Current_parse_token = Get_Valid_Token();
    }
    else{
        generate_error_report("Expected keyword \"Program\" not found");
        if(debugging){
            std::cout<<"parser failed on parse_program_header()"<<std::endl;
        }
        return false;
    }
    if(Current_parse_token_type == T_IDENTIFIER){
        valid_parse = true;
        Current_parse_token = Get_Valid_Token();
    }
    else{
        generate_error_report("Expected \"identifier\" not found");
        if(debugging){
            std::cout<<"parser failed on parse_program_header()"<<std::endl;
        }
        return false;
    }  
    if(Current_parse_token_type == T_IS){
        valid_parse = true;
        Current_parse_token = Get_Valid_Token();
    }
    else{
        generate_error_report("Expected keyword \"is\" is not found");
        if(debugging){
            std::cout<<"parser failed on parse_program_header()"<<std::endl;
        }
        return false;
    }

    return valid_parse;


}

//ready to test
//refactored 1 time
bool parser::parse_program_body(){
    bool valid_parse;
        //keeps parsing until keyword begin is found
        while(Current_parse_token_type!=T_BEGIN){
            valid_parse = parse_base_declaration();
            //after any declaration type you need a semicolon
            if(Current_parse_token_type!=T_SEMICOLON){
                if(debugging){
                    std::cout<<"parser failed on parse_program_body()"<<std::endl;
                 }
                generate_error_report("Missing \";\" to complete declaration");
                return false;
            }
            if(!valid_parse){
                break;  
            }
            //ADDED ON 4/21
            Current_parse_token = Get_Valid_Token();

        }
        //once the begin token is recieved
        if(Current_parse_token_type == T_BEGIN){
            Current_parse_token = Get_Valid_Token();
            while(Current_parse_token_type!=T_END){
                valid_parse = parse_base_statement();
                if(Current_parse_token_type!=T_SEMICOLON){
                    generate_error_report("Missing \";\" to end program statement");
                    return false;
                }
                else{
                    Current_parse_token = Get_Valid_Token();
                }
            }
            //once the end token is recieved
            Current_parse_token = Get_Valid_Token();
            if(Current_parse_token_type == T_PROGRAM){
                Current_parse_token = Get_Valid_Token();
            }
            else{
                generate_error_report("Missing keyworkd \"program\" to end program");
                return false;
            }
        }
        else{
            generate_error_report("Missing keyword \"begin\" to begin program statements");
            return false;
        }
    return valid_parse;
}

//ready to test
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
        if(debugging){
            std::cout<<"parser failed on parse_base_declaration()"<<std::endl;
        }
        generate_error_report("Expected keywords \"procedure\",\"variable\", or \"type\" not found");
        return false;
    }
    
    return valid_parse;

}

//ready to test
//the token procedure is used to enter this function
bool parser::parse_procedure_header(){
    bool valid_parse;
    if(Current_parse_token_type == T_IDENTIFIER){
        Current_parse_token = Get_Valid_Token();
    }
    else{
        if(debugging){
            std::cout<<"parser failed on parse_procedure_header()"<<std::endl;
        }
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
    //must have left and right paretheses, parameters are optional
    if(Current_parse_token_type == T_LPARAM){
        //If the procedure has no parameters
        if(Next_parse_token_type == T_RPARAM){
            //sets valid_parse to true to allow parsing of procedure body
            valid_parse = true;
            Current_parse_token = Get_Valid_Token();
            Current_parse_token = Get_Valid_Token();
        }
        //else it is not a RPARAM, meaning there are parameters, meaning they need to be parsed. Or errors which will be detected later
        else{
        Current_parse_token = Get_Valid_Token();
        valid_parse = parse_parameter_list();
        }
        //this if statement may not be needed
        if(valid_parse){
            //COMMENTED OUT 4/22
            //Current_parse_token = Get_Valid_Token();
            valid_parse = parse_procedure_body();
        }
        

    }
    //must have left and right paretheses, parameters are optional
    else{
        if(debugging){
            std::cout<<"parser failed on parse_procedure_header()"<<std::endl;
        }
        generate_error_report("Missing \"(\" needed to for procedure declaration");
        return false;
    }
    //Current_parse_token = Get_Valid_Token();

    return valid_parse;

}

//likely will need debugging due to while loop
//ready to test
bool parser::parse_procedure_body(){
    bool valid_parse;
    //must be able to parse declarations until T_BEGIN is found
    while(Current_parse_token_type!=T_BEGIN){
        valid_parse = parse_base_declaration();
        //after every declaration there has to be a semicolon
        if(Current_parse_token_type!=T_SEMICOLON){
            if(debugging){
                std::cout<<"parser failed on parse_procedure_body()"<<std::endl;
            }
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
    //after doen parsing any and all declarations, must start parsing statements\
    //need to parse more than one base statement
    //add this if statement in the case that never enters while loop
    if(Current_parse_token_type == T_BEGIN){
        Current_parse_token = Get_Valid_Token();
    }
    while(Current_parse_token_type!=T_END){
        valid_parse = parse_base_statement();
        if(Current_parse_token_type == T_SEMICOLON){
            Current_parse_token = Get_Valid_Token();
        }
        else{
            generate_error_report("Missing \";\" to end statement");
            return false;
        }
    }
    if(Current_parse_token_type == T_END){
        Current_parse_token = Get_Valid_Token();
        if(Current_parse_token_type == T_PROCEDURE){
            Current_parse_token = Get_Valid_Token();
        }
        else{
            generate_error_report("Missing keyword \"procedure\" to close procedure body");
            return false;
        }
    }

    return valid_parse;
}

//ready to test
//enum needs tested
//write test prog for this
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
                    if(debugging){
                        std::cout<<"parser failed on parse_type_mark()"<<std::endl;
                    }
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
                                            if(debugging){
                                                std::cout<<"parser failed on parse_type_mark()"<<std::endl;
                                            }
                                            generate_error_report("Missing expected identifier after comma in enumeration list");
                                            return false;

                                        }
                                    }
                                    //else it contains some other invalid token
                                    else{
                                        if(debugging){
                                            std::cout<<"parser failed on parse_type_mark()"<<std::endl;
                                        }
                                        generate_error_report("Enumeration list must be either a comma or a identifier");
                                        return false;

                                    }

                                }
                            }

                        }
                        else{
                            if(debugging){
                                std::cout<<"parser failed on parse_type_mark()"<<std::endl;
                            }
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


//ready to test
//refactored 1 time
bool parser::parse_parameter_list(){
    bool valid_parse;
    //gets identifier token since it must be a variable, then parses variable
    Current_parse_token = Get_Valid_Token();
    valid_parse = parse_parameter();
    //if no errors from parsing the parameter
    if(valid_parse){
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
            if(debugging){
                std::cout<<"parser failed on parse_parameter_list()"<<std::endl;
            }
            generate_error_report("Missing expected comma or parameter");
            return false;
        }
    }
    return valid_parse;
}

//variable token is consumed to enter this function
//ready to test
//refactored 1 time
bool parser::parse_variable_declaration(){
    bool valid_parse;
    if(Current_parse_token_type == T_IDENTIFIER){
        Current_parse_token = Get_Valid_Token();
    }
    else{
        if(debugging){
            std::cout<<"parser failed on parse_variable_declaration()"<<std::endl;
        }
        generate_error_report("Missing identifier for variable declaration");
        return false;
    }
    if(Current_parse_token_type == T_COLON){
        Current_parse_token = Get_Valid_Token();
        valid_parse = parse_type_mark();
        if(valid_parse){
            //checks for optional bracket to declare an array
            if(Current_parse_token_type == T_LBRACKET){
                Current_parse_token = Get_Valid_Token();
                valid_parse = parse_bound();
                if(Current_parse_token_type == T_RBRACKET){
                    Current_parse_token = Get_Valid_Token();
                }
                //must have closing right bracket
                else{
                    generate_error_report("Missing \"]\" to close the array declaration");
                    return false;
                }
            }
        }
    }
    else{
        if(debugging){
            std::cout<<"parser failed on parse_variable_declaration()"<<std::endl;
        }
        generate_error_report("Missing colon for delcaration of variable type");
        return false;
    }


    return valid_parse;

}


//ready to test
//refactored 1 time
bool parser::parse_bound(){
    bool valid_parse;
    valid_parse = parse_number();
    if(!valid_parse){
        generate_error_report("Missing expected number");
    }
    return valid_parse;
}

//token type is consumed to enter this function
//ready test
//write test prog for
bool parser::parse_type_declaration(){
    bool valid_parse;
    //T_TYPE has already been parsed;  May need changed in the future
    if(Current_parse_token_type == T_IDENTIFIER){
        Current_parse_token = Get_Valid_Token();
    }
    else{
        if(debugging){
            std::cout<<"parser failed on parse_type_declaration()"<<std::endl;
        }
        generate_error_report("Missing required identifier for type declaration");
        return false;
    }
    if(Current_parse_token_type == T_IS){
        Current_parse_token = Get_Valid_Token();
    }
    else{
        if(debugging){
            std::cout<<"parser failed on parse_type_declaration()"<<std::endl;
        }
        generate_error_report("Missing required \"is\" for type declaration");
        return false;
    }
    valid_parse = parse_type_mark();

    return valid_parse;

}

//ready to test
//refactored 1 time
bool parser::parse_base_statement(){
    bool valid_parse;
    //an identifier means it will be an assignment statement
    if(Current_parse_token_type == T_IDENTIFIER){
        Current_parse_token = Get_Valid_Token();
        valid_parse = parse_assignment_statement();
    }
    else if(Current_parse_token_type == T_IF){
        Current_parse_token = Get_Valid_Token();
        valid_parse = parse_if_statement();
    }
    else if(Current_parse_token_type == T_FOR){
        Current_parse_token = Get_Valid_Token();
        valid_parse = parse_loop_statement();
    }
    else if(Current_parse_token_type == T_RETURN){
        Current_parse_token = Get_Valid_Token();
        valid_parse = parse_return_statement();
    }
    else{
        if(debugging){
            std::cout<<"parser failed on parse_base_statement()"<<std::endl;
        }
        generate_error_report("Invalid statement; Not an assignment, if, loop, or return");
        return false;
    }

    return valid_parse;
}

//ready to test
bool parser::parse_parameter(){
    bool valid_parse;
    valid_parse = parse_variable_declaration();
    return valid_parse;
}

//ready to test
bool parser::parse_number(){
    bool valid_parse;
    //the token will be either an integer or a float, or and error
    if(Current_parse_token_type == T_INTEGER_VALUE){
        valid_parse = true;
        Current_parse_token = Get_Valid_Token();
    }
    else if(Current_parse_token_type == T_FLOAT_VALUE){
        valid_parse = true;
        Current_parse_token = Get_Valid_Token();
    }
    //not an integer, not a float, so is an error
    else{
        if(debugging){
            std::cout<<"parser failed on parse_number()"<<std::endl;
        }
        generate_error_report("Missing expected float or integer");
        return false;

    }
    return valid_parse;
}

//ready to test
//consumes an identifer before parsing
//refactored 1 time
bool parser::parse_assignment_statement(){
    bool valid_parse;
    valid_parse = parse_assignment_destination();
    if(valid_parse){
        //MAY BE A BUG HERE, SHOULD THROW ERROR?
        if(Current_parse_token_type == T_COLON){
            Current_parse_token = Get_Valid_Token();
        }
        else{
            generate_error_report("Missing \":\" needed for assignment statement");
            return false;
        }
        if(Current_parse_token_type == T_ASSIGN){
            Current_parse_token = Get_Valid_Token();
            valid_parse = parse_expression();
        }
        else{
            generate_error_report("Missing \"=\" needed for assignment statement");
            return false;
        }

    }
    //not a valid parse from parse assignment_destination
    else{
        std::cout<<"parser failed on parse_assignment_destination()"<<std::endl;
        return valid_parse;

    }

    return valid_parse;
}

//ready for testing
//consumes if token before parsing
bool parser::parse_if_statement(){
    bool valid_parse;
    if(Current_parse_token_type == T_LPARAM){
        Current_parse_token = Get_Valid_Token();
        valid_parse = parse_expression();
        if(Current_parse_token_type == T_RPARAM){
            Current_parse_token = Get_Valid_Token();
            if(Current_parse_token_type == T_THEN){
                Current_parse_token = Get_Valid_Token();
                //may need to remove this if statement
                    while(Current_parse_token_type!=T_ELSE && Current_parse_token_type!=T_END){
                        valid_parse = parse_base_statement();
                        //required to have semicolon after parsing a statement
                        if(Current_parse_token_type!=T_SEMICOLON){
                            if(debugging){
                                std::cout<<"parser failed on parse_if_statement()"<<std::endl;
                            }
                            generate_error_report("Missing require \";\" after statement");
                            return false;
                        }
                        //else is a semicolon
                        else{
                            //ADDED ON 4/23
                            Current_parse_token = Get_Valid_Token();

                        }
                    }
                    //once the else token is recieved
                    if(Current_parse_token_type == T_ELSE){
                        Current_parse_token = Get_Valid_Token();
                        while(Current_parse_token_type!=T_END){
                            valid_parse = parse_base_statement();
                            //required to have a semicolon after parsing a statement
                            if(Current_parse_token_type!=T_SEMICOLON){
                                if(debugging){
                                    std::cout<<"parser failed on parse_if_statement()"<<std::endl;
                                }
                                generate_error_report("Missing require \";\" after statement");
                                return false;
                            }
                            //else is a semicolon
                            else{
                                Current_parse_token = Get_Valid_Token();
                            }
                        }
                        if(Current_parse_token_type == T_END){
                            Current_parse_token = Get_Valid_Token();

                        }
                        else{
                            if(debugging){
                                std::cout<<"parser failed on parse_if_statement()"<<std::endl;
                            }
                            generate_error_report("Missing expected keyword \"end\" for end of statement");
                            return false;
                        }
                        if(Current_parse_token_type == T_IF){
                            Current_parse_token = Get_Valid_Token();
                        }
                        else{
                            if(debugging){
                                std::cout<<"parser failed on parse_if_statement()"<<std::endl;
                            }
                            generate_error_report("Missing expected keyword \"if\" for end of statement");
                            return false;
                        }
                        
                    }
                    else if(Current_parse_token_type == T_END){
                        Current_parse_token = Get_Valid_Token();
                        if(Current_parse_token_type == T_IF){
                            Current_parse_token = Get_Valid_Token();
                            //end of if statement
                            //set valid_parse to true?
                            valid_parse = true;
                        }
                        else{
                            generate_error_report("Missing \"if\" to complete \"end if\" to end the if statement");
                            return false;
                        }

                    }
                //NEED TO INCLDUE ELSE FOR IF STATEMENT WITH NO STATEMENTS
            }
            else{
                if(debugging){
                    std::cout<<"parser failed on parse_if_statement()"<<std::endl;
                }
                generate_error_report("Missing expected keyword \"then\" for if statements");
                return false;

            }
        }
        else{
            if(debugging){
                std::cout<<"parser failed on parse_if_statement()"<<std::endl;
            }
            generate_error_report("Missing \")\" expected for if statment");
            return false;
        }
    }
    else{
        if(debugging){
            std::cout<<"parser failed on parse_if_statement()"<<std::endl;
        }
        generate_error_report("Missing \"(\" expected for if statment");
        return false;
    }


    return valid_parse;
}


//ready to test
//consumes for token before entering this function
//refactored 1 time
bool parser::parse_loop_statement(){
    bool valid_parse;
    if(Current_parse_token_type == T_LPARAM){
        //grabs what should be an identifier
        Current_parse_token = Get_Valid_Token();   
    }
    else{
        generate_error_report("Missing \"(\" required for loop");
        return false;
    }
    //needs to consume an identifier before parsing the assignment declaration
    if(Current_parse_token_type == T_IDENTIFIER){
        Current_parse_token = Get_Valid_Token();
        valid_parse = parse_assignment_statement();
        if(Current_parse_token_type != T_SEMICOLON){
            generate_error_report("Missing \";\" for loop assignment statement");
            return false;
        }
        else{
            Current_parse_token = Get_Valid_Token();
        }
        
    }
    //missing required identifier for an assignment statement
    else{
        if(debugging){
            std::cout<<"parser failed on parse_loop_statement()"<<std::endl;
        }
        generate_error_report("Missing expeceted identifier for assignment statement");
        return false;
    }
    //required semicolon after assignment statement
    //if(Current_parse_token_type == T_SEMICOLON){
        //Current_parse_token = Get_Valid_Token();
        valid_parse = parse_expression();
        if(!valid_parse){
            if(debugging){
                std::cout<<"parser failed on parse_loop_statement()"<<std::endl;
            }
            generate_error_report("Missing expected expression for loop");
            return false;
        }
        else{
            //COMMENTED OUT 5/5
            //Current_parse_token = Get_Valid_Token();
        }
    //}
    //COMMENTED OUT 5/5
    //Current_parse_token = Get_Valid_Token();
    if(Current_parse_token_type == T_RPARAM){
        Current_parse_token = Get_Valid_Token();
        while(Current_parse_token_type!=T_END){
            valid_parse = parse_base_statement();
            if(!valid_parse){
                if(debugging){
                    std::cout<<"parser failed on parse_loop_statement()"<<std::endl;
                }
                generate_error_report("Missing expected valid statement inside for loop");
                return false;
            }
            if(Current_parse_token_type != T_SEMICOLON){
                generate_error_report("Missing \";\" for end statement inside of loop");
                return false;
            }
            //else is semicolon
            else{
                Current_parse_token = Get_Valid_Token();
            }

        }
        //checks for ending two tokens
        if(Current_parse_token_type == T_END){
            Current_parse_token = Get_Valid_Token();
        }
        else{
            if(debugging){
                std::cout<<"parser failed on parse_loop_statement()"<<std::endl;
            }
            generate_error_report("Missing expected keyword \"end\" for end of statement");
            return false;
        }
        if(Current_parse_token_type == T_FOR){
            Current_parse_token = Get_Valid_Token();
        }
        else{
            if(debugging){
                std::cout<<"parser failed on parse_loop_statement()"<<std::endl;
            }
            generate_error_report("Missing expected keyword \"for\" for end of statement");
            return false;
        }
    }

    return valid_parse;
}

//ready to test
//consumes return token before entering function
bool parser::parse_return_statement(){
    bool valid_parse;
    //COMMENTED OUT 4/23
    //Current_parse_token = Get_Valid_Token();
    valid_parse = parse_expression();
    return valid_parse;
}


//ready to test
//already consumes identifier before parsing
bool parser::parse_assignment_destination(){
    bool valid_parse;
    //this means that the optional bracketed expression should exist
    if(Current_parse_token_type == T_LBRACKET){
        Current_parse_token = Get_Valid_Token();
        valid_parse = parse_expression();
        //after parsing the expression, it should have a right bracket
        if(Current_parse_token_type == T_RBRACKET){
            Current_parse_token = Get_Valid_Token();

        }
        //required right bracket missing
        else{
            if(debugging){
                std::cout<<"parser failed on parse_assignment_destination()"<<std::endl;
            }
            generate_error_report("Missing closing right bracket to the identifier expression");
            return false;

        }
    }
    //optional bracket not there
    else{
        //REMOVED 5/1
        //Current_parse_token = Get_Valid_Token();
        valid_parse = true;

    }
    return valid_parse;
}

//ready to test
//consumes a token before entering this function
//all expressions start be thought to start with a ArithOp?
bool parser::parse_expression(){
    bool valid_parse;

    if(Current_parse_token_type == T_AMPERSAND){
        Current_parse_token = Get_Valid_Token();
        valid_parse = parse_arithOp();
    }
    //meaning that this is <expression>|<arithOp> rather than just <arithOp>
    else if(Current_parse_token_type == T_VERTICAL_BAR){
        Current_parse_token = Get_Valid_Token();
        valid_parse = parse_arithOp();
    }
    //else it was just an <arithOp>
    else if(Current_parse_token_type == T_NOT){
        Current_parse_token = Get_Valid_Token();
        valid_parse = parse_arithOp();
    }
    else{
        //Current_parse_token = Get_Valid_Token();
        valid_parse = parse_arithOp();
    }
    //check to see if expression continues with another Arithop
    if(valid_parse){
        if(Current_parse_token_type == T_AMPERSAND || Current_parse_token_type == T_VERTICAL_BAR){
            if(Current_parse_token_type == T_AMPERSAND){
                //code generation different here than a pipe
            }
            else if(Current_parse_token_type == T_VERTICAL_BAR){
                //code generation different here thana pipe
            }
            else{
                //not valid ever?
            }
            Current_parse_token = Get_Valid_Token();
            valid_parse = parse_arithOp();
        }
    }

    
    return valid_parse;
}

//ready to test
//consumes a token before entering this function
//all arithOps can be thought to starts with relations?
bool parser::parse_arithOp(){
    bool valid_parse;
    //Current_parse_token = Get_Valid_Token();

    //meaning that this is <arithOp>+<relation> rather than just <relation>
    if(Current_parse_token_type == T_PLUS){
        Current_parse_token = Get_Valid_Token();
        valid_parse = parse_relation();
    }
    //meaning that this is <arithOp>-<relation> rather than just <relation>
    else if(Current_parse_token_type == T_MINUS){
        Current_parse_token = Get_Valid_Token();
        valid_parse = parse_relation();
    }
    //else if was just a <relation>
    else{
        //Current_parse_token = Get_Valid_Token();
        valid_parse = parse_relation();
    }
    //allows parsing of relation again
    if(valid_parse){
        if(Current_parse_token_type == T_PLUS || Current_parse_token_type == T_MINUS){
            if(Current_parse_token_type == T_PLUS){
                //code generation stuff here probably different than T_MINUS
            }
            else if(Current_parse_token_type == T_MINUS){
                //code generation stuff here probably different than T_PLUS
            }
            Current_parse_token = Get_Valid_Token();
            valid_parse = parse_relation();
        }
        //else not adding or subtracting and is just a relation
        else{
            //nothing ever?

        }
    }

    
    return valid_parse;
}

//ready to test
//consumes a token before entering this function
//all arithOps can be thought to starts with terms?
bool parser::parse_relation(){
    bool valid_parse;
    
    if(Current_parse_token_type == T_LESS){
        Current_parse_token = Get_Valid_Token();
        //meaning less than or equal to
        if(Current_parse_token_type == T_ASSIGN){
            Current_parse_token = Get_Valid_Token();
            valid_parse = parse_term();
        }
        //else just less than
        else{
            valid_parse = parse_term();
        }
        
    }
    else if(Current_parse_token_type == T_GREATER){
        Current_parse_token = Get_Valid_Token();
        //greater than or equal to
        if(Current_parse_token_type == T_ASSIGN){
            Current_parse_token = Get_Valid_Token();
            valid_parse = parse_term();
        }
        //else just greateer than
        else{
            valid_parse = parse_term();
        }

    }
    else if(Current_parse_token_type == T_ASSIGN){
        Current_parse_token = Get_Valid_Token();
        if(Current_parse_token_type!=T_ASSIGN){
            if(debugging){
                std::cout<<"parser failed on parse_relation()"<<std::endl;
            }
            generate_error_report("\"=\" is not a valid relational operator, did you mean \"==\"");
            return false;
        }
        else{
            valid_parse = parse_term();
        }
    }
    else if(Current_parse_token_type == T_EXCLAM){
        Current_parse_token = Get_Valid_Token();
        if(Current_parse_token_type!=T_ASSIGN){
            if(debugging){
                std::cout<<"parser failed on parse_relation()"<<std::endl;
            }
            generate_error_report("Invalid relational operator detected");
        }
    }
    //else just a term
    else{
        //COMMENTED OUT 4/22
        //Current_parse_token = Get_Valid_Token();
        valid_parse = parse_term();
        if(valid_parse){
            if(Current_parse_token_type == T_LESS || Current_parse_token_type == T_GREATER || Current_parse_token_type == T_ASSIGN || Current_parse_token_type == T_EXCLAM){
                Current_parse_token = Get_Valid_Token();
                if(Current_parse_token_type == T_ASSIGN){
                    Current_parse_token = Get_Valid_Token();
                    valid_parse = parse_term();
                }
                else{
                    valid_parse = parse_term();
                }
            }
        }

    }

    return valid_parse;
}

//ready to test
//already consumes a token before being parsed
bool parser::parse_term(){
    bool valid_parse;
    if(Current_parse_token_type == T_MULT){
        Current_parse_token = Get_Valid_Token();
        valid_parse = parse_factor();
    }
    else if(Current_parse_token_type == T_SLASH){
        Current_parse_token = Get_Valid_Token();
        valid_parse = parse_factor();
    }
    //else is just a factor
    else{
        valid_parse = parse_factor();
        //ADDED 4/22
        if(valid_parse){
            if(Current_parse_token_type == T_MULT || Current_parse_token_type == T_SLASH){
                if(Current_parse_token_type == T_MULT){
                    //code generation probaly different here than division
                }
                else if(Current_parse_token_type == T_SLASH){
                    //code generation probably different here than multiplication
                }
                Current_parse_token = Get_Valid_Token();
                valid_parse = parse_factor();
            }
        }
    }



    return valid_parse;
}


//not done
//already consumes a token before being parsed
bool parser::parse_factor(){
    bool valid_parse;
    if(Current_parse_token_type == T_LPARAM){
        valid_parse = parse_expression();
        Current_parse_token = Get_Valid_Token();
        if(Current_parse_token_type == T_RPARAM){
            Current_parse_token = Get_Valid_Token();
        }
        else{
            if(debugging){
                std::cout<<"parser failed on parse_factor()"<<std::endl;
            }
            generate_error_report("Missing \")\" to close expresssion factor");
            return false;
        }
    }
    //This means that this is either a procedure call or a name
    else if(Current_parse_token_type == T_IDENTIFIER){
        //this means that it is a procedure call
        if(Next_parse_token_type == T_LPARAM){
            Current_parse_token = Get_Valid_Token();
            valid_parse = parse_procedure_call();
        }
        //else it must be a name
        else{
            //COMMENTED OUT 4/22
            Current_parse_token = Get_Valid_Token();
            valid_parse = parse_name();
        }
    }
    //this means that it must be either a name or a number
    else if(Current_parse_token_type == T_MINUS){
        Current_parse_token = Get_Valid_Token();
        //means that it isn't a name
        if(Current_parse_token_type == T_INTEGER_TYPE || Current_parse_token_type == T_FLOAT_TYPE){
            Current_parse_token = Get_Valid_Token();
        }
        //else it is a name
        else if(Current_parse_token_type == T_IDENTIFIER){
            Current_parse_token = Get_Valid_Token();
            valid_parse = parse_name();
        }
        //else it had a negative sign, it isn't a number, and it isn't a name
        else{
            if(debugging){
                std::cout<<"parser failed on parse_factor()"<<std::endl;
            }
            generate_error_report("Unexpected negative factor is not a name or a number");
            return false;
        }
    }
    //else is a non negative number
    else if(Current_parse_token_type == T_INTEGER_VALUE || Current_parse_token_type == T_FLOAT_VALUE){
        Current_parse_token = Get_Valid_Token();
        //type checking will need to be done here?
        return true;
    }
    else if(Current_parse_token_type == T_STRING_VALUE){
        Current_parse_token = Get_Valid_Token();
        //type checking will need to be done here?
        return true;
    }
    else if(Current_parse_token_type == T_TRUE){
        Current_parse_token = Get_Valid_Token();
        //type checking will need to be done here?
        return true;
    }
    else if(Current_parse_token_type == T_FALSE){
        Current_parse_token =Get_Valid_Token();
        //type checking will need to be done here?
        return true;
    }
    //else nothing valid was seen
    else{
        if(debugging){
            std::cout<<"parser failed on parse_factor()"<<std::endl;
        }
        generate_error_report("Invalid token for factor discovered");
        return false;
    }

    return valid_parse;
}

//not done
//already consumes indentifier token before being parsed
bool parser::parse_name(){
    bool valid_parse;
    if(Current_parse_token_type == T_LBRACKET){
        Current_parse_token = Get_Valid_Token();
        valid_parse = parse_expression();
        if(Current_parse_token_type == T_RBRACKET){
            Current_parse_token =Get_Valid_Token();
        }
        //missing right bracket for the end of an optional expression
        else{
            if(debugging){
                std::cout<<"parser failed on parse_name()"<<std::endl;
            }
            generate_error_report("Missing require \"]\" for the end of optional expression for name");
            return false;
        }
    }
    //the optional expression does not exist do nothing
    else{
        //Current_parse_token = Get_Valid_Token();
        return true;
    }

    return valid_parse;
}

//not done
//consumes one token before starting
bool parser::parse_argument_list(){
    bool valid_parse;
    valid_parse = parse_expression();
    if(valid_parse){
        if(Current_parse_token_type == T_COMMA){
            valid_parse = parse_argument_list();
        }
    }

    return valid_parse;
}

//not done
//already consumes identifier token before parsing
bool parser::parse_procedure_call(){
    bool valid_parse;
    if(Current_parse_token_type == T_LPARAM){
        Current_parse_token = Get_Valid_Token();
        //if has no parameters
        if(Current_parse_token_type == T_RPARAM){
            valid_parse = true;
            Current_parse_token = Get_Valid_Token();
        }
        else{
            valid_parse = parse_argument_list();
            if(Current_parse_token_type == T_RPARAM){
                Current_parse_token = Get_Valid_Token();
            }
            //missing needed right param for the end of a procedure call
            else{
                if(debugging){
                    std::cout<<"parser failed on parse_procedure_call()"<<std::endl;
                }
                generate_error_report("Missing required \")\" for the end of a procedure call");
                return false;
                }
        }

    }
    //missing needed left param for the start of the procedure call
    else{
        if(debugging){
            std::cout<<"parser failed on parse_procedure_call()"<<std::endl;
        }
        generate_error_report("Missing required \"(\" for the end of a procedure call");
        return false;
    }

    return valid_parse;
}