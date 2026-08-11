//Unit tests for the small pieces around the scanner: the shared string helper
//and the part of the typechecker that can safely be exercised on its own.
//
//SAFETY NOTE about the typechecker: its no-argument constructor
//(Typechecker.cpp:3) never sets parser_parent, and roughly nineteen error paths
//dereference that pointer, so a default constructed Typechecker must not be
//driven into one.  Only functions that provably cannot reach an error path are
//called below.  token_types_compatible_at_all (Typechecker.cpp:626-1085) reads
//first_token/second_token, writes a local return object, and calls nothing at
//all - no member function, no parser_parent, no output.  is_float_or_int,
//is_bool_or_int, both_are_strings and give_token_type_name
//(Typechecker.cpp:1112-1171) are likewise pure functions of their arguments.
//Nothing else in the typechecker is tested here: the accumulator side of it
//(feed_in_tokens and friends) is scheduled for replacement.
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
           left.is_array == right.is_array;
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
    const token integer = checker.make_expression_result(TYPE_INT, anchor);
    const token floating = checker.make_expression_result(TYPE_FLOAT, anchor);
    const token boolean = checker.make_expression_result(TYPE_BOOL, anchor);
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
    first.procedure_params = {TYPE_INT, TYPE_BOOL};
    first.identifier_data_type = TYPE_INT;
    first.is_array = true;

    token second = first;
    second.line_found = 8;
    second.stringValue = "second";
    second.identifier_data_type = TYPE_BOOL;
    second.procedure_params = {TYPE_FLOAT};

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
    const token integer = checker.make_expression_result(TYPE_INT, first);
    const token boolean = checker.make_expression_result(TYPE_BOOL, second);
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
    canonical.procedure_params = {TYPE_INT, TYPE_FLOAT};
    token occurrence;
    occurrence.type = T_IDENTIFIER;
    occurrence.stringValue = "q";
    occurrence.line_found = 5;

    Typechecker valid_checker;
    const token_and_status integer_argument = {
        true, true, valid_checker.make_expression_result(TYPE_INT, occurrence)};
    const token_and_status float_argument = {
        true, true, valid_checker.make_expression_result(TYPE_FLOAT, occurrence)};
    CHECK(valid_checker.validate_procedure_call(canonical, occurrence,
                                                {integer_argument, float_argument}));
    CHECK_FALSE(valid_checker.statement_suppressed);

    Typechecker mismatched_checker;
    const token_and_status bool_argument = {
        true, true, mismatched_checker.make_expression_result(TYPE_BOOL, occurrence)};
    CHECK_FALSE(mismatched_checker.validate_procedure_call(
        canonical, occurrence, {integer_argument, bool_argument}));
    CHECK(mismatched_checker.statement_suppressed);
    CHECK(mismatched_checker.type_error_occured);

    Typechecker unresolved_parameter_checker;
    token unresolved_parameter = canonical;
    unresolved_parameter.procedure_params[1] = TYPE_NONE;
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
