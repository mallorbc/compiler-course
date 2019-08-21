#include "parser.h"
//ready for testing
parser::parser(std::string file_to_parse){
    //initalized the Current_parse_token type to 9999 as a flag to say that nothing has been loaded yet
    Current_parse_token.type = 9999;
    bool valid_parse;
    parse_file = file_to_parse;
    Lexer = new scanner(parse_file);
    //type_checker = new TypeChecker(Lexer->get_SymbolTable_map());
    valid_parse = parse_program();
    if(valid_parse){
        if(errors_occured){
            std::cout<<"The program parsed successfully with errors"<<std::endl;
        }
        else{
            std::cout<<"The program parsed successfully with no errors"<<std::endl;
        }
        
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
    //each time we get a token we are potentially updating the symbol table so we need to make sure the typchecker shares that value
    //type_checker->copy_table(Lexer->get_SymbolTable_map());
    Lexer->symbol_table.update_token_scope_id(Current_parse_token,current_scope_id);
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
        if(Lexer->is_nested_commented == false){
            if(Current_parse_token.first_token_on_line){
                full_error_message = "Error on line " + std::to_string(prev_token.line_found) + ": ";
            }
            else{
                full_error_message = "Error on line " + std::to_string(Current_parse_token.line_found) + ": ";
            }
        }
        else{
            full_error_message = "Error on line " + std::to_string(Lexer->nested_comment_line) + ": ";
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
        errors_occured = true;
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
        errors_occured = true;
        if(debugging){
            std::cout<<"parser failed on parse_program_header()"<<std::endl;
        }
        return false;
    }
    if(Current_parse_token_type == T_IDENTIFIER){
        //this identifier is a program name
        Current_parse_token.identifer_type = I_PROGRAM_NAME;
        //updates the token in the symbol tables
        Lexer->symbol_table.update_identifier_type(Current_parse_token,current_scope_id);
        valid_parse = true;
        Current_parse_token = Get_Valid_Token();
    }
    else{
        generate_error_report("Expected \"identifier\" not found");
        errors_occured = true;
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
        errors_occured = true;
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
            //required a semicolon after parse
            if(Current_parse_token_type == T_SEMICOLON){
                Current_parse_token = Get_Valid_Token();
            }
            else{
                if(valid_parse){
                    generate_error_report("Missing \";\" to complete declaration");
                    errors_occured = true;
                }
                valid_parse = resync_parser(state);
                    //if have run out of tokens
                    if(Current_parse_token_type == T_INVALID){
                        if(Lexer->is_nested_commented){
                            generate_error_report("Unclosed block comment detected");
                            errors_occured = true;
                            resync_status = true;
                        }
                        return false;
                    }
                }
            //happens after trying to resync
            if(valid_parse){
                //breaks out on tokens indicating begin was aborbed
                //these all indicate statements or eof
                if(Current_parse_token_type == T_IF || Current_parse_token_type == T_RETURN
                 || Current_parse_token_type == T_FOR || Current_parse_token_type == T_IDENTIFIER 
                 || Current_parse_token_type == T_INVALID){
                     break;
                }
                
            }
            //else parse_base_declaration failed
            else{
                if(debugging){
                    std::cout<<"parser failed on parse_program_body()"<<std::endl;
                 }
                valid_parse = resync_parser(state);
                if(Current_parse_token_type == T_INVALID){
                    return false;
                }
            }
            parsing_statements = false;
        }
        //once the begin token is recieved
        if(Current_parse_token_type == T_BEGIN){
            Current_parse_token = Get_Valid_Token();
        }
        else{
            generate_error_report("Missing keyword \"begin\" to begin program statements");
            errors_occured = true;
            valid_parse = resync_parser(state);
            if(Current_parse_token_type == T_INVALID){
                return false;
            }
            //return false;
        }
        while(Current_parse_token_type!=T_END){
            valid_parse = parse_base_statement();
            if(Current_parse_token_type == T_SEMICOLON){
                Current_parse_token = Get_Valid_Token();
            }
            else{
                if(valid_parse){
                    generate_error_report("Missing \";\" to end program statement");
                    errors_occured = true;
                }
                valid_parse = resync_parser(state);
                //if have run out of tokens
                if(Current_parse_token_type == T_INVALID){
                    if(Lexer->is_nested_commented){
                        generate_error_report("Unclosed block comment detected");
                        errors_occured = true;
                        resync_status = true;
                    }
                    return false;
                }
            }
                //conditions to break loop    
            if(valid_parse){
                if(Current_parse_token_type == T_PROGRAM || Current_parse_token_type == T_PERIOD || Current_parse_token_type ==T_INVALID){
                    break;
                }
            }
            else{
                valid_parse = resync_parser(state);
            }
            }
            //once the end token is recieveds
            if(Current_parse_token_type == T_END){
                Current_parse_token = Get_Valid_Token();
            }
            else{
                generate_error_report("Missing keyword \"end\" to end program");
                errors_occured = true;
                valid_parse = resync_parser(state);
            }
            
            if(Current_parse_token_type == T_PROGRAM){
                Current_parse_token = Get_Valid_Token();
            }
            else{
                generate_error_report("Missing keyworkd \"program\" to end program");
                errors_occured = true;
                valid_parse = resync_parser(state);
                //return false;
            }
        


    return valid_parse;
}

//ready to test
//refactored 1 time
bool parser::parse_base_declaration(){
    //tracks whether base declaration is global or not
    bool is_global_declaration = false;
    //this tracks the state of the parser
    parser_state state = S_BASE_DECLARATION;
    bool valid_parse;
    if(Current_parse_token_type == T_GLOBAL){
        //Do work for global declarations here
        is_global_declaration = true;
        Current_parse_token = Get_Valid_Token();

        if(Current_parse_token_type == T_PROCEDURE){
            Current_parse_token = Get_Valid_Token();
            valid_parse = parse_procedure_declaration(is_global_declaration);

        }
        else if(Current_parse_token_type == T_VARIABLE){
            Current_parse_token = Get_Valid_Token();
            valid_parse = parse_variable_declaration(is_global_declaration);
        }
        else if(Current_parse_token_type == T_TYPE){
            Current_parse_token = Get_Valid_Token();
            valid_parse = parse_type_declaration(is_global_declaration);
        }
        else{
            if(debugging){
                std::cout<<"parser failed on parse_base_declaration()"<<std::endl;
            }
            generate_error_report("Expected keywords \"procedure\",\"variable\" \"type\", or \"begin\" not found");
            errors_occured = true;
            return false;
        }
    }
    else{
        if(Current_parse_token_type == T_PROCEDURE){
            //increments the scope id
            update_scopes(true);
            Current_parse_token = Get_Valid_Token();
            valid_parse = parse_procedure_declaration(is_global_declaration);

        }
        else if(Current_parse_token_type == T_VARIABLE){
            Current_parse_token = Get_Valid_Token();
            valid_parse = parse_variable_declaration(is_global_declaration);
        }
        else if(Current_parse_token_type == T_TYPE){
            Current_parse_token = Get_Valid_Token();
            valid_parse = parse_type_declaration(is_global_declaration);
        }
        else{
            if(debugging){
                std::cout<<"parser failed on parse_base_declaration()"<<std::endl;
            }
            generate_error_report("Expected keywords \"procedure\",\"variable\" \"type\", or \"begin\" not found");
            errors_occured = true;
            return false;
        }
    }
    
    return valid_parse;

}


bool parser::parse_procedure_declaration(bool is_global){
    //this tracks the state of the parser
    parser_state state = S_PROCEDURE_DECLARATION;
    bool valid_parse;
    valid_parse = parse_procedure_header(is_global);
    valid_parse = parse_procedure_body();
    return valid_parse;
}


//ready to test
//the token procedure is used to enter this function
bool parser::parse_procedure_header(bool is_global){
    //this variable will hold the string of the procedure name, this will be used to later add the valid parameters of the procedure
    std::string procedure_name="";
    //this tracks the state of the parser
    parser_state state = S_PROCEDURE_HEADER;
    bool valid_parse;
    if(Current_parse_token_type == T_IDENTIFIER){
        if(is_global){
            //checks to see if the token is already a global token
            if(!Lexer->symbol_table.is_global_token(Current_parse_token)){
                //makes the current parse token global since the previous token was global
                Lexer->symbol_table.make_token_global(Current_parse_token);
            }
            //else it is global; can we redefine global?
            else{

            }
        }
        //marks this identifier as a procedure
        Current_parse_token.identifer_type = I_PROCEDURE;
        //saves the procedure name
        procedure_name = Current_parse_token.stringValue;
        //updates the token in the symbol tables
        Lexer->symbol_table.update_identifier_type(Current_parse_token, current_scope_id);
        Current_parse_token = Get_Valid_Token();
    }
    else{
        if(debugging){
            std::cout<<"parser failed on parse_procedure_header()"<<std::endl;
        }
        generate_error_report("Procedure must be named a valid identifier");
        errors_occured = true;
        Current_parse_token = Get_Valid_Token();
        //valid_parse = resync_parser(state);
        //return false;
    }
    if(Current_parse_token_type == T_COLON){
        Current_parse_token = Get_Valid_Token();
    }
    else{
        generate_error_report("Expected \":\" before type mark declaration");
        errors_occured = true;

    }
    valid_parse = parse_type_mark();
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
        valid_parse = parse_parameter_list(procedure_name);
        }

    }
    //must have left and right paretheses, parameters are optional
    else{
        if(debugging){
            std::cout<<"parser failed on parse_procedure_header()"<<std::endl;
        }
        generate_error_report("Missing \"(\" needed to for procedure declaration");
        errors_occured = true;
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
        valid_parse = parse_base_declaration();
        if(Current_parse_token_type == T_SEMICOLON){
            Current_parse_token = Get_Valid_Token();
        }
        else{
            if(valid_parse){
            generate_error_report("Missing \";\" to complete declaration");
            errors_occured = true;
            }
            valid_parse = resync_parser(state);
                //if have run out of tokens
                if(Current_parse_token_type == T_INVALID){
                    if(Lexer->is_nested_commented){
                        generate_error_report("Unclosed block comment detected");
                        errors_occured = true;
                        resync_status = true;
                    }
                    return false;
                }

        }
        if(valid_parse){
            if(Current_parse_token_type == T_BEGIN ||Current_parse_token_type == T_IF 
            || Current_parse_token_type == T_FOR || Current_parse_token_type == T_RETURN 
            || Current_parse_token_type == T_END){
                break;
            }

        }
        //else parse_base_declaration failed
        else{
            if(debugging){
                std::cout<<"parser failed on parse_procedure_body()"<<std::endl;
            }
            valid_parse = resync_parser(state);
            if(Current_parse_token_type == T_INVALID){
                return false;
            }
        }
    }

    
    //after doen parsing any and all declarations, must start parsing statements\
    //need to parse more than one base statement
    //add this if statement in the case that never enters while loop
    if(Current_parse_token_type == T_BEGIN){
        Current_parse_token = Get_Valid_Token();
    }
    else{
        generate_error_report("Missing keyword \"begin\" to begin procedure statements");
        errors_occured = true;
        valid_parse = resync_parser(state);
        if(Current_parse_token_type == T_INVALID){
            return false;
        }
    }
    parsing_statements = true;
    while(Current_parse_token_type!=T_END){
        valid_parse = parse_base_statement();
        if(Current_parse_token_type == T_SEMICOLON){
            Current_parse_token = Get_Valid_Token();
        }
        else{
            if(valid_parse){
                generate_error_report("Missing \";\" to end program statement");
                errors_occured = true;
            }
            valid_parse = resync_parser(state);
            //if have run out of tokens
            if(Current_parse_token_type == T_INVALID){
                if(Lexer->is_nested_commented){
                    generate_error_report("Unclosed block comment detected");
                    errors_occured = true;
                    resync_status = true;
                }
                return false;
            }

        }
        if(valid_parse){
            if(Current_parse_token_type == T_PROCEDURE || Current_parse_token_type == T_INVALID){
               break; 
            }
        }
        else{
            valid_parse = resync_parser(state);
            if(Current_parse_token_type == T_INVALID){
                return false;
            }
        }
    }
    if(Current_parse_token_type == T_END){
        Current_parse_token = Get_Valid_Token();
    }
    else{
        generate_error_report("Missing keyword \"end\" to close procedure body");
        errors_occured = true;
        valid_parse = resync_parser(state);

    }
    if(Current_parse_token_type == T_PROCEDURE){
        //updates the scope id 
        update_scopes(false);
        Current_parse_token = Get_Valid_Token();
    }
    else{
        generate_error_report("Missing keyword \"procedure\" to close procedure body");
        errors_occured = true;
        valid_parse = resync_parser(state);
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
        //marks this identifier as a type
        Current_parse_token.identifer_type = I_TYPE;
        //updates the token in the symbol tables
        Lexer->symbol_table.update_identifier_type(Current_parse_token, current_scope_id);
        Current_parse_token = Get_Valid_Token();
        valid_parse = true;
    }
    else if(Current_parse_token_type == T_ENUM){
        Current_parse_token = Get_Valid_Token();
        if(Current_parse_token_type == T_LBRACE){
            Current_parse_token = Get_Valid_Token();
            // if(Current_parse_token_type == T_LBRACE){
            //     Current_parse_token = Get_Valid_Token();
                if(Current_parse_token_type!=T_IDENTIFIER){
                    generate_error_report("Expected identifier as part of Enum");
                    errors_occured = true;
                    if(debugging){
                        std::cout<<"parser failed on parse_type_mark()"<<std::endl;
                    }
                    return false;
                }
                else{
                    //it is an enum which is a type mark
                    Current_parse_token.identifer_type = I_TYPE;
                    //updates the token in the symbol tables
                    Lexer->symbol_table.update_identifier_type(Current_parse_token, current_scope_id);
                    Current_parse_token = Get_Valid_Token();
                    //end of enumeration, contains one identifier
                    if(Current_parse_token_type == T_RBRACE){
                        Current_parse_token = Get_Valid_Token();
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
                                    //Current token is an identifier in an enum
                                    Current_parse_token.identifer_type = I_TYPE;
                                    //updates the token in the symbol tables
                                    Lexer->symbol_table.update_identifier_type(Current_parse_token, current_scope_id);
                                    //if the next token after the identifer is a RBRACE, it is valid and end of the parse 
                                    if(Next_parse_token_type == T_RBRACE){
                                        Current_parse_token = Get_Valid_Token();
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
                                            errors_occured = true;
                                            return false;

                                        }
                                    }
                                    //else it contains some other invalid token
                                    else{
                                        if(debugging){
                                            std::cout<<"parser failed on parse_type_mark()"<<std::endl;
                                        }
                                        generate_error_report("Enumeration list must be either a comma or a identifier");
                                        errors_occured = true;
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
                            errors_occured = true;
                            return false;
                        }
                    }
                }
            //}
        }
    }
    else{
        generate_error_report("Missing valid type mark");
        errors_occured = true;
        return false;
    }
    return valid_parse;
}

