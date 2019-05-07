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
    print_errors();

}


//ready for testing; May have issues at the end of the program
token parser::Get_Valid_Token(){
    token first_token;
    //if first token in the list;  may need a similar condition for the last token
    if(Current_parse_token.type == 9999){
        Current_parse_token = Lexer->Get_token();
        Next_parse_token = Lexer->Get_token();
        Current_parse_token_type = Current_parse_token.type;
        Next_parse_token_type = Next_parse_token.type;
        
    }
    //if not the first token
    else{
        prev_token = Current_parse_token;
        prev_token_type = Current_parse_token_type;
        Current_parse_token = Next_parse_token;
        Current_parse_token_type = Next_parse_token_type;
        Next_parse_token = Lexer->Get_token();
        Next_parse_token_type = Next_parse_token.type;
        //for some reason random junk sometimes appears
        while(Next_parse_token_type>T_INVALID || Next_parse_token_type<0){
        Next_parse_token = Lexer->Get_token();
        Next_parse_token_type = Next_parse_token.type;
        }
    }
    //for some reason junk is somtimes recieved
    // if (Current_parse_token_type>T_INVALID || Current_parse_token_type<0){
    //     Current_parse_token = Get_Valid_Token();
    // }
    // if(Next_parse_token_type>T_INVALID || Next_parse_token_type < 0){

    // }

    return Current_parse_token;
}


//ready for testing
void parser::add_error_report(std::string error_report){
    error_reports.push_back(error_report);
}

//ready for testing
void parser::generate_error_report(std::string error_message){
    std::string full_error_message = "";
    if(!resync_status){
        if(Current_parse_token.first_token_on_line){
            full_error_message = "Error on line " + std::to_string(prev_token.line_found) + ": ";
        }
        else{
            full_error_message = "Error on line " + std::to_string(Current_parse_token.line_found) + ": ";
        }
        full_error_message = full_error_message + error_message;
        add_error_report(full_error_message);
    }
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
    //this tracks the state of the parser
    parser_state state = S_PROGRAM;
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
    //resync_parser(state);
    return valid_parse;
}

