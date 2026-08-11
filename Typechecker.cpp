#include "Typechecker.h"

namespace
{

bool is_boolean_literal(int token_type)
{
    return token_type == T_BOOL_VALUE || token_type == T_TRUE || token_type == T_FALSE;
}

int valid_line(int line_number)
{
    return line_number > 0 ? line_number : 1;
}

int operation_line(const std::vector<token> &relation_tokens, const token &first_token, const token &second_token)
{
    if (!relation_tokens.empty())
    {
        return valid_line(relation_tokens.front().line_found);
    }
    if (second_token.line_found > 0)
    {
        return second_token.line_found;
    }
    return valid_line(first_token.line_found);
}

data_types expression_data_type(const token &value)
{
    if (value.type == T_IDENTIFIER)
    {
        return value.identifier_data_type;
    }
    switch (value.type)
    {
    case T_INTEGER_VALUE:
        return TYPE_INT;
    case T_FLOAT_VALUE:
        return TYPE_FLOAT;
    case T_STRING_VALUE:
        return TYPE_STRING;
    case T_BOOL_VALUE:
    case T_TRUE:
    case T_FALSE:
        return TYPE_BOOL;
    default:
        return TYPE_NONE;
    }
}

bool is_numeric(data_types value_type)
{
    return value_type == TYPE_INT || value_type == TYPE_FLOAT;
}

bool is_bool_or_integer(data_types value_type)
{
    return value_type == TYPE_BOOL || value_type == TYPE_INT;
}

std::string procedure_type_name(data_types value_type)
{
    switch (value_type)
    {
    case TYPE_INT:
        return "integer";
    case TYPE_FLOAT:
        return "float";
    case TYPE_STRING:
        return "string";
    case TYPE_BOOL:
        return "bool";
    case TYPE_NONE:
        return "unknown";
    }
    return "unknown";
}

void report_expression_error(Typechecker *checker, const std::string &message,
                             const token &operator_token)
{
    if (checker->statement_suppressed)
    {
        return;
    }
    if (checker->parser_parent != NULL)
    {
        checker->parser_parent->errors_occured = true;
        checker->parser_parent->generate_error_report(message,
                                                      valid_line(operator_token.line_found));
    }
    //Expression folding is deliberately independent from the legacy token
    //accumulator.  In particular, an invalid fold must not clear or otherwise
    //mutate first_token, second_token, or relation_tokens.
    checker->statement_suppressed = true;
    checker->type_error_occured = true;
}

void report_call_error(Typechecker *checker, const std::string &message,
                       const token &anchor)
{
    if (checker->statement_suppressed)
    {
        return;
    }
    if (checker->parser_parent != NULL)
    {
        checker->parser_parent->errors_occured = true;
        checker->parser_parent->generate_error_report(message,
                                                      valid_line(anchor.line_found));
    }
    checker->statement_suppressed = true;
    checker->type_error_occured = true;
}

void report_statement_error(Typechecker *checker, const std::string &message,
                            const token &anchor)
{
    if (checker->statement_suppressed)
    {
        return;
    }
    if (checker->parser_parent != NULL)
    {
        checker->parser_parent->errors_occured = true;
        checker->parser_parent->generate_error_report(message,
                                                      valid_line(anchor.line_found));
    }
    //Scalar statement checks intentionally stay independent from the retired
    //streaming accumulator.  A semantic failure must not erase its sentinel
    //state or create a later accumulator-driven cascade.
    checker->statement_suppressed = true;
    checker->type_error_occured = true;
}

bool assignment_types_compatible(data_types destination_type, data_types expression_type)
{
    if (destination_type == TYPE_NONE || expression_type == TYPE_NONE)
    {
        return false;
    }
    if (destination_type == expression_type)
    {
        return true;
    }
    if ((destination_type == TYPE_BOOL && expression_type == TYPE_INT) ||
        (destination_type == TYPE_INT && expression_type == TYPE_BOOL))
    {
        return true;
    }
    return (destination_type == TYPE_INT && expression_type == TYPE_FLOAT) ||
           (destination_type == TYPE_FLOAT && expression_type == TYPE_INT);
}

bool is_ordering_operation(semantic_operator operation)
{
    return operation == SEM_LESS || operation == SEM_LESS_EQUAL ||
           operation == SEM_GREATER || operation == SEM_GREATER_EQUAL;
}

bool is_equality_operation(semantic_operator operation)
{
    return operation == SEM_EQUAL || operation == SEM_NOT_EQUAL;
}

} // namespace

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
    statement_key_token = key_token;
    statement_suppressed = false;
    type_error_occured = false;
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