//parse_type_mark but for procedure headers
bool parser::parse_type_mark(std::string identifier_name, int proc_or_var)
{
    //this tracks the state of the parser
    parser_state state = S_TYPE_MARK;
    bool valid_parse;
    //May need to do something once the type is determined
    //the procedure takes a integer input
    if (Current_parse_token_type == T_INTEGER_TYPE)
    {
        if(proc_or_var == 0){
            Lexer->symbol_table.add_procedure_valid_inputs(identifier_name,TYPE_INT);
        }
        Current_parse_token = Get_Valid_Token();
        valid_parse = true;
    }
    //the procedure takes a float input
    else if (Current_parse_token_type == T_FLOAT_TYPE)
    {
        if(proc_or_var == 0){
            Lexer->symbol_table.add_procedure_valid_inputs(identifier_name, TYPE_FLOAT);
        }
        Current_parse_token = Get_Valid_Token();
        valid_parse = true;
    }
    //the procedure takes a string input
    else if (Current_parse_token_type == T_STRING_TYPE)
    {
        if(proc_or_var == 0){
            Lexer->symbol_table.add_procedure_valid_inputs(identifier_name, TYPE_STRING);
        }
        Current_parse_token = Get_Valid_Token();
        valid_parse = true;
    }
    //the procedure takes a bool input
    else if (Current_parse_token_type == T_BOOL_TYPE)
    {
        if(proc_or_var == 0){
            Lexer->symbol_table.add_procedure_valid_inputs(identifier_name, TYPE_BOOL);
        }
        Current_parse_token = Get_Valid_Token();
        valid_parse = true;
    }
    //I think this means that the this will be the same type as the identifier
    //if the above comment is right, we need to look out the above scope and figure out what the type really is
    else if (Current_parse_token_type == T_IDENTIFIER)
    {
        //marks this identifier as a type
        Current_parse_token.identifer_type = I_TYPE;
        //updates the token in the symbol tables
        Lexer->symbol_table.update_identifier_type(Current_parse_token, current_scope_id);
        Current_parse_token = Get_Valid_Token();
        valid_parse = true;
    }
    //fuck this for now when it comes to type; COME BACK
    else if (Current_parse_token_type == T_ENUM)
    {
        Current_parse_token = Get_Valid_Token();
        if (Current_parse_token_type == T_LBRACE)
        {
            Current_parse_token = Get_Valid_Token();
            // if(Current_parse_token_type == T_LBRACE){
            //     Current_parse_token = Get_Valid_Token();
            if (Current_parse_token_type != T_IDENTIFIER)
            {
                generate_error_report("Expected identifier as part of Enum");
                errors_occured = true;
                if (debugging)
                {
                    std::cout << "parser failed on parse_type_mark()" << std::endl;
                }
                return false;
            }
            else
            {
                //it is an enum which is a type mark
                Current_parse_token.identifer_type = I_TYPE;
                //updates the token in the symbol tables
                Lexer->symbol_table.update_identifier_type(Current_parse_token, current_scope_id);
                Current_parse_token = Get_Valid_Token();
                //end of enumeration, contains one identifier
                if (Current_parse_token_type == T_RBRACE)
                {
                    Current_parse_token = Get_Valid_Token();
                    valid_parse = true;
                }
                //else at least 2 identifiers exist in the enum
                else
                {
                    //the first token has to be a comma
                    if (Current_parse_token_type == T_COMMA)
                    {
                        //The next token after a comma has to be an identifier
                        if (Next_parse_token_type == T_IDENTIFIER)
                        {
                            while (true)
                            {
                                Current_parse_token = Get_Valid_Token();
                                //Current token is an identifier in an enum
                                Current_parse_token.identifer_type = I_TYPE;
                                //updates the token in the symbol tables
                                Lexer->symbol_table.update_identifier_type(Current_parse_token, current_scope_id);
                                //if the next token after the identifer is a RBRACE, it is valid and end of the parse
                                if (Next_parse_token_type == T_RBRACE)
                                {
                                    Current_parse_token = Get_Valid_Token();
                                    Current_parse_token = Get_Valid_Token();
                                    return true;
                                }
                                //else it better be a comma
                                else if (Next_parse_token_type == T_COMMA)
                                {
                                    Current_parse_token = Get_Valid_Token();
                                    //The current token is now a comma, so the next token needs to be an identifier
                                    if (Next_parse_token_type != T_IDENTIFIER)
                                    {
                                        if (debugging)
                                        {
                                            std::cout << "parser failed on parse_type_mark()" << std::endl;
                                        }
                                        generate_error_report("Missing expected identifier after comma in enumeration list");
                                        errors_occured = true;
                                        return false;
                                    }
                                }
                                //else it contains some other invalid token
                                else
                                {
                                    if (debugging)
                                    {
                                        std::cout << "parser failed on parse_type_mark()" << std::endl;
                                    }
                                    generate_error_report("Enumeration list must be either a comma or a identifier");
                                    errors_occured = true;
                                    return false;
                                }
                            }
                        }
                    }
                    else
                    {
                        if (debugging)
                        {
                            std::cout << "parser failed on parse_type_mark()" << std::endl;
                        }
                        generate_error_report("Missing expected comma for list of enumerations");
                        errors_occured = true;
                        return false;
                    }
                }
            }
            //}
        }
    }
    else
    {
        generate_error_report("Missing valid type mark");
        errors_occured = true;
        return false;
    }
    return valid_parse;
}

