#ifndef TYPECHECKER_H
#define TYPECHECKER_H

#include "token.h"
#include <vector>
#include <iostream>

enum type_of_statement
{
    STATEMENT_ASSIGN = 0,
    STATEMENT_IF = 1,
    STATEMENT_LOOP = 2,
    STATEMENT_RETURN = 3
};

class Typechecker
{
public:
    token first_token;
    token second_token;
    std::vector<token> relation_tokens;
    type_of_statement current_statement_type;

    Typechecker();
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
    bool token_types_compatible_at_all();

    bool debugger = false;
};

#endif // !TYPECHECKER_H