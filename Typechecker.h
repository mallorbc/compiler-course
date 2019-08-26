#ifndef TYPECHECKER_H
#define TYPECHECKER_H

#include "token.h"
#include <vector>
#include <iostream>
#include "parser.h"
#include "scanner.h"

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

    Typechecker();
    Typechecker(parser *parent);
    bool set_statement_type(token statement_key_token);
    bool statement_is_finished();
    bool is_valid_relation();
    bool second_to_first();
    bool token_is_relationship(token token_to_check);
    bool second_relation_token_chains(token token_to_check);
    bool feed_in_tokens(token token_to_feed);
    bool clear_tokens(bool move_second_to_first);
    bool is_valid_operation();

    bool check_assignment_statement(token destination_token, token resolved_token);
    bool are_tokens_full();
    token_types_and_status token_types_compatible_at_all();

    bool first_relation_token_is_valid();

    bool is_float_or_int(typechecker_types token_one, typechecker_types token_two);
    bool is_bool_or_int(typechecker_types token_one, typechecker_types token_two);
    bool both_are_strings(typechecker_types token_one, typechecker_types token_two);
    std::string give_token_type_name(typechecker_types type_to_get);
    bool check_return_statement(token resolved_token, token procedure_token);

    bool debugger = false;
    parser *parser_parent;

    bool type_error_occured = false;
    //Typechecker(parser *parent_test);
};

#endif // !TYPECHECKER_H