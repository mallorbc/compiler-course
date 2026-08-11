//Unit tests for the small pieces around the scanner: the shared string helper
//and the part of the typechecker that can safely be exercised on its own.
//
//SAFETY NOTE about the typechecker: default construction deliberately leaves
//parser_parent null.  Legacy accumulator APIs are therefore exercised only on
//their pure paths.  The synthesized-expression, call, scalar-statement, and
//TY-8 shape helpers are explicit exceptions: their error reporters are null
//safe and must preserve first_token, second_token, and relation_tokens.  The
//tests below pin that isolation rather than treating the legacy accumulator as
//a safe integration surface.
#include "../vendor/doctest.h"
#include "../../CustomFunctions.h"
#include "../../Typechecker.h"

#include <string>

namespace
{

//an identifier token as the parser would have filled it in from a declaration
token identifier_of(data_types declared_type)
{
    token built;
    built.type = T_IDENTIFIER;
    built.identifier_data_type = declared_type;
    return built;
}

//a literal token as the scanner produces it
token literal_of(token_type literal_type)
{
    token built;
    built.type = literal_type;
    return built;
}

value_shape shape_of_type(data_types type, bool is_array = false, int upper_bound = -1)
{
    value_shape shape;
    shape.element_type = type;
    shape.is_array = is_array;
    shape.array_upper_bound = is_array ? upper_bound : -1;
    return shape;
}

token_and_status expression_of(Typechecker &checker, data_types type, const token &anchor,
                               bool is_array = false, int upper_bound = -1)
{
    return checker.make_shaped_expression_result(shape_of_type(type, is_array, upper_bound),
                                                 anchor);
}

token_types_and_status compatibility_of(token first, token second)
{
    Typechecker checker;
    checker.first_token = first;
    checker.second_token = second;
    return checker.token_types_compatible_at_all();
}

bool same_token(const token &left, const token &right)
{
    return left.type == right.type &&
           left.line_found == right.line_found &&
           left.column_found == right.column_found &&
           left.global_scope == right.global_scope &&
           left.scope_id == right.scope_id &&
           left.intValue == right.intValue &&
           left.stringValue == right.stringValue &&
           left.boolValue == right.boolValue &&
           left.floatValue == right.floatValue &&
           left.charValue == right.charValue &&
           left.first_token_on_line == right.first_token_on_line &&
           left.identifer_type == right.identifer_type &&
           left.procedure_params == right.procedure_params &&
           left.identifier_data_type == right.identifier_data_type &&
           left.is_array == right.is_array &&
           left.array_upper_bound == right.array_upper_bound;
}

} // namespace

TEST_CASE("Tolower_string lowercases every letter and leaves the rest alone")
{
    CHECK(Tolower_string("MiXeD") == "mixed");
    CHECK(Tolower_string("already") == "already");
    CHECK(Tolower_string("") == "");
    //this is what makes the language case insensitive at the symbol table level
    CHECK(Tolower_string("GLOBAL") == Tolower_string("global"));
    CHECK(Tolower_string("A_1 b-2!") == "a_1 b-2!");
}

TEST_CASE("token_types_compatible_at_all pairs two declared identifiers")
{
    CHECK(compatibility_of(identifier_of(TYPE_INT), identifier_of(TYPE_INT)).compatible == true);
    //integers and floats mix
    CHECK(compatibility_of(identifier_of(TYPE_INT), identifier_of(TYPE_FLOAT)).compatible == true);
    //so do bools and integers
    CHECK(compatibility_of(identifier_of(TYPE_BOOL), identifier_of(TYPE_INT)).compatible == true);
    CHECK(compatibility_of(identifier_of(TYPE_STRING), identifier_of(TYPE_STRING)).compatible == true);
    //strings mix with nothing else
    CHECK(compatibility_of(identifier_of(TYPE_STRING), identifier_of(TYPE_INT)).compatible == false);
    CHECK(compatibility_of(identifier_of(TYPE_FLOAT), identifier_of(TYPE_BOOL)).compatible == false);
}

