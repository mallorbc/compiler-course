#ifndef PARSER_H
#define PARSER_H
#include "token.h"
#include "Typechecker.h"
#include "IRBuilder.h"
#include <iostream>
#include <cstddef>
#include <memory>
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

//we need to return both a token and a parse status; this struct will help with that
// struct token_and_status
// {
//     bool valid_parse = true;
//     token resolved_token;
// };
class Typechecker;

//These are parser/IR composition values, not frontend tokens.  Keeping the
//IDs here prevents declaration/occurrence tokens from accidentally becoming
//backend objects while the handwritten recursive descent remains intact.
struct lowered_expression
{
    token_and_status semantics;
    ir::ValueId value;
};

struct lowered_destination
{
    token_and_status semantics;
    ir::StorageId storage;
    ir::ValueId checked_index;
};

class parser
{
public:
    bool debugging = false;
    //constructors for the parser
    parser(std::string parse_file);
    ~parser();

    //data structures and methods for current tokens and look ahead tokens
    token Current_parse_token;
    int Current_parse_token_type;
    //These values will store Look_ahead_tokens[0] for ease of access
    token Next_parse_token;
    int Next_parse_token_type;
    //Monotonically records token-window advances so recovery loops can prove
    //that each iteration either changed grammar state or consumed input.
    std::size_t token_generation = 0;
    //Tracks nested expression calls solely to avoid repeating a parent
    //syntax-recovery diagnostic from an already-failed child expression.
    std::size_t expression_depth = 0;
    //vector that could be used to build up a queue of tokens
    std::vector<token> Look_ahead_tokens;
    token Get_Valid_Token();
    void collect_scanner_diagnostics();

    //Lexer object and the file that will be lexed by it
    scanner *Lexer = nullptr;
    std::string parse_file;

    //data strucutres and methods for generating error reports
    std::vector<std::string> error_reports;
    void add_error_report(std::string error_report);
    void generate_error_report(std::string error_message);
    void generate_error_report(std::string error_message, int line_number);
    void generate_error_report_previous_token(std::string error_message);
    void print_errors();
    int error_count();
    bool frontend_valid() const noexcept;
    bool can_generate_code() const noexcept;
    ir::ModuleStatus ir_status() const noexcept;
    const ir::Module &ir_module() const noexcept;
    const std::string &ir_reason() const noexcept;

    //parsing parts of the program
    bool parse_program();
    bool parse_program_header();
    bool parse_program_body();

    //methods for parsing declarations
    bool parse_base_declaration();
    //for standard variables
    bool parse_variable_declaration(bool is_global);
    bool parse_type_declaration(bool is_global);

    //methods for parsing part of the procedures
    bool parse_procedure_declaration(bool is_global);
    bool parse_procedure_header(bool is_global, token &candidate,
                                std::vector<token> &parameters,
                                std::vector<token> &header_symbols,
                                bool &semantic_valid);
    bool parse_procedure_body();

    //method for parsing type_mark which is used for type declarations
    bool parse_declared_type(data_types &resolved_type, std::vector<token> &enum_symbols);
    //methods used for parsing one or more parameters in a procedure declaration
    bool parse_parameter_list(std::vector<token> &parameters,
                              std::vector<token> &header_symbols,
                              bool &semantic_valid);
    bool parse_parameter(token &parameter, std::vector<token> &header_symbols,
                         bool &semantic_valid);

    //methods used for parsing statements
    bool parse_base_statement();

    ///bool parse_assignment_statement(token token_for_context);
    bool parse_assignment_statement(token destination_token);

    lowered_destination parse_assignment_destination(token destination_token);
    bool parse_optional_index(const token &base_occurrence, bool base_resolved,
                              const value_shape &base_shape,
                              ir::StorageId storage, bool load_element,
                              lowered_expression &base_result,
                              ir::ValueId &checked_index);

    bool parse_if_statement(const token &if_token);
    bool parse_loop_statement();
    bool parse_return_statement(const token &return_token);

    // bool parse_expression(token token_for_context);
    lowered_expression parse_expression();
    lowered_expression parse_arithOp();
    lowered_expression parse_relation();
    lowered_expression parse_term();
    lowered_expression parse_factor();

    bool parse_bound(int &upper_bound, bool &semantic_valid);
    bool parse_array_suffix(token &candidate, bool &semantic_valid);
    bool parse_number();
    lowered_expression parse_name(token identifier_token);

    bool parse_argument_list(std::vector<lowered_expression> &arguments);

    lowered_expression parse_procedure_call(const token &callee_occurrence,
                                            const token &canonical_callee,
                                            bool callee_resolved,
                                            const lowered_expression &callee_result);

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
    std::vector<int> active_scope_ids = {0};
    int next_scope_id = 1;

    //section for Typechecker
    bool resolve_identifier_use(const token &occurrence, token &resolved_token,
                                const std::string &kind = "identifier");
    bool resolve_procedure_use(const token &occurrence, token &resolved_token);
    bool declaration_target(bool explicitly_global) const;
    int declaration_scope(bool explicitly_global) const;
    void report_duplicate_declaration(const token &occurrence);

    Typechecker *type_checker = nullptr;
    ir::IRBuilder *ir_builder = nullptr;

private:
    //Keep the original public raw seams while making their ownership explicit.
    //The out-of-line destructor permits Typechecker to remain forward-declared
    //when this header is reached through Typechecker.h's legacy include cycle.
    std::unique_ptr<ir::IRBuilder> ir_builder_owner;
    std::unique_ptr<scanner> lexer_owner;
    std::unique_ptr<Typechecker> type_checker_owner;
};

#endif // !PARSER_H