//ready to test
//refactored 2 time
//consumes variable token first
//tracks the procedure name so that valid inputs of this procedure can be tracked
bool parser::parse_parameter_list(std::string procedure_name){
    //this tracks the state of the parser
    parser_state state = S_PARAMETER_LIST;
    bool valid_parse;
    //gets identifier token since it must be a variable, then parses variable
    Current_parse_token = Get_Valid_Token();
    valid_parse = parse_parameter(procedure_name);
    //if no errors from parsing the parameter
    if(valid_parse){
        ///meaning that there are more parameters
        if(Current_parse_token_type == T_COMMA){
            Current_parse_token = Get_Valid_Token();
            valid_parse = parse_parameter_list(procedure_name);

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
            errors_occured = true;
            return false;
        }
    }
    return valid_parse;
}

//variable token is consumed to enter this function
//ready to test
//refactored 1 time
bool parser::parse_variable_declaration(bool is_global){
    //this tracks the name of the variable
    std::string variable_name = "";
    //this tracks the state of the parser
    parser_state state = S_VARIABLE_DECLARATION;
    bool valid_parse;
    if(Current_parse_token_type == T_IDENTIFIER){
        if (is_global)
        {
            //checks to see if the token is already a global token
            if (!Lexer->symbol_table.is_global_token(Current_parse_token))
            {
                //makes the current parse token global since the previous token was global
                Lexer->symbol_table.make_token_global(Current_parse_token);
            }
            //else it is global; can we redefine global?
            else
            {
            }
        }
        //this means that the identifer is a variable
        Current_parse_token.identifer_type = I_VARIABLE;
        //updates the token in the symbol tables
        Lexer->symbol_table.update_identifier_type(Current_parse_token, current_scope_id);
        Current_parse_token = Get_Valid_Token();
    }
    else{
        if(debugging){
            std::cout<<"parser failed on parse_variable_declaration()"<<std::endl;
        }
        generate_error_report("Missing identifier for variable declaration");
        errors_occured = true;
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
                    errors_occured = true;
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
        errors_occured = true;
        return false;
    }


    return valid_parse;

}