TEST_CASE("token_types_compatible_at_all reports the type it resolved each side to")
{
    token_types_and_status resolved = compatibility_of(identifier_of(TYPE_BOOL), literal_of(T_INTEGER_VALUE));
    CHECK(resolved.token_one_type == typechecker_bool);
    CHECK(resolved.token_two_type == typechecker_int);
    CHECK(resolved.compatible == true);

    //a literal on the left is read from its token type instead
    token_types_and_status literals = compatibility_of(literal_of(T_INTEGER_VALUE), literal_of(T_STRING_VALUE));
    CHECK(literals.token_one_type == typechecker_int);
    CHECK(literals.token_two_type == typechecker_string);
    CHECK(literals.compatible == false);
}

TEST_CASE("token_types_compatible_at_all recognizes scanner boolean literals")
{
    token_types_and_status literals = compatibility_of(literal_of(T_TRUE), literal_of(T_FALSE));
    CHECK(literals.token_one_type == typechecker_bool);
    CHECK(literals.token_two_type == typechecker_bool);
    CHECK(literals.compatible == true);

    token_types_and_status identifier_and_literal = compatibility_of(
        identifier_of(TYPE_BOOL), literal_of(T_TRUE));
    CHECK(identifier_and_literal.token_one_type == typechecker_bool);
    CHECK(identifier_and_literal.token_two_type == typechecker_bool);
    CHECK(identifier_and_literal.compatible == true);

    token_types_and_status literal_and_identifier = compatibility_of(
        literal_of(T_FALSE), identifier_of(TYPE_INT));
    CHECK(literal_and_identifier.token_one_type == typechecker_bool);
    CHECK(literal_and_identifier.token_two_type == typechecker_int);
    CHECK(literal_and_identifier.compatible == true);
}

TEST_CASE("token_types_compatible_at_all rejects an identifier with no declared type")
{
    token_types_and_status undeclared = compatibility_of(identifier_of(TYPE_NONE), literal_of(T_INTEGER_VALUE));

    CHECK(undeclared.compatible == false);
    CHECK(undeclared.token_one_type == typechecker_null);
    CHECK(undeclared.token_two_type == typechecker_null);
}

TEST_CASE("KNOWN-BUG TY-10: token_types_compatible_at_all is not symmetric")
{
    //a float identifier on the left accepts a string or a bool literal on the
    //right (Typechecker.cpp:892-913 sets return_value = true for both), while
    //the same pair in the other order is rejected, and two identifiers of those
    //types are rejected as well.  Left hand float therefore swallows anything.
    CHECK(compatibility_of(identifier_of(TYPE_FLOAT), literal_of(T_STRING_VALUE)).compatible == true);
    CHECK(compatibility_of(literal_of(T_STRING_VALUE), identifier_of(TYPE_FLOAT)).compatible == false);
    CHECK(compatibility_of(identifier_of(TYPE_FLOAT), literal_of(T_BOOL_VALUE)).compatible == true);
    CHECK(compatibility_of(literal_of(T_BOOL_VALUE), identifier_of(TYPE_FLOAT)).compatible == false);
}

TEST_CASE("the typechecker's type predicates, conversions, and names")
{
    Typechecker checker;

    CHECK(checker.is_float_or_int(typechecker_int, typechecker_float) == true);
    CHECK(checker.is_float_or_int(typechecker_int, typechecker_string) == false);
    CHECK(checker.is_bool_or_int(typechecker_bool, typechecker_int) == true);
    CHECK(checker.is_bool_or_int(typechecker_bool, typechecker_string) == false);
    CHECK(checker.both_are_strings(typechecker_string, typechecker_string) == true);
    CHECK(checker.both_are_strings(typechecker_string, typechecker_int) == false);

    CHECK(checker.give_token_type_name(typechecker_int) == "Integer");
    CHECK(checker.give_token_type_name(typechecker_float) == "Float");
    CHECK(checker.give_token_type_name(typechecker_bool) == "Bool");
    CHECK(checker.give_token_type_name(typechecker_string) == "String");
    CHECK(checker.give_token_type_name(typechecker_null) == "Unknown");

    token no_type;
    CHECK(checker.convert_to_typechecker_types(no_type) == typechecker_null);
    CHECK(checker.convert_to_typechecker_types(identifier_of(TYPE_NONE)) == typechecker_null);
    CHECK(checker.convert_to_typechecker_types(literal_of(T_STRING_VALUE)) == typechecker_string);
    CHECK(checker.convert_to_typechecker_types(literal_of(T_TRUE)) == typechecker_bool);
    CHECK(checker.convert_to_typechecker_types(literal_of(T_FALSE)) == typechecker_bool);
}

