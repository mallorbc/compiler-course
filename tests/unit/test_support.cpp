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