//this is the same as the regular parse_variable_declaration, though it allows tracking of the procedure name
bool parser::parse_variable_declaration(bool is_global,std::string procedure_name)
{
    //this tracks the state of the parser
    parser_state state = S_VARIABLE_DECLARATION;
    bool valid_parse;
    if (Current_parse_token_type == T_IDENTIFIER)
    {
        if (is_global)
        {
            //checks to see if the token is already a global token
            if (!Lexer->symbol_table.is_global_token(Current_parse_token))
            {
                //makes the current parse token global since the previous token was global
                Lexer->symbol_table.make_token_global(Current_parse_token);
            }
            //else it is global; can we redefine global?
            else
            {
            }
        }
        //this means that the identifer is a variable
        Current_parse_token.identifer_type = I_VARIABLE;
        //updates the token in the symbol tables
        Lexer->symbol_table.update_identifier_type(Current_parse_token, current_scope_id);
        Current_parse_token = Get_Valid_Token();
    }
    else
    {
        if (debugging)
        {
            std::cout << "parser failed on parse_variable_declaration()" << std::endl;
        }
        generate_error_report("Missing identifier for variable declaration");
        errors_occured = true;
        return false;
    }
    if (Current_parse_token_type == T_COLON)
    {
        Current_parse_token = Get_Valid_Token();
        valid_parse = parse_type_mark(procedure_name,0);
        if (valid_parse)
        {
            //checks for optional bracket to declare an array
            if (Current_parse_token_type == T_LBRACKET)
            {
                Current_parse_token = Get_Valid_Token();
                valid_parse = parse_bound();
                if (Current_parse_token_type == T_RBRACKET)
                {
                    Current_parse_token = Get_Valid_Token();
                }
                //must have closing right bracket
                else
                {
                    generate_error_report("Missing \"]\" to close the array declaration");
                    errors_occured = true;
                    return false;
                }
            }
        }
    }
    else
    {
        if (debugging)
        {
            std::cout << "parser failed on parse_variable_declaration()" << std::endl;
        }
        generate_error_report("Missing colon for delcaration of variable type");
        errors_occured = true;
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
        errors_occured = true;
    }
    return valid_parse;
}

