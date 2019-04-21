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
//ready for testing
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
    full_error_message = "Error on line " + std::to_string(Current_parse_token.line_found) + ':';
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
bool parser::parse_program(){
    bool valid_parse;
    valid_parse = parse_program_header();
    return valid_parse;
}

//ready for testing
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

//ready to test
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
        //the program body ends with "end program"
        //statements are technically optional
        while(Current_parse_token_type!=T_END && Next_parse_token_type!=T_PROGRAM){
            valid_parse = parse_base_statement();
            if(Current_parse_token_type!=T_SEMICOLON){
                generate_error_report("Missing \";\" to complete statement");
                return false;
            }
            if(!valid_parse){
                break;
            }
            //tokens "end" and "program" needed to end the program
            if(Current_parse_token_type == T_END && Next_parse_token_type == T_PROGRAM){
                break;
            }
        }
        //valid_parse = parse_base_statement();
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
    //must have left and right paretheses, parameters are optional
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
    //must have left and right paretheses, parameters are optional
    else{
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
    //after doen parsing any and all declarations, must start parsing statements
    valid_parse = parse_base_statement();

    return valid_parse;
}

//ready to test
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
//ready to test
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

//variable token is consumed to enter this function
//ready to test
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
//ready to test
bool parser::parse_bound(){
    bool valid_parse;
    //minus is an optional token
    if(Current_parse_token_type == T_MINUS){
        Current_parse_token = Get_Valid_Token();
    }
    valid_parse = parse_number();

    return valid_parse;
}

//token type is consumed to enter this function
//ready test
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

//not done
bool parser::parse_base_statement(){
    bool valid_parse;
    //an identifier means it will be an assignment statement
    if(Current_parse_token_type == T_IDENTIFIER){
        //Current_parse_token = Get_Valid_Token();
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
        generate_error_report("Invalid statmentl; Not an assignment, if, loop, or return");
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
    return valid_parse;
}

//ready to test
//already parsed an identifier token
bool parser::parse_assignment_statement(){
    bool valid_parse;
    Current_parse_token = Get_Valid_Token();
    valid_parse = parse_assignment_destination();
    if(valid_parse){
        if(Current_parse_token_type == T_COLON){
            Current_parse_token = Get_Valid_Token();
        }
        if(Current_parse_token_type == T_ASSIGN){
            Current_parse_token = Get_Valid_Token();
            valid_parse = parse_expression();
        }

    }
    //not a valid parse from parse assignment_destination
    else{
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
                if(Current_parse_token_type!=T_ELSE && Current_parse_token_type!=T_END){
                    while(Current_parse_token_type!=T_ELSE && Current_parse_token_type!=T_END){
                        valid_parse = parse_base_statement();
                        //required to have semicolon after parsing a statement
                        if(Current_parse_token_type!=T_SEMICOLON){
                            generate_error_report("Missing require \";\" after statement");
                            return false;
                        }
                        //else is a semicolon
                        else{

                        }
                    }
                    if(Current_parse_token_type == T_ELSE){
                        Current_parse_token = Get_Valid_Token();
                        while(Current_parse_token_type!=T_END){
                            valid_parse = parse_base_statement();
                            //required to have a semicolon after parsing a statement
                            if(Current_parse_token_type!=T_SEMICOLON){
                                generate_error_report("Missing require \";\" after statement");
                                return false;
                            }
                            //else is a semicolon
                            else{

                            }
                        }
                        if(Current_parse_token_type == T_END){
                            Current_parse_token = Get_Valid_Token();

                        }
                        else{
                            generate_error_report("Missing expected keyword \"end\" for end of statement");
                            return false;
                        }
                        if(Current_parse_token_type == T_IF){
                            Current_parse_token = Get_Valid_Token();
                        }
                        else{
                            generate_error_report("Missing expected keyword \"if\" for end of statement");
                            return false;
                        }
                        
                    }
                }
            }
            else{
                generate_error_report("Missing expected keyword \"then\" for if statements");
                return false;

            }
        }
        else{
            generate_error_report("Missing \")\" expected for if statment");
            return false;
        }
    }
    else{
        generate_error_report("Missing \"(\" expected for if statment");
        return false;
    }


    return valid_parse;
}