TEST_CASE("TY-2E expression helpers synthesize independent result tokens")
{
    Typechecker checker;
    token anchor;
    anchor.line_found = 7;
    anchor.column_found = 3;
    anchor.first_token_on_line = true;
    const token_and_status integer = expression_of(checker, TYPE_INT, anchor);
    const token_and_status floating = expression_of(checker, TYPE_FLOAT, anchor);
    const token_and_status boolean = expression_of(checker, TYPE_BOOL, anchor);
    token operation;
    operation.line_found = 9;

    token_and_status int_plus_float = checker.check_binary_expression(
        SEM_ADD, operation, integer, floating);
    CHECK(int_plus_float.valid_parse);
    CHECK(int_plus_float.semantic_valid);
    CHECK(int_plus_float.resolved_token.type == T_IDENTIFIER);
    CHECK(int_plus_float.resolved_token.identifer_type == I_NONE);
    CHECK(int_plus_float.resolved_token.identifier_data_type == TYPE_FLOAT);
    CHECK(int_plus_float.resolved_token.line_found == 7);
    CHECK(int_plus_float.resolved_token.column_found == 3);
    CHECK(int_plus_float.resolved_token.first_token_on_line);

    token_and_status float_plus_int = checker.check_binary_expression(
        SEM_ADD, operation, floating, integer);
    CHECK(float_plus_int.semantic_valid);
    CHECK(float_plus_int.resolved_token.identifier_data_type == TYPE_FLOAT);

    token_and_status division = checker.check_binary_expression(
        SEM_DIVIDE, operation, integer, integer);
    CHECK(division.semantic_valid);
    CHECK(division.resolved_token.identifier_data_type == TYPE_INT);

    token_and_status logical_not = checker.check_unary_expression(SEM_NOT, operation, boolean);
    CHECK(logical_not.semantic_valid);
    CHECK(logical_not.resolved_token.identifier_data_type == TYPE_BOOL);

    token_and_status ordering = checker.check_binary_expression(
        SEM_LESS_EQUAL, operation, boolean, integer);
    CHECK(ordering.semantic_valid);
    CHECK(ordering.resolved_token.identifier_data_type == TYPE_BOOL);
}

TEST_CASE("TY-2E invalid helpers preserve the legacy accumulator without a parent")
{
    Typechecker checker;
    CHECK(checker.parser_parent == nullptr);

    token first;
    first.type = T_IDENTIFIER;
    first.line_found = 3;
    first.column_found = 4;
    first.global_scope = true;
    first.scope_id = 7;
    first.intValue = 11;
    first.stringValue = "first";
    first.boolValue = true;
    first.floatValue = 1.25F;
    first.charValue = 'f';
    first.first_token_on_line = true;
    first.identifer_type = I_VARIABLE;
    first.procedure_params = {shape_of_type(TYPE_INT), shape_of_type(TYPE_BOOL)};
    first.identifier_data_type = TYPE_INT;
    first.is_array = true;
    first.array_upper_bound = 4;

    token second = first;
    second.line_found = 8;
    second.stringValue = "second";
    second.identifier_data_type = TYPE_BOOL;
    second.procedure_params = {shape_of_type(TYPE_FLOAT)};

    token relation_one = first;
    relation_one.type = T_LESS;
    relation_one.stringValue = "relation-one";
    token relation_two = second;
    relation_two.type = T_ASSIGN;
    relation_two.stringValue = "relation-two";

    checker.first_token = first;
    checker.second_token = second;
    checker.relation_tokens = {relation_one, relation_two};
    const token saved_first = checker.first_token;
    const token saved_second = checker.second_token;
    const std::vector<token> saved_relations = checker.relation_tokens;

    token operator_token;
    operator_token.line_found = 12;
    const token_and_status integer = expression_of(checker, TYPE_INT, first);
    const token_and_status boolean = expression_of(checker, TYPE_BOOL, second);
    const token_and_status invalid = checker.check_binary_expression(
        SEM_AND, operator_token, integer, boolean);

    CHECK(invalid.valid_parse);
    CHECK_FALSE(invalid.semantic_valid);
    CHECK(invalid.resolved_token.type == 0);
    CHECK(invalid.resolved_token.identifier_data_type == TYPE_NONE);
    CHECK(checker.statement_suppressed);
    CHECK(checker.type_error_occured);
    CHECK(same_token(checker.first_token, saved_first));
    CHECK(same_token(checker.second_token, saved_second));
    REQUIRE(checker.relation_tokens.size() == saved_relations.size());
    for (std::size_t i = 0; i < saved_relations.size(); i++)
    {
        CHECK(same_token(checker.relation_tokens[i], saved_relations[i]));
    }
}