//token type is consumed to enter this function
//ready test
//write test prog for
bool parser::parse_type_declaration(bool is_global){
    //this tracks the state of the parser
    parser_state state = S_TYPE_DECLARATION;
    bool valid_parse;
    //T_TYPE has already been parsed;  May need changed in the future
    if(Current_parse_token_type == T_IDENTIFIER){
        if (is_global)
        {
            //checks to see if the token is already a global token
            if (!Lexer->symbol_table.is_global_token(Current_parse_token))
            {
                //makes the current parse token global since the previous token was global
                Lexer->symbol_table.make_token_global(Current_parse_token);
            }
            //else it is global; can we redefine global?
            else
            {
            }
        }
        //this identifier is a type
        Current_parse_token.identifer_type = I_TYPE;
        //updates the token in the symbol tables
        Lexer->symbol_table.update_identifier_type(Current_parse_token, current_scope_id);
        Current_parse_token = Get_Valid_Token();
    }
    else{
        if(debugging){
            std::cout<<"parser failed on parse_type_declaration()"<<std::endl;
        }
        generate_error_report("Missing required identifier for type declaration");
        errors_occured = true;
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
        errors_occured = true;
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
        errors_occured = true;
        return false;
    }

    return valid_parse;
}

//ready to test
bool parser::parse_parameter(std::string procedure_name){
    //this tracks the state of the parser
    parser_state state = S_PARAMETER;
    bool valid_parse;
    valid_parse = parse_variable_declaration(false,procedure_name);
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
        errors_occured = true;
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
            errors_occured = true;
            return false;
        }
        if(Current_parse_token_type == T_ASSIGN){
            Current_parse_token = Get_Valid_Token();
            valid_parse = parse_expression();
        }
        else{
            generate_error_report("Missing \"=\" needed for assignment statement");
            errors_occured = true;
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
        }
        else{
            generate_error_report("Missing \")\" expected for if statment");
            errors_occured = true;
            valid_parse = resync_parser(state);
            //return false;
        }
        if(Current_parse_token_type == T_THEN){
                Current_parse_token = Get_Valid_Token();
        }
        else{
            generate_error_report("Missing expected keyword \"then\" for if statements");
            errors_occured = true;
            valid_parse = resync_parser(state);
            if(Current_parse_token_type == T_INVALID){
                return false;                }
            }
                //may need to remove this if statement
                    while(Current_parse_token_type!=T_END){
                        if(Current_parse_token_type ==T_ELSE){
                            Current_parse_token = Get_Valid_Token();
                        }
                        valid_parse = parse_base_statement();
                        if(Current_parse_token_type == T_SEMICOLON){
                            Current_parse_token = Get_Valid_Token();
                        }
                        else{
                            if(valid_parse){
                                generate_error_report("Missing \";\" to end statement in if statement");
                                errors_occured = true;
                            }
                            valid_parse = resync_parser(state);
                            //if have run out of tokens
                            if(Current_parse_token_type == T_INVALID){
                                if(Lexer->is_nested_commented){
                                    generate_error_report("Unclosed block comment detected");
                                    errors_occured = true;
                                    resync_status = true;
                                }
                                return false;
                            }
                        }
                        //conditions to break loop
                        if(valid_parse){
                            if(Current_parse_token_type == T_END || Current_parse_token_type == T_INVALID){
                                break;
                            }
                            if(Current_parse_token_type == T_IF && Next_parse_token_type!=T_LPARAM){
                                break;
                            }
                        }
                        else{
                            valid_parse = resync_parser(state);
                            if(Current_parse_token_type == T_INVALID){
                                return false;
                            }
                        }

                    }
                    if(Current_parse_token_type == T_END){
                        Current_parse_token = Get_Valid_Token();
                    }
                    else{
                        generate_error_report("Missing keyword \"end\" to end if statement");
                        errors_occured = true;
                        //return false;
                    }
                    if(Current_parse_token_type == T_IF){
                        Current_parse_token = Get_Valid_Token();
                    }
                    else{
                        generate_error_report("Missing keyword \"if\"to end if statement");
                        errors_occured = true;
                        //return false;
                    }


    }
    else{
        if(debugging){
            std::cout<<"parser failed on parse_if_statement()"<<std::endl;
        }
        generate_error_report("Missing \"(\" expected for if statment");
        errors_occured = true;
        return false;
    }


    return valid_parse;
}


