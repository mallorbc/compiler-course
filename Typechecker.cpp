#include "Typechecker.h"

Typechecker::Typechecker()
{
    first_token.type = T_NULL;
    second_token.type = T_NULL;
}

bool Typechecker::is_valid_relation()
{

    return true;
}

bool Typechecker::statement_is_finished()
{

    return true;
}

bool Typechecker::second_to_first()
{
    token temp_token;
    temp_token = second_token;
    clear_tokens();
    first_token = second_token;
    return true;
}

bool Typechecker::set_statement_type(token key_token)
{
    //assingment statements start with identifiers
    if (key_token.type == T_IDENTIFIER)
    {
        current_statement_type = STATEMENT_ASSIGN;
    }
    else if (key_token.type == T_IF)
    {
        current_statement_type = STATEMENT_IF;
    }
    else if (key_token.type == T_FOR)
    {
        current_statement_type = STATEMENT_LOOP;
    }
    else if (key_token.type == T_RETURN)
    {
        current_statement_type = STATEMENT_RETURN;
    }

    return true;
}

bool Typechecker::feed_in_tokens(token token_to_feed)
{
    bool return_value = false;
    //if nothing has been read in yet
    if (first_token.type == T_NULL)
    {
        first_token = token_to_feed;
        return true;
    }
    else if ((first_token.type != T_NULL) && (second_token.type == T_NULL) && token_is_relationship(token_to_feed))
    {
        //token must be either an arithop or a relation
        if (!token_is_relationship(token_to_feed))
        {
            if (debugger)
            {
                std::cout << "This is an error" << std::endl;
            }
        }
        else
        {
            if (relation_tokens.size() < 2)
            {
                //adds the token if it is valid and there are no other tokens
                if (relation_tokens.size() == 0)
                {
                    relation_tokens.push_back(token_to_feed);
                    return true;
                }
                //only some tokens can chain
                if ((second_relation_token_chains(token_to_feed)) && (relation_tokens.size() == 1))
                {
                    relation_tokens.push_back(token_to_feed);
                    return true;
                }
            }
            else
            {
                if (debugger)
                {
                    std::cout << "To many relation tokens" << std::endl;
                }
            }
        }
    }
    else if (second_token.type == T_NULL)
    {
        second_token = token_to_feed;
        return_value = true;
    }
    else
    {
        //std::cout << "to many tokens" << std::endl;
    }
    if (are_tokens_full())
    {
        is_valid_operation();
    }
    return return_value;
}

bool Typechecker::token_is_relationship(token token_to_check)
{

    int token_type = token_to_check.type;

    bool return_value = true;
    switch (token_type)
    {

    case T_AMPERSAND:

        break;

    case T_ASSIGN:

        break;

    case T_COLON:

        break;

    case T_EXCLAM:

        break;

    case T_GREATER:

        break;

    case T_LESS:

        break;

    case T_MINUS:

        break;

    case T_MULT:

        break;

    case T_NOT:

        break;

    case T_PLUS:

        break;

    case T_SLASH:

        break;

    case T_VERTICAL_BAR:

        break;

    default:
        return false;
    }

    return return_value;
}

bool Typechecker::second_relation_token_chains(token token_to_check)
{
    //the only valid token chain is where the second token is equals or a colon
    if (token_to_check.type != T_ASSIGN)
    {
        return false;
    }
    else
    {
        //only some tokens allow chains
        int previous_token_type = relation_tokens[0].type;

        bool return_value = true;
        switch (previous_token_type)
        {
        case T_ASSIGN:

            break;

        case T_COLON:

            break;

        case T_EXCLAM:
            break;

        case T_GREATER:

            break;

        case T_LESS:

            break;

        default:
            return false;
        }
    }

    //return true;
}

bool Typechecker::clear_tokens()
{
    first_token.type = T_NULL;
    second_token.type = T_NULL;
    relation_tokens.clear();
    return true;
}

bool Typechecker::is_valid_operation()
{
    if (!token_types_compatible_at_all())
    {
        return false;
    }
    return true;
}

bool Typechecker::check_assignment_statement(token destination_token, token resolved_token)
{

    return true;
}

bool Typechecker::are_tokens_full()
{
    if ((first_token.type != T_NULL) && (second_token.type != T_NULL) && (relation_tokens.size() > 0))
    {
        return true;
    }
    else
    {
        return false;
    }
}