bool Typechecker::begin_loop_condition(token condition_anchor)
{
    statement_key_token = condition_anchor;
    current_statement_type = STATEMENT_LOOP;
    statement_suppressed = false;
    type_error_occured = false;
    clear_tokens(false);
    return true;
}

token_and_status Typechecker::feed_in_tokens(token token_to_feed)
{
    token_and_status return_object;
    if (statement_suppressed)
    {
        return_object.valid_parse = false;
        return return_object;
    }
    bool return_value = false;
    // if (current_statement_type == STATEMENT_ASSIGN)
    // {
    //if nothing has been read in yet
    if (first_token.type == T_NULL)
    {
        first_token = token_to_feed;
        return_value = false;
        return_object.valid_parse = return_value;
        return return_object;
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
                    return_value = false;
                    return_object.valid_parse = return_value;
                    return return_object;
                }
                //only some tokens can chain
                if ((second_relation_token_chains(token_to_feed)) && (relation_tokens.size() == 1))
                {
                    relation_tokens.push_back(token_to_feed);
                    return_value = false;
                    return_object.valid_parse = return_value;
                    return return_object;
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
        return_object = is_valid_operation();
        clear_tokens(true);
    }
    return return_object;
}

void Typechecker::suppress_current_statement()
{
    clear_tokens(false);
    statement_suppressed = true;
    type_error_occured = true;
}

token Typechecker::make_expression_result(data_types result_type, const token &anchor) const
{
    token result;
    result.type = T_IDENTIFIER;
    result.identifer_type = I_NONE;
    result.identifier_data_type = result_type;
    result.line_found = valid_line(anchor.line_found);
    result.column_found = anchor.column_found;
    result.first_token_on_line = anchor.first_token_on_line;
    result.global_scope = false;
    result.scope_id = 0;
    result.is_array = false;
    return result;
}

token_and_status Typechecker::check_unary_expression(semantic_operator operation,
                                                      const token &operator_token,
                                                      const token &operand)
{
    token_and_status result;
    result.valid_parse = true;
    if (statement_suppressed)
    {
        return result;
    }

    const data_types operand_type = expression_data_type(operand);
    data_types result_type = TYPE_NONE;
    std::string error_message;
    if (operation == SEM_NEGATE)
    {
        if (is_numeric(operand_type))
        {
            result_type = operand_type;
        }
        else
        {
            error_message = "Negative factors must be integers or floats";
        }
    }
    else if (operation == SEM_NOT)
    {
        if (operand_type == TYPE_INT || operand_type == TYPE_BOOL)
        {
            result_type = operand_type;
        }
        else
        {
            error_message = "Bitwise and logical \"not\" operations require an integer or bool";
        }
    }
    else
    {
        error_message = "Invalid unary expression operation";
    }

    if (!error_message.empty())
    {
        report_expression_error(this, error_message, operator_token);
        return result;
    }

    result.resolved_token = make_expression_result(result_type, operator_token);
    result.semantic_valid = true;
    return result;
}

