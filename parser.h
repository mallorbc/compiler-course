#ifndef PARSER_H
#define PARSER_H
#include "token.h"
#include <iostream>
#include <string>
#include <vector>
#include "scanner.h"
// #include "TypeChecker.h"

//this will be passed into the resyncer to know the state of the parser
enum parser_state
{
    S_PROGRAM = 1,
    S_PROGRAM_HEADER = 2,
    S_PROGRAM_BODY = 3,
    S_BASE_DECLARATION = 4,
    S_PROCEDURE_DECLARATION = 5,
    S_PROCEDURE_HEADER = 6,
    S_PARAMETER_LIST = 7,
    S_PARAMETER = 8,
    S_PROCEDURE_BODY = 9,
    S_VARIABLE_DECLARATION = 10,
    S_TYPE_DECLARATION = 11,
    S_TYPE_MARK = 12,
    S_BOUND = 13,
    S_BASE_STATEMENT = 14,
    S_PROCEDURE_CALL = 15,
    S_ASSIGNMENT_STATMENT = 16,
    S_ASSIGNMENT_DESTINATION = 17,
    S_IF_STATEMENT = 18,
    S_LOOP_STATEMENT = 19,
    S_RETURN_STATEMENT = 20,
    S_EXPRESSION = 21,
    S_ARITH_OP = 22,
    S_RELATION = 23,
    S_TERM = 24,
    S_FACTOR = 25,
    S_NAME = 26,
    S_ARGUMENT_LIST = 27,
    S_NUMBER = 28
};

class parser
{
public:
    bool debugging = false;
    //constructors for the parser
    parser(std::string parse_file);

    //data structures and methods for current tokens and look ahead tokens
    token Current_parse_token;
    int Current_parse_token_type;
    //These values will store Look_ahead_tokens[0] for ease of access
    token Next_parse_token;
    int Next_parse_token_type;
    //vector that could be used to build up a queue of tokens
    std::vector<token> Look_ahead_tokens;
    token Get_Valid_Token();

    //Lexer object and the file that will be lexed by it
    scanner *Lexer;
    std::string parse_file;

    //data strucutres and methods for generating error reports
    std::vector<std::string> error_reports;
    void add_error_report(std::string error_report);
    void generate_error_report(std::string error_message);
    void print_errors();

    //parsing parts of the program
    bool parse_program();
    bool parse_program_header();
    bool parse_program_body();

    //methods for parsing declarations
    bool parse_base_declaration();
    //for standard variables
    bool parse_variable_declaration(bool is_global);
    //for procedure parameters in their declaration
    bool parse_variable_declaration(bool is_global, std::string procedure_name);
    bool parse_type_declaration(bool is_global);

    //methods for parsing part of the procedures
    bool parse_procedure_declaration(bool is_global);
    bool parse_procedure_header(bool is_global);
    bool parse_procedure_body();

    //method for parsing type_mark which is used for type declarations
    bool parse_type_mark();
    //same as parse_type_mark but only for procedure variables and variables; 0 means procedure, 1 means var
    bool parse_type_mark(std::string procedure_name, int proc_or_var);
    //methods used for parsing one or more parameters in a procedure declaration
    bool parse_parameter_list(std::string procedure_name);
    bool parse_parameter(std::string procedure_name);

    //methods used for parsing statements
    bool parse_base_statement();

    bool parse_assignment_statement();
    bool parse_assignment_destination();

    bool parse_if_statement();
    bool parse_loop_statement();
    bool parse_return_statement();

    bool parse_expression();
    bool parse_arithOp();
    bool parse_relation();
    bool parse_term();
    bool parse_factor();

    bool parse_bound();
    bool parse_number();
    bool parse_name();

    bool parse_argument_list();

    bool parse_procedure_call();

    bool resync_parser(parser_state state);

    void update_scopes(bool increment_scope_id);

    token prev_token;
    int prev_token_type;
    //tracks whether or not the parser is resyncing, is used for breaking out of infinite loops
    bool resync_status = false;
    //not sure if this does anything anymore; big oof
    bool parsing_statements = false;
    //tracks whether an error has occured
    bool errors_occured = false;
    //tracks the current scope the parser is in, 0 is the outermost scope
    int current_scope_id = 0;
    //tracks the total number of scopes that have been made, will be used for the id
    int number_of_scopes = 0;

    //section for Typechecker

    // TypeChecker *type_checker;
};

#endif // !PARSER_H