bool Typechecker::token_types_compatible_at_all()
{
    bool return_value = false;
    bool first_token_is_identifer = false;
    bool second_token_is_identifier = false;

    //we store the data types in different locations depending on whether or not the token is an identifier
    if (first_token.type == T_IDENTIFIER)
    {
        first_token_is_identifer = true;
    }
    if (second_token.type == T_IDENTIFIER)
    {
        second_token_is_identifier = true;
    }

    if ((first_token_is_identifer) && (second_token_is_identifier))
    {
        switch (first_token.identifier_data_type)
        {
        case TYPE_BOOL:
            if (second_token.identifier_data_type == TYPE_INT)
            {
                return_value = true;
            }
            if (second_token.identifier_data_type == TYPE_BOOL)
            {
                return_value = true;
            }

            break;

        case TYPE_FLOAT:
            if (second_token.identifier_data_type == TYPE_FLOAT)
            {
                return_value = true;
            }
            if (second_token.identifier_data_type == TYPE_INT)
            {
                return_value = true;
            }

            break;

        case TYPE_INT:
            if (second_token.identifier_data_type == TYPE_FLOAT)
            {
                return_value = true;
            }
            if (second_token.identifier_data_type == TYPE_BOOL)
            {
                return_value = true;
            }
            if (second_token.identifier_data_type == TYPE_INT)
            {
                return_value = true;
            }

            break;

        case TYPE_STRING:
            if (second_token.identifier_data_type == TYPE_STRING)
            {
                return_value = true;
            }

            break;

        default:
            return_value = false;
            break;
        }
    }

    else if ((!first_token_is_identifer) && (second_token_is_identifier))
    {
        switch (second_token.identifier_data_type)
        {
        case TYPE_BOOL:
            if (first_token.type == T_INTEGER_VALUE)
            {
                return_value = true;
            }
            if (first_token.type == T_BOOL_VALUE)
            {
                return_value = true;
            }

            break;

        case TYPE_FLOAT:
            if (first_token.type == T_FLOAT_VALUE)
            {
                return_value = true;
            }
            if (first_token.type == T_INTEGER_VALUE)
            {
                return_value = true;
            }

            break;

        case TYPE_INT:
            if (first_token.type == T_FLOAT_VALUE)
            {
                return_value = true;
            }
            if (first_token.type == T_BOOL_VALUE)
            {
                return_value = true;
            }
            if (first_token.type == T_INTEGER_VALUE)
            {
                return_value = true;
            }

            break;

        case TYPE_STRING:
            if (first_token.type == T_STRING_VALUE)
            {
                return_value = true;
            }

            break;

        default:
            return_value = false;
            break;
        }
    }

    else if ((first_token_is_identifer) && (!second_token_is_identifier))
    {
        switch (first_token.identifier_data_type)
        {
        case TYPE_BOOL:
            if (second_token.type == T_INTEGER_VALUE)
            {
                return_value = true;
            }
            if (second_token.type == T_BOOL_VALUE)
            {
                return_value = true;
            }

            break;

        case TYPE_FLOAT:
            if (second_token.type == T_FLOAT_VALUE)
            {
                return_value = true;
            }
            if (second_token.type == T_INTEGER_VALUE)
            {
                return_value = true;
            }

            break;

        case TYPE_INT:
            if (second_token.type == T_FLOAT_VALUE)
            {
                return_value = true;
            }
            if (second_token.type == T_BOOL_VALUE)
            {
                return_value = true;
            }
            if (second_token.type == T_INTEGER_VALUE)
            {
                return_value = true;
            }

            break;

        case TYPE_STRING:
            if (second_token.type == T_STRING_VALUE)
            {
                return_value = true;
            }

            break;

        default:
            return_value = false;
            break;
        }
    }

    else if ((!first_token_is_identifer) && (!second_token_is_identifier))
    {
        switch (first_token.type)
        {
        case T_BOOL_VALUE:
            if (second_token.type == T_INTEGER_VALUE)
            {
                return_value = true;
            }
            if (second_token.type == T_BOOL_VALUE)
            {
                return_value = true;
            }

            break;

        case T_FLOAT_VALUE:
            if (second_token.type == T_FLOAT_VALUE)
            {
                return_value = true;
            }
            if (second_token.type == T_INTEGER_VALUE)
            {
                return_value = true;
            }

            break;

        case T_INTEGER_VALUE:
            if (second_token.type == T_FLOAT_VALUE)
            {
                return_value = true;
            }
            if (second_token.type == T_BOOL_VALUE)
            {
                return_value = true;
            }
            if (second_token.type == T_INTEGER_VALUE)
            {
                return_value = true;
            }

            break;

        case T_STRING_VALUE:
            if (second_token.type == T_STRING_VALUE)
            {
                return_value = true;
            }

            break;

        default:
            return_value = false;
            break;
        }
    }
    return return_value;
}