token_and_status Typechecker::check_binary_expression(semantic_operator operation,
                                                       const token &operator_token,
                                                       const token &left_operand,
                                                       const token &right_operand)
{
    token_and_status result;
    result.valid_parse = true;
    if (statement_suppressed)
    {
        return result;
    }

    const data_types left_type = expression_data_type(left_operand);
    const data_types right_type = expression_data_type(right_operand);
    data_types result_type = TYPE_NONE;
    std::string error_message;

    if (operation == SEM_ADD || operation == SEM_SUBTRACT ||
        operation == SEM_MULTIPLY || operation == SEM_DIVIDE)
    {
        if (is_numeric(left_type) && is_numeric(right_type))
        {
            result_type = left_type == TYPE_FLOAT || right_type == TYPE_FLOAT ?
                              TYPE_FLOAT : TYPE_INT;
        }
        else
        {
            error_message = "Arithmetic operations must be between floats and integers";
        }
    }
    else if (operation == SEM_AND || operation == SEM_OR)
    {
        if (left_type == TYPE_INT && right_type == TYPE_INT)
        {
            result_type = TYPE_INT;
        }
        else if (left_type == TYPE_BOOL && right_type == TYPE_BOOL)
        {
            result_type = TYPE_BOOL;
        }
        else
        {
            error_message = operation == SEM_AND ?
                                "Bitwise and logical \"&\" operations require two integers or two bools" :
                                "Bitwise and logical \"|\" operations require two integers or two bools";
        }
    }
    else if (is_ordering_operation(operation))
    {
        if ((is_numeric(left_type) && is_numeric(right_type)) ||
            (is_bool_or_integer(left_type) && is_bool_or_integer(right_type)))
        {
            result_type = TYPE_BOOL;
        }
        else
        {
            error_message = "Ordering relations require compatible integers, floats, or bools";
        }
    }
    else if (is_equality_operation(operation))
    {
        if ((is_numeric(left_type) && is_numeric(right_type)) ||
            (is_bool_or_integer(left_type) && is_bool_or_integer(right_type)) ||
            (left_type == TYPE_STRING && right_type == TYPE_STRING))
        {
            result_type = TYPE_BOOL;
        }
        else
        {
            error_message = "Equality relations require compatible integers, floats, bools, or strings";
        }
    }
    else
    {
        error_message = "Invalid binary expression operation";
    }

    if (!error_message.empty())
    {
        report_expression_error(this, error_message, operator_token);
        return result;
    }

    result.resolved_token = make_expression_result(result_type, left_operand);
    result.semantic_valid = true;
    return result;
}