TEST_CASE("SIL-1 call validation requires exact types and rejects unresolved types")
{
    token canonical;
    canonical.type = T_IDENTIFIER;
    canonical.identifer_type = I_PROCEDURE;
    canonical.procedure_params = {shape_of_type(TYPE_INT), shape_of_type(TYPE_FLOAT)};
    token occurrence;
    occurrence.type = T_IDENTIFIER;
    occurrence.stringValue = "q";
    occurrence.line_found = 5;

    Typechecker valid_checker;
    const token_and_status integer_argument = expression_of(valid_checker, TYPE_INT, occurrence);
    const token_and_status float_argument = expression_of(valid_checker, TYPE_FLOAT, occurrence);
    CHECK(valid_checker.validate_procedure_call(canonical, occurrence,
                                                {integer_argument, float_argument}));
    CHECK_FALSE(valid_checker.statement_suppressed);

    Typechecker mismatched_checker;
    const token_and_status bool_argument = expression_of(mismatched_checker, TYPE_BOOL, occurrence);
    CHECK_FALSE(mismatched_checker.validate_procedure_call(
        canonical, occurrence, {integer_argument, bool_argument}));
    CHECK(mismatched_checker.statement_suppressed);
    CHECK(mismatched_checker.type_error_occured);

    Typechecker unresolved_parameter_checker;
    token unresolved_parameter = canonical;
    unresolved_parameter.procedure_params[1] = shape_of_type(TYPE_NONE);
    CHECK_FALSE(unresolved_parameter_checker.validate_procedure_call(
        unresolved_parameter, occurrence, {integer_argument, float_argument}));
    CHECK(unresolved_parameter_checker.statement_suppressed);

    Typechecker unresolved_argument_checker;
    const token_and_status unresolved_argument = {
        true, true, unresolved_argument_checker.make_expression_result(TYPE_NONE, occurrence)};
    CHECK_FALSE(unresolved_argument_checker.validate_procedure_call(
        canonical, occurrence, {integer_argument, unresolved_argument}));
    CHECK(unresolved_argument_checker.statement_suppressed);
}

