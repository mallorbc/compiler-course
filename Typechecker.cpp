#include "Typechecker.h"

Typechecker::Typechecker()
{
    first_token.type = T_NULL;
    second_token.type = T_NULL;
}

Typechecker::Typechecker(parser *parent)
{
    parser_parent = parent;
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
    //clear_tokens();
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
    clear_tokens(false);

    return true;
}

bool Typechecker::feed_in_tokens(token token_to_feed)
{
    bool return_value = false;
    // if (current_statement_type == STATEMENT_ASSIGN)
    // {
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
    // }
    // else if (current_statement_type == STATEMENT_RETURN)
    // {
    //     if (are_tokens_full())
    //     {
    //     }
    // }
    if (are_tokens_full())
    {
        is_valid_operation();
        clear_tokens(true);
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

bool Typechecker::clear_tokens(bool move_second_to_first)
{
    token temp_token;
    if (move_second_to_first)
    {
        temp_token = second_token;
    }
    first_token.type = T_NULL;
    second_token.type = T_NULL;
    relation_tokens.clear();
    if (move_second_to_first)
    {
        first_token = temp_token;
    }
    return true;
}

bool Typechecker::is_valid_operation()
{
    //goes through a giant case statement and then converts the types into one single type for easy comparison
    token_types_and_status checked_tokens;
    typechecker_types token_one_type;
    typechecker_types token_two_type;
    checked_tokens = token_types_compatible_at_all();
    token_one_type = checked_tokens.token_one_type;
    token_two_type = checked_tokens.token_two_type;
    bool compatible = checked_tokens.compatible;
    //these two strings will be used to build error messages
    std::string token_one_type_name = "";
    std::string token_two_type_name = "";
    std::string error_message = "";
    int line_error = 0;
    token_one_type_name = give_token_type_name(token_one_type);
    token_two_type_name = give_token_type_name(token_two_type);
    //this means they are never compatible
    if (!compatible)
    {
        error_message = "Type \"" + token_one_type_name + "\" and type \"" + token_two_type_name + "\" have no valid operations";
        // line_error = second_token.line_found;
        line_error = parser_parent->Lexer->current_line;
        parser_parent->errors_occured = true;
        parser_parent->generate_error_report(error_message, line_error);
        error_message = "";
        type_error_occured = true;
        //set error message?
        return false;
    }
    //check the relation operators first
    //no relation token, therfore an error occured
    if (relation_tokens.size() == 0)
    {
        return false;
    }
    //there is one relation
    else if (relation_tokens.size() == 1)
    {
        //these are all the relation tokens that can be on there own
        switch (relation_tokens[0].type)
        {
            //must be an iteger or float for both
        case T_PLUS:
            if (is_float_or_int(token_one_type, token_two_type))
            {
                return true;
            }
            else
            {
                parser_parent->generate_error_report("Arithmetic operations must be between floats and integers");
                parser_parent->errors_occured = true;
                return false;
            }

            break;

            //must be an iteger or float for both
        case T_MINUS:
            if (is_float_or_int(token_one_type, token_two_type))
            {
                return true;
            }
            else
            {
                parser_parent->generate_error_report("Arithmetic operations must be between floats and integers");
                return false;
            }

            break;

        case T_GREATER:
            if (is_float_or_int(token_one_type, token_two_type))
            {
                return true;
            }

            break;

        case T_LESS:
            if (is_float_or_int(token_one_type, token_two_type))
            {
                return true;
            }

            break;

            //must be an iteger or float for both
        case T_MULT:
            if (is_float_or_int(token_one_type, token_two_type))
            {
                return true;
            }
            else
            {
                parser_parent->generate_error_report("Arithmetic operations must be between floats and integers");
                return false;
            }
            break;
            //must be an iteger or float for both
        case T_SLASH:
            if (is_float_or_int(token_one_type, token_two_type))
            {
                return true;
            }
            else
            {
                parser_parent->generate_error_report("Arithmetic operations must be between floats and integers");
                return false;
            }

            break;

        default:
            return false;
        }
    }
    //there are two relation tokens in the relation
    else if (relation_tokens.size() == 2)
    {
        //the second token when there are two tokens must be an equal
        if (relation_tokens[1].type != T_ASSIGN)
        {

            return false;
        }
        else
        {
            if (!first_relation_token_is_valid())
            {
                return false;
            }
            switch (relation_tokens[0].type)
            {

            case T_GREATER:

                break;

            case T_LESS:

                break;

            case T_ASSIGN:
                break;

            case T_EXCLAM:
                break;
            }
        }
    }
    else
    {
        //an error occured as it can't have more than 2
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

token_types_and_status Typechecker::token_types_compatible_at_all()
{
    token_types_and_status return_object;
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
            return_object.token_one_type = typechecker_bool;
            if (second_token.identifier_data_type == TYPE_INT)
            {
                return_object.token_two_type = typechecker_int;
                return_value = true;
            }
            if (second_token.identifier_data_type == TYPE_BOOL)
            {
                return_object.token_two_type = typechecker_bool;
                return_value = true;
            }
            if (second_token.identifier_data_type == TYPE_FLOAT)
            {
                return_object.token_two_type = typechecker_float;
                return_value = false;
            }
            if (second_token.identifier_data_type == TYPE_STRING)
            {
                return_object.token_two_type = typechecker_string;
                return_value = false;
            }

            break;

        case TYPE_FLOAT:
            return_object.token_one_type = typechecker_float;
            if (second_token.identifier_data_type == TYPE_FLOAT)
            {
                return_object.token_two_type = typechecker_float;
                return_value = true;
            }
            if (second_token.identifier_data_type == TYPE_INT)
            {
                return_object.token_two_type = typechecker_int;
                return_value = true;
            }
            if (second_token.identifier_data_type == TYPE_STRING)
            {
                return_object.token_two_type = typechecker_string;
                return_value = false;
            }
            if (second_token.identifier_data_type == TYPE_BOOL)
            {
                return_object.token_two_type = typechecker_bool;
                return_value = false;
            }

            break;

        case TYPE_INT:
            return_object.token_one_type = typechecker_int;
            if (second_token.identifier_data_type == TYPE_FLOAT)
            {
                return_object.token_two_type = typechecker_float;
                return_value = true;
            }
            if (second_token.identifier_data_type == TYPE_BOOL)
            {
                return_object.token_two_type = typechecker_bool;
                return_value = true;
            }
            if (second_token.identifier_data_type == TYPE_INT)
            {
                return_object.token_two_type = typechecker_int;
                return_value = true;
            }
            if (second_token.identifier_data_type == TYPE_STRING)
            {
                return_object.token_two_type = typechecker_string;
                return_value = false;
            }

            break;

        case TYPE_STRING:
            return_object.token_one_type = typechecker_string;
            if (second_token.identifier_data_type == TYPE_STRING)
            {
                return_object.token_two_type = typechecker_string;
                return_value = true;
            }
            if (second_token.identifier_data_type == TYPE_FLOAT)
            {
                return_object.token_two_type = typechecker_float;
                return_value = false;
            }
            if (second_token.identifier_data_type == TYPE_BOOL)
            {
                return_object.token_two_type = typechecker_bool;
                return_value = false;
            }
            if (second_token.identifier_data_type == TYPE_INT)
            {
                return_object.token_two_type = typechecker_int;
                return_value = false;
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
            return_object.token_two_type = typechecker_bool;
            if (first_token.type == T_INTEGER_VALUE)
            {
                return_object.token_one_type = typechecker_int;
                return_value = true;
            }
            if (first_token.type == T_BOOL_VALUE)
            {
                return_object.token_one_type = typechecker_bool;
                return_value = true;
            }
            if (first_token.type == T_FLOAT_VALUE)
            {
                return_object.token_one_type = typechecker_float;
                return_value = false;
            }
            if (first_token.type == T_STRING_VALUE)
            {
                return_object.token_one_type = typechecker_string;
                return_value = false;
            }

            break;

        case TYPE_FLOAT:
            return_object.token_two_type = typechecker_float;
            if (first_token.type == T_FLOAT_VALUE)
            {
                return_object.token_one_type = typechecker_float;
                return_value = true;
            }
            if (first_token.type == T_INTEGER_VALUE)
            {
                return_object.token_one_type = typechecker_int;
                return_value = true;
            }
            if (first_token.type == T_BOOL_VALUE)
            {
                return_object.token_one_type = typechecker_bool;
                return_value = false;
            }
            if (first_token.type == T_STRING_VALUE)
            {
                return_object.token_one_type = typechecker_string;
                return_value = false;
            }

            break;

        case TYPE_INT:
            return_object.token_two_type = typechecker_int;
            if (first_token.type == T_FLOAT_VALUE)
            {
                return_object.token_one_type = typechecker_float;
                return_value = true;
            }
            if (first_token.type == T_BOOL_VALUE)
            {
                return_object.token_one_type = typechecker_bool;
                return_value = true;
            }
            if (first_token.type == T_INTEGER_VALUE)
            {
                return_object.token_one_type = typechecker_int;
                return_value = true;
            }
            if (first_token.type == T_STRING_VALUE)
            {
                return_object.token_one_type = typechecker_int;
                return_value = false;
            }

            break;

        case TYPE_STRING:
            return_object.token_two_type = typechecker_string;
            if (first_token.type == T_STRING_VALUE)
            {
                return_object.token_one_type = typechecker_string;
                return_value = true;
            }
            if (first_token.type == T_FLOAT_VALUE)
            {
                return_object.token_one_type = typechecker_float;
                return_value = false;
            }
            if (first_token.type == T_BOOL_VALUE)
            {
                return_object.token_one_type = typechecker_bool;
                return_value = false;
            }
            if (first_token.type == T_INTEGER_VALUE)
            {
                return_object.token_one_type = typechecker_int;
                return_value = false;
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
            return_object.token_one_type = typechecker_bool;
            if (second_token.type == T_INTEGER_VALUE)
            {
                return_object.token_two_type = typechecker_int;
                return_value = true;
            }
            if (second_token.type == T_BOOL_VALUE)
            {
                return_object.token_two_type = typechecker_bool;
                return_value = true;
            }
            if (second_token.type == T_FLOAT_VALUE)
            {
                return_object.token_two_type = typechecker_float;
                return_value = false;
            }
            if (second_token.type == T_STRING_VALUE)
            {
                return_object.token_two_type = typechecker_string;
                return_value = false;
            }

            break;

        case TYPE_FLOAT:
            return_object.token_one_type = typechecker_float;
            if (second_token.type == T_FLOAT_VALUE)
            {
                return_object.token_two_type = typechecker_float;
                return_value = true;
            }
            if (second_token.type == T_INTEGER_VALUE)
            {
                return_object.token_two_type = typechecker_int;
                return_value = true;
            }
            if (second_token.type == T_BOOL_VALUE)
            {
                return_object.token_two_type = typechecker_bool;
                return_value = true;
            }
            if (second_token.type == T_STRING_VALUE)
            {
                return_object.token_two_type = typechecker_string;
                return_value = true;
            }

            break;

        case TYPE_INT:
            return_object.token_one_type = typechecker_int;
            if (second_token.type == T_FLOAT_VALUE)
            {
                return_object.token_two_type = typechecker_float;
                return_value = true;
            }
            if (second_token.type == T_BOOL_VALUE)
            {
                return_object.token_two_type = typechecker_bool;
                return_value = true;
            }
            if (second_token.type == T_INTEGER_VALUE)
            {
                return_object.token_two_type = typechecker_int;
                return_value = true;
            }
            if (second_token.type == T_STRING_VALUE)
            {
                return_object.token_two_type = typechecker_string;
                return_value = false;
            }

            break;

        case TYPE_STRING:
            return_object.token_one_type = typechecker_string;
            if (second_token.type == T_STRING_VALUE)
            {
                return_object.token_two_type = typechecker_string;
                return_value = true;
            }
            if (second_token.type == T_FLOAT_VALUE)
            {
                return_object.token_two_type = typechecker_float;
                return_value = false;
            }
            if (second_token.type == T_BOOL_VALUE)
            {
                return_object.token_two_type = typechecker_bool;
                return_value = false;
            }
            if (second_token.type == T_INTEGER_VALUE)
            {
                return_object.token_two_type = typechecker_int;
                return_value = false;
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
            return_object.token_one_type = typechecker_bool;
            if (second_token.type == T_INTEGER_VALUE)
            {
                return_object.token_two_type = typechecker_int;
                return_value = true;
            }
            if (second_token.type == T_BOOL_VALUE)
            {
                return_object.token_two_type = typechecker_bool;
                return_value = true;
            }
            if (second_token.type == T_FLOAT_VALUE)
            {
                return_object.token_two_type = typechecker_float;
                return_value = false;
            }
            if (second_token.type == T_STRING_VALUE)
            {
                return_object.token_two_type = typechecker_string;
                return_value = false;
            }

            break;

        case T_FLOAT_VALUE:
            return_object.token_one_type = typechecker_float;
            if (second_token.type == T_FLOAT_VALUE)
            {
                return_object.token_two_type = typechecker_float;
                return_value = true;
            }
            if (second_token.type == T_INTEGER_VALUE)
            {
                return_object.token_two_type = typechecker_int;
                return_value = true;
            }
            if (second_token.type == T_BOOL_VALUE)
            {
                return_object.token_two_type = typechecker_bool;
                return_value = false;
            }
            if (second_token.type == T_STRING_VALUE)
            {
                return_object.token_two_type = typechecker_string;
                return_value = false;
            }

            break;

        case T_INTEGER_VALUE:
            return_object.token_one_type = typechecker_int;
            if (second_token.type == T_FLOAT_VALUE)
            {
                return_object.token_two_type = typechecker_float;
                return_value = true;
            }
            if (second_token.type == T_BOOL_VALUE)
            {
                return_object.token_two_type = typechecker_bool;
                return_value = true;
            }
            if (second_token.type == T_INTEGER_VALUE)
            {
                return_object.token_two_type = typechecker_int;
                return_value = true;
            }
            if (second_token.type == T_STRING_VALUE)
            {
                return_object.token_two_type = typechecker_string;
                return_value = false;
            }

            break;

        case T_STRING_VALUE:
            return_object.token_one_type = typechecker_string;
            if (second_token.type == T_STRING_VALUE)
            {
                return_object.token_two_type = typechecker_string;
                return_value = true;
            }
            if (second_token.type == T_FLOAT_VALUE)
            {
                return_object.token_two_type = typechecker_float;
                return_value = true;
            }
            if (second_token.type == T_BOOL_VALUE)
            {
                return_object.token_two_type = typechecker_bool;
                return_value = true;
            }
            if (second_token.type == T_INTEGER_VALUE)
            {
                return_object.token_two_type = typechecker_int;
                return_value = true;
            }

            break;

        default:
            return_value = false;
            break;
        }
    }
    return_object.compatible = return_value;
    return return_object;
}

bool Typechecker::first_relation_token_is_valid()
{
    bool return_value = true;
    switch (relation_tokens[0].type)
    {

    case T_GREATER:

        break;

    case T_LESS:

        break;

    case T_ASSIGN:
        break;

    case T_EXCLAM:
        break;

    default:
        return_value = false;
    }
    return return_value;
}

bool Typechecker::is_float_or_int(typechecker_types token_one, typechecker_types token_two)
{
    if (((token_one == typechecker_float) || (token_one == typechecker_int)) && ((token_two == typechecker_float) || (token_two == typechecker_int)))
    {
        return true;
    }
    else
    {
        return false;
    }
}

bool Typechecker::is_bool_or_int(typechecker_types token_one, typechecker_types token_two)
{
    if (((token_one == typechecker_bool) || (token_one == typechecker_int)) && ((token_two == typechecker_bool) || (token_two == typechecker_int)))
    {
        return true;
    }
    else
    {
        return false;
    }
}

std::string Typechecker::give_token_type_name(typechecker_types type_to_get)
{
    std::string return_string;
    switch (type_to_get)
    {
    case typechecker_bool:
        return_string = "Bool";
        break;

    case typechecker_float:
        return_string = "Float";
        break;

    case typechecker_int:
        return_string = "Integer";
        break;

    case typechecker_string:
        return_string = "String";
        break;
    }
    return return_string;
}

bool Typechecker::check_return_statement(token resolved_token, token procedure_token)
{
    bool compatible = false;
    token_types_and_status checked_tokens;
    typechecker_types token_one_type;
    typechecker_types token_two_type;
    //these two strings will be used to build error messages
    std::string token_one_type_name = "";
    std::string token_two_type_name = "";
    std::string error_message = "";
    int line_error = 0;
    clear_tokens(false);
    first_token = resolved_token;
    second_token = procedure_token;
    checked_tokens = token_types_compatible_at_all();
    token_one_type = checked_tokens.token_one_type;
    token_two_type = checked_tokens.token_two_type;
    compatible = checked_tokens.compatible;
    token_one_type_name = give_token_type_name(token_one_type);
    token_two_type_name = give_token_type_name(token_two_type);
    if (!compatible)
    {
        error_message = "Procedure is of type \"" + token_one_type_name + "\" which is not compatible with return type of \"" + token_two_type_name + "\"";
        line_error = parser_parent->Lexer->current_line;
        parser_parent->errors_occured = true;
        parser_parent->generate_error_report(error_message, line_error);
        error_message = "";
        //set error message?
        return false;
    }
    //it may be compatible, need to check
    else
    {
        //the same types always work
        if (token_one_type == token_two_type)
        {
            return true;
        }
    }
}