//ready for testing
//refactored 1
bool parser::parse_program_header(){
    parser_state state = S_PROGRAM_HEADER;
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
    //this tracks the state of the parser
    parser_state state = S_PROGRAM_BODY;
    bool valid_parse;
        //keeps parsing until keyword begin is found
        while(Current_parse_token_type!=T_BEGIN){
            valid_parse = parse_base_declaration();
            if(valid_parse){
                if(Current_parse_token_type == T_SEMICOLON){
                    Current_parse_token = Get_Valid_Token();
                }
                else{
                    generate_error_report("Missing \";\" to complete declaration");
                    valid_parse = resync_parser(state);
                }
            }
            //else parse_base_declaration failed
            else{
                if(debugging){
                    std::cout<<"parser failed on parse_program_body()"<<std::endl;
                 }
                valid_parse = resync_parser(state);
            }
        }
        // if(!valid_parse){
        //     resync_parser(state);
        // }
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
//refactored 1 time
bool parser::parse_base_declaration(){
    //this tracks the state of the parser
    parser_state state = S_BASE_DECLARATION;
    bool valid_parse;
    if(Current_parse_token_type == T_GLOBAL){
        //Do work for global declarations here
        Current_parse_token = Get_Valid_Token();
    }
    if(Current_parse_token_type == T_PROCEDURE){
        Current_parse_token = Get_Valid_Token();
        valid_parse = parse_procedure_declaration();

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


bool parser::parse_procedure_declaration(){
    //this tracks the state of the parser
    parser_state state = S_PROCEDURE_DECLARATION;
    bool valid_parse;
    valid_parse = parse_procedure_header();
    valid_parse = parse_procedure_body();
    return valid_parse;
}


//ready to test
//the token procedure is used to enter this function
bool parser::parse_procedure_header(){
    //this tracks the state of the parser
    parser_state state = S_PROCEDURE_HEADER;
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


//ready to test
bool parser::parse_procedure_body(){
    //this tracks the state of the parser
    parser_state state = S_PROCEDURE_BODY;
    bool valid_parse;
    //must be able to parse declarations until T_BEGIN is found
    while(Current_parse_token_type!=T_BEGIN){
        //for resyncing
        if(Current_parse_token_type == T_END){
            break;
        }
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
    //this tracks the state of the parser
    parser_state state = S_TYPE_MARK;
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
//refactored 2 time
bool parser::parse_parameter_list(){
    //this tracks the state of the parser
    parser_state state = S_PARAMETER_LIST;
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
        if(Current_parse_token_type == T_RPARAM){
            Current_parse_token = Get_Valid_Token();
            valid_parse = true;

        }
        //an invalid token was detected
        else{
            if(debugging){
                std::cout<<"parser failed on parse_parameter_list()"<<std::endl;
            }
            generate_error_report("Missing \")\" to close procedure parameter list");
            return false;
        }
    }
    return valid_parse;
}

//variable token is consumed to enter this function
//ready to test
//refactored 1 time
bool parser::parse_variable_declaration(){
    //this tracks the state of the parser
    parser_state state = S_VARIABLE_DECLARATION;
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
    //this tracks the state of the parser
    parser_state state = S_BOUND;
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
    //this tracks the state of the parser
    parser_state state = S_TYPE_DECLARATION;
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
    //this tracks the state of the parser
    parser_state state = S_BASE_STATEMENT;
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
    //this tracks the state of the parser
    parser_state state = S_PARAMETER;
    bool valid_parse;
    valid_parse = parse_variable_declaration();
    return valid_parse;
}

//ready to test
bool parser::parse_number(){
    //this tracks the state of the parser
    parser_state state = S_NUMBER;
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
    //this tracks the state of the parser
    parser_state state = S_ASSIGNMENT_STATMENT;
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
//refactored 1 time
bool parser::parse_if_statement(){
    //this tracks the state of the parser
    parser_state state = S_IF_STATEMENT;
    bool valid_parse;
    if(Current_parse_token_type == T_LPARAM){
        Current_parse_token = Get_Valid_Token();
        valid_parse = parse_expression();
        if(Current_parse_token_type == T_RPARAM){
            Current_parse_token = Get_Valid_Token();
            if(Current_parse_token_type == T_THEN){
                Current_parse_token = Get_Valid_Token();
                //may need to remove this if statement
                    while(Current_parse_token_type!=T_END){
                        if(Current_parse_token_type ==T_ELSE){
                            Current_parse_token = Get_Valid_Token();
                        }
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
                    if(Current_parse_token_type == T_END){
                        Current_parse_token = Get_Valid_Token();
                    }
                    else{
                        generate_error_report("Missing keyword \"end\" to end if statement");
                        return false;
                    }
                    if(Current_parse_token_type == T_IF){
                        Current_parse_token = Get_Valid_Token();
                    }
                    else{
                        generate_error_report("Missing keyword \"if\"to end if statement");
                        return false;
                    }
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
    //this tracks the state of the parser
    parser_state state = S_LOOP_STATEMENT;
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
        //required semicolon after parse
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
        valid_parse = parse_expression();
        if(!valid_parse){
            if(debugging){
                std::cout<<"parser failed on parse_loop_statement()"<<std::endl;
            }
            generate_error_report("Missing expected expression for loop");
            return false;
        }
        //else is valid
        else{
            //code gen?
        }
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
    //else missing right paratheses
    else{
        generate_error_report("Missing \")\" for loop declaration");
        return false;
    }

    return valid_parse;
}

//ready to test
//consumes return token before entering function
//refactored 1 time
bool parser::parse_return_statement(){
    //this tracks the state of the parser
    parser_state state = S_RETURN_STATEMENT;
    bool valid_parse;
    valid_parse = parse_expression();
    return valid_parse;
}


//ready to test
//already consumes identifier before parsing
//refactored 1 time
bool parser::parse_assignment_destination(){
    //this tracks the state of the parser
    parser_state state = S_ASSIGNMENT_DESTINATION;
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
        valid_parse = true;
    }
    return valid_parse;
}

//ready to test
//consumes a token before entering this function
//all expressions start be thought to start with a ArithOp?
bool parser::parse_expression(){
    //this tracks the state of the parser
    parser_state state = S_EXPRESSION;
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
    //this tracks the state of the parser
    parser_state state = S_ARITH_OP;
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
    //this tracks the state of the parser
    parser_state state = S_RELATION;
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
        valid_parse = parse_term();
        //checks to see if are any more terms to parse
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
    //this tracks the state of the parser
    parser_state state = S_TERM;
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


//ready to test
//already consumes a token before being parsed
bool parser::parse_factor(){
    //this tracks the state of the parser
    parser_state state = S_FACTOR;
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

//ready to test
//already consumes indentifier token before being parsed
bool parser::parse_name(){
    //this tracks the state of the parser
    parser_state state = S_NAME;
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

//ready to test
//consumes one token before starting
bool parser::parse_argument_list(){
    //this tracks the state of the parser
    parser_state state = S_ARGUMENT_LIST;
    bool valid_parse;
    valid_parse = parse_expression();
    if(valid_parse){
        if(Current_parse_token_type == T_COMMA){
            valid_parse = parse_argument_list();
        }
    }

    return valid_parse;
}

//ready to test
//already consumes identifier token before parsing
bool parser::parse_procedure_call(){
    //this tracks the state of the parser
    parser_state state = S_PROCEDURE_CALL;
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


bool parser::resync_parser(parser_state state){
    resync_status = true;
    //state to return
    bool return_state;
    //may be used to call proper parse function if needed
    parser_state new_state;
    //consumes tokens until it can resync
    switch (state){
        //1
        //not possible?
        case S_PROGRAM:

        break;

        //2
        case S_PROGRAM_HEADER:


        break;

        //3
        case S_PROGRAM_BODY:
        while(Current_parse_token_type!=T_SEMICOLON && Current_parse_token_type!=T_INVALID){
            if(prev_token_type == T_PROCEDURE){
                new_state = S_PROCEDURE_DECLARATION;
                break;
            }
            else if(prev_token_type == T_VARIABLE){
                new_state = S_VARIABLE_DECLARATION;
                break;
            }
            else if(prev_token_type == T_TYPE){
                new_state = S_TYPE_DECLARATION;
                break;
            }
            else if(prev_token_type == T_GLOBAL){
                new_state = S_BASE_DECLARATION;
                break;
            }


            if(Current_parse_token_type == T_BEGIN){
                new_state = S_PROGRAM_BODY;
                break;
            }
            else if(Current_parse_token_type == T_PROCEDURE){
                new_state = S_BASE_DECLARATION;
                break;
            }
            else if(Current_parse_token_type == T_VARIABLE){
                new_state = S_BASE_DECLARATION;
                break;
            }
            else if(Current_parse_token_type == T_TYPE){
                new_state = S_BASE_DECLARATION;
            }

            Current_parse_token = Get_Valid_Token();
            if(Current_parse_token_type == T_SEMICOLON){
                Current_parse_token = Get_Valid_Token();
                new_state = S_PROGRAM_BODY;
                break;
            }
        }

       

        break;

        //4
        case S_BASE_DECLARATION:

        break;

        //5
        case S_PROCEDURE_DECLARATION:


        break;

        //6
        case S_PROCEDURE_HEADER:

        break;

        //7
        case S_PARAMETER_LIST:

        break;

        //8
        case S_PARAMETER:

        break;

        //9
        case S_PROCEDURE_BODY:

        break;

        //10
        case S_VARIABLE_DECLARATION:


        break;

        //11
        case S_TYPE_DECLARATION:
  

        break;

        //12
        case S_TYPE_MARK:

        break;

        //13
        case S_BOUND:

        break;

        //14
        case S_BASE_STATEMENT:

        break;
        
        //15
        case S_PROCEDURE_CALL:

        break;

        //16
        case S_ASSIGNMENT_STATMENT:

        break;

        //17
        case S_ASSIGNMENT_DESTINATION:

        break;

        //18
        case S_IF_STATEMENT:

        break;

        //19
        case S_LOOP_STATEMENT:

        break;

        //20
        case S_RETURN_STATEMENT:

        break;

        //21
        case S_EXPRESSION:

        break;

        //22
        case S_ARITH_OP:

        break;

        //23
        case S_RELATION:

        break;

        //24
        case S_TERM:

        break;

        //25
        case S_FACTOR:

        break;

        //26
        case S_NAME:

        break;

        //27
        case S_ARGUMENT_LIST:

        break;

        //28
        case S_NUMBER:

        break;

        //This should never happen
        default:
        std::cout<<"Error in resync start state"<<std::endl;
        break;
    }




    resync_status = false;
    //calls appropriate parse function based on the new state
    switch (new_state){
        //1
        //not possible?
        case S_PROGRAM:

        break;

        //2
        case S_PROGRAM_HEADER:

        break;

        //3
        case S_PROGRAM_BODY:
        //return_state = parse_program_body();
        return_state = true;

        break;

        //4
        case S_BASE_DECLARATION:
        return_state = parse_base_declaration();
        if(return_state){
            if(Current_parse_token_type == T_SEMICOLON){
                Current_parse_token = Get_Valid_Token();
            }
            else{
                generate_error_report("Missing \";\" to complete declaration");
                resync_parser(S_PROGRAM_BODY);
            }
        }
        else{

        }

        break;

        //5
        case S_PROCEDURE_DECLARATION:
        return_state = parse_procedure_declaration();
        if(return_state){
            if(Current_parse_token_type ==T_SEMICOLON){
                Current_parse_token = Get_Valid_Token();
            }
            else{
                generate_error_report("Missing \";\" to complete declaration");
                resync_parser(S_PROGRAM_BODY);
            }
        }
        else{

        }


        break;

        //6
        case S_PROCEDURE_HEADER:
        // return_state = parse_procedure_header();

        break;

        //7
        case S_PARAMETER_LIST:

        break;

        //8
        case S_PARAMETER:

        break;

        //9
        case S_PROCEDURE_BODY:

        break;

        //10
        case S_VARIABLE_DECLARATION:
        return_state = parse_variable_declaration();
        if(return_state){
            if(Current_parse_token_type ==T_SEMICOLON){
                Current_parse_token = Get_Valid_Token();
            }
            else{
                generate_error_report("Missing \";\" to complete declaration");
                resync_parser(S_PROGRAM_BODY);
            }
        }
        else{

        }


        break;

        //11
        case S_TYPE_DECLARATION:
        return_state = parse_type_declaration();
        if(return_state){
            if(Current_parse_token_type ==T_SEMICOLON){
                Current_parse_token = Get_Valid_Token();
            }
            else{
                generate_error_report("Missing \";\" to complete declaration");
                resync_parser(S_PROGRAM_BODY);
            }
        }
        else{

        } 

        break;

        //12
        case S_TYPE_MARK:

        break;

        //13
        case S_BOUND:

        break;

        //14
        case S_BASE_STATEMENT:

        break;
        
        //15
        case S_PROCEDURE_CALL:

        break;

        //16
        case S_ASSIGNMENT_STATMENT:

        break;

        //17
        case S_ASSIGNMENT_DESTINATION:

        break;

        //18
        case S_IF_STATEMENT:

        break;

        //19
        case S_LOOP_STATEMENT:

        break;

        //20
        case S_RETURN_STATEMENT:

        break;

        //21
        case S_EXPRESSION:

        break;

        //22
        case S_ARITH_OP:

        break;

        //23
        case S_RELATION:

        break;

        //24
        case S_TERM:

        break;

        //25
        case S_FACTOR:

        break;

        //26
        case S_NAME:

        break;

        //27
        case S_ARGUMENT_LIST:

        break;

        //28
        case S_NUMBER:

        break;

        //This should never happen
        default:
        std::cout<<"Error in resync start state"<<std::endl;
        break;
    }
    resync_status = false;
    return return_state;
}