TEST_CASE("Stage 2D scalar assignment and loop checks are direct and accumulator-safe")
{
    token anchor;
    anchor.type = T_IDENTIFIER;
    anchor.line_found = 9;

    Typechecker compatible_checker;
    const token_and_status integer = expression_of(compatible_checker, TYPE_INT, anchor);
    const token_and_status floating = expression_of(compatible_checker, TYPE_FLOAT, anchor);
    const token_and_status boolean = expression_of(compatible_checker, TYPE_BOOL, anchor);
    const token_and_status string = expression_of(compatible_checker, TYPE_STRING, anchor);
    CHECK(compatible_checker.check_assignment_statement(integer, integer));
    CHECK(compatible_checker.check_assignment_statement(floating, floating));
    CHECK(compatible_checker.check_assignment_statement(boolean, boolean));
    CHECK(compatible_checker.check_assignment_statement(integer, boolean));
    CHECK(compatible_checker.check_assignment_statement(boolean, integer));
    CHECK(compatible_checker.check_assignment_statement(integer, floating));
    CHECK(compatible_checker.check_assignment_statement(floating, integer));
    CHECK(compatible_checker.check_assignment_statement(string, string));
    CHECK_FALSE(compatible_checker.statement_suppressed);

    Typechecker rejected_assignment;
    token sentinel;
    sentinel.type = T_IDENTIFIER;
    sentinel.line_found = 3;
    sentinel.stringValue = "assignment-sentinel";
    rejected_assignment.first_token = sentinel;
    rejected_assignment.second_token = string.resolved_token;
    rejected_assignment.relation_tokens = {anchor};
    const token saved_first = rejected_assignment.first_token;
    const token saved_second = rejected_assignment.second_token;
    const std::vector<token> saved_relations = rejected_assignment.relation_tokens;
    CHECK_FALSE(rejected_assignment.check_assignment_statement(boolean, floating));
    CHECK(rejected_assignment.statement_suppressed);
    CHECK(rejected_assignment.type_error_occured);
    CHECK(same_token(rejected_assignment.first_token, saved_first));
    CHECK(same_token(rejected_assignment.second_token, saved_second));
    REQUIRE(rejected_assignment.relation_tokens.size() == saved_relations.size());
    CHECK(same_token(rejected_assignment.relation_tokens[0], saved_relations[0]));

    Typechecker unresolved_assignment;
    const token_and_status unknown = {
        true, true, unresolved_assignment.make_expression_result(TYPE_NONE, anchor)};
    CHECK_FALSE(unresolved_assignment.check_assignment_statement(integer, unknown));
    CHECK(unresolved_assignment.statement_suppressed);

    Typechecker unresolved_destination;
    CHECK_FALSE(unresolved_destination.check_assignment_statement(unknown, integer));
    CHECK(unresolved_destination.statement_suppressed);

    Typechecker loop_checker;
    loop_checker.first_token = sentinel;
    loop_checker.second_token = sentinel;
    loop_checker.relation_tokens = {sentinel};
    loop_checker.statement_suppressed = true;
    loop_checker.type_error_occured = true;
    CHECK(loop_checker.begin_loop_condition(anchor));
    CHECK(loop_checker.current_statement_type == STATEMENT_LOOP);
    CHECK_FALSE(loop_checker.statement_suppressed);
    CHECK_FALSE(loop_checker.type_error_occured);
    CHECK(loop_checker.statement_key_token.line_found == 9);
    CHECK(loop_checker.first_token.type == T_NULL);
    CHECK(loop_checker.second_token.type == T_NULL);
    CHECK(loop_checker.relation_tokens.empty());
    CHECK(loop_checker.check_loop_statement(boolean));
    CHECK(loop_checker.check_loop_statement(integer));

    loop_checker.first_token = sentinel;
    loop_checker.second_token = string.resolved_token;
    loop_checker.relation_tokens = {anchor};
    const token loop_saved_first = loop_checker.first_token;
    const token loop_saved_second = loop_checker.second_token;
    const std::vector<token> loop_saved_relations = loop_checker.relation_tokens;
    CHECK_FALSE(loop_checker.check_loop_statement(floating));
    CHECK(loop_checker.statement_suppressed);
    CHECK(loop_checker.type_error_occured);
    CHECK(same_token(loop_checker.first_token, loop_saved_first));
    CHECK(same_token(loop_checker.second_token, loop_saved_second));
    REQUIRE(loop_checker.relation_tokens.size() == loop_saved_relations.size());
    CHECK(same_token(loop_checker.relation_tokens[0], loop_saved_relations[0]));

    Typechecker unresolved_loop;
    CHECK(unresolved_loop.begin_loop_condition(anchor));
    CHECK_FALSE(unresolved_loop.check_loop_statement(unknown));
    CHECK(unresolved_loop.statement_suppressed);
}

