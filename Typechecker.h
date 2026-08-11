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

//A conversion plan records frontend compatibility only.  It deliberately
//contains no lowering instruction or AST value; a later backend can consume
//the plan without having to rediscover the scalar/array rules.
enum class conversion_kind
{
    Invalid,
    Exact,
    IntToFloat,
    FloatToInt,
    BoolToInt,
    IntToBool
};

enum class conversion_failure
{
    None,
    UnresolvedShape,
    ScalarArrayMismatch,
    ArrayBoundMismatch,
    IncompatibleElementTypes
};

struct conversion_plan
{
    value_shape source_shape;
    value_shape target_shape;
    conversion_kind kind = conversion_kind::Invalid;
    conversion_failure failure = conversion_failure::None;
    bool valid = false;
    bool requires_conversion = false;
    bool is_elementwise = false;

    operator bool() const noexcept
    {
        return valid;
    }
};

enum class condition_context
{
    If,
    Loop
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
    bool begin_loop_condition(token condition_anchor);
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
    token_and_status make_shaped_expression_result(const value_shape &shape,
                                                   const token &anchor) const;
    token_and_status check_unary_expression(semantic_operator operation,
                                             const token &operator_token,
                                             const token_and_status &operand);
    token_and_status check_binary_expression(semantic_operator operation,
                                              const token &operator_token,
                                              const token_and_status &left_operand,
                                              const token_and_status &right_operand);
    bool validate_array_index(const token &base_occurrence,
                              const value_shape &base_shape,
                              const token_and_status &index_expression);
    //Procedure declarations remain canonical symbols in the scope table.  A
    //call expression carries only a synthetic result type, so validate the
    //canonical signature separately once the parser has consumed the full
    //argument list.
    bool validate_procedure_call(const token &canonical_procedure,
                                 const token &callee_occurrence,
                                 const std::vector<token_and_status> &arguments);

    //These planners are pure: they neither report diagnostics nor touch the
    //legacy expression accumulator.  Callers own diagnostic policy.
    static conversion_plan plan_target_conversion(const value_shape &source,
                                                  const value_shape &target);
    static conversion_plan plan_condition(const value_shape &source);

    //Statement consumers return the retained plan for future lowering.  They
    //report at most one focused error and preserve the legacy accumulator.
    conversion_plan check_assignment_statement(const token_and_status &destination,
                                               const token_and_status &expression);
    conversion_plan check_return_statement(const token_and_status &resolved_token,
                                           token procedure_token,
                                           const token &return_anchor);
    conversion_plan check_return_statement(const token_and_status &resolved_token,
                                           token procedure_token);
    conversion_plan check_condition_statement(const token_and_status &token_to_check,
                                              const token &anchor,
                                              condition_context context);
    conversion_plan check_if_statement(const token_and_status &token_to_check);
    conversion_plan check_loop_statement(const token_and_status &token_to_check);
    void mark_current_statement_invalid();
    bool are_tokens_full();
    token_types_and_status token_types_compatible_at_all();

    bool first_relation_token_is_valid();

    bool is_float_or_int(typechecker_types token_one, typechecker_types token_two);
    bool is_bool_or_int(typechecker_types token_one, typechecker_types token_two);
    bool both_are_strings(typechecker_types token_one, typechecker_types token_two);
    std::string give_token_type_name(typechecker_types type_to_get);
    typechecker_types convert_to_typechecker_types(token token_to_convert);

    bool debugger = false;
    parser *parser_parent = nullptr;

    bool type_error_occured = false;
    bool statement_suppressed = false;
    //Typechecker(parser *parent_test);
};

#endif // !TYPECHECKER_H