bool Typechecker::validate_procedure_call(
    const token &canonical_procedure, const token &callee_occurrence,
    const std::vector<token_and_status> &arguments)
{
    if (statement_suppressed || canonical_procedure.identifer_type != I_PROCEDURE)
    {
        return false;
    }

    //The parser calls us only after it has consumed a closing ')'.  Keep this
    //guard here too: malformed or already-invalid arguments must never turn
    //into a secondary arity/type diagnostic if a future call site forgets the
    //parser-side gate.
    for (std::size_t i = 0; i < arguments.size(); i++)
    {
        if (!arguments[i].valid_parse || !arguments[i].semantic_valid)
        {
            return false;
        }
    }

    if (canonical_procedure.procedure_params.size() != arguments.size())
    {
        report_call_error(
            this,
            "Procedure \"" + callee_occurrence.stringValue + "\" expects " +
                std::to_string(canonical_procedure.procedure_params.size()) +
                " argument(s), got " + std::to_string(arguments.size()),
            callee_occurrence);
        return false;
    }

    for (std::size_t i = 0; i < arguments.size(); i++)
    {
        const data_types expected_type = canonical_procedure.procedure_params[i];
        const data_types actual_type = expression_data_type(arguments[i].resolved_token);
        if (expected_type == TYPE_NONE)
        {
            report_call_error(
                this,
                "Procedure \"" + callee_occurrence.stringValue + "\" parameter " +
                    std::to_string(i + 1) + " has an unsupported unresolved type",
                callee_occurrence);
            return false;
        }
        if (actual_type == TYPE_NONE)
        {
            report_call_error(
                this,
                "Argument " + std::to_string(i + 1) + " to procedure \"" +
                    callee_occurrence.stringValue +
                    "\" has an unsupported unresolved type",
                arguments[i].resolved_token);
            return false;
        }
        if (expected_type != actual_type)
        {
            report_call_error(
                this,
                "Argument " + std::to_string(i + 1) + " to procedure \"" +
                    callee_occurrence.stringValue + "\" has type \"" +
                    procedure_type_name(actual_type) + "\"; expected \"" +
                    procedure_type_name(expected_type) + "\"",
                arguments[i].resolved_token);
            return false;
        }
    }
    return true;
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

    return true;
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

token_and_status Typechecker::is_valid_operation()
{
    token_and_status return_object;
    bool return_value = false;
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
    int line_error = operation_line(relation_tokens, first_token, second_token);
    token_one_type_name = give_token_type_name(token_one_type);
    token_two_type_name = give_token_type_name(token_two_type);
    //this means they are never compatible
    if (!compatible)
    {
        error_message = "Type \"" + token_one_type_name + "\" and type \"" + token_two_type_name + "\" have no valid operations";
        parser_parent->errors_occured = true;
        parser_parent->generate_error_report(error_message, line_error);
        error_message = "";
        type_error_occured = true;
        //set error message?
        return_value = false;
        return_object.valid_parse = return_value;
        return return_object;
    }
    //check the relation operators first
    //no relation token, therfore an error occured
    if (relation_tokens.size() == 0)
    {
        return_value = false;
        return_object.valid_parse = return_value;
        return return_object;
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
                if (token_two_type == typechecker_int)
                {
                    return_object.resolved_token.type = T_INTEGER_TYPE;
                    return_object.resolved_token.identifier_data_type = TYPE_INT;
                    return_object.valid_parse = true;
                    return return_object;
                }
                else
                {
                    return_object.resolved_token.type = T_FLOAT_TYPE;
                    return_object.resolved_token.identifier_data_type = TYPE_FLOAT;
                    return_object.valid_parse = true;
                    return return_object;
                }
            }
            else
            {
                parser_parent->generate_error_report("Arithmetic operations must be between floats and integers", line_error);
                parser_parent->errors_occured = true;
                return_value = false;
                return_object.valid_parse = return_value;
                return return_object;
            }

            break;

            //must be an iteger or float for both
        case T_MINUS:
            if (is_float_or_int(token_one_type, token_two_type))
            {
                if (token_two_type == typechecker_int)
                {
                    return_object.resolved_token.type = T_INTEGER_TYPE;
                    return_object.resolved_token.identifier_data_type = TYPE_INT;
                    return_object.valid_parse = true;
                    return return_object;
                }
                else
                {
                    return_object.resolved_token.type = T_FLOAT_TYPE;
                    return_object.resolved_token.identifier_data_type = TYPE_FLOAT;
                    return_object.valid_parse = true;
                    return return_object;
                }
            }
            else
            {
                parser_parent->generate_error_report("Arithmetic operations must be between floats and integers", line_error);
                return_value = false;
                return_object.valid_parse = return_value;
                return return_object;
            }

            break;

        case T_GREATER:
            if (is_float_or_int(token_one_type, token_two_type))
            {
                return_object.resolved_token.type = T_BOOL_TYPE;
                return_object.resolved_token.identifier_data_type = TYPE_BOOL;
                return_object.valid_parse = true;
                return return_object;
            }
            else if (is_bool_or_int(token_one_type, token_two_type))
            {
                return_object.resolved_token.type = T_BOOL_TYPE;
                return_object.resolved_token.identifier_data_type = TYPE_BOOL;
                return_object.valid_parse = true;
                return return_object;
            }
            else
            {
                parser_parent->generate_error_report("Greater than relations must relate \"Bools with Bools\", \"Bools with Integers\", \"Integers with Floats\" or \"Floats with Floats\"", line_error);
            }

            break;

        case T_LESS:
            if (is_float_or_int(token_one_type, token_two_type))
            {
                return_object.resolved_token.type = T_BOOL_TYPE;
                return_object.resolved_token.identifier_data_type = TYPE_BOOL;
                return_object.valid_parse = true;
                return return_object;
            }
            else if (is_bool_or_int(token_one_type, token_two_type))
            {
                return_object.resolved_token.type = T_BOOL_TYPE;
                return_object.resolved_token.identifier_data_type = TYPE_BOOL;
                return_object.valid_parse = true;
                return return_object;
            }
            else
            {
                parser_parent->generate_error_report("Less than relations must relate \"Bools with Bools\", \"Bools with Integers\", \"Integers with Floats\" or \"Floats with Floats\"", line_error);
            }

            break;

            //must be an iteger or float for both
        case T_MULT:
            if (is_float_or_int(token_one_type, token_two_type))
            {
                if (token_two_type == typechecker_int)
                {
                    return_object.resolved_token.type = T_INTEGER_TYPE;
                    return_object.resolved_token.identifier_data_type = TYPE_INT;
                    return_object.valid_parse = true;
                    return return_object;
                }
                else
                {
                    return_object.resolved_token.type = T_FLOAT_TYPE;
                    return_object.resolved_token.identifier_data_type = TYPE_FLOAT;
                    return_object.valid_parse = true;
                    return return_object;
                }
            }
            else
            {
                parser_parent->generate_error_report("Arithmetic operations must be between floats and integers", line_error);
                return_value = false;
                return_object.valid_parse = return_value;
                return return_object;
            }
            break;
            //must be an iteger or float for both
        case T_SLASH:
            if (is_float_or_int(token_one_type, token_two_type))
            {
                if (token_two_type == typechecker_int)
                {
                    return_object.resolved_token.type = T_INTEGER_TYPE;
                    return_object.resolved_token.identifier_data_type = TYPE_INT;
                    return_object.valid_parse = true;
                    return return_object;
                }
                else
                {
                    return_object.resolved_token.type = T_FLOAT_TYPE;
                    return_object.resolved_token.identifier_data_type = TYPE_FLOAT;
                    return_object.valid_parse = true;
                    return return_object;
                }
            }
            else
            {
                parser_parent->generate_error_report("Arithmetic operations must be between floats and integers", line_error);
                return_value = false;
                return_object.valid_parse = return_value;
                return return_object;
            }

            break;

        case T_AMPERSAND:
            if (token_one_type == typechecker_int && token_two_type == typechecker_int)
            {
                return_object.resolved_token.type = T_INTEGER_TYPE;
                return_object.resolved_token.identifier_data_type = TYPE_INT;
                return_object.valid_parse = true;
                return return_object;
            }
            if (token_one_type == typechecker_bool && token_two_type == typechecker_bool)
            {
                return_object.resolved_token.type = T_BOOL_TYPE;
                return_object.resolved_token.identifier_data_type = TYPE_BOOL;
                return_object.valid_parse = true;
                return return_object;
            }
            parser_parent->generate_error_report("Bitwise and logical \"&\" operations require two integers or two bools", line_error);
            parser_parent->errors_occured = true;
            type_error_occured = true;
            return_object.valid_parse = false;
            return return_object;

        case T_VERTICAL_BAR:
            if (token_one_type == typechecker_int && token_two_type == typechecker_int)
            {
                return_object.resolved_token.type = T_INTEGER_TYPE;
                return_object.resolved_token.identifier_data_type = TYPE_INT;
                return_object.valid_parse = true;
                return return_object;
            }
            if (token_one_type == typechecker_bool && token_two_type == typechecker_bool)
            {
                return_object.resolved_token.type = T_BOOL_TYPE;
                return_object.resolved_token.identifier_data_type = TYPE_BOOL;
                return_object.valid_parse = true;
                return return_object;
            }
            parser_parent->generate_error_report("Bitwise and logical \"|\" operations require two integers or two bools", line_error);
            parser_parent->errors_occured = true;
            type_error_occured = true;
            return_object.valid_parse = false;
            return return_object;

        default:
            return_value = false;
            return_object.valid_parse = return_value;
            return return_object;
        }
    }
    //there are two relation tokens in the relation
    else if (relation_tokens.size() == 2)
    {
        //the second token when there are two tokens must be an equal
        if (relation_tokens[1].type != T_ASSIGN)
        {

            return_value = false;
            return_object.valid_parse = return_value;
            return return_object;
        }
        else
        {
            if (!first_relation_token_is_valid())
            {
                return_value = false;
                return_object.valid_parse = return_value;
                return return_object;
            }
            switch (relation_tokens[0].type)
            {

            case T_GREATER:
                if (is_float_or_int(token_one_type, token_two_type))
                {
                    return_object.resolved_token.type = T_BOOL_TYPE;
                    return_object.resolved_token.identifier_data_type = TYPE_BOOL;
                    return_object.valid_parse = true;
                    return return_object;
                }
                else if (is_bool_or_int(token_one_type, token_two_type))
                {
                    return_object.resolved_token.type = T_BOOL_TYPE;
                    return_object.resolved_token.identifier_data_type = TYPE_BOOL;
                    return_object.valid_parse = true;
                    return return_object;
                }
                else
                {
                    parser_parent->generate_error_report("Greater than or equal relations must relate \"Bools with Bools\", \"Bools with Integers\", \"Integers with Floats\" or \"Floats with Floats\"", line_error);
                }

                break;

            case T_LESS:
                if (is_float_or_int(token_one_type, token_two_type))
                {
                    return_object.resolved_token.type = T_BOOL_TYPE;
                    return_object.resolved_token.identifier_data_type = TYPE_BOOL;
                    return_object.valid_parse = true;
                    return return_object;
                }
                else if (is_bool_or_int(token_one_type, token_two_type))
                {
                    return_object.resolved_token.type = T_BOOL_TYPE;
                    return_object.resolved_token.identifier_data_type = TYPE_BOOL;
                    return_object.valid_parse = true;
                    return return_object;
                }
                else
                {
                    parser_parent->generate_error_report("Less than or equal relations must relate \"Bools with Bools\", \"Bools with Integers\", \"Integers with Floats\" or \"Floats with Floats\"", line_error);
                }

                break;

            case T_ASSIGN:
                if (is_float_or_int(token_one_type, token_two_type))
                {
                    return_object.resolved_token.type = T_BOOL_TYPE;
                    return_object.resolved_token.identifier_data_type = TYPE_BOOL;
                    return_object.valid_parse = true;
                    return return_object;
                }
                else if (is_bool_or_int(token_one_type, token_two_type))
                {
                    return_object.resolved_token.type = T_BOOL_TYPE;
                    return_object.resolved_token.identifier_data_type = TYPE_BOOL;
                    return_object.valid_parse = true;
                    return return_object;
                }
                else if (both_are_strings(token_one_type, token_two_type))
                {
                    return_object.resolved_token.type = T_BOOL_TYPE;
                    return_object.resolved_token.identifier_data_type = TYPE_BOOL;
                    return_object.valid_parse = true;
                    return return_object;
                }
                else
                {
                    parser_parent->generate_error_report("Equality relations must relate \"Bools with Bools\", \"Bools with Integers\", \"Integers with Floats\" or \"Floats with Floats\"", line_error);
                }
                break;

            case T_EXCLAM:
                if (is_float_or_int(token_one_type, token_two_type))
                {
                    return_object.resolved_token.type = T_BOOL_TYPE;
                    return_object.resolved_token.identifier_data_type = TYPE_BOOL;
                    return_object.valid_parse = true;
                    return return_object;
                }
                else if (is_bool_or_int(token_one_type, token_two_type))
                {
                    return_object.resolved_token.type = T_BOOL_TYPE;
                    return_object.resolved_token.identifier_data_type = TYPE_BOOL;
                    return_object.valid_parse = true;
                    return return_object;
                }
                else if (both_are_strings(token_one_type, token_two_type))
                {
                    return_object.resolved_token.type = T_BOOL_TYPE;
                    return_object.resolved_token.identifier_data_type = TYPE_BOOL;
                    return_object.valid_parse = true;
                    return return_object;
                }
                else
                {
                    parser_parent->generate_error_report("Inequality relations must relate \"Bools with Bools\", \"Bools with Integers\", \"Integers with Floats\", \"Floats with Floats\", or \"Strings with Strings\"", line_error);
                }
                break;
            }
        }
    }
    else
    {
        //an error occured as it can't have more than 2
    }

    return_object.valid_parse = false;
    return return_object;
}