//ready to test
//consumes for token before entering this function
//refactored 2 times
bool parser::parse_loop_statement(){
    //this tracks the state of the parser
    parser_state state = S_LOOP_STATEMENT;
    bool valid_parse;
    if(Current_parse_token_type == T_LPARAM){
        //grabs what should be an identifier
        Current_parse_token = Get_Valid_Token();  
        if(Current_parse_token_type == T_IDENTIFIER){
            Current_parse_token = Get_Valid_Token();
            valid_parse = parse_assignment_statement();
            if(Current_parse_token_type == T_SEMICOLON){
                Current_parse_token = Get_Valid_Token();
                valid_parse = parse_expression();
                if(Current_parse_token_type == T_RPARAM){
                    Current_parse_token = Get_Valid_Token();
                    while(Current_parse_token_type!=T_END){
                        valid_parse = parse_base_statement();
                        if(Current_parse_token_type == T_SEMICOLON){
                            Current_parse_token = Get_Valid_Token();
                        }
                        else{
                            if(valid_parse){
                                generate_error_report("Missing \";\" to end statement in loop statement");
                                errors_occured = true;
                            }
                            valid_parse = resync_parser(state);
                            //if have run out of tokens
                            if(Current_parse_token_type == T_INVALID){
                                if(Lexer->is_nested_commented){
                                    generate_error_report("Unclosed block comment detected");
                                    errors_occured = true;
                                    resync_status = true;
                                }
                                return false;
                            }
                        }
                        if(valid_parse){
                            if(Current_parse_token_type == T_END || Current_parse_token_type == T_INVALID){
                                break;
                            }
                            if(Current_parse_token_type == T_FOR && Next_parse_token_type!=T_LPARAM){
                                break;
                            }

                        }
                        else{
                            valid_parse = resync_parser(state);
                            if(Current_parse_token_type == T_INVALID){
                                return false;
                            }
                        }
                    }
                    if(Current_parse_token_type == T_END){
                        Current_parse_token = Get_Valid_Token();
                        if(Current_parse_token_type == T_FOR){
                            Current_parse_token = Get_Valid_Token();
                        }
                        else{
                            generate_error_report("Missing expected keyword \"for\" for end of statement");
                            errors_occured = true;

                        }
                    }
                    else{
                        generate_error_report("Missing expected keyword \"end\" for end of statement");
                        errors_occured = true;
                    }
                }
                else{
                    generate_error_report("Missing \")\" for loop declaration");
                    errors_occured = true;

                }

            }
            else{
                generate_error_report("Missing \";\" for loop assignment statement");
                errors_occured = true;
            }
        } 
        else{
            generate_error_report("Missing expeceted identifier for assignment statement");
            errors_occured = true;
        }
    }
    else{
        generate_error_report("Missing \"(\" required for loop");
        errors_occured = true;
        //return false;
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
            errors_occured = true;
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
    else{
        generate_error_report("Error in expression");
        errors_occured = true;
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
        if(Current_parse_token_type==T_ASSIGN){
            valid_parse = parse_term();
        }
        else{
            if(debugging){
                std::cout<<"parser failed on parse_relation()"<<std::endl;
            }
            //Current_parse_token = Get_Valid_Token();
            generate_error_report("\"=\" is not a valid relational operator, did you mean \"==\"");
            errors_occured = true;
            return false;
            
        }
    }
    else if(Current_parse_token_type == T_EXCLAM){
        Current_parse_token = Get_Valid_Token();
        if(Current_parse_token_type!=T_ASSIGN){
            if(debugging){
                std::cout<<"parser failed on parse_relation()"<<std::endl;
            }
            generate_error_report("Invalid relational operator detected");
            errors_occured = true;
        }
    }
    //else just a term
    else{
        valid_parse = parse_term();
        //checks to see if are any more terms to parse
        if(valid_parse){
            if(Current_parse_token_type == T_ASSIGN){
                Current_parse_token = Get_Valid_Token();
                if(Current_parse_token_type==T_ASSIGN){
                    Current_parse_token = Get_Valid_Token();
                    valid_parse = parse_term();

                }
                else{
                    generate_error_report("Not a valid relational operator");
                    errors_occured = true;
                    
                }
            }
            else if(Current_parse_token_type == T_LESS || Current_parse_token_type == T_GREATER ||  Current_parse_token_type == T_EXCLAM){
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
            errors_occured = true;
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
            errors_occured = true;
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
        errors_occured = true;
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
            errors_occured = true;
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
                errors_occured = true;
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
        errors_occured = true;
        return false;
    }

    return valid_parse;
}


bool parser::resync_parser(parser_state state){
    int temp_token_type;
    resync_status = true;
    //state to return
    bool return_state;
    //may be used to call proper parse function if needed
    parser_state new_state;
    //will be used to store the original state
    parser_state original_state;
    original_state = state;
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
            
            if(Current_parse_token_type == T_BEGIN){
                new_state = original_state;
                break;
            }
            else if(Current_parse_token_type == T_END){
                new_state = original_state;
                break;
            }
            else if(Current_parse_token_type == T_PROGRAM){
                new_state = original_state;
                break;
            }
            else if(Current_parse_token_type == T_PERIOD){
                new_state = original_state;
                break;
            }
            //DECLARATIONS


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



            if(Current_parse_token_type == T_PROCEDURE){
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


            //STATEMENTS


            //is a an assignment statement
            if(Current_parse_token_type == T_IDENTIFIER){
                new_state = S_BASE_STATEMENT;
                break;

            }
            else if(Current_parse_token_type == T_IF){
                new_state = S_BASE_STATEMENT;
                break;

            }
            else if(Current_parse_token_type == T_FOR){
                new_state = S_BASE_STATEMENT;
                break;

            }
            else if(Current_parse_token_type == T_RETURN){
                new_state = S_BASE_STATEMENT;
                break;

            }


            //SEMICOLONS
            Current_parse_token = Get_Valid_Token();
            if(Current_parse_token_type == T_SEMICOLON){
                Current_parse_token = Get_Valid_Token();
                new_state = original_state;
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
        new_state = original_state;

        break;

        //7
        case S_PARAMETER_LIST:

        break;

        //8
        case S_PARAMETER:

        break;

        //9
        case S_PROCEDURE_BODY:
         while(Current_parse_token_type!=T_SEMICOLON && Current_parse_token_type!=T_INVALID){
            
            if(Current_parse_token_type == T_BEGIN){
                new_state = original_state;
                break;
            }
            else if(Current_parse_token_type == T_END){
                new_state = original_state;
                break;
            }
            else if(Current_parse_token_type == T_PROCEDURE){
                new_state = original_state;
                break;
            }

            //DECLARATIONS


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



            if(Current_parse_token_type == T_PROCEDURE){
                new_state = S_BASE_DECLARATION;
                break;
            }
            else if(Current_parse_token_type == T_VARIABLE){
                if(prev_token_type == T_LPARAM){
                    new_state = S_PARAMETER_LIST;
                    break;
                }
                else{
                    new_state = S_BASE_DECLARATION;
                    break;
                }
            }
            else if(Current_parse_token_type == T_TYPE){
                new_state = S_BASE_DECLARATION;
                break;
            }


            //STATEMENTS


            //is a an assignment statement
            if(Current_parse_token_type == T_IDENTIFIER){
                new_state = S_BASE_STATEMENT;
                break;

            }
            else if(Current_parse_token_type == T_IF){
                new_state = S_BASE_STATEMENT;
                break;

            }
            else if(Current_parse_token_type == T_FOR){
                new_state = S_BASE_STATEMENT;
                break;

            }
            else if(Current_parse_token_type == T_RETURN){
                new_state = S_BASE_STATEMENT;
                break;

            }


            //SEMICOLONS
            Current_parse_token = Get_Valid_Token();
            if(Current_parse_token_type == T_SEMICOLON){
                Current_parse_token = Get_Valid_Token();
                new_state = original_state;
                break;
            }

        }

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
        while(Current_parse_token_type!=T_SEMICOLON && Current_parse_token_type!=T_INVALID){
            if(Current_parse_token_type == T_IF){
                new_state = S_BASE_STATEMENT;
                break;
            }
            // else if(Current_parse_token_type == T_THEN){
            //     Cure
            // }
            else if(Current_parse_token_type == T_FOR){
                new_state = S_BASE_STATEMENT;
                break;
            }
            else if(Current_parse_token_type == T_RETURN){
                new_state = S_BASE_STATEMENT;
                break;
            }
            // else if(Current_parse_token_type ==T_IDENTIFIER){
            //     new_state = S_BASE_DECLARATION;
            //     break;    
            // }
            else if(Current_parse_token_type == T_END){
                return true;
            }
            else if(Current_parse_token_type == T_IDENTIFIER){
                new_state = S_BASE_STATEMENT;
                break;

            }

            Current_parse_token = Get_Valid_Token();
        }

        break;

        //19
        case S_LOOP_STATEMENT:
        while(Current_parse_token_type!=T_SEMICOLON && Current_parse_token_type!=T_INVALID){
            if(Current_parse_token_type == T_IF){
                new_state = S_BASE_STATEMENT;
                break;
            }
            else if(Current_parse_token_type == T_FOR){
                new_state = S_BASE_STATEMENT;
                break;
            }
            else if(Current_parse_token_type == T_RETURN){
                new_state = S_BASE_STATEMENT;
                break;
            }
            else if(Current_parse_token_type == T_END){
                return true;
            }
            else if(Current_parse_token_type == T_IDENTIFIER){
                new_state = S_BASE_STATEMENT;
                break;

            }

            Current_parse_token = Get_Valid_Token();
            // if(Current_parse_token_type ==T_SEMICOLON){
            //     Current_parse_token = Get_Valid_Token();
            //     return true;
            // }
        }

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
        temp_token_type = Current_parse_token_type;
        return_state = parse_base_declaration();
        if(return_state){
            if((Current_parse_token_type == T_SEMICOLON) || (Current_parse_token_type == T_RPARAM && temp_token_type == T_VARIABLE)){
                Current_parse_token = Get_Valid_Token();
            }
            else{
                generate_error_report("Missing \";\" to complete declaration");
                errors_occured = true;
                resync_parser(original_state);
            }
        }
        else{

        }

        break;

        //5
        case S_PROCEDURE_DECLARATION:
        return_state = parse_procedure_declaration(false);
        if(return_state){
            if(Current_parse_token_type ==T_SEMICOLON){
                Current_parse_token = Get_Valid_Token();
            }
            else{
                generate_error_report("Missing \";\" to complete declaration");
                errors_occured = true;
                resync_parser(original_state);
            }
        }
        else{

        }


        break;

        //6
        case S_PROCEDURE_HEADER:
        //Current_parse_token = Get_Valid_Token();
        return true;
        // return_state = parse_procedure_header();

        break;

        //7
        case S_PARAMETER_LIST:
        //THESE USED STILL?
        return_state = parse_parameter_list("ERROR");
        return_state = parse_procedure_body();

        break;

        //8
        case S_PARAMETER:

        break;

        //9
        case S_PROCEDURE_BODY:

        break;

        //10
        case S_VARIABLE_DECLARATION:
        return_state = parse_variable_declaration(false);
        if(return_state){
            if(Current_parse_token_type ==T_SEMICOLON){
                Current_parse_token = Get_Valid_Token();
            }
            else{
                generate_error_report("Missing \";\" to complete declaration");
                errors_occured = true;
                return_state = resync_parser(original_state);
            }
        }
        else{

        }


        break;

        //11
        case S_TYPE_DECLARATION:
        return_state = parse_type_declaration(false);
        if(return_state){
            if(Current_parse_token_type ==T_SEMICOLON){
                Current_parse_token = Get_Valid_Token();
            }
            else{
                generate_error_report("Missing \";\" to complete declaration");
                errors_occured = true;
                return_state = resync_parser(original_state);
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
        return_state = parse_base_statement();
        if(return_state){
            if(Current_parse_token_type == T_SEMICOLON){
                Current_parse_token = Get_Valid_Token();
            }
            else{
                generate_error_report("Missing \";\" to end program statement");
                errors_occured = true;
                 return_state = resync_parser(original_state);
            }
        }

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

void parser::update_scopes(bool increment_scope_id){
    if(increment_scope_id){
        current_scope_id = number_of_scopes + 1;
        number_of_scopes++;
    }
    else{
        Lexer->symbol_table.remove_scope(current_scope_id);
        current_scope_id--;
        number_of_scopes--;
    }
}