//ready to test
//consumes for token before entering this function
bool parser::parse_loop_statement(){
    bool valid_parse;
    if(Current_parse_token_type == T_LPARAM){
        //grabs what should be an identifier
        Current_parse_token = Get_Valid_Token();   
    }
    //needs to consume an identifier before parsing the assignment declaration
    if(Current_parse_token_type!=T_IDENTIFIER){
        valid_parse = parse_assignment_statement();
        Current_parse_token = Get_Valid_Token();
    }
    //missing required identifier for an assignment statement
    else{
        generate_error_report("Missing expeceted identifier for assignment statement");
        return false;
    }
    //required semicolon after assignment statement
    if(Current_parse_token_type == T_SEMICOLON){
        Current_parse_token = Get_Valid_Token();
        valid_parse = parse_expression();
        if(!valid_parse){
            generate_error_report("Missing expected expression for loop");
            return false;
        }
        else{
            Current_parse_token = Get_Valid_Token();
        }
    }
    Current_parse_token = Get_Valid_Token();
    if(Current_parse_token_type == T_RPARAM){
        Current_parse_token = Get_Valid_Token();
        while(Current_parse_token_type!=T_END){
            valid_parse = parse_base_statement();
            if(!valid_parse){
                generate_error_report("Missing expected valid statement inside for loop");
                return false;
            }
            else{
                Current_parse_token = Get_Valid_Token();
                if(Current_parse_token_type==T_SEMICOLON){
                    Current_parse_token = Get_Valid_Token();
                }
                else{
                    generate_error_report("Missing expected \";\" at the end of a statement");
                }
            }
        }
        if(Current_parse_token_type == T_END){
            Current_parse_token = Get_Valid_Token();
        }
        else{
            generate_error_report("Missing expected keyword \"end\" for end of statement");
            return false;
        }
        if(Current_parse_token_type == T_FOR){
            Current_parse_token = Get_Valid_Token();
        }
        else{
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
    Current_parse_token = Get_Valid_Token();
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
            generate_error_report("Missing closing right bracket to the identifier expression");
            return false;

        }
    }
    //optional bracket not there
    else{
        Current_parse_token = Get_Valid_Token();
        valid_parse = true;

    }
    return valid_parse;
}

//not done; Looks very hard
//consumes a token before entering this function
//all expressions start be thought to start with a ArithOp?
bool parser::parse_expression(){
    bool valid_parse;
    //if starts with not token, the next thing is arithOp
    if(Current_parse_token_type == T_NOT){
        Current_parse_token = Get_Valid_Token();
    }
    else{
        valid_parse = parse_arithOp();
    }
    //meaning that this is <expression>&<arithOp> rather than just <arithOp>
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
    else{

    }

    
    return valid_parse;
}

//not done; Looks very hard
//consumes a token before entering this function
//all arithOps can be thought to starts with relations?
bool parser::parse_arithOp(){
    bool valid_parse;
    //Current_parse_token = Get_Valid_Token();
    valid_parse = parse_relation();
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

    }

    
    return valid_parse;
}

//not done; Looks very hard
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
            generate_error_report("\"=\" is not a valid relational operator");
            return false;
        }
        else{
            valid_parse = parse_term();
        }
    }
    else if(Current_parse_token_type = T_EXCLAM){
        Current_parse_token = Get_Valid_Token();
        if(Current_parse_token_type!=T_ASSIGN){
            generate_error_report("Invalid relational operator detected");
        }
    }
    //else something else not expected
    else{

    }

    return valid_parse;
}

bool parser::parse_term(){
    bool valid_parse;


    return valid_parse;
}
bool parser::parse_name(){
    bool valid_parse;

    return valid_parse;
}

bool parser::parse_argument_list(){
    bool valid_parse;

    return valid_parse;
}