TEST_CASE("TY-8 shaped helpers preserve bounds, lift arrays, and isolate sentinels")
{
    token anchor;
    anchor.type = T_IDENTIFIER;
    anchor.stringValue = "arr";
    anchor.line_found = 7;
    anchor.column_found = 5;

    Typechecker checker;
    const token_and_status array_integer = expression_of(checker, TYPE_INT, anchor, true, 5);
    const token_and_status second_array = expression_of(checker, TYPE_INT, anchor, true, 5);
    const token_and_status different_array = expression_of(checker, TYPE_INT, anchor, true, 2);
    const token_and_status array_float = expression_of(checker, TYPE_FLOAT, anchor, true, 5);
    const token_and_status array_bool = expression_of(checker, TYPE_BOOL, anchor, true, 5);
    const token_and_status array_string = expression_of(checker, TYPE_STRING, anchor, true, 5);
    const token_and_status scalar_integer = expression_of(checker, TYPE_INT, anchor);
    const token_and_status scalar_float = expression_of(checker, TYPE_FLOAT, anchor);
    const token_and_status scalar_bool = expression_of(checker, TYPE_BOOL, anchor);
    token operation;
    operation.line_found = 9;

    CHECK(array_integer.semantic_valid);
    CHECK(array_integer.resolved_token.is_array);
    CHECK(array_integer.resolved_token.array_upper_bound == 5);
    CHECK(array_integer.resolved_token.identifier_data_type == TYPE_INT);

    const token_and_status broadcast = checker.check_binary_expression(
        SEM_ADD, operation, array_integer, scalar_integer);
    CHECK(broadcast.semantic_valid);
    CHECK(broadcast.resolved_token.is_array);
    CHECK(broadcast.resolved_token.array_upper_bound == 5);
    CHECK(broadcast.resolved_token.identifier_data_type == TYPE_INT);

    const token_and_status reverse_broadcast = checker.check_binary_expression(
        SEM_ADD, operation, scalar_float, array_integer);
    CHECK(reverse_broadcast.semantic_valid);
    CHECK(reverse_broadcast.resolved_token.is_array);
    CHECK(reverse_broadcast.resolved_token.array_upper_bound == 5);
    CHECK(reverse_broadcast.resolved_token.identifier_data_type == TYPE_FLOAT);
    CHECK(checker.check_binary_expression(SEM_SUBTRACT, operation, array_integer,
                                          array_float).semantic_valid);
    CHECK(checker.check_binary_expression(SEM_MULTIPLY, operation, array_integer,
                                          scalar_float).semantic_valid);
    const token_and_status divided = checker.check_binary_expression(
        SEM_DIVIDE, operation, array_integer, array_float);
    CHECK(divided.semantic_valid);
    CHECK(divided.resolved_token.identifier_data_type == TYPE_FLOAT);
    CHECK(divided.resolved_token.is_array);
    CHECK(checker.check_binary_expression(SEM_AND, operation, array_integer,
                                          scalar_integer).semantic_valid);
    CHECK(checker.check_binary_expression(SEM_OR, operation, array_bool,
                                          scalar_bool).semantic_valid);

    const token_and_status relation = checker.check_binary_expression(
        SEM_LESS, operation, array_integer, second_array);
    CHECK(relation.semantic_valid);
    CHECK(relation.resolved_token.is_array);
    CHECK(relation.resolved_token.array_upper_bound == 5);
    CHECK(relation.resolved_token.identifier_data_type == TYPE_BOOL);
    for (semantic_operator relation_operator : {SEM_LESS, SEM_LESS_EQUAL, SEM_GREATER,
                                                 SEM_GREATER_EQUAL, SEM_EQUAL,
                                                 SEM_NOT_EQUAL})
    {
        const token_and_status relation_result = checker.check_binary_expression(
            relation_operator, operation, array_integer, array_float);
        CHECK(relation_result.semantic_valid);
        CHECK(relation_result.resolved_token.is_array);
        CHECK(relation_result.resolved_token.identifier_data_type == TYPE_BOOL);
    }
    const token_and_status bool_relation = checker.check_binary_expression(
        SEM_EQUAL, operation, array_bool, array_bool);
    CHECK(bool_relation.semantic_valid);
    CHECK(bool_relation.resolved_token.is_array);
    CHECK(bool_relation.resolved_token.identifier_data_type == TYPE_BOOL);

    const token_and_status negate = checker.check_unary_expression(
        SEM_NEGATE, operation, array_integer);
    CHECK(negate.semantic_valid);
    CHECK(negate.resolved_token.is_array);
    CHECK(negate.resolved_token.array_upper_bound == 5);
    const token_and_status logical_not = checker.check_unary_expression(
        SEM_NOT, operation, array_bool);
    CHECK(logical_not.semantic_valid);
    CHECK(logical_not.resolved_token.is_array);
    CHECK(logical_not.resolved_token.identifier_data_type == TYPE_BOOL);

    token sentinel;
    sentinel.type = T_IDENTIFIER;
    sentinel.stringValue = "sentinel";
    sentinel.line_found = 2;
    checker.first_token = sentinel;
    checker.second_token = sentinel;
    checker.relation_tokens = {sentinel};
    const token saved_first = checker.first_token;
    const token saved_second = checker.second_token;
    const std::vector<token> saved_relations = checker.relation_tokens;
    const token_and_status mismatch = checker.check_binary_expression(
        SEM_ADD, operation, array_integer, different_array);
    CHECK(mismatch.valid_parse);
    CHECK_FALSE(mismatch.semantic_valid);
    CHECK(checker.statement_suppressed);
    CHECK(checker.type_error_occured);
    CHECK(same_token(checker.first_token, saved_first));
    CHECK(same_token(checker.second_token, saved_second));
    REQUIRE(checker.relation_tokens.size() == saved_relations.size());
    CHECK(same_token(checker.relation_tokens[0], saved_relations[0]));

    Typechecker invalid_primitive_checker;
    const token_and_status invalid_primitive = invalid_primitive_checker.check_binary_expression(
        SEM_AND, operation, array_float, array_string);
    CHECK_FALSE(invalid_primitive.semantic_valid);
    CHECK(invalid_primitive_checker.statement_suppressed);
    CHECK(invalid_primitive_checker.type_error_occured);
    Typechecker invalid_unary_checker;
    CHECK_FALSE(invalid_unary_checker.check_unary_expression(SEM_NOT, operation,
                                                             array_string).semantic_valid);
    Typechecker invalid_relation_checker;
    CHECK_FALSE(invalid_relation_checker.check_binary_expression(
        SEM_LESS, operation, array_string, array_string).semantic_valid);

    Typechecker direct_checker;
    const token_and_status direct_array = expression_of(direct_checker, TYPE_INT, anchor, true, 5);
    const token_and_status direct_scalar = expression_of(direct_checker, TYPE_INT, anchor);
    CHECK(direct_checker.check_assignment_statement(direct_array, direct_array));
    CHECK_FALSE(direct_checker.check_assignment_statement(direct_array, direct_scalar));
    CHECK(direct_checker.statement_suppressed);

    Typechecker array_assignment_sentinel;
    array_assignment_sentinel.first_token = sentinel;
    array_assignment_sentinel.second_token = array_float.resolved_token;
    array_assignment_sentinel.relation_tokens = {anchor};
    const token assignment_saved_first = array_assignment_sentinel.first_token;
    const token assignment_saved_second = array_assignment_sentinel.second_token;
    const std::vector<token> assignment_saved_relations =
        array_assignment_sentinel.relation_tokens;
    CHECK_FALSE(array_assignment_sentinel.check_assignment_statement(array_integer,
                                                                       direct_scalar));
    CHECK(array_assignment_sentinel.statement_suppressed);
    CHECK(array_assignment_sentinel.type_error_occured);
    CHECK(same_token(array_assignment_sentinel.first_token, assignment_saved_first));
    CHECK(same_token(array_assignment_sentinel.second_token, assignment_saved_second));
    CHECK(array_assignment_sentinel.relation_tokens.size() ==
          assignment_saved_relations.size());
    CHECK(same_token(array_assignment_sentinel.relation_tokens[0],
                     assignment_saved_relations[0]));

    Typechecker int_float_assignment;
    CHECK(int_float_assignment.check_assignment_statement(array_integer, array_float));
    CHECK(int_float_assignment.check_assignment_statement(array_float, array_integer));
    Typechecker bool_int_assignment;
    CHECK(bool_int_assignment.check_assignment_statement(array_bool, array_integer));
    CHECK(bool_int_assignment.check_assignment_statement(array_integer, array_bool));
    Typechecker bool_float_assignment;
    CHECK_FALSE(bool_float_assignment.check_assignment_statement(array_bool, array_float));
    Typechecker string_assignment;
    CHECK_FALSE(string_assignment.check_assignment_statement(array_string, array_integer));

    Typechecker index_checker;
    CHECK(index_checker.validate_array_index(anchor, shape_of(direct_array.resolved_token),
                                              direct_scalar));
    const token_and_status bad_index = expression_of(index_checker, TYPE_FLOAT, anchor);
    index_checker.first_token = sentinel;
    index_checker.second_token = array_float.resolved_token;
    index_checker.relation_tokens = {anchor};
    const token index_saved_first = index_checker.first_token;
    const token index_saved_second = index_checker.second_token;
    const std::vector<token> index_saved_relations = index_checker.relation_tokens;
    CHECK_FALSE(index_checker.validate_array_index(anchor, shape_of(direct_array.resolved_token),
                                                    bad_index));
    CHECK(index_checker.statement_suppressed);
    CHECK(same_token(index_checker.first_token, index_saved_first));
    CHECK(same_token(index_checker.second_token, index_saved_second));
    CHECK(index_checker.relation_tokens.size() == index_saved_relations.size());
    CHECK(same_token(index_checker.relation_tokens[0], index_saved_relations[0]));

    Typechecker if_checker;
    token if_anchor;
    if_anchor.type = T_IF;
    if_anchor.line_found = 11;
    CHECK(if_checker.set_statement_type(if_anchor));
    if_checker.first_token = sentinel;
    if_checker.second_token = array_float.resolved_token;
    if_checker.relation_tokens = {anchor};
    const token if_saved_first = if_checker.first_token;
    const token if_saved_second = if_checker.second_token;
    const std::vector<token> if_saved_relations = if_checker.relation_tokens;
    CHECK_FALSE(if_checker.check_if_statement(direct_array));
    CHECK(if_checker.statement_suppressed);
    CHECK(same_token(if_checker.first_token, if_saved_first));
    CHECK(same_token(if_checker.second_token, if_saved_second));
    CHECK(if_checker.relation_tokens.size() == if_saved_relations.size());
    CHECK(same_token(if_checker.relation_tokens[0], if_saved_relations[0]));

    Typechecker loop_checker;
    CHECK(loop_checker.begin_loop_condition(anchor));
    loop_checker.first_token = sentinel;
    loop_checker.second_token = array_float.resolved_token;
    loop_checker.relation_tokens = {anchor};
    const token loop_saved_first = loop_checker.first_token;
    const token loop_saved_second = loop_checker.second_token;
    const std::vector<token> loop_saved_relations = loop_checker.relation_tokens;
    CHECK_FALSE(loop_checker.check_loop_statement(direct_array));
    CHECK(loop_checker.statement_suppressed);
    CHECK(same_token(loop_checker.first_token, loop_saved_first));
    CHECK(same_token(loop_checker.second_token, loop_saved_second));
    CHECK(loop_checker.relation_tokens.size() == loop_saved_relations.size());
    CHECK(same_token(loop_checker.relation_tokens[0], loop_saved_relations[0]));

    Typechecker return_checker;
    token procedure;
    procedure.type = T_IDENTIFIER;
    procedure.identifier_data_type = TYPE_INT;
    token return_anchor;
    return_anchor.type = T_RETURN;
    return_anchor.line_found = 19;
    return_checker.statement_key_token = return_anchor;
    return_checker.first_token = sentinel;
    return_checker.second_token = array_float.resolved_token;
    return_checker.relation_tokens = {anchor};
    const token return_saved_first = return_checker.first_token;
    const token return_saved_second = return_checker.second_token;
    const std::vector<token> return_saved_relations = return_checker.relation_tokens;
    CHECK_FALSE(return_checker.check_return_statement(direct_array, procedure));
    CHECK(return_checker.statement_suppressed);
    CHECK(same_token(return_checker.first_token, return_saved_first));
    CHECK(same_token(return_checker.second_token, return_saved_second));
    CHECK(return_checker.relation_tokens.size() == return_saved_relations.size());
    CHECK(same_token(return_checker.relation_tokens[0], return_saved_relations[0]));
}

TEST_CASE("TY-8 procedure signatures require exact array shape")
{
    token canonical;
    canonical.type = T_IDENTIFIER;
    canonical.identifer_type = I_PROCEDURE;
    canonical.procedure_params = {{TYPE_INT, true, 5}};
    token occurrence;
    occurrence.type = T_IDENTIFIER;
    occurrence.stringValue = "arrayProc";
    occurrence.line_found = 4;

    Typechecker matching_checker;
    const token_and_status matching = expression_of(matching_checker, TYPE_INT, occurrence, true, 5);
    CHECK(matching_checker.validate_procedure_call(canonical, occurrence, {matching}));

    Typechecker bound_checker;
    const token_and_status wrong_bound = expression_of(bound_checker, TYPE_INT, occurrence, true, 4);
    CHECK_FALSE(bound_checker.validate_procedure_call(canonical, occurrence, {wrong_bound}));
    CHECK(bound_checker.statement_suppressed);

    Typechecker scalar_checker;
    const token_and_status scalar = expression_of(scalar_checker, TYPE_INT, occurrence);
    CHECK_FALSE(scalar_checker.validate_procedure_call(canonical, occurrence, {scalar}));
    CHECK(scalar_checker.statement_suppressed);
}
