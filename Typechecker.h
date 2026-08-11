#ifndef TYPECHECKER_H
#define TYPECHECKER_H

#include "token.h"
#include <vector>
#include <iostream>
#include "parser.h"
#include "scanner.h"

// struct token_and_status
// {
//     bool valid_parse = true;
//     token resolved_token;
// };

enum type_of_statement
{
    STATEMENT_ASSIGN = 0,
    STATEMENT_IF = 1,
    STATEMENT_LOOP = 2,
    STATEMENT_RETURN = 3
};

enum typechecker_types
{
    typechecker_int = 100,
    typechecker_bool = 101,
    typechecker_float = 102,
    typechecker_string = 103,
    typechecker_null = 104
};

//These are semantic operations, not scanner tokens.  Keeping this small
//internal vocabulary separate lets the parser assemble <=, >=, ==, and !=
//without teaching the scanner artificial compound tokens.
enum semantic_operator
{
    SEM_ADD,
    SEM_SUBTRACT,
    SEM_MULTIPLY,
    SEM_DIVIDE,
    SEM_LESS,
    SEM_LESS_EQUAL,
    SEM_GREATER,
    SEM_GREATER_EQUAL,
    SEM_EQUAL,
    SEM_NOT_EQUAL,
    SEM_AND,
    SEM_OR,
    SEM_NOT,
    SEM_NEGATE
};

//this will be used to convert to a single type and for the base check
struct token_types_and_status
{
    typechecker_types token_one_type = typechecker_null;
    typechecker_types token_two_type = typechecker_null;
    bool compatible = false;
};
class parser;

class Typechecker
{
public:
    token first_token;
    token second_token;
    std::vector<token> relation_tokens;
    type_of_statement current_statement_type;
    token statement_key_token;

    Typechecker();
    Typechecker(parser *parent);
    bool set_statement_type(token statement_key_token);
    bool statement_is_finished();
    bool is_valid_relation();
    bool second_to_first();
    bool token_is_relationship(token token_to_check);
    bool second_relation_token_chains(token token_to_check);
    token_and_status feed_in_tokens(token token_to_feed);
    bool clear_tokens(bool move_second_to_first);
    void suppress_current_statement();
    token_and_status is_valid_operation();

    //The handwritten parser owns grammar traversal.  These helpers are a
    //non-streaming semantic layer for the type of one already-parsed node;
    //the returned token is a synthetic expression type, never a declaration.
    token make_expression_result(data_types result_type, const token &anchor) const;
    token_and_status check_unary_expression(semantic_operator operation,
                                             const token &operator_token,
                                             const token &operand);
    token_and_status check_binary_expression(semantic_operator operation,
                                              const token &operator_token,
                                              const token &left_operand,
                                              const token &right_operand);

    bool check_assignment_statement(token destination_token, token resolved_token);
    bool are_tokens_full();
    token_types_and_status token_types_compatible_at_all();

    bool first_relation_token_is_valid();

    bool is_float_or_int(typechecker_types token_one, typechecker_types token_two);
    bool is_bool_or_int(typechecker_types token_one, typechecker_types token_two);
    bool both_are_strings(typechecker_types token_one, typechecker_types token_two);
    std::string give_token_type_name(typechecker_types type_to_get);
    bool check_return_statement(token resolved_token, token procedure_token);
    bool check_if_statement(token token_to_check);

    bool check_loop_statement(token token_to_check);

    typechecker_types convert_to_typechecker_types(token token_to_convert);

    bool debugger = false;
    parser *parser_parent = nullptr;

    bool type_error_occured = false;
    bool statement_suppressed = false;
    //Typechecker(parser *parent_test);
};

#endif // !TYPECHECKER_H