bool Typechecker::check_assignment_statement(token destination_token, token resolved_token)
{
    if (statement_suppressed)
    {
        return false;
    }

    const data_types destination_type = expression_data_type(destination_token);
    const data_types expression_type = expression_data_type(resolved_token);
    if (assignment_types_compatible(destination_type, expression_type))
    {
        return true;
    }

    report_statement_error(
        this,
        "Assignment target type \"" + procedure_type_name(destination_type) +
            "\" is not compatible with expression type \"" +
            procedure_type_name(expression_type) + "\"",
        destination_token);
    return false;
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
            if (is_boolean_literal(first_token.type))
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
            if (is_boolean_literal(first_token.type))
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
            if (is_boolean_literal(first_token.type))
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
            if (is_boolean_literal(first_token.type))
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
            if (is_boolean_literal(second_token.type))
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
            if (is_boolean_literal(second_token.type))
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
            if (is_boolean_literal(second_token.type))
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
            if (is_boolean_literal(second_token.type))
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
        case T_TRUE:
        case T_FALSE:
        case T_BOOL_VALUE:
            return_object.token_one_type = typechecker_bool;
            if (second_token.type == T_INTEGER_VALUE)
            {
                return_object.token_two_type = typechecker_int;
                return_value = true;
            }
            if (is_boolean_literal(second_token.type))
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
            if (is_boolean_literal(second_token.type))
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
            if (is_boolean_literal(second_token.type))
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
            if (is_boolean_literal(second_token.type))
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

bool Typechecker::both_are_strings(typechecker_types token_one, typechecker_types token_two)
{
    if (token_one == typechecker_string && token_two == typechecker_string)
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
    std::string return_string = "Unknown";
    switch (type_to_get)
    {
    case typechecker_null:
        return_string = "Unknown";
        break;

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
    int line_error = valid_line(statement_key_token.line_found);
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
        //compatible-but-not-equal types are valid returns per spec rule 14
        return true;
    }
}

bool Typechecker::check_if_statement(token token_to_check)
{
    bool return_value = false;
    typechecker_types type_to_check;
    type_to_check = convert_to_typechecker_types(token_to_check);
    //first check if it is an identifier
    if (type_to_check != typechecker_bool && type_to_check != typechecker_int)
    {
        parser_parent->generate_error_report("If statements must resolve to either type Bool or Integer", valid_line(statement_key_token.line_found));
        return_value = false;
        type_error_occured = true;
    }
    else
    {
        return_value = true;
    }
    return return_value;
}

typechecker_types Typechecker::convert_to_typechecker_types(token token_to_convert)
{
    typechecker_types return_conversion = typechecker_null;
    if (token_to_convert.type == T_IDENTIFIER)
    {
        switch (token_to_convert.identifier_data_type)
        {
        case TYPE_BOOL:
            return_conversion = typechecker_bool;
            break;

        case TYPE_FLOAT:
            return_conversion = typechecker_float;

            break;

        case TYPE_INT:
            return_conversion = typechecker_int;

            break;

        case TYPE_STRING:
            return_conversion = typechecker_string;

            break;

        case TYPE_NONE:
            return_conversion = typechecker_null;

            break;

        default:
            return_conversion = typechecker_null;

            break;
        }
    }
    else if (token_to_convert.type == T_INTEGER_VALUE)
    {
        return_conversion = typechecker_int;
    }
    else if (token_to_convert.type == T_FLOAT_VALUE)
    {
        return_conversion = typechecker_float;
    }
    else if (token_to_convert.type == T_STRING_VALUE)
    {
        return_conversion = typechecker_string;
    }
    else if (token_to_convert.type == T_BOOL_VALUE || token_to_convert.type == T_TRUE || token_to_convert.type == T_FALSE)
    {
        return_conversion = typechecker_bool;
    }
    return return_conversion;
}

bool Typechecker::check_loop_statement(token token_to_check)
{
    if (statement_suppressed)
    {
        return false;
    }
    const typechecker_types type_to_check = convert_to_typechecker_types(token_to_check);
    if (type_to_check == typechecker_bool || type_to_check == typechecker_int)
    {
        return true;
    }
    report_statement_error(this,
                           "Loop statements must resolve to either type Bool or Integer",
                           statement_key_token);
    return false;
}
