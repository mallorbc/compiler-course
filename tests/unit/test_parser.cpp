//Focused invariants for parser recovery.  Process-level timeout coverage lives
//in tests/test_cli.py; these tests inspect state that is intentionally public in
//the original parser design.
#include "../vendor/doctest.h"
#include "../../parser.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace
{

class temp_source_file
{
public:
    explicit temp_source_file(const std::string &contents)
    {
        const char *temp_dir = std::getenv("TMPDIR");
        std::string name_template = std::string(temp_dir ? temp_dir : "/tmp") +
                                    "/compiler_parser_unit_XXXXXX";
        file_name.assign(name_template.begin(), name_template.end());
        file_name.push_back('\0');
        int file_descriptor = mkstemp(&file_name[0]);
        REQUIRE(file_descriptor != -1);
        std::FILE *fixture = fdopen(file_descriptor, "w");
        REQUIRE(fixture != NULL);
        REQUIRE(std::fwrite(contents.data(), 1, contents.size(), fixture) == contents.size());
        std::fclose(fixture);
    }

    ~temp_source_file()
    {
        std::remove(name());
    }

    const char *name() const
    {
        return &file_name[0];
    }

private:
    std::vector<char> file_name;
};

class captured_stdout
{
public:
    captured_stdout() : previous(std::cout.rdbuf(output.rdbuf()))
    {
    }

    ~captured_stdout()
    {
        restore();
    }

    void restore()
    {
        if (previous != NULL)
        {
            std::cout.rdbuf(previous);
            previous = NULL;
        }
    }

    std::string str() const
    {
        return output.str();
    }

private:
    std::ostringstream output;
    std::streambuf *previous;
};

bool has_error(const parser &parsed, const std::string &message)
{
    for (std::size_t i = 0; i < parsed.error_reports.size(); i++)
    {
        if (parsed.error_reports[i].find(message) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

std::size_t count_errors(const parser &parsed, const std::string &message)
{
    std::size_t count = 0;
    for (std::size_t i = 0; i < parsed.error_reports.size(); i++)
    {
        if (parsed.error_reports[i].find(message) != std::string::npos)
        {
            count++;
        }
    }
    return count;
}

} // namespace

TEST_CASE("malformed procedure recovery balances its entered scope")
{
    temp_source_file fixture(
        "program test is\n"
        "procedure foo : integer ()\n"
        "begin\n"
        "    return 0;\n"
        "begin\n"
        "    missing := 1;\n"
        "end program.\n");
    captured_stdout capture;
    parser parsed(fixture.name());
    capture.restore();

    CHECK(parsed.current_scope_id == 0);
    CHECK(parsed.number_of_scopes == 0);
    CHECK(parsed.resync_status == false);
    CHECK(parsed.error_count() >= 3);
}

TEST_CASE("recovery status is cleared after an end-token synchronization")
{
    temp_source_file fixture(
        "program test is\n"
        "    variable x : integer;\n"
        "begin\n"
        "    if (true then\n"
        "        x := 1;\n"
        "    end if;\n"
        "end program.\n");
    captured_stdout capture;
    parser parsed(fixture.name());
    capture.restore();

    CHECK(parsed.resync_status == false);
    CHECK(has_error(parsed, "Missing \")\" expected for if statment"));
    CHECK_FALSE(has_error(parsed, "Missing expected keyword \"then\" for if statements"));
}

TEST_CASE("loop recovery does not suppress a following statement error")
{
    temp_source_file fixture(
        "program test is\n"
        "    variable x : integer;\n"
        "begin\n"
        "    for (x := 0; true)\n"
        "        x := 1\n"
        "    end for;\n"
        "    x := 2\n"
        "end program.\n");
    captured_stdout capture;
    parser parsed(fixture.name());
    capture.restore();

    CHECK(parsed.resync_status == false);
    CHECK(has_error(parsed, "Missing \";\" to end statement in loop statement"));
    CHECK(has_error(parsed, "Missing \";\" to end program statement"));
}

TEST_CASE("successful procedure parsing also balances its entered scope")
{
    temp_source_file fixture(
        "program test is\n"
        "procedure foo : integer ()\n"
        "begin\n"
        "    return 0;\n"
        "end procedure;\n"
        "begin\n"
        "end program.\n");
    captured_stdout capture;
    parser parsed(fixture.name());
    capture.restore();

    CHECK(parsed.current_scope_id == 0);
    CHECK(parsed.number_of_scopes == 0);
    CHECK(parsed.resync_status == false);
}

TEST_CASE("valid nested procedures unwind only the scopes they entered")
{
    temp_source_file fixture(
        "program test is\n"
        "procedure outer : integer ()\n"
        "    procedure inner : integer ()\n"
        "    begin\n"
        "        return 1;\n"
        "    end procedure;\n"
        "begin\n"
        "    return 0;\n"
        "end procedure;\n"
        "begin\n"
        "end program.\n");
    captured_stdout capture;
    parser parsed(fixture.name());
    capture.restore();

    CHECK(parsed.current_scope_id == 0);
    CHECK(parsed.number_of_scopes == 0);
    CHECK(parsed.resync_status == false);
    CHECK(parsed.error_count() == 0);
}

TEST_CASE("a malformed procedure header still unwinds its entered scope")
{
    temp_source_file fixture(
        "program test is\n"
        "procedure : integer ()\n"
        "begin\n"
        "    return 0;\n"
        "end procedure;\n"
        "begin\n"
        "end program.\n");
    captured_stdout capture;
    parser parsed(fixture.name());
    capture.restore();

    CHECK(parsed.current_scope_id == 0);
    CHECK(parsed.number_of_scopes == 0);
    CHECK(parsed.resync_status == false);
    CHECK(has_error(parsed, "Procedure must be named a valid identifier"));
}

TEST_CASE("a missing procedure closer leaves a sibling procedure for its parent")
{
    temp_source_file fixture(
        "program test is\n"
        "procedure first : integer ()\n"
        "begin\n"
        "    return 0;\n"
        "procedure second : integer ()\n"
        "begin\n"
        "    return 1;\n"
        "end procedure;\n"
        "begin\n"
        "end program.\n");
    captured_stdout capture;
    parser parsed(fixture.name());
    capture.restore();

    CHECK(parsed.current_scope_id == 0);
    CHECK(parsed.number_of_scopes == 0);
    CHECK(parsed.resync_status == false);
    CHECK(has_error(parsed, "Missing keyword \"end\" to close procedure body"));
    CHECK(has_error(parsed, "Missing keyword \"procedure\" to close procedure body"));
    CHECK_FALSE(has_error(parsed, "Procedure must be named a valid identifier"));
}

TEST_CASE("missing nested-procedure semicolon retains the enclosing scope")
{
    temp_source_file fixture(
        "program test is\n"
        "procedure outer : integer ()\n"
        "    procedure inner : integer ()\n"
        "    begin\n"
        "        return 1;\n"
        "    end procedure\n"
        "    variable retained : integer;\n"
        "begin\n"
        "    retained := 2;\n"
        "    return retained;\n"
        "end procedure;\n"
        "begin\n"
        "end program.\n");
    captured_stdout capture;
    parser parsed(fixture.name());
    capture.restore();

    CHECK(parsed.current_scope_id == 0);
    CHECK(parsed.number_of_scopes == 0);
    CHECK(parsed.resync_status == false);
    CHECK(has_error(parsed, "Missing \";\" to complete declaration"));
    CHECK(parsed.error_count() == 1);
}

TEST_CASE("recovery-dispatched procedure parsing balances its own recovery scope")
{
    temp_source_file fixture(
        "program test is\n"
        "begin\n"
        "end program.\n");
    captured_stdout capture;
    parser parsed(fixture.name());
    capture.restore();
    parsed.update_scopes(true);
    int enclosing_scope = parsed.current_scope_id;

    parsed.parse_procedure_declaration(false);

    CHECK(parsed.current_scope_id == enclosing_scope);
    CHECK(parsed.number_of_scopes == 1);
    CHECK(parsed.resync_status == false);
    parsed.update_scopes(false);
}

TEST_CASE("missing program begin preserves the end-program boundary")
{
    temp_source_file fixture(
        "program test is\n"
        "    variable x : integer;\n"
        "end program.\n");
    captured_stdout capture;
    parser parsed(fixture.name());
    capture.restore();

    CHECK(parsed.resync_status == false);
    CHECK(has_error(parsed, "Missing keyword \"begin\" to begin program statements"));
    CHECK_FALSE(has_error(parsed, "Missing \".\" to end the program"));
}

TEST_CASE("unterminated comment recovery always clears resync status")
{
    temp_source_file fixture(
        "program test is\n"
        "begin\n"
        "/* unterminated\n");
    captured_stdout capture;
    parser parsed(fixture.name());
    capture.restore();

    CHECK(parsed.resync_status == false);
    CHECK(has_error(parsed, "Unclosed block comment detected"));
}

TEST_CASE("unterminated comments retain their saved opener line")
{
    temp_source_file nested_opener(
        "program p7 is\n"
        "/* outer comment\n"
        "   /* inner comment */\n"
        "variable a : integer;\n"
        "begin\n"
        "a := 1;\n"
        "end program.\n");
    captured_stdout nested_capture;
    parser nested(nested_opener.name());
    nested_capture.restore();

    CHECK(nested.Lexer->current_line == 8);
    CHECK(has_error(nested, "Error on line 3: Unclosed block comment detected"));
    CHECK_FALSE(has_error(nested, "Error on line 8: Unclosed block comment detected"));

    temp_source_file first_line_opener(
        "/* start of comment\n"
        "x := 5;\n");
    captured_stdout first_line_capture;
    parser first_line(first_line_opener.name());
    first_line_capture.restore();

    CHECK(first_line.Lexer->current_line == 3);
    CHECK(has_error(first_line, "Error on line 1: Unclosed block comment detected"));
    CHECK_FALSE(has_error(first_line, "Error on line 3: Unclosed block comment detected"));
}

TEST_CASE("procedure parameter lists own one closing parenthesis")
{
    const std::vector<std::string> programs = {
        "program test is\n"
        "procedure one : integer(variable a : integer)\n"
        "begin\n"
        "    return a;\n"
        "end procedure;\n"
        "begin\n"
        "end program.\n",
        "program test is\n"
        "procedure two : integer(variable a : integer, variable b : float)\n"
        "begin\n"
        "    return a;\n"
        "end procedure;\n"
        "begin\n"
        "end program.\n",
        "program test is\n"
        "procedure three : integer(variable a : integer, variable b : float, variable c : bool)\n"
        "begin\n"
        "    return a;\n"
        "end procedure;\n"
        "variable result : integer;\n"
        "begin\n"
        "    result := three(1, 2.0, true);\n"
        "end program.\n"};
    const std::vector<std::string> procedure_names = {"one", "two", "three"};
    const std::vector<std::vector<value_shape>> expected_parameters = {
        {{TYPE_INT, false, -1}},
        {{TYPE_INT, false, -1}, {TYPE_FLOAT, false, -1}},
        {{TYPE_INT, false, -1}, {TYPE_FLOAT, false, -1}, {TYPE_BOOL, false, -1}}};

    for (std::size_t i = 0; i < programs.size(); i++)
    {
        temp_source_file fixture(programs[i]);
        captured_stdout capture;
        parser parsed(fixture.name());
        capture.restore();

        CHECK(parsed.error_count() == 0);
        CHECK(parsed.current_scope_id == 0);
        CHECK(parsed.number_of_scopes == 0);
        std::unordered_map<int, ScopeTable>::const_iterator root_scope =
            parsed.Lexer->symbol_table.scope_table.find(0);
        REQUIRE(root_scope != parsed.Lexer->symbol_table.scope_table.end());
        CHECK(root_scope->second.is_in_table(procedure_names[i]));
        std::unordered_map<std::string, token>::const_iterator procedure =
            root_scope->second.scope_map.find(procedure_names[i]);
        REQUIRE(procedure != root_scope->second.scope_map.end());
        CHECK(procedure->second.procedure_params == expected_parameters[i]);
    }
}

TEST_CASE("parameter lists diagnose malformed entries without losing recovery")
{
    temp_source_file trailing_parameter(
        "program test is\n"
        "procedure foo : integer(variable a : integer, )\n"
        "begin\n"
        "    return a;\n"
        "end procedure;\n"
        "begin\n"
        "end program.\n");
    captured_stdout trailing_parameter_capture;
    parser malformed_parameter(trailing_parameter.name());
    trailing_parameter_capture.restore();

    CHECK(has_error(malformed_parameter, "Missing parameter after comma in procedure parameter list"));

    temp_source_file missing_closer(
        "program test is\n"
        "procedure foo : integer(variable a : integer\n"
        "begin\n"
        "    return a;\n"
        "end procedure;\n"
        "begin\n"
        "end program.\n");
    captured_stdout missing_closer_capture;
    parser malformed_closer(missing_closer.name());
    missing_closer_capture.restore();

    CHECK(has_error(malformed_closer, "Missing \")\" to close procedure parameter list"));

    const std::vector<std::pair<std::string, std::string>> malformed_entries = {
        {", variable a : integer", "Expected keyword \"variable\" in procedure parameter list"},
        {"variable a : integer, , variable b : integer", "Expected keyword \"variable\" in procedure parameter list"},
        {"variable a : integer, b : integer", "Expected keyword \"variable\" in procedure parameter list"},
        {"variable : integer", "Missing identifier for variable declaration"},
        {"variable a : )", "Missing valid type mark"}};
    for (std::size_t i = 0; i < malformed_entries.size(); i++)
    {
        temp_source_file fixture(
            "program test is\n"
            "procedure foo : integer(" + malformed_entries[i].first + ")\n"
            "begin\n"
            "    return 0;\n"
            "end procedure;\n"
            "begin\n"
            "end program.\n");
        captured_stdout capture;
        parser parsed(fixture.name());
        capture.restore();

        CHECK(has_error(parsed, malformed_entries[i].second));
        CHECK(parsed.resync_status == false);
    }
}

TEST_CASE("invalid multiplicative and relational prefixes report focused errors")
{
    const std::vector<std::pair<std::string, std::string>> malformed_expressions = {
        {"* 2", "Missing left operand before \"*\" operator"},
        {"/ 2", "Missing left operand before \"/\" operator"},
        {"* * 2", "Missing left operand before \"*\" operator"},
        {"1 *", "Invalid token for factor discovered"},
        {"1 /", "Invalid token for factor discovered"},
        {"< 2", "Missing left operand before \"<\" operator"},
        {"<= 2", "Missing left operand before \"<\" operator"},
        {"> 2", "Missing left operand before \">\" operator"},
        {">= 2", "Missing left operand before \">\" operator"},
        {"== 2", "Missing left operand before \"==\" operator"},
        {"!= 2", "Missing left operand before \"!=\" operator"}};
    for (std::size_t i = 0; i < malformed_expressions.size(); i++)
    {
        temp_source_file fixture(
            "program test is\n"
            "variable x : integer;\n"
            "begin\n"
            "    x := " + malformed_expressions[i].first + ";\n"
            "end program.\n");
        captured_stdout capture;
        parser parsed(fixture.name());
        capture.restore();

        CHECK(has_error(parsed, malformed_expressions[i].second));
        CHECK_FALSE(has_error(parsed, "Type \"\""));
        CHECK(parsed.resync_status == false);
    }
}

TEST_CASE("trailing expression operators report errors")
{
    temp_source_file trailing_operator(
        "program test is\n"
        "variable x : integer;\n"
        "variable a : bool;\n"
        "begin\n"
        "    x := 1 *;\n"
        "    a := 1 <;\n"
        "end program.\n");
    captured_stdout trailing_operator_capture;
    parser malformed_operator(trailing_operator.name());
    trailing_operator_capture.restore();

    CHECK(has_error(malformed_operator, "Invalid token for factor discovered"));
    CHECK(has_error(malformed_operator, "Error in expression"));
}

TEST_CASE("term and relation chains consume every operator")
{
    temp_source_file fixture(
        "program test is\n"
        "variable x : integer;\n"
        "variable a : bool;\n"
        "begin\n"
        "    x := 8 / 2 * 3 / 2;\n"
        "    a := 1 < 2 < 3;\n"
        "    a := 3 <= 4 <= 5;\n"
        "    a := 5 > 4 > 3;\n"
        "    a := 5 >= 4 >= 3;\n"
        "    a := 1 == 1 == 1;\n"
        "    a := 1 != 2 != 3;\n"
        "    a := 1 < 2 >= 0 == 0 != 1;\n"
        "end program.\n");
    captured_stdout capture;
    parser parsed(fixture.name());
    capture.restore();

    CHECK(parsed.error_count() == 0);
}

TEST_CASE("parser preserves TY-7 operator error state")
{
    const std::vector<std::tuple<std::string, std::string, bool>> cases = {
        {"variable left : bool;\nvariable right : integer;\nvariable result : bool;\n",
         "result := left | right;",
         true},
        {"variable left : float;\nvariable right : string;\nvariable result : float;\n",
         "result := left & right;",
         true},
        {"variable left : bool;\nvariable right : bool;\nvariable result : bool;\n",
         "result := left & right;",
         false}};

    for (std::size_t i = 0; i < cases.size(); i++)
    {
        temp_source_file fixture(
            "program test is\n" + std::get<0>(cases[i]) +
            "begin\n" + std::get<1>(cases[i]) + "\nend program.\n");
        captured_stdout capture;
        parser parsed(fixture.name());
        capture.restore();

        CHECK(parsed.errors_occured == std::get<2>(cases[i]));
        CHECK(parsed.type_checker->type_error_occured == std::get<2>(cases[i]));
    }
}

TEST_CASE("parser diagnostics distinguish offending and omitted-token lines")
{
    temp_source_file wrong_token_on_later_line(
        "program\n"
        "is\n"
        "begin\n"
        "end program.\n");
    captured_stdout wrong_token_capture;
    parser wrong_token(wrong_token_on_later_line.name());
    wrong_token_capture.restore();
    CHECK(has_error(wrong_token, "Error on line 2: Expected \"identifier\" not found"));

    temp_source_file wrong_first_token("unexpected\n");
    captured_stdout first_token_capture;
    parser first_token(wrong_first_token.name());
    first_token_capture.restore();
    CHECK(has_error(first_token, "Error on line 1: Expected keyword \"Program\" not found"));

    temp_source_file declaration_semicolon(
        "program test is\n"
        "variable value : integer\n"
        "begin\n"
        "end program.\n");
    captured_stdout declaration_semicolon_capture;
    parser missing_semicolon(declaration_semicolon.name());
    declaration_semicolon_capture.restore();
    CHECK(has_error(missing_semicolon, "Error on line 2: Missing \";\" to complete declaration"));
}

TEST_CASE("unterminated strings retain earlier parser diagnostics")
{
    temp_source_file fixture(
        "program test is\n"
        "variable x : integer;\n"
        "variable s : string;\n"
        "begin\n"
        "    x := 2\n"
        "    x := 3\n"
        "    s := \"hello\n"
        "end program.\n");
    captured_stdout capture;
    parser parsed(fixture.name());
    capture.restore();

    CHECK(has_error(parsed, "Error on line 5: Missing \";\" to end program statement"));
    CHECK(has_error(parsed, "Error on line 6: Missing \";\" to end program statement"));
    CHECK(has_error(parsed, "Error on line 7: quotation left open"));
    CHECK(has_error(parsed, "Error on line 9: Missing \";\" to end program statement"));
    CHECK(has_error(parsed, "Error on line 9: Missing \".\" to end the program"));
}

TEST_CASE("Stage 2A retains canonical scopes and resolves local before root")
{
    temp_source_file fixture(
        "program scopes is\n"
        "variable shared : integer;\n"
        "procedure outer : integer(variable shared : bool)\n"
        "variable local : integer;\n"
        "procedure inner : integer()\n"
        "begin\n"
        "    return 0;\n"
        "end procedure;\n"
        "begin\n"
        "    return 0;\n"
        "end procedure;\n"
        "procedure sibling : integer(variable shared : float)\n"
        "begin\n"
        "    return 0;\n"
        "end procedure;\n"
        "begin\n"
        "end program.\n");
    captured_stdout capture;
    parser parsed(fixture.name());
    capture.restore();

    CHECK(parsed.error_count() == 0);
    CHECK(parsed.Lexer->symbol_table.has_scope(0));
    CHECK_FALSE(parsed.Lexer->symbol_table.has_scope(-1));
    CHECK(parsed.current_scope_id == 0);
    CHECK(parsed.number_of_scopes == 0);
    CHECK(parsed.active_scope_ids.size() == 1);
    CHECK(parsed.Lexer->symbol_table.has_scope(1));
    CHECK(parsed.Lexer->symbol_table.has_scope(2));
    CHECK(parsed.Lexer->symbol_table.has_scope(3));
    CHECK(parsed.Lexer->symbol_table.scope_table.find(1)->second.parent_scope_id == 0);
    CHECK(parsed.Lexer->symbol_table.scope_table.find(2)->second.parent_scope_id == 1);
    CHECK(parsed.Lexer->symbol_table.scope_table.find(3)->second.parent_scope_id == 0);
    CHECK(parsed.Lexer->symbol_table.scope_table.find(1)->second.has_owner_procedure);
    CHECK(parsed.Lexer->symbol_table.scope_table.find(1)->second.owner_procedure.name == "outer");

    token resolved;
    CHECK(parsed.Lexer->symbol_table.resolve_name("shared", 1, resolved));
    CHECK(resolved.identifier_data_type == TYPE_BOOL);
    CHECK(parsed.Lexer->symbol_table.resolve_name("shared", 0, resolved));
    CHECK(resolved.identifier_data_type == TYPE_INT);
    CHECK(parsed.Lexer->symbol_table.resolve_name("shared", 3, resolved));
    CHECK(resolved.identifier_data_type == TYPE_FLOAT);
}

TEST_CASE("Stage 2A declarations are transactional and retain the first signature")
{
    temp_source_file fixture(
        "program declarations is\n"
        "variable broken : ;\n"
        "procedure keep : integer(variable first : integer)\n"
        "begin\n"
        "    return 0;\n"
        "end procedure;\n"
        "procedure keep : integer(variable second : bool)\n"
        "begin\n"
        "    return 0;\n"
        "end procedure;\n"
        "begin\n"
        "end program.\n");
    captured_stdout capture;
    parser parsed(fixture.name());
    capture.restore();

    CHECK(has_error(parsed, "Missing valid type mark"));
    CHECK(has_error(parsed, "Duplicate declaration for \"keep\""));
    CHECK_FALSE(parsed.Lexer->symbol_table.has_declared(0, "broken"));
    token keep;
    CHECK(parsed.Lexer->symbol_table.lookup_declared({0, "keep"}, keep));
    REQUIRE(keep.procedure_params.size() == 1);
    const value_shape expected_keep_parameter{TYPE_INT, false, -1};
    CHECK(keep.procedure_params[0] == expected_keep_parameter);
}

TEST_CASE("Stage 2A prevents undeclared-use autovivification and type cascades")
{
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"missing := 1;", "Undeclared identifier \"missing\""},
        {"known := missing + 1;", "Undeclared identifier \"missing\""},
        {"known := missing();", "Undeclared procedure \"missing\""}};
    for (const std::pair<std::string, std::string> &test_case : cases)
    {
        temp_source_file fixture(
            "program uses is\n"
            "variable known : integer;\n"
            "begin\n"
            "    " + test_case.first + "\n"
            "end program.\n");
        captured_stdout capture;
        parser parsed(fixture.name());
        capture.restore();

        CHECK(parsed.error_reports.size() == 1);
        CHECK(has_error(parsed, test_case.second));
        CHECK_FALSE(parsed.Lexer->symbol_table.has_declared(0, "missing"));
        CHECK(parsed.Lexer->symbol_table.scope_table.size() == 1);
    }
}

TEST_CASE("Stage 2A reports wrong-kind names without a typechecker cascade")
{
    temp_source_file fixture(
        "program kinds is\n"
        "procedure callable : integer()\n"
        "begin\n"
        "    return 0;\n"
        "end procedure;\n"
        "begin\n"
        "    callable := 1;\n"
        "end program.\n");
    captured_stdout capture;
    parser parsed(fixture.name());
    capture.restore();

    CHECK(parsed.error_reports.size() == 1);
    CHECK(has_error(parsed, "Identifier \"callable\" is not a variable"));
    CHECK(parsed.Lexer->symbol_table.has_declared(0, "callable"));
}

TEST_CASE("Stage 2A semantic lookup failures preserve expression grammar")
{
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"if (missing == 0) then\nend if;",
         "Undeclared identifier \"missing\""},
        {"a := (missing + 1);", "Undeclared identifier \"missing\""},
        {"a[missing + 1] := 1;", "Undeclared identifier \"missing\""},
        {"a[missing] := 1;", "Undeclared identifier \"missing\""},
        {"if (q == 0) then\nend if;",
         "Identifier \"q\" is not a variable"},
        {"a[q] := 1;", "Identifier \"q\" is not a variable"},
        {"a := q(missing, 1);", "Undeclared identifier \"missing\""},
        {"a := absent(1, 2);", "Undeclared procedure \"absent\""}};
    for (const std::pair<std::string, std::string> &test_case : cases)
    {
        temp_source_file fixture(
            "program syntax is\n"
            "variable a : integer;\n"
            "procedure q : integer(variable first : integer, variable second : integer)\n"
            "begin\n"
            "    return 0;\n"
            "end procedure;\n"
            "begin\n" + test_case.first + "\nend program.\n");
        captured_stdout capture;
        parser parsed(fixture.name());
        capture.restore();

        CHECK(parsed.error_reports.size() == 1);
        CHECK(has_error(parsed, test_case.second));
        CHECK_FALSE(has_error(parsed, "Missing \")\""));
        CHECK_FALSE(has_error(parsed, "Missing expected keyword \"then\""));
        CHECK(capture.str().find("parser failed on parse_assignment_destination") ==
              std::string::npos);
    }
}

TEST_CASE("Stage 2A suppression ends with its statement")
{
    temp_source_file fixture(
        "program reset is\n"
        "variable a : integer;\n"
        "begin\n"
        "    a := missing;\n"
        "    a := \"string\" + 1;\n"
        "end program.\n");
    captured_stdout capture;
    parser parsed(fixture.name());
    capture.restore();

    CHECK(has_error(parsed, "Undeclared identifier \"missing\""));
    CHECK(has_error(parsed, "Arithmetic operations must be between floats and integers"));
    CHECK(parsed.error_reports.size() == 2);
}

TEST_CASE("Stage 2A separates lexical cache, builtins, aliases, and enum declarations")
{
    temp_source_file fixture(
        "program metadata is\n"
        "type amount is integer;\n"
        "type color is enum{red, green};\n"
        "variable value : amount;\n"
        "begin\n"
        "value := getInteger();\n"
        "end program.\n");
    captured_stdout capture;
    parser parsed(fixture.name());
    capture.restore();

    CHECK(parsed.error_count() == 0);
    token amount;
    token get_integer;
    CHECK(parsed.Lexer->symbol_table.lookup_declared({0, "amount"}, amount));
    CHECK(amount.identifer_type == I_TYPE);
    CHECK(amount.identifier_data_type == TYPE_INT);
    CHECK(parsed.Lexer->symbol_table.has_declared(0, "red"));
    CHECK(parsed.Lexer->symbol_table.lookup_declared({0, "getinteger"}, get_integer));
    CHECK(get_integer.identifer_type == I_PROCEDURE);
    CHECK(get_integer.identifier_data_type == TYPE_INT);
    const std::vector<std::tuple<std::string, data_types, std::vector<value_shape>>> builtins = {
        {"getbool", TYPE_BOOL, {}}, {"getinteger", TYPE_INT, {}},
        {"getfloat", TYPE_FLOAT, {}}, {"getstring", TYPE_STRING, {}},
        {"putbool", TYPE_BOOL, {{TYPE_BOOL, false, -1}}},
        {"putinteger", TYPE_BOOL, {{TYPE_INT, false, -1}}},
        {"putfloat", TYPE_BOOL, {{TYPE_FLOAT, false, -1}}},
        {"putstring", TYPE_BOOL, {{TYPE_STRING, false, -1}}},
        {"sqrt", TYPE_FLOAT, {{TYPE_INT, false, -1}}}};
    for (const std::tuple<std::string, data_types, std::vector<value_shape>> &builtin : builtins)
    {
        token builtin_symbol;
        CHECK(parsed.Lexer->symbol_table.lookup_declared({0, std::get<0>(builtin)},
                                                          builtin_symbol));
        CHECK(builtin_symbol.identifer_type == I_PROCEDURE);
        CHECK(builtin_symbol.identifier_data_type == std::get<1>(builtin));
        CHECK(builtin_symbol.procedure_params == std::get<2>(builtin));
    }
    CHECK(parsed.Lexer->symbol_table.map.find("amount") !=
          parsed.Lexer->symbol_table.map.end());
    CHECK(parsed.Lexer->symbol_table.map.find("amount")->second.identifer_type == I_NONE);
}

TEST_CASE("Stage 2A rejects every same-scope duplicate without partial publication")
{
    temp_source_file fixture(
        "program duplicates is\n"
        "variable taken : integer;\n"
        "type taken is integer;\n"
        "type repeated is enum{first, first};\n"
        "procedure duplicate_param : integer(variable value : integer, variable value : bool)\n"
        "begin\n"
        "    return 0;\n"
        "end procedure;\n"
        "begin\n"
        "end program.\n");
    captured_stdout capture;
    parser parsed(fixture.name());
    capture.restore();

    CHECK(has_error(parsed, "Duplicate declaration for \"taken\""));
    CHECK(has_error(parsed, "Duplicate declaration in procedure header"));
    CHECK(parsed.Lexer->symbol_table.has_declared(0, "taken"));
    token taken;
    CHECK(parsed.Lexer->symbol_table.lookup_declared({0, "taken"}, taken));
    CHECK(taken.identifer_type == I_VARIABLE);
    CHECK_FALSE(parsed.Lexer->symbol_table.has_declared(0, "repeated"));
    CHECK_FALSE(parsed.Lexer->symbol_table.has_declared(0, "first"));
    CHECK_FALSE(parsed.Lexer->symbol_table.has_declared(0, "duplicate_param"));
}

TEST_CASE("Stage 2A local declarations shadow builtin procedures")
{
    temp_source_file fixture(
        "program shadowbuiltin is\n"
        "procedure local : integer()\n"
        "variable getinteger : integer;\n"
        "begin\n"
        "    return getinteger;\n"
        "end procedure;\n"
        "begin\n"
        "end program.\n");
    captured_stdout capture;
    parser parsed(fixture.name());
    capture.restore();

    CHECK(parsed.error_count() == 0);
    token local_get_integer;
    CHECK(parsed.Lexer->symbol_table.resolve_name("getinteger", 1,
                                                  local_get_integer));
    CHECK(local_get_integer.identifer_type == I_VARIABLE);
    CHECK(local_get_integer.identifier_data_type == TYPE_INT);
}

TEST_CASE("TY-2E synthesizes nested calls, parentheses, and exact operator results")
{
    temp_source_file fixture(
        "program expressions is\n"
        "procedure predicate : bool(variable value : integer)\n"
        "begin\n"
        "    return value > 0;\n"
        "end procedure;\n"
        "variable i : integer;\n"
        "variable f : float;\n"
        "variable b : bool;\n"
        "begin\n"
        "    i := 7 / 2;\n"
        "    f := 1 + 2.0;\n"
        "    f := 2.0 + 1;\n"
        "    i := (1 + 2) * 3;\n"
        "    b := not true & false;\n"
        "    i := not 3;\n"
        "    f := -1.0;\n"
        "    i := -i;\n"
        "    b := (1 + 2) < 4;\n"
        "    b := 3 <= 4;\n"
        "    b := 5 > 4;\n"
        "    b := 5 >= 4;\n"
        "    b := \"a\" == \"b\";\n"
        "    b := \"a\" != \"b\";\n"
        "    b := predicate((1 + 2) * 3);\n"
        "    b := predicate(1) & true;\n"
        "end program.\n");
    captured_stdout capture;
    parser parsed(fixture.name());
    capture.restore();

    CHECK(parsed.error_count() == 0);
    CHECK_FALSE(parsed.type_checker->statement_suppressed);
}

TEST_CASE("TY-2E distinguishes syntax errors from semantic folds and resets suppression")
{
    temp_source_file fixture(
        "program invalid is\n"
        "variable i : integer;\n"
        "variable b : bool;\n"
        "begin\n"
        "    i := 1 & true | \"s\";\n"
        "    b := 1 < 2 < 3.0;\n"
        "    i := \"s\" + 1;\n"
        "end program.\n");
    captured_stdout capture;
    parser parsed(fixture.name());
    capture.restore();

    CHECK(parsed.error_reports.size() == 3);
    CHECK(has_error(parsed, "Bitwise and logical \"&\" operations require two integers or two bools"));
    CHECK(has_error(parsed, "Ordering relations require compatible integers, floats, or bools"));
    CHECK(has_error(parsed, "Arithmetic operations must be between floats and integers"));
    CHECK_FALSE(has_error(parsed, "no valid operations"));
}

TEST_CASE("TY-2E reports an invalid fold at the physical operator line")
{
    temp_source_file fixture(
        "program lines is\n"
        "variable i : integer;\n"
        "begin\n"
        "    i := 1\n"
        "        & true;\n"
        "end program.\n");
    captured_stdout capture;
    parser parsed(fixture.name());
    capture.restore();

    CHECK(parsed.error_reports.size() == 1);
    CHECK(has_error(parsed, "Error on line 5: Bitwise and logical \"&\" operations require two integers or two bools"));
}

TEST_CASE("TY-2E keeps unusual precedence and malformed logical right operands structural")
{
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"i := 1 + 2 < 3;", "Arithmetic operations must be between floats and integers"},
        {"b := true & not false;", "Invalid token for factor discovered"},
        {"b := not not true;", "Invalid token for factor discovered"},
        {"i := + 1;", "Missing left operand before \"+\" operator"},
        {"i := 1 & & 2;", "Missing left operand before \"&\" operator"}};
    for (const std::pair<std::string, std::string> &test_case : cases)
    {
        temp_source_file fixture(
            "program precedence is\n"
            "variable i : integer;\n"
            "variable b : bool;\n"
            "begin\n"
            "    " + test_case.first + "\n"
            "end program.\n");
        captured_stdout capture;
        parser parsed(fixture.name());
        capture.restore();

        CHECK(has_error(parsed, test_case.second));
        CHECK(parsed.error_count() >= 1);
    }
}

TEST_CASE("TY-2E keeps invalid call arguments safe and enum values focused")
{
    temp_source_file call_fixture(
        "program calls is\n"
        "procedure q : integer(variable first : integer, variable second : integer, variable third : bool)\n"
        "begin\n"
        "    return first;\n"
        "end procedure;\n"
        "variable a : integer;\n"
        "begin\n"
        "    a := q(missing, 1 + 2, not false) + 1;\n"
        "    a := \"s\" + 1;\n"
        "end program.\n");
    captured_stdout call_capture;
    parser calls(call_fixture.name());
    call_capture.restore();

    CHECK(calls.error_reports.size() == 2);
    CHECK(has_error(calls, "Undeclared identifier \"missing\""));
    CHECK(has_error(calls, "Arithmetic operations must be between floats and integers"));

    temp_source_file enum_fixture(
        "program enumtype is\n"
        "type color is enum{red, green};\n"
        "variable c : color;\n"
        "variable b : bool;\n"
        "begin\n"
        "    b := c == c;\n"
        "end program.\n");
    captured_stdout enum_capture;
    parser enum_program(enum_fixture.name());
    enum_capture.restore();

    CHECK(enum_program.error_reports.size() == 1);
    CHECK(has_error(enum_program, "Identifier \"c\" has no resolved type"));
}

TEST_CASE("SIL-1 validates complete procedure calls by exact ordered signature")
{
    const std::vector<std::tuple<std::string, std::string, int>> cases = {
        {"q(1, 2.0, true)", "", 0},
        {"q(1 + 2, (1 + 2.0), not false)", "", 0},
        {"zero()", "", 0},
        {"q(1, 2.0, true, 0)", "Procedure \"q\" expects 3 argument(s), got 4", 1},
        {"q(1)", "Procedure \"q\" expects 3 argument(s), got 1", 1},
        {"zero(1)", "Procedure \"zero\" expects 0 argument(s), got 1", 1},
        {"q(1.0, 2.0, true)", "Argument 1 to procedure \"q\" has type \"float\"; expected \"integer\"", 1},
        {"q(1, 2, true)", "Argument 2 to procedure \"q\" has type \"integer\"; expected \"float\"", 1},
        {"q(1, 2.0, 1)", "Argument 3 to procedure \"q\" has type \"integer\"; expected \"bool\"", 1}};

    for (const std::tuple<std::string, std::string, int> &test_case : cases)
    {
        temp_source_file fixture(
            "program calls is\n"
            "type count is integer;\n"
            "procedure q : integer(variable first : count, variable second : float, variable third : bool)\n"
            "begin\n"
            "    return first;\n"
            "end procedure;\n"
            "procedure zero : integer()\n"
            "begin\n"
            "    return 0;\n"
            "end procedure;\n"
            "variable value : integer;\n"
            "begin\n"
            "    value := " + std::get<0>(test_case) + ";\n"
            "end program.\n");
        captured_stdout capture;
        parser parsed(fixture.name());
        capture.restore();

        CHECK(parsed.error_count() == std::get<2>(test_case));
        if (!std::get<1>(test_case).empty())
        {
            CHECK(has_error(parsed, std::get<1>(test_case)));
        }
    }
}

TEST_CASE("SIL-1 keeps malformed and unresolved calls structural without signature cascades")
{
    const std::vector<std::string> malformed_calls = {
        "q(, 1)", "q(1,)", "q(1,, 2)", "q(1"};
    for (const std::string &call : malformed_calls)
    {
        temp_source_file fixture(
            "program malformed is\n"
            "procedure q : integer(variable value : integer)\n"
            "begin\n"
            "    return value;\n"
            "end procedure;\n"
            "variable result : integer;\n"
            "begin\n"
            "    result := " + call + ";\n"
            "end program.\n");
        captured_stdout capture;
        parser parsed(fixture.name());
        capture.restore();

        CHECK(parsed.error_count() >= 1);
        CHECK_FALSE(has_error(parsed, "expects "));
        CHECK_FALSE(has_error(parsed, "Argument 1 to procedure"));
    }

    temp_source_file failed_then_valid(
        "program reset is\n"
        "procedure q : integer(variable value : integer)\n"
        "begin\n"
        "    return value;\n"
        "end procedure;\n"
        "variable callable : integer;\n"
        "variable result : integer;\n"
        "begin\n"
        "    result := q(1, 2);\n"
        "    result := q(1);\n"
        "    result := callable(1, 2);\n"
        "    result := missing(1, 2);\n"
        "end program.\n");
    captured_stdout reset_capture;
    parser reset(failed_then_valid.name());
    reset_capture.restore();

    CHECK(reset.error_reports.size() == 3);
    CHECK(has_error(reset, "Procedure \"q\" expects 1 argument(s), got 2"));
    CHECK(has_error(reset, "Identifier \"callable\" is not a procedure"));
    CHECK(has_error(reset, "Undeclared procedure \"missing\""));
    CHECK_FALSE(has_error(reset, "Procedure \"callable\" expects"));
    CHECK_FALSE(has_error(reset, "Procedure \"missing\" expects"));
}

TEST_CASE("SIL-1 anchors type mismatches at their argument and supports recursion")
{
    temp_source_file mismatch(
        "program lines is\n"
        "procedure q : integer(variable first : integer, variable second : float)\n"
        "begin\n"
        "    return first;\n"
        "end procedure;\n"
        "variable value : integer;\n"
        "begin\n"
        "    value := q(1,\n"
        "               true);\n"
        "end program.\n");
    captured_stdout mismatch_capture;
    parser multiline(mismatch.name());
    mismatch_capture.restore();

    CHECK(multiline.error_reports.size() == 1);
    CHECK(has_error(multiline, "Error on line 9: Argument 2 to procedure \"q\""));

    const std::vector<std::pair<std::string, std::string>> recursion_cases = {
        {"recurse(1)", ""},
        {"recurse(1.0)", "Argument 1 to procedure \"recurse\" has type \"float\"; expected \"integer\""}};
    for (const std::pair<std::string, std::string> &test_case : recursion_cases)
    {
        temp_source_file fixture(
            "program recursion is\n"
            "procedure recurse : integer(variable value : integer)\n"
            "begin\n"
            "    return recurse(value);\n"
            "end procedure;\n"
            "variable answer : integer;\n"
            "begin\n"
            "    answer := " + test_case.first + ";\n"
            "end program.\n");
        captured_stdout capture;
        parser parsed(fixture.name());
        capture.restore();

        CHECK(parsed.error_count() == (test_case.second.empty() ? 0 : 1));
        if (!test_case.second.empty())
        {
            CHECK(has_error(parsed, test_case.second));
        }
    }
}

TEST_CASE("Stage 2D accepts the scalar assignment compatibility matrix")
{
    temp_source_file fixture(
        "program assignments is\n"
        "type count is integer;\n"
        "procedure integerValue : integer()\n"
        "begin\n"
        "    return 1;\n"
        "end procedure;\n"
        "variable i : integer;\n"
        "variable f : float;\n"
        "variable b : bool;\n"
        "variable s : string;\n"
        "variable alias : count;\n"
        "begin\n"
        "    i := i;\n"
        "    f := f;\n"
        "    b := b;\n"
        "    i := f;\n"
        "    f := i;\n"
        "    i := b;\n"
        "    b := i;\n"
        "    s := s;\n"
        "    alias := i;\n"
        "    i := alias;\n"
        "    i := integerValue();\n"
        "end program.\n");
    captured_stdout capture;
    parser parsed(fixture.name());
    capture.restore();

    CHECK(parsed.error_count() == 0);
}

TEST_CASE("Stage 2D reports scalar assignment failures at the target and resets")
{
    temp_source_file fixture(
        "program assignments is\n"
        "type color is enum{red};\n"
        "procedure stringValue : string()\n"
        "begin\n"
        "    return \"s\";\n"
        "end procedure;\n"
        "variable i : integer;\n"
        "variable f : float;\n"
        "variable b : bool;\n"
        "variable s : string;\n"
        "variable c : color;\n"
        "begin\n"
        "    b := f;\n"
        "    f := b;\n"
        "    s := i;\n"
        "    i := stringValue();\n"
        "    i := c;\n"
        "    c := i;\n"
        "    i := \"s\";\n"
        "end program.\n");
    captured_stdout capture;
    parser parsed(fixture.name());
    capture.restore();

    CHECK(parsed.error_reports.size() == 7);
    CHECK(has_error(parsed, "Error on line 13: Assignment target type \"bool\" is not compatible with expression type \"float\""));
    CHECK(has_error(parsed, "Error on line 14: Assignment target type \"float\" is not compatible with expression type \"bool\""));
    CHECK(has_error(parsed, "Error on line 15: Assignment target type \"string\" is not compatible with expression type \"integer\""));
    CHECK(has_error(parsed, "Error on line 16: Assignment target type \"integer\" is not compatible with expression type \"string\""));
    CHECK(has_error(parsed, "Identifier \"c\" has no resolved type"));
    CHECK_FALSE(has_error(parsed, "expression type \"unknown\""));
    CHECK(has_error(parsed, "Error on line 19: Assignment target type \"integer\" is not compatible with expression type \"string\""));

    temp_source_file multiline(
        "program lines is\n"
        "variable i : integer;\n"
        "begin\n"
        "    i\n"
        "        := \"s\";\n"
        "end program.\n");
    captured_stdout multiline_capture;
    parser multiline_program(multiline.name());
    multiline_capture.restore();

    CHECK(multiline_program.error_reports.size() == 1);
    CHECK(has_error(multiline_program, "Error on line 4: Assignment target type"));
}

TEST_CASE("Stage 2D gives loop initializers and conditions separate semantic boundaries")
{
    temp_source_file valid_fixture(
        "program loops is\n"
        "variable i : integer;\n"
        "begin\n"
        "    for (i := 0; true)\n"
        "        i := i + 1;\n"
        "    end for;\n"
        "    for (i := 0; i)\n"
        "        i := i + 1;\n"
        "    end for;\n"
        "    for (i := 0; i < 3)\n"
        "        i := i + 1;\n"
        "    end for;\n"
        "end program.\n");
    captured_stdout valid_capture;
    parser valid(valid_fixture.name());
    valid_capture.restore();
    CHECK(valid.error_count() == 0);

    temp_source_file invalid_fixture(
        "program loops is\n"
        "variable i : integer;\n"
        "variable f : float;\n"
        "variable s : string;\n"
        "begin\n"
        "    for (i := 0;\n"
        "         f)\n"
        "        i := i + 1;\n"
        "    end for;\n"
        "    for (i := \"s\"; s)\n"
        "        i := i + 1;\n"
        "    end for;\n"
        "    for (i := 0; \"s\" + 1)\n"
        "        i := i + 1;\n"
        "    end for;\n"
        "    for (i := 0; missing())\n"
        "        i := i + 1;\n"
        "    end for;\n"
        "    i := \"after\";\n"
        "end program.\n");
    captured_stdout invalid_capture;
    parser invalid(invalid_fixture.name());
    invalid_capture.restore();

    CHECK(invalid.error_reports.size() == 6);
    CHECK(has_error(invalid, "Error on line 7: Loop statements must resolve to either type Bool or Integer"));
    CHECK(has_error(invalid, "Error on line 10: Assignment target type \"integer\" is not compatible with expression type \"string\""));
    CHECK(has_error(invalid, "Error on line 10: Loop statements must resolve to either type Bool or Integer"));
    CHECK(has_error(invalid, "Arithmetic operations must be between floats and integers"));
    CHECK(has_error(invalid, "Undeclared procedure \"missing\""));
    CHECK(has_error(invalid, "Error on line 19: Assignment target type \"integer\" is not compatible with expression type \"string\""));
    CHECK_FALSE(has_error(invalid, "Error on line 14: Loop statements"));
    CHECK_FALSE(has_error(invalid, "Error on line 17: Loop statements"));

    temp_source_file missing_closer(
        "program malformed is\n"
        "variable i : integer;\n"
        "variable f : float;\n"
        "begin\n"
        "    for (i := 0; f\n"
        "        i := i + 1;\n"
        "    end for;\n"
        "end program.\n");
    captured_stdout missing_capture;
    parser malformed(missing_closer.name());
    missing_capture.restore();

    CHECK(has_error(malformed, "Missing \")\" for loop declaration"));
    CHECK_FALSE(has_error(malformed, "Loop statements must resolve to either type Bool or Integer"));

    temp_source_file nested_reset(
        "program nested is\n"
        "variable i : integer;\n"
        "variable f : float;\n"
        "variable s : string;\n"
        "begin\n"
        "    for (i := 0; f)\n"
        "        for (i := 0; s)\n"
        "            i := i + 1;\n"
        "        end for;\n"
        "        i := \"body\";\n"
        "    end for;\n"
        "    i := \"after\";\n"
        "end program.\n");
    captured_stdout nested_capture;
    parser nested(nested_reset.name());
    nested_capture.restore();

    CHECK(nested.error_reports.size() == 4);
    CHECK(has_error(nested, "Error on line 6: Loop statements must resolve to either type Bool or Integer"));
    CHECK(has_error(nested, "Error on line 7: Loop statements must resolve to either type Bool or Integer"));
    CHECK(has_error(nested, "Error on line 10: Assignment target type \"integer\" is not compatible with expression type \"string\""));
    CHECK(has_error(nested, "Error on line 12: Assignment target type \"integer\" is not compatible with expression type \"string\""));
}

TEST_CASE("TY-8 records inclusive canonical bounds and exact parameter shapes")
{
    temp_source_file fixture(
        "program arrays is\n"
        "variable zero : integer[0];\n"
        "variable five : integer[5];\n"
        "procedure keep : integer(variable values : integer[5])\n"
        "begin\n"
        "    return values[0];\n"
        "end procedure;\n"
        "begin\n"
        "    zero[0] := keep(five);\n"
        "end program.\n");
    captured_stdout capture;
    parser parsed(fixture.name());
    capture.restore();

    CHECK(parsed.error_count() == 0);
    token zero;
    token five;
    token keep;
    CHECK(parsed.Lexer->symbol_table.lookup_declared({0, "zero"}, zero));
    CHECK(parsed.Lexer->symbol_table.lookup_declared({0, "five"}, five));
    CHECK(parsed.Lexer->symbol_table.lookup_declared({0, "keep"}, keep));
    CHECK(zero.is_array);
    CHECK(zero.array_upper_bound == 0);
    CHECK(five.is_array);
    CHECK(five.array_upper_bound == 5);
    REQUIRE(keep.procedure_params.size() == 1);
    const value_shape expected_array_parameter{TYPE_INT, true, 5};
    CHECK(keep.procedure_params[0] == expected_array_parameter);
    REQUIRE(parsed.can_generate_code());
    REQUIRE(parsed.ir_status() == ir::ModuleStatus::Ready);
    const ir::Function &program = parsed.ir_module().functions[0];
    std::size_t destination_check = program.blocks[0].instructions.size();
    std::size_t rhs_snapshot = program.blocks[0].instructions.size();
    std::size_t call = program.blocks[0].instructions.size();
    std::size_t element_store = program.blocks[0].instructions.size();
    bool loaded_destination_base = false;
    for (std::size_t i = 0; i < program.blocks[0].instructions.size(); i++)
    {
        const ir::Instruction &instruction = program.blocks[0].instructions[i];
        if (const ir::CheckIndex *check = std::get_if<ir::CheckIndex>(&instruction))
        {
            destination_check = std::min(destination_check, i);
            CHECK(check->storage == parsed.ir_module().storages[0].id);
        }
        else if (const ir::Load *load = std::get_if<ir::Load>(&instruction))
        {
            loaded_destination_base = loaded_destination_base ||
                                      load->source == parsed.ir_module().storages[0].id;
            if (load->source == parsed.ir_module().storages[1].id)
            {
                rhs_snapshot = i;
            }
        }
        else if (std::holds_alternative<ir::Call>(instruction))
        {
            call = i;
        }
        else if (std::holds_alternative<ir::ElementStore>(instruction))
        {
            element_store = i;
        }
    }
    CHECK_FALSE(loaded_destination_base);
    CHECK(destination_check < rhs_snapshot);
    CHECK(rhs_snapshot < call);
    CHECK(call < element_store);
}

TEST_CASE("TY-8 bound errors are transactional and retain a first procedure signature")
{
    temp_source_file fixture(
        "program arrays is\n"
        "variable floatBound : integer[2.0];\n"
        "variable negativeBound : integer[-2];\n"
        "variable kept : integer[5];\n"
        "procedure retained : integer(variable values : integer[5])\n"
        "begin\n"
        "    return 0;\n"
        "end procedure;\n"
        "procedure retained : integer(variable values : integer[2])\n"
        "begin\n"
        "    return 0;\n"
        "end procedure;\n"
        "begin\n"
        "    kept[0] := retained(kept);\n"
        "end program.\n");
    captured_stdout capture;
    parser parsed(fixture.name());
    capture.restore();

    CHECK(has_error(parsed, "Array upper bound must be a non-negative integer"));
    CHECK(has_error(parsed, "Duplicate declaration for \"retained\""));
    CHECK_FALSE(parsed.Lexer->symbol_table.has_declared(0, "floatbound"));
    CHECK_FALSE(parsed.Lexer->symbol_table.has_declared(0, "negativebound"));
    token retained;
    CHECK(parsed.Lexer->symbol_table.lookup_declared({0, "retained"}, retained));
    REQUIRE(retained.procedure_params.size() == 1);
    const value_shape expected_retained_parameter{TYPE_INT, true, 5};
    CHECK(retained.procedure_params[0] == expected_retained_parameter);

    temp_source_file malformed(
        "program arrays is\n"
        "variable missing : integer[];\n"
        "variable next : integer;\n"
        "begin\n"
        "    next := 1;\n"
        "end program.\n");
    captured_stdout malformed_capture;
    parser malformed_parse(malformed.name());
    malformed_capture.restore();
    CHECK(has_error(malformed_parse, "Missing expected number"));
    CHECK_FALSE(malformed_parse.Lexer->symbol_table.has_declared(0, "missing"));
    CHECK(malformed_parse.Lexer->symbol_table.has_declared(0, "next"));

    temp_source_file invalid_header(
        "program arrays is\n"
        "procedure rejected : integer(variable values : integer[1.5])\n"
        "begin\n"
        "    return 0;\n"
        "end procedure;\n"
        "variable after : integer;\n"
        "begin\n"
        "    after := 1;\n"
        "end program.\n");
    captured_stdout invalid_header_capture;
    parser invalid_header_parse(invalid_header.name());
    invalid_header_capture.restore();
    CHECK(invalid_header_parse.error_reports.size() == 1);
    CHECK(has_error(invalid_header_parse, "Array upper bound must be a non-negative integer"));
    CHECK_FALSE(invalid_header_parse.Lexer->symbol_table.has_declared(0, "rejected"));
    CHECK(invalid_header_parse.Lexer->symbol_table.has_declared(0, "after"));
    CHECK(invalid_header_parse.current_scope_id == 0);
}

TEST_CASE("TY-8 semantically rejected procedure headers still complete structurally")
{
    temp_source_file fixture(
        "program arrays is\n"
        "procedure retained : integer(variable values : integer[5])\n"
        "begin\n"
        "    return 0;\n"
        "end procedure;\n"
        "procedure retained : integer(variable values : integer[2])\n"
        "begin\n"
        "    return 0;\n"
        "end procedure;\n"
        "procedure negative : integer(variable values : integer[-1])\n"
        "begin\n"
        "    return 0;\n"
        "end procedure;\n"
        "procedure floating : integer(variable values : integer[1.5])\n"
        "begin\n"
        "    return 0;\n"
        "end procedure;\n"
        "variable later : integer[5];\n"
        "begin\n"
        "    later[0] := retained(later);\n"
        "end program.\n");
    captured_stdout capture;
    parser parsed(fixture.name());
    capture.restore();

    CHECK(parsed.error_reports.size() == 3);
    CHECK(has_error(parsed, "Duplicate declaration for \"retained\""));
    CHECK(has_error(parsed, "Array upper bound must be a non-negative integer"));
    CHECK(count_errors(parsed, "Array upper bound must be a non-negative integer") == 2);
    CHECK_FALSE(has_error(parsed, "Expected keyword \"variable\" in procedure parameter list"));
    token retained;
    CHECK(parsed.Lexer->symbol_table.lookup_declared({0, "retained"}, retained));
    REQUIRE(retained.procedure_params.size() == 1);
    CHECK((retained.procedure_params[0] == value_shape{TYPE_INT, true, 5}));
    CHECK_FALSE(parsed.Lexer->symbol_table.has_declared(0, "negative"));
    CHECK_FALSE(parsed.Lexer->symbol_table.has_declared(0, "floating"));
    CHECK(parsed.Lexer->symbol_table.has_declared(0, "later"));
    CHECK(parsed.current_scope_id == 0);
    CHECK(parsed.number_of_scopes == 0);
    CHECK(parsed.active_scope_ids.size() == 1);
    CHECK_FALSE(parsed.resync_status);

    temp_source_file missing_close(
        "program arrays is\n"
        "procedure retained : integer(variable value : integer[5])\n"
        "begin\n"
        "    return 0;\n"
        "end procedure;\n"
        "procedure retained : integer(variable value : integer[5)\n"
        "begin\n"
        "    return 0;\n"
        "end procedure;\n"
        "variable later : integer;\n"
        "begin\n"
        "    later := 1;\n"
        "end program.\n");
    captured_stdout missing_close_capture;
    parser malformed(missing_close.name());
    missing_close_capture.restore();

    //A missing closer is deliberately still a syntax failure: normal recovery
    //reports the bad declaration start after the focused suffix diagnostic.
    CHECK(malformed.error_reports.size() == 3);
    CHECK(has_error(malformed, "Missing \"]\" to close the array declaration"));
    CHECK(has_error(malformed, "Duplicate declaration for \"retained\""));
    CHECK(has_error(malformed,
                    "Expected keywords \"procedure\",\"variable\" \"type\", or \"begin\" not found"));
    token original;
    CHECK(malformed.Lexer->symbol_table.lookup_declared({0, "retained"}, original));
    REQUIRE(original.procedure_params.size() == 1);
    CHECK((original.procedure_params[0] == value_shape{TYPE_INT, true, 5}));
    CHECK(malformed.Lexer->symbol_table.has_declared(0, "later"));
    CHECK(malformed.current_scope_id == 0);
    CHECK(malformed.number_of_scopes == 0);
    CHECK_FALSE(malformed.resync_status);
}

TEST_CASE("TY-8 array return errors anchor the return keyword")
{
    temp_source_file fixture(
        "program arrays is\n"
        "procedure invalid : integer()\n"
        "variable values : integer[5];\n"
        "begin\n"
        "    return\n"
        "        values;\n"
        "end procedure;\n"
        "begin\n"
        "end program.\n");
    captured_stdout capture;
    parser parsed(fixture.name());
    capture.restore();

    CHECK(parsed.error_reports.size() == 1);
    CHECK(has_error(parsed, "Error on line 5: Procedure return values must be scalar"));
}

TEST_CASE("TY-8 index checks consume syntax and leave declaration metadata unchanged")
{
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"scalar[0] := 1;", "Identifier \"scalar\" is not an array"},
        {"arr[f] := 1;", "Array index must resolve to type integer"},
        {"arr[flag] := 1;", "Array index must resolve to type integer"},
        {"arr[\"s\"] := 1;", "Array index must resolve to type integer"},
        {"arr[other] := 1;", "Array index must resolve to type integer"},
        {"arr[missing + 1] := 1;", "Undeclared identifier \"missing\""},
        {"arr[proc] := 1;", "Identifier \"proc\" is not a variable"}};
    for (const std::pair<std::string, std::string> &test_case : cases)
    {
        temp_source_file fixture(
            "program arrays is\n"
            "variable scalar : integer;\n"
            "variable arr : integer[5];\n"
            "variable other : integer[5];\n"
            "variable f : float;\n"
            "variable flag : bool;\n"
            "procedure proc : integer()\n"
            "begin\n"
            "    return 0;\n"
            "end procedure;\n"
            "begin\n"
            "    " + test_case.first + "\n"
            "end program.\n");
        captured_stdout capture;
        parser parsed(fixture.name());
        capture.restore();

        CHECK(parsed.error_reports.size() == 1);
        CHECK(has_error(parsed, test_case.second));
        token canonical_arr;
        CHECK(parsed.Lexer->symbol_table.lookup_declared({0, "arr"}, canonical_arr));
        CHECK(canonical_arr.is_array);
        CHECK(canonical_arr.array_upper_bound == 5);
    }
}

TEST_CASE("TY-8 index recovery gives syntax and child failures precedence")
{
    temp_source_file child_failure(
        "program arrays is\n"
        "variable scalar : integer;\n"
        "begin\n"
        "    scalar[missing + 1] := 1;\n"
        "    scalar := 1;\n"
        "end program.\n");
    captured_stdout child_capture;
    parser child(child_failure.name());
    child_capture.restore();
    CHECK(child.error_reports.size() == 1);
    CHECK(has_error(child, "Undeclared identifier \"missing\""));
    CHECK_FALSE(has_error(child, "is not an array"));

    temp_source_file missing_closer(
        "program arrays is\n"
        "variable a : integer[5];\n"
        "begin\n"
        "    a[1.0 := 1;\n"
        "    a[0] := 1;\n"
        "end program.\n");
    captured_stdout closer_capture;
    parser closer(missing_closer.name());
    closer_capture.restore();
    CHECK(has_error(closer, "Missing closing right bracket to the identifier expression"));
    CHECK_FALSE(has_error(closer, "Array index must resolve to type integer"));

    temp_source_file multiline_index(
        "program arrays is\n"
        "variable a : integer[5];\n"
        "variable f : float;\n"
        "begin\n"
        "    a[\n"
        "        f] := 1;\n"
        "    a[0] := 1;\n"
        "end program.\n");
    captured_stdout multiline_capture;
    parser multiline(multiline_index.name());
    multiline_capture.restore();
    CHECK(multiline.error_reports.size() == 1);
    CHECK(has_error(multiline, "Error on line 6: Array index must resolve to type integer"));
}

TEST_CASE("TY-8 scalar builtin signatures reject array call arguments exactly")
{
    temp_source_file fixture(
        "program arrays is\n"
        "variable values : integer[5];\n"
        "variable result : bool;\n"
        "begin\n"
        "    result := putInteger(values);\n"
        "end program.\n");
    captured_stdout capture;
    parser parsed(fixture.name());
    capture.restore();

    CHECK(parsed.error_reports.size() == 1);
    CHECK(has_error(parsed,
                    "Argument 1 to procedure \"putinteger\" has type \"integer[5]\"; expected \"integer\""));
}

TEST_CASE("Stage 2F return consumers validate conversions and placement")
{
    temp_source_file fixture(
        "program returns is\n"
        "procedure integerFromFloat : integer()\n"
        "begin\n"
        "    return 1.0;\n"
        "end procedure;\n"
        "procedure badBool : bool()\n"
        "begin\n"
        "    return\n"
        "        1.0;\n"
        "end procedure;\n"
        "procedure noReturn : integer()\n"
        "begin\n"
        "end procedure;\n"
        "begin\n"
        "    return 1;\n"
        "end program.\n");
    captured_stdout capture;
    parser parsed(fixture.name());
    capture.restore();

    CHECK(parsed.error_reports.size() == 2);
    CHECK(has_error(parsed,
                    "Error on line 8: Procedure is of type \"Float\" which is not compatible with return type of \"Bool\""));
    CHECK(has_error(parsed,
                    "Error on line 15: Return statements are only valid inside procedures"));
    CHECK_FALSE(parsed.can_generate_code());

    temp_source_file syntax_failure(
        "program returns is\n"
        "begin\n"
        "    return ();\n"
        "end program.\n");
    captured_stdout syntax_capture;
    parser malformed(syntax_failure.name());
    syntax_capture.restore();
    CHECK_FALSE(has_error(malformed, "Return statements are only valid inside procedures"));

    temp_source_file ownerless_recovery(
        "program returns is\n"
        "procedure rejected : integer(variable value : integer[-1])\n"
        "begin\n"
        "    return 0;\n"
        "end procedure;\n"
        "begin\n"
        "end program.\n");
    captured_stdout ownerless_capture;
    parser ownerless(ownerless_recovery.name());
    ownerless_capture.restore();
    CHECK(ownerless.error_reports.size() == 1);
    CHECK(has_error(ownerless, "Array upper bound must be a non-negative integer"));
    CHECK_FALSE(has_error(ownerless, "Return statements are only valid inside procedures"));
}

TEST_CASE("Stage 2F if phases keep branch delimiters and recover once")
{
    temp_source_file valid_fixture(
        "program branches is\n"
        "variable i : integer;\n"
        "begin\n"
        "    if (true) then\n"
        "    else\n"
        "    end if;\n"
        "    if (i) then\n"
        "        if (false) then\n"
        "        else\n"
        "            i := 1;\n"
        "        end if;\n"
        "    else\n"
        "        i := 2;\n"
        "    end if;\n"
        "end program.\n");
    captured_stdout valid_capture;
    parser valid(valid_fixture.name());
    valid_capture.restore();
    CHECK(valid.error_reports.empty());
    CHECK(valid.frontend_valid());
    CHECK(valid.can_generate_code());
    CHECK(valid.ir_status() == ir::ModuleStatus::Ready);

    temp_source_file recovered_rparam(
        "program branches is\n"
        "variable i : integer;\n"
        "begin\n"
        "    if (true then\n"
        "        i := 1;\n"
        "    end if;\n"
        "end program.\n");
    captured_stdout rparam_capture;
    parser rparam(recovered_rparam.name());
    rparam_capture.restore();
    CHECK(rparam.error_reports.size() == 1);
    CHECK(has_error(rparam, "Missing \")\" expected for if statment"));
    CHECK_FALSE(has_error(rparam, "Missing expected keyword \"then\" for if statements"));
    CHECK(rparam.ir_status() == ir::ModuleStatus::FrontendError);
    CHECK(rparam.ir_module().functions.empty());
    CHECK(rparam.ir_module().storages.empty());

    temp_source_file delimiter_recovery(
        "program branches is\n"
        "variable i : integer;\n"
        "begin\n"
        "    if (true) then\n"
        "        i := 1\n"
        "    else\n"
        "        i := 2\n"
        "    else\n"
        "        i := 3\n"
        "    end if;\n"
        "end program.\n");
    captured_stdout delimiter_capture;
    parser delimiter(delimiter_recovery.name());
    delimiter_capture.restore();
    CHECK(delimiter.error_reports.size() == 4);
    CHECK(count_errors(delimiter, "Missing \";\" to end statement in if statement") == 3);
    CHECK(count_errors(delimiter, "Unexpected repeated \"else\" in if statement") == 1);

    temp_source_file repeated_else(
        "program branches is\n"
        "variable i : integer;\n"
        "begin\n"
        "    if (true) then\n"
        "    else\n"
        "    else\n"
        "    else\n"
        "    else\n"
        "    end if;\n"
        "    i := \"later\";\n"
        "end program.\n");
    captured_stdout repeated_capture;
    parser repeated(repeated_else.name());
    repeated_capture.restore();
    CHECK(repeated.error_reports.size() == 2);
    CHECK(count_errors(repeated, "Unexpected repeated \"else\" in if statement") == 1);
    CHECK(has_error(repeated,
                    "Assignment target type \"integer\" is not compatible with expression type \"string\""));
    CHECK(repeated.ir_status() == ir::ModuleStatus::FrontendError);
    CHECK(repeated.ir_module().functions.empty());
    CHECK(repeated.ir_module().storages.empty());

    temp_source_file semantic_condition(
        "program branches is\n"
        "begin\n"
        "    if (\"not-a-condition\") then\n"
        "    end if;\n"
        "end program.\n");
    captured_stdout semantic_condition_capture;
    parser semantic_invalid(semantic_condition.name());
    semantic_condition_capture.restore();
    CHECK(semantic_invalid.error_reports.size() == 1);
    CHECK(has_error(semantic_invalid,
                    "If statements must resolve to either type Bool or Integer"));
    CHECK(semantic_invalid.ir_status() == ir::ModuleStatus::FrontendError);
    CHECK(semantic_invalid.ir_module().functions.empty());
    CHECK(semantic_invalid.ir_module().storages.empty());

    temp_source_file missing_then(
        "program branches is\n"
        "variable i : integer;\n"
        "begin\n"
        "    if (true) i := 1; end if;\n"
        "    i := \"later\";\n"
        "end program.\n");
    captured_stdout then_capture;
    parser then_missing(missing_then.name());
    then_capture.restore();
    CHECK(then_missing.error_reports.size() == 2);
    CHECK(has_error(then_missing, "Missing expected keyword \"then\" for if statements"));
    CHECK(has_error(then_missing,
                    "Assignment target type \"integer\" is not compatible with expression type \"string\""));
    CHECK_FALSE(has_error(then_missing, "Missing keyworkd \"program\""));

    temp_source_file missing_lparam(
        "program branches is\n"
        "variable i : integer;\n"
        "begin\n"
        "    if true) then i := 1; end if;\n"
        "    i := \"later\";\n"
        "end program.\n");
    captured_stdout lparam_capture;
    parser lparam_missing(missing_lparam.name());
    lparam_capture.restore();
    CHECK(lparam_missing.error_reports.size() == 2);
    CHECK(has_error(lparam_missing, "Missing \"(\" expected for if statment"));
    CHECK(has_error(lparam_missing,
                    "Assignment target type \"integer\" is not compatible with expression type \"string\""));
    CHECK_FALSE(has_error(lparam_missing, "Missing keyworkd \"program\""));

    temp_source_file syntax_child(
        "program branches is\n"
        "variable i : integer;\n"
        "begin\n"
        "    if (true) then\n"
        "        i := ;\n"
        "    else\n"
        "        i := 1;\n"
        "    end if;\n"
        "end program.\n");
    captured_stdout child_capture;
    parser child(syntax_child.name());
    child_capture.restore();
    CHECK_FALSE(has_error(child, "Missing \";\" to end statement in if statement"));
}

TEST_CASE("Stage 5A parser lowers Program if headers and restores nested joins")
{
    temp_source_file fixture(
        "program control is\n"
        "variable value : integer;\n"
        "begin\n"
        "    value := 1;\n"
        "    if (value) then\n"
        "        if (true) then\n"
        "            value := 2;\n"
        "        end if;\n"
        "    else\n"
        "        value := 3;\n"
        "    end if;\n"
        "    value := 4;\n"
        "end program.\n");
    captured_stdout capture;
    parser parsed(fixture.name());
    capture.restore();

    REQUIRE(parsed.error_reports.empty());
    REQUIRE(parsed.can_generate_code());
    REQUIRE(parsed.ir_status() == ir::ModuleStatus::Ready);
    const ir::Function &program = parsed.ir_module().functions[0];
    REQUIRE(program.blocks.size() == 7);
    const ir::BranchTerminator *outer =
        std::get_if<ir::BranchTerminator>(&std::get<ir::Terminator>(program.blocks[0].terminator));
    REQUIRE(outer != NULL);
    CHECK(outer->when_true == ir::BlockId(program.id, 1));
    CHECK(outer->when_false == ir::BlockId(program.id, 2));
    CHECK(std::holds_alternative<ir::BranchTerminator>(
        std::get<ir::Terminator>(program.blocks[1].terminator)));
    CHECK(std::holds_alternative<ir::JumpTerminator>(
        std::get<ir::Terminator>(program.blocks[6].terminator)));
    CHECK(std::holds_alternative<ir::HaltTerminator>(
        std::get<ir::Terminator>(program.blocks[3].terminator)));

    bool saw_integer_condition_cast = false;
    for (const ir::Instruction &instruction : program.blocks[0].instructions)
    {
        const ir::Cast *cast = std::get_if<ir::Cast>(&instruction);
        saw_integer_condition_cast = saw_integer_condition_cast ||
                                     (cast != NULL &&
                                      cast->operation == ir::CastOp::IntToBool);
    }
    CHECK(saw_integer_condition_cast);
    const ir::JumpTerminator *nested_join =
        std::get_if<ir::JumpTerminator>(&std::get<ir::Terminator>(program.blocks[6].terminator));
    REQUIRE(nested_join != NULL);
    CHECK(nested_join->target == ir::BlockId(program.id, 3));
    CHECK(ir::verify_module(parsed.ir_module()).valid);
}

TEST_CASE("Stage 5C lowers procedure conditionals and loops into typed CFGs")
{
    temp_source_file procedure_fixture(
        "program procedure_control is\n"
        "procedure choose : integer()\n"
        "begin\n"
        "    if (true) then\n"
        "        return 1;\n"
        "    end if;\n"
        "    return 0;\n"
        "end procedure;\n"
        "begin\n"
        "end program.\n");
    captured_stdout procedure_capture;
    parser procedure(procedure_fixture.name());
    procedure_capture.restore();
    CHECK(procedure.frontend_valid());
    CHECK(procedure.ir_status() == ir::ModuleStatus::Ready);
    REQUIRE(procedure.ir_module().functions.size() > 1);
    CHECK(procedure.ir_module().functions[10].blocks.size() > 1);

    temp_source_file loop_fixture(
        "program procedure_loop is\n"
        "procedure count : integer()\n"
        "variable i : integer;\n"
        "begin\n"
        "    for (i := 0; true)\n"
        "    end for;\n"
        "    return i;\n"
        "end procedure;\n"
        "begin\n"
        "end program.\n");
    captured_stdout loop_capture;
    parser loop(loop_fixture.name());
    loop_capture.restore();
    CHECK(loop.frontend_valid());
    CHECK(loop.ir_status() == ir::ModuleStatus::Ready);
    REQUIRE(loop.ir_module().functions.size() > 1);
    CHECK(loop.ir_module().functions[10].blocks.size() == 4);
}

TEST_CASE("Stage 5B parser lowers Program loops into condition and backedge blocks")
{
    temp_source_file fixture(
        "program lowered_loop is\n"
        "variable i : integer;\n"
        "variable printed : bool;\n"
        "begin\n"
        "    for (i := 0; i < 3)\n"
        "        printed := putInteger(i);\n"
        "        i := i + 1;\n"
        "    end for;\n"
        "    printed := putInteger(i);\n"
        "end program.\n");
    captured_stdout capture;
    parser parsed(fixture.name());
    capture.restore();

    REQUIRE(parsed.error_reports.empty());
    REQUIRE(parsed.can_generate_code());
    REQUIRE(parsed.ir_status() == ir::ModuleStatus::Ready);
    const ir::Module &module = parsed.ir_module();
    const ir::Function &program = module.functions[0];
    REQUIRE(program.blocks.size() == 4);
    const ir::Storage *counter = NULL;
    for (const ir::Storage &storage : module.storages)
    {
        if (storage.symbol.name == "i")
        {
            counter = &storage;
        }
    }
    REQUIRE(counter != NULL);
    const ir::JumpTerminator *preheader =
        std::get_if<ir::JumpTerminator>(&std::get<ir::Terminator>(program.blocks[0].terminator));
    const ir::BranchTerminator *condition =
        std::get_if<ir::BranchTerminator>(&std::get<ir::Terminator>(program.blocks[1].terminator));
    const ir::JumpTerminator *backedge =
        std::get_if<ir::JumpTerminator>(&std::get<ir::Terminator>(program.blocks[2].terminator));
    REQUIRE(preheader != NULL);
    REQUIRE(condition != NULL);
    REQUIRE(backedge != NULL);
    CHECK(preheader->target == ir::BlockId(program.id, 1));
    CHECK(condition->when_true == ir::BlockId(program.id, 2));
    CHECK(condition->when_false == ir::BlockId(program.id, 3));
    CHECK(backedge->target == ir::BlockId(program.id, 1));
    CHECK(std::holds_alternative<ir::HaltTerminator>(
        std::get<ir::Terminator>(program.blocks[3].terminator)));

    bool init_store = false;
    bool condition_reload = false;
    bool update_store = false;
    bool body_call = false;
    bool exit_call = false;
    for (const ir::Instruction &instruction : program.blocks[0].instructions)
    {
        const ir::Store *store = std::get_if<ir::Store>(&instruction);
        init_store = init_store || (store != NULL && store->destination == counter->id);
    }
    for (const ir::Instruction &instruction : program.blocks[1].instructions)
    {
        const ir::Load *load = std::get_if<ir::Load>(&instruction);
        condition_reload = condition_reload || (load != NULL && load->source == counter->id);
    }
    for (const ir::Instruction &instruction : program.blocks[2].instructions)
    {
        const ir::Store *store = std::get_if<ir::Store>(&instruction);
        update_store = update_store || (store != NULL && store->destination == counter->id);
        body_call = body_call || std::holds_alternative<ir::Call>(instruction);
    }
    for (const ir::Instruction &instruction : program.blocks[3].instructions)
    {
        exit_call = exit_call || std::holds_alternative<ir::Call>(instruction);
    }
    CHECK(init_store);
    CHECK(condition_reload);
    CHECK(update_store);
    CHECK(body_call);
    CHECK(exit_call);
    CHECK(ir::verify_module(module).valid);

    temp_source_file integer_condition(
        "program integer_loop is\n"
        "variable i : integer;\n"
        "variable printed : bool;\n"
        "begin\n"
        "    for (i := -2; i)\n"
        "        printed := putInteger(i);\n"
        "        i := i + 1;\n"
        "    end for;\n"
        "    for (i := 0; 0)\n"
        "    end for;\n"
        "    for (i := 2; i)\n"
        "        i := i - 1;\n"
        "    end for;\n"
        "end program.\n");
    captured_stdout integer_capture;
    parser integer_loop(integer_condition.name());
    integer_capture.restore();
    REQUIRE(integer_loop.can_generate_code());
    const ir::Function &integer_program = integer_loop.ir_module().functions[0];
    std::size_t first_header_int_to_bool_count = 0;
    std::size_t total_int_to_bool_count = 0;
    for (std::size_t block_index = 0; block_index < integer_program.blocks.size(); block_index++)
    {
        for (const ir::Instruction &instruction : integer_program.blocks[block_index].instructions)
        {
            const ir::Cast *cast = std::get_if<ir::Cast>(&instruction);
            if (cast != NULL && cast->operation == ir::CastOp::IntToBool)
            {
                total_int_to_bool_count++;
                if (block_index == 1)
                {
                    first_header_int_to_bool_count++;
                }
            }
        }
    }
    CHECK(first_header_int_to_bool_count == 1);
    CHECK(total_int_to_bool_count == 3);

    temp_source_file nested_fixture(
        "program nested_loops is\n"
        "variable i : integer;\n"
        "variable j : integer;\n"
        "variable printed : bool;\n"
        "begin\n"
        "    if (true) then\n"
        "        for (i := 0; i < 1)\n"
        "            if (true) then\n"
        "                for (j := 0; j < 1)\n"
        "                    printed := putInteger(j);\n"
        "                    j := j + 1;\n"
        "                end for;\n"
        "            end if;\n"
        "            i := i + 1;\n"
        "        end for;\n"
        "    end if;\n"
        "    for (i := 0; false)\n"
        "    end for;\n"
        "end program.\n");
    captured_stdout nested_capture;
    parser nested(nested_fixture.name());
    nested_capture.restore();
    CHECK(nested.error_reports.empty());
    CHECK(nested.ir_status() == ir::ModuleStatus::Ready);
    CHECK(nested.ir_module().functions[0].blocks.size() == 16);
    CHECK(ir::verify_module(nested.ir_module()).valid);

    temp_source_file third_clause(
        "program third_clause is\n"
        "variable i : integer;\n"
        "begin\n"
        "    for (i := 0; i < 1; i := i + 1)\n"
        "    end for;\n"
        "end program.\n");
    captured_stdout third_clause_capture;
    parser third_clause_parsed(third_clause.name());
    third_clause_capture.restore();
    CHECK(has_error(third_clause_parsed, "Missing \")\" for loop declaration"));
    CHECK(third_clause_parsed.ir_status() == ir::ModuleStatus::FrontendError);
    CHECK(third_clause_parsed.ir_module().functions.empty());
    CHECK(third_clause_parsed.ir_module().storages.empty());

    temp_source_file invalid_condition(
        "program invalid_loop_condition is\n"
        "variable i : integer;\n"
        "begin\n"
        "    for (i := 0; \"bad\")\n"
        "    end for;\n"
        "end program.\n");
    captured_stdout invalid_condition_capture;
    parser condition_error(invalid_condition.name());
    invalid_condition_capture.restore();
    CHECK(count_errors(condition_error,
                       "Loop statements must resolve to either type Bool or Integer") == 1);
    CHECK(condition_error.ir_status() == ir::ModuleStatus::FrontendError);
    CHECK(condition_error.ir_module().functions.empty());
    CHECK(condition_error.ir_module().storages.empty());

    temp_source_file missing_right_parenthesis(
        "program missing_loop_close is\n"
        "variable i : integer;\n"
        "variable f : float;\n"
        "begin\n"
        "    for (i := 0; f\n"
        "    end for;\n"
        "end program.\n");
    captured_stdout missing_right_capture;
    parser missing_right(missing_right_parenthesis.name());
    missing_right_capture.restore();
    CHECK(has_error(missing_right, "Missing \")\" for loop declaration"));
    CHECK_FALSE(has_error(missing_right,
                          "Loop statements must resolve to either type Bool or Integer"));
    CHECK(missing_right.ir_status() == ir::ModuleStatus::FrontendError);
    CHECK(missing_right.ir_module().functions.empty());
    CHECK(missing_right.ir_module().storages.empty());
}

TEST_CASE("Stage 5B loop recovery retains focused diagnostics and atomic IR")
{
    const auto expect_frontend_error = [](const std::string &statement,
                                          const std::string &expected_error) {
        temp_source_file fixture(
            "program loop_recovery is\n"
            "variable i : integer;\n"
            "variable f : float;\n"
            "begin\n" + statement + "\nend program.\n");
        captured_stdout capture;
        parser parsed(fixture.name());
        capture.restore();
        CAPTURE(statement);
        CHECK(has_error(parsed, expected_error));
        CHECK(parsed.ir_status() == ir::ModuleStatus::FrontendError);
        CHECK(parsed.ir_module().functions.empty());
        CHECK(parsed.ir_module().storages.empty());
    };

    expect_frontend_error("    for i := 0; true)\n    end for;",
                          "Missing \"(\" required for loop");
    expect_frontend_error("    for (; true)\n    end for;",
                          "Missing expeceted identifier for assignment statement");
    expect_frontend_error("    for (i := 0 true)\n    end for;",
                          "Missing \";\" for loop assignment statement");
    expect_frontend_error("    for (i := 0; true)\n        i := 1\n    end for;",
                          "Missing \";\" to end statement in loop statement");
    expect_frontend_error("    for (i := 0; true)\n    end;",
                          "Missing expected keyword \"for\" for end of statement");

    temp_source_file independent_boundaries(
        "program independent_loop_errors is\n"
        "variable i : integer;\n"
        "variable f : float;\n"
        "begin\n"
        "    for (i := \"bad\"; f)\n"
        "    end for;\n"
        "end program.\n");
    captured_stdout boundaries_capture;
    parser boundaries(independent_boundaries.name());
    boundaries_capture.restore();
    CHECK(count_errors(boundaries,
                       "Assignment target type \"integer\" is not compatible with expression type \"string\"") ==
          1);
    CHECK(count_errors(boundaries,
                       "Loop statements must resolve to either type Bool or Integer") == 1);
    CHECK(boundaries.ir_status() == ir::ModuleStatus::FrontendError);
    CHECK(boundaries.ir_module().functions.empty());
    CHECK(boundaries.ir_module().storages.empty());
}

TEST_CASE("Stage 5B malformed loop headers retain their own end-for boundary")
{
    const auto expect_two_errors_after_loop = [](const std::string &loop_header,
                                                 const std::string &header_error) {
        temp_source_file fixture(
            "program loop_boundary is\n"
            "variable i : integer;\n"
            "begin\n" + loop_header +
            "\n        i := 1;\n"
            "    end for;\n"
            "    i := \"later\";\n"
            "end program.\n");
        captured_stdout capture;
        parser parsed(fixture.name());
        capture.restore();
        CAPTURE(loop_header);
        CHECK(parsed.error_reports.size() == 2);
        CHECK(count_errors(parsed, header_error) == 1);
        CHECK(count_errors(parsed,
                           "Assignment target type \"integer\" is not compatible with expression type \"string\"") ==
              1);
        CHECK_FALSE(has_error(parsed, "Missing \";\" to end statement in loop statement"));
        CHECK_FALSE(has_error(parsed, "Missing keyworkd \"program\""));
        CHECK(parsed.ir_status() == ir::ModuleStatus::FrontendError);
        CHECK(parsed.ir_module().functions.empty());
        CHECK(parsed.ir_module().storages.empty());
    };

    struct malformed_loop_header
    {
        const char *header;
        const char *error;
    };
    const malformed_loop_header malformed_headers[] = {
        {"    for (i = 0; true)", "Missing \":\" needed for assignment statement"},
        {"    for (i := 0 true)", "Missing \";\" for loop assignment statement"},
        {"    for (; true)", "Missing expeceted identifier for assignment statement"},
        {"    for i := 0; true)", "Missing \"(\" required for loop"},
    };
    for (const malformed_loop_header &malformed_header : malformed_headers)
    {
        expect_two_errors_after_loop(malformed_header.header, malformed_header.error);
    }

    temp_source_file nested_fixture(
        "program nested_loop_boundary is\n"
        "variable i : integer;\n"
        "begin\n"
        "    for (i := 0; true)\n"
        "        for (i := 0; true\n"
        "            i := 1;\n"
        "        end for;\n"
        "        i := \"outer\";\n"
        "    end for;\n"
        "    i := \"later\";\n"
        "end program.\n");
    captured_stdout nested_capture;
    parser nested(nested_fixture.name());
    nested_capture.restore();
    CHECK(nested.error_reports.size() == 3);
    CHECK(count_errors(nested, "Missing \")\" for loop declaration") == 1);
    CHECK(count_errors(nested,
                       "Assignment target type \"integer\" is not compatible with expression type \"string\"") ==
          2);
    CHECK_FALSE(has_error(nested, "Missing \";\" to end statement in loop statement"));
    CHECK_FALSE(has_error(nested, "Missing keyworkd \"program\""));
    CHECK(nested.ir_status() == ir::ModuleStatus::FrontendError);
    CHECK(nested.ir_module().functions.empty());
    CHECK(nested.ir_module().storages.empty());

    //Here recovery begins in the outer malformed loop, so it must balance
    //the nested for before consuming the outer loop's own terminator.
    temp_source_file nested_depth_fixture(
        "program nested_loop_depth is\n"
        "variable i : integer;\n"
        "begin\n"
        "    for (i := 0; true\n"
        "        for (i := 0; true)\n"
        "            i := 1;\n"
        "        end for;\n"
        "    end for;\n"
        "    i := \"later\";\n"
        "end program.\n");
    captured_stdout nested_depth_capture;
    parser nested_depth(nested_depth_fixture.name());
    nested_depth_capture.restore();
    CHECK(nested_depth.error_reports.size() == 2);
    CHECK(count_errors(nested_depth, "Missing \")\" for loop declaration") == 1);
    CHECK(count_errors(nested_depth,
                       "Assignment target type \"integer\" is not compatible with expression type \"string\"") ==
          1);
    CHECK_FALSE(has_error(nested_depth, "Missing \";\" to end statement in loop statement"));
    CHECK_FALSE(has_error(nested_depth, "Missing keyworkd \"program\""));
    CHECK(nested_depth.ir_status() == ir::ModuleStatus::FrontendError);
    CHECK(nested_depth.ir_module().functions.empty());
    CHECK(nested_depth.ir_module().storages.empty());

    temp_source_file nested_if_fixture(
        "program nested_if_loop_boundary is\n"
        "variable i : integer;\n"
        "begin\n"
        "    for (i := 0; true)\n"
        "        for (i := 0; true\n"
        "            if (true) then\n"
        "                i := 1;\n"
        "            end if;\n"
        "        end for;\n"
        "    end for;\n"
        "    i := \"later\";\n"
        "end program.\n");
    captured_stdout nested_if_capture;
    parser nested_if(nested_if_fixture.name());
    nested_if_capture.restore();
    CHECK(nested_if.error_reports.size() == 2);
    CHECK(count_errors(nested_if, "Missing \")\" for loop declaration") == 1);
    CHECK(count_errors(nested_if,
                       "Assignment target type \"integer\" is not compatible with expression type \"string\"") ==
          1);
    CHECK_FALSE(has_error(nested_if, "Missing \";\" to end statement in loop statement"));
    CHECK_FALSE(has_error(nested_if, "Missing keyworkd \"program\""));
    CHECK(nested_if.ir_status() == ir::ModuleStatus::FrontendError);
    CHECK(nested_if.ir_module().functions.empty());
    CHECK(nested_if.ir_module().storages.empty());

    //Without an `end for`, an empty-stack `end if` belongs to this enclosing
    //conditional.  Loop recovery must leave it for parse_if_statement.
    temp_source_file enclosing_if_fixture(
        "program enclosing_if_loop_boundary is\n"
        "variable i : integer;\n"
        "begin\n"
        "    if (true) then\n"
        "        for (i := 0; true\n"
        "            i := 1;\n"
        "    end if;\n"
        "    i := \"later\";\n"
        "end program.\n");
    captured_stdout enclosing_if_capture;
    parser enclosing_if(enclosing_if_fixture.name());
    enclosing_if_capture.restore();
    CHECK(enclosing_if.error_reports.size() == 2);
    CHECK(count_errors(enclosing_if, "Missing \")\" for loop declaration") == 1);
    CHECK(count_errors(enclosing_if,
                       "Assignment target type \"integer\" is not compatible with expression type \"string\"") ==
          1);
    CHECK_FALSE(has_error(enclosing_if, "Missing keyword \"end\" to end if statement"));
    CHECK_FALSE(has_error(enclosing_if, "Missing keyworkd \"program\""));
    CHECK(enclosing_if.ir_status() == ir::ModuleStatus::FrontendError);
    CHECK(enclosing_if.ir_module().functions.empty());
    CHECK(enclosing_if.ir_module().storages.empty());
}

TEST_CASE("Stage 4A parser publishes scalar straight-line IR without changing frontend validity")
{
    temp_source_file scalar_fixture(
        "program lowered is\n"
        "variable result : integer;\n"
        "begin\n"
        "    result := 1 + 2 * 3;\n"
        "end program.\n");
    captured_stdout scalar_capture;
    parser scalar(scalar_fixture.name());
    scalar_capture.restore();
    CHECK(scalar.frontend_valid());
    CAPTURE(scalar.ir_reason());
    CHECK(scalar.can_generate_code());
    CHECK(scalar.ir_status() == ir::ModuleStatus::Ready);
    REQUIRE(scalar.ir_module().functions.size() == 10);
    REQUIRE(scalar.ir_module().storages.size() == 1);
    const ir::Function &scalar_program = scalar.ir_module().functions[0];
    CHECK(scalar_program.kind == ir::FunctionKind::Program);
    CHECK(scalar_program.return_type.element_type == TYPE_NONE);
    const ir::Terminator *scalar_terminator =
        std::get_if<ir::Terminator>(&scalar_program.blocks[0].terminator);
    REQUIRE(scalar_terminator != NULL);
    CHECK(std::holds_alternative<ir::HaltTerminator>(*scalar_terminator));
    std::size_t scalar_stores = 0;
    std::size_t scalar_destination_loads = 0;
    for (const ir::Instruction &instruction : scalar_program.blocks[0].instructions)
    {
        if (const ir::Store *store = std::get_if<ir::Store>(&instruction))
        {
            scalar_stores++;
            for (const ir::Instruction &candidate : scalar_program.blocks[0].instructions)
            {
                const ir::Load *load = std::get_if<ir::Load>(&candidate);
                if (load != NULL && load->source == store->destination)
                {
                    scalar_destination_loads++;
                }
            }
        }
    }
    CHECK(scalar_stores == 1);
    CHECK(scalar_destination_loads == 0);
    CHECK(ir::verify_module(scalar.ir_module()).valid);

    temp_source_file load_store_fixture(
        "program loads is\n"
        "variable source : integer;\n"
        "variable destination : integer;\n"
        "begin\n"
        "    source := 1;\n"
        "    destination := source;\n"
        "end program.\n");
    captured_stdout load_store_capture;
    parser load_store(load_store_fixture.name());
    load_store_capture.restore();
    REQUIRE(load_store.can_generate_code());
    const ir::Function &load_store_program = load_store.ir_module().functions[0];
    std::size_t loads = 0;
    std::size_t stores = 0;
    bool loaded_destination = false;
    for (const ir::Instruction &instruction : load_store_program.blocks[0].instructions)
    {
        if (const ir::Load *load = std::get_if<ir::Load>(&instruction))
        {
            loads++;
            loaded_destination = loaded_destination || load->source == ir::StorageId(1);
        }
        stores += std::holds_alternative<ir::Store>(instruction) ? 1U : 0U;
    }
    CHECK(loads == 1);
    CHECK(stores == 2);
    CHECK_FALSE(loaded_destination);

    temp_source_file procedure_fixture(
        "program calls is\n"
        "variable result : integer;\n"
        "procedure addone : integer(variable value : integer)\n"
        "begin\n"
        "    return value + 1;\n"
        "end procedure;\n"
        "begin\n"
        "    result := addone(2);\n"
        "end program.\n");
    captured_stdout procedure_capture;
    parser procedure(procedure_fixture.name());
    procedure_capture.restore();
    CHECK(procedure.frontend_valid());
    CHECK(procedure.can_generate_code());
    CHECK(procedure.ir_status() == ir::ModuleStatus::Ready);
    REQUIRE(procedure.ir_module().functions.size() == 11);
    const ir::Function &procedure_function = procedure.ir_module().functions[10];
    const SymbolRef addone_ref{0, "addone"};
    CHECK(procedure_function.symbol == addone_ref);
    REQUIRE(procedure_function.parameters.size() == 1);
    CHECK(procedure.ir_module().storages[procedure_function.parameters[0].index].kind ==
          ir::StorageKind::Parameter);
    const ir::Terminator *procedure_terminator =
        std::get_if<ir::Terminator>(&procedure_function.blocks[0].terminator);
    REQUIRE(procedure_terminator != NULL);
    CHECK(std::holds_alternative<ir::ReturnTerminator>(*procedure_terminator));
    bool saw_call = false;
    for (const ir::Instruction &instruction : procedure.ir_module().functions[0].blocks[0].instructions)
    {
        saw_call = saw_call || std::holds_alternative<ir::Call>(instruction);
    }
    CHECK(saw_call);
    CHECK(ir::verify_module(procedure.ir_module()).valid);

    temp_source_file recursive_fixture(
        "program recursion is\n"
        "variable result : integer;\n"
        "procedure self : integer(variable value : integer)\n"
        "begin\n"
        "    return self(value);\n"
        "end procedure;\n"
        "begin\n"
        "    result := self(1);\n"
        "end program.\n");
    captured_stdout recursive_capture;
    parser recursive(recursive_fixture.name());
    recursive_capture.restore();
    REQUIRE(recursive.frontend_valid());
    REQUIRE(recursive.can_generate_code());
    REQUIRE(recursive.ir_module().functions.size() == 11);
    const ir::Function &self = recursive.ir_module().functions[10];
    bool saw_recursive_call = false;
    for (const ir::Instruction &instruction : self.blocks[0].instructions)
    {
        const ir::Call *call = std::get_if<ir::Call>(&instruction);
        saw_recursive_call = saw_recursive_call ||
                             (call != NULL && call->callee == self.id);
    }
    CHECK(saw_recursive_call);
    CHECK(ir::verify_module(recursive.ir_module()).valid);

    temp_source_file nested_fixture(
        "program scopes is\n"
        "variable result : integer;\n"
        "procedure outer : integer(variable input : integer)\n"
        "global variable shared : integer;\n"
        "variable local : integer;\n"
        "procedure inner : integer(variable value : integer)\n"
        "begin\n"
        "    return value;\n"
        "end procedure;\n"
        "begin\n"
        "    local := input;\n"
        "    shared := inner(local);\n"
        "    return shared;\n"
        "end procedure;\n"
        "begin\n"
        "    result := outer(result);\n"
        "end program.\n");
    captured_stdout nested_capture;
    parser nested(nested_fixture.name());
    nested_capture.restore();
    REQUIRE(nested.frontend_valid());
    REQUIRE(nested.can_generate_code());
    REQUIRE(nested.ir_module().functions.size() == 12);
    bool saw_global = false;
    bool saw_local = false;
    bool saw_parameter = false;
    for (const ir::Storage &storage : nested.ir_module().storages)
    {
        saw_global = saw_global || (storage.symbol.name == "shared" &&
                                    storage.symbol.scope_id == 0 &&
                                    storage.kind == ir::StorageKind::Global &&
                                    storage.owner == nested.ir_module().functions[0].id);
        saw_local = saw_local || (storage.symbol.name == "local" &&
                                   storage.symbol.scope_id > 0 &&
                                   storage.kind == ir::StorageKind::Local &&
                                   storage.owner != nested.ir_module().functions[0].id);
        saw_parameter = saw_parameter || (storage.symbol.name == "input" &&
                                           storage.symbol.scope_id > 0 &&
                                           storage.kind == ir::StorageKind::Parameter &&
                                           storage.owner != nested.ir_module().functions[0].id);
    }
    CHECK(saw_global);
    CHECK(saw_local);
    CHECK(saw_parameter);
    CHECK(ir::verify_module(nested.ir_module()).valid);

    temp_source_file cast_fixture(
        "program casts is\n"
        "variable source : integer;\n"
        "variable target : float;\n"
        "begin\n"
        "    source := 1;\n"
        "    target := source;\n"
        "end program.\n");
    captured_stdout cast_capture;
    parser cast(cast_fixture.name());
    cast_capture.restore();
    REQUIRE(cast.can_generate_code());
    bool saw_int_to_float = false;
    for (const ir::Instruction &instruction : cast.ir_module().functions[0].blocks[0].instructions)
    {
        const ir::Cast *conversion = std::get_if<ir::Cast>(&instruction);
        saw_int_to_float = saw_int_to_float ||
                           (conversion != NULL && conversion->operation == ir::CastOp::IntToFloat);
    }
    CHECK(saw_int_to_float);
}

TEST_CASE("Stage 4A unsupported frontend features never expose partial IR")
{
    temp_source_file if_fixture(
        "program branches is\n"
        "begin\n"
        "    if (true) then\n"
        "    end if;\n"
        "end program.\n");
    captured_stdout if_capture;
    parser conditional(if_fixture.name());
    if_capture.restore();
    CHECK(conditional.frontend_valid());
    CHECK(conditional.can_generate_code());
    CHECK(conditional.ir_status() == ir::ModuleStatus::Ready);
    CHECK_FALSE(conditional.ir_module().functions.empty());

    temp_source_file array_fixture(
        "program arrays is\n"
        "variable values : integer[0];\n"
        "begin\n"
        "end program.\n");
    captured_stdout array_capture;
    parser array(array_fixture.name());
    array_capture.restore();
    CHECK(array.frontend_valid());
    CHECK(array.can_generate_code());
    CHECK(array.ir_status() == ir::ModuleStatus::Ready);
    REQUIRE(array.ir_module().storages.size() == 1);
    CHECK((array.ir_module().storages[0].type == value_shape{TYPE_INT, true, 0}));

    temp_source_file inline_enum_fixture(
        "program inline_enum is\n"
        "variable value : enum{red, green};\n"
        "begin\n"
        "end program.\n");
    captured_stdout inline_enum_capture;
    parser inline_enum(inline_enum_fixture.name());
    inline_enum_capture.restore();
    CHECK(inline_enum.frontend_valid());
    CHECK_FALSE(inline_enum.can_generate_code());
    CHECK(inline_enum.ir_status() == ir::ModuleStatus::Unsupported);
    CHECK(inline_enum.ir_module().functions.empty());
    CHECK(inline_enum.ir_module().storages.empty());

    temp_source_file mixed_call_fixture(
        "program mixed is\n"
        "variable result : float;\n"
        "procedure identity : float(variable value : float)\n"
        "begin\n"
        "    return value;\n"
        "end procedure;\n"
        "begin\n"
        "    result := identity(1 + 2.0);\n"
        "end program.\n");
    captured_stdout mixed_call_capture;
    parser mixed_call(mixed_call_fixture.name());
    mixed_call_capture.restore();
    CHECK(mixed_call.frontend_valid());
    CHECK(mixed_call.ir_status() == ir::ModuleStatus::Ready);
    CHECK_FALSE(mixed_call.ir_module().functions.empty());

    temp_source_file mixed_binary_fixture(
        "program mixed_binary is\n"
        "variable result : float;\n"
        "begin\n"
        "    result := 1 + 2.0;\n"
        "end program.\n");
    captured_stdout mixed_binary_capture;
    parser mixed_binary(mixed_binary_fixture.name());
    mixed_binary_capture.restore();
    CHECK(mixed_binary.frontend_valid());
    CHECK(mixed_binary.ir_status() == ir::ModuleStatus::Ready);
    CHECK_FALSE(mixed_binary.ir_module().functions.empty());

    temp_source_file if_call_fixture(
        "program conditional_call is\n"
        "variable result : integer;\n"
        "procedure identity : integer(variable value : integer)\n"
        "begin\n"
        "    return value;\n"
        "end procedure;\n"
        "begin\n"
        "    if (true) then\n"
        "        result := identity(result);\n"
        "    end if;\n"
        "end program.\n");
    captured_stdout if_call_capture;
    parser if_call(if_call_fixture.name());
    if_call_capture.restore();
    CHECK(if_call.frontend_valid());
    CHECK(if_call.ir_status() == ir::ModuleStatus::Ready);
    CHECK_FALSE(if_call.ir_module().functions.empty());
}

TEST_CASE("Stage 6B scalar binary lowering inserts only operand promotions")
{
    temp_source_file fixture(
        "program float_promotions is\n"
        "variable result : float;\n"
        "variable truth : bool;\n"
        "begin\n"
        "    result := 1 + 2.0 * 3;\n"
        "    result := 1.0 + 2 + 3;\n"
        "    result := (1 + 2.0) * (3.0 - 4);\n"
        "    truth := true < 2;\n"
        "    truth := 2 >= false;\n"
        "    truth := 1.0 == 1;\n"
        "end program.\n");
    captured_stdout capture;
    parser parsed(fixture.name());
    capture.restore();
    REQUIRE(parsed.frontend_valid());
    REQUIRE(parsed.ir_status() == ir::ModuleStatus::Ready);

    std::size_t int_to_float = 0;
    std::size_t bool_to_int = 0;
    for (const ir::BasicBlock &block : parsed.ir_module().functions[0].blocks)
    {
        for (const ir::Instruction &instruction : block.instructions)
        {
            const ir::Cast *cast = std::get_if<ir::Cast>(&instruction);
            if (cast != NULL && cast->operation == ir::CastOp::IntToFloat)
            {
                int_to_float++;
            }
            if (cast != NULL && cast->operation == ir::CastOp::BoolToInt)
            {
                bool_to_int++;
            }
        }
    }
    CHECK(int_to_float == 7);
    CHECK(bool_to_int == 2);
}

TEST_CASE("Stage 6B procedure returns lower both scalar Float conversions")
{
    temp_source_file fixture(
        "program float_returns is\n"
        "procedure promote : float()\n"
        "begin\n"
        "    return 1;\n"
        "end procedure;\n"
        "procedure truncate : integer()\n"
        "begin\n"
        "    return 1.5;\n"
        "end procedure;\n"
        "begin\n"
        "end program.\n");
    captured_stdout capture;
    parser parsed(fixture.name());
    capture.restore();
    REQUIRE(parsed.frontend_valid());
    REQUIRE(parsed.ir_status() == ir::ModuleStatus::Ready);

    bool saw_int_to_float_return = false;
    bool saw_float_to_int_return = false;
    for (const ir::Function &function : parsed.ir_module().functions)
    {
        if (function.kind != ir::FunctionKind::Procedure)
        {
            continue;
        }
        for (const ir::BasicBlock &block : function.blocks)
        {
            const ir::ReturnTerminator *returned = std::get_if<ir::ReturnTerminator>(
                &std::get<ir::Terminator>(block.terminator));
            for (const ir::Instruction &instruction : block.instructions)
            {
                const ir::Cast *cast = std::get_if<ir::Cast>(&instruction);
                if (cast == NULL || returned == NULL || returned->value != cast->result)
                {
                    continue;
                }
                saw_int_to_float_return = saw_int_to_float_return ||
                                           cast->operation == ir::CastOp::IntToFloat;
                saw_float_to_int_return = saw_float_to_int_return ||
                                           cast->operation == ir::CastOp::FloatToInt;
            }
        }
    }
    CHECK(saw_int_to_float_return);
    CHECK(saw_float_to_int_return);
}

TEST_CASE("Stage 6C parser lowers String literals as semantic bytes")
{
    temp_source_file fixture(
        "program string_payloads is\n"
        "variable value : string;\n"
        "begin\n"
        "    value := \"MiXeD\";\n"
        "    value := \"\";\n"
        "    value := \"two\nlines\";\n"
        "end program.\n");
    captured_stdout capture;
    parser parsed(fixture.name());
    capture.restore();
    REQUIRE(parsed.frontend_valid());
    REQUIRE(parsed.ir_status() == ir::ModuleStatus::Ready);

    std::vector<std::string> payloads;
    for (const ir::Instruction &instruction : parsed.ir_module().functions[0].blocks[0].instructions)
    {
        const ir::Constant *constant = std::get_if<ir::Constant>(&instruction);
        if (constant != NULL && std::holds_alternative<std::string>(constant->payload))
        {
            payloads.push_back(std::get<std::string>(constant->payload));
        }
    }
    REQUIRE(payloads.size() == 3);
    CHECK(payloads[0] == "MiXeD");
    CHECK(payloads[1].empty());
    CHECK(payloads[2] == "two\nlines");
}

TEST_CASE("Stage 6D1 parser lowers aggregate assignment casts")
{
    temp_source_file fixture(
        "program array_conversions is\n"
        "variable integers : integer[1];\n"
        "variable floats : float[1];\n"
        "variable bools : bool[1];\n"
        "begin\n"
        "    floats := integers;\n"
        "    integers := floats;\n"
        "    bools := integers;\n"
        "    integers := bools;\n"
        "end program.\n");
    captured_stdout capture;
    parser parsed(fixture.name());
    capture.restore();
    REQUIRE(parsed.frontend_valid());
    REQUIRE(parsed.can_generate_code());
    REQUIRE(parsed.ir_status() == ir::ModuleStatus::Ready);

    const std::vector<ir::CastOp> expected{
        ir::CastOp::IntToFloat, ir::CastOp::FloatToInt,
        ir::CastOp::IntToBool, ir::CastOp::BoolToInt};
    std::vector<ir::CastOp> actual;
    for (const ir::Instruction &instruction :
         parsed.ir_module().functions[0].blocks[0].instructions)
    {
        if (const ir::Cast *cast = std::get_if<ir::Cast>(&instruction))
        {
            actual.push_back(cast->operation);
            const ir::Value &result =
                parsed.ir_module().functions[0].values[cast->result.index];
            CHECK(result.type.is_array);
            CHECK(result.type.array_upper_bound == 1);
        }
    }
    CHECK(actual == expected);

    temp_source_file lifted_fixture(
        "program lifted_array is\n"
        "variable values : integer[1];\n"
        "begin\n"
        "    values := values + 1;\n"
        "end program.\n");
    captured_stdout lifted_capture;
    parser lifted(lifted_fixture.name());
    lifted_capture.restore();
    CHECK(lifted.frontend_valid());
    CHECK(lifted.can_generate_code());
    CHECK(lifted.ir_status() == ir::ModuleStatus::Ready);
    CHECK(ir::verify_module(lifted.ir_module()).valid);
}

TEST_CASE("Stage 6D2 parser preserves lifted folds, broadcasts, and promotions")
{
    temp_source_file fixture(
        "program lifted_matrix is\n"
        "variable ints : integer[1];\n"
        "variable more : integer[1];\n"
        "variable floats : float[1];\n"
        "variable bools : bool[1];\n"
        "variable strings : string[1];\n"
        "variable scalar_int : integer;\n"
        "variable scalar_bool : bool;\n"
        "begin\n"
        "    ints := -ints;\n"
        "    ints := not ints;\n"
        "    bools := not bools;\n"
        "    ints := ints + more;\n"
        "    ints := ints - scalar_int;\n"
        "    ints := scalar_int + ints;\n"
        "    floats := ints * floats;\n"
        "    floats := floats / scalar_int;\n"
        "    floats := ints + 1.0;\n"
        "    floats := 1.0 + ints;\n"
        "    bools := ints < floats;\n"
        "    bools := bools == scalar_int;\n"
        "    bools := scalar_int != bools;\n"
        "    bools := ints == scalar_bool;\n"
        "    bools := strings == \"x\";\n"
        "    bools := \"x\" != strings;\n"
        "    bools := bools & scalar_bool;\n"
        "    bools := scalar_bool | bools;\n"
        "    ints := ints + scalar_int + more;\n"
        "end program.\n");
    captured_stdout capture;
    parser parsed(fixture.name());
    capture.restore();
    REQUIRE(parsed.frontend_valid());
    REQUIRE(parsed.can_generate_code());
    REQUIRE(parsed.ir_status() == ir::ModuleStatus::Ready);
    REQUIRE(ir::verify_module(parsed.ir_module()).valid);

    std::size_t aggregate_unaries = 0;
    std::size_t aggregate_binaries = 0;
    std::size_t int_to_float = 0;
    std::size_t bool_to_int = 0;
    bool saw_scalar_left = false;
    bool saw_scalar_right = false;
    bool saw_string_left = false;
    bool saw_string_right = false;
    const ir::Function &program = parsed.ir_module().functions[0];
    for (const ir::Instruction &instruction : program.blocks[0].instructions)
    {
        if (const ir::Unary *unary = std::get_if<ir::Unary>(&instruction))
        {
            CHECK(program.values[unary->result.index].type.is_array);
            aggregate_unaries++;
        }
        else if (const ir::Binary *binary = std::get_if<ir::Binary>(&instruction))
        {
            const value_shape left = program.values[binary->left.index].type;
            const value_shape right = program.values[binary->right.index].type;
            const value_shape result = program.values[binary->result.index].type;
            CHECK(result.is_array);
            CHECK(result.array_upper_bound == 1);
            aggregate_binaries++;
            saw_scalar_left = saw_scalar_left || (!left.is_array && right.is_array);
            saw_scalar_right = saw_scalar_right || (left.is_array && !right.is_array);
            saw_string_left = saw_string_left ||
                              (left.element_type == TYPE_STRING && left.is_array);
            saw_string_right = saw_string_right ||
                               (right.element_type == TYPE_STRING && right.is_array);
        }
        else if (const ir::Cast *cast = std::get_if<ir::Cast>(&instruction))
        {
            int_to_float += cast->operation == ir::CastOp::IntToFloat ? 1U : 0U;
            bool_to_int += cast->operation == ir::CastOp::BoolToInt ? 1U : 0U;
        }
        else if (const ir::Store *store = std::get_if<ir::Store>(&instruction))
        {
            CHECK(program.values[store->value.index].type.is_array);
            const bool expression_result =
                program.values[store->value.index].location == ir::ValueLocation::Unary ||
                program.values[store->value.index].location == ir::ValueLocation::Binary;
            CHECK(expression_result);
        }
    }
    CHECK(aggregate_unaries == 3);
    CHECK(aggregate_binaries == 17);
    CHECK(int_to_float == 5);
    CHECK(bool_to_int == 3);
    CHECK(saw_scalar_left);
    CHECK(saw_scalar_right);
    CHECK(saw_string_left);
    CHECK(saw_string_right);

    temp_source_file mismatch_fixture(
        "program mismatch is\n"
        "variable left : integer[1];\n"
        "variable right : integer[2];\n"
        "begin\n"
        "    left := left + right;\n"
        "end program.\n");
    captured_stdout mismatch_capture;
    parser mismatch(mismatch_fixture.name());
    mismatch_capture.restore();
    CHECK_FALSE(mismatch.frontend_valid());
    CHECK(mismatch.ir_status() == ir::ModuleStatus::FrontendError);
    CHECK(mismatch.ir_module().functions.empty());
    CHECK(mismatch.error_reports.size() == 1);
    CHECK(has_error(mismatch, "Array operands must have the same upper bound"));

    temp_source_file string_order_fixture(
        "program string_order is\n"
        "variable left : string[1];\n"
        "variable result : bool[1];\n"
        "begin\n"
        "    result := left < \"x\";\n"
        "end program.\n");
    captured_stdout string_order_capture;
    parser string_order(string_order_fixture.name());
    string_order_capture.restore();
    CHECK_FALSE(string_order.frontend_valid());
    CHECK(string_order.ir_status() == ir::ModuleStatus::FrontendError);
    CHECK(string_order.ir_module().functions.empty());
    CHECK(string_order.error_reports.size() == 1);
    CHECK(has_error(string_order,
                    "Ordering relations require compatible integers, floats, or bools"));
}

TEST_CASE("Stage 2F code-generation readiness follows recorded diagnostics")
{
    temp_source_file valid_source(
        "program ready is\n"
        "begin\n"
        "end program.\n");
    captured_stdout valid_capture;
    parser valid(valid_source.name());
    valid_capture.restore();
    CHECK(valid.can_generate_code());
    valid.errors_occured = true;
    CHECK(valid.can_generate_code());

    temp_source_file syntax_source(
        "program ready is\n"
        "begin\n"
        "end program\n");
    captured_stdout syntax_capture;
    parser syntax(syntax_source.name());
    syntax_capture.restore();
    CHECK_FALSE(syntax.can_generate_code());
    CHECK(syntax.ir_status() == ir::ModuleStatus::FrontendError);
    CHECK(syntax.ir_module().functions.empty());
    CHECK(syntax.ir_module().storages.empty());

    temp_source_file scanner_source(
        "program ready is\n"
        "begin\n"
        "    \"unterminated\n"
        "end program.\n");
    captured_stdout scanner_capture;
    parser scanner(scanner_source.name());
    scanner_capture.restore();
    CHECK_FALSE(scanner.can_generate_code());
    CHECK(scanner.ir_status() == ir::ModuleStatus::FrontendError);
    CHECK(scanner.ir_module().functions.empty());
    CHECK(scanner.ir_module().storages.empty());

    temp_source_file semantic_source(
        "program ready is\n"
        "variable i : integer;\n"
        "begin\n"
        "    i := \"bad\";\n"
        "end program.\n");
    captured_stdout semantic_capture;
    parser semantic(semantic_source.name());
    semantic_capture.restore();
    CHECK_FALSE(semantic.can_generate_code());
    CHECK(semantic.ir_status() == ir::ModuleStatus::FrontendError);
    CHECK(semantic.ir_module().functions.empty());
    CHECK(semantic.ir_module().storages.empty());
}

TEST_CASE("Stage 5C parses unreachable procedure statements without stale IR emission")
{
    temp_source_file fixture(
        "program unreachable_procedure is\n"
        "procedure choose : integer()\n"
        "begin\n"
        "    return 1;\n"
        "    missing := 2;\n"
        "end procedure;\n"
        "begin\n"
        "end program.\n");
    captured_stdout capture;
    parser parsed(fixture.name());
    capture.restore();
    CHECK(has_error(parsed, "Undeclared identifier \"missing\""));
    CHECK(parsed.ir_status() == ir::ModuleStatus::FrontendError);
    CHECK(parsed.ir_module().functions.empty());
    CHECK(parsed.ir_module().storages.empty());

    temp_source_file valid_dead_fixture(
        "program dead_mixed is\n"
        "variable target : integer;\n"
        "procedure done : integer()\n"
        "begin\n"
        "    return 1;\n"
        "    target := 1 + 2.0;\n"
        "end procedure;\n"
        "begin\n"
        "    target := done();\n"
        "end program.\n");
    captured_stdout valid_dead_capture;
    parser valid_dead(valid_dead_fixture.name());
    valid_dead_capture.restore();
    CHECK(valid_dead.frontend_valid());
    CHECK(valid_dead.can_generate_code());
    CHECK(valid_dead.ir_status() == ir::ModuleStatus::Ready);
    CHECK(ir::verify_module(valid_dead.ir_module()).valid);
}

TEST_CASE("Stage 6E lowers empty and statement fallthrough to exact typed defaults")
{
    temp_source_file fixture(
        "program defaults is\n"
        "variable i : integer;\n"
        "variable f : float;\n"
        "variable b : bool;\n"
        "variable s : string;\n"
        "procedure integerDefault : integer()\n"
        "begin\n"
        "end procedure;\n"
        "procedure floatDefault : float()\n"
        "variable local : float;\n"
        "begin\n"
        "    local := 1.0;\n"
        "end procedure;\n"
        "procedure boolDefault : bool()\n"
        "begin\n"
        "end procedure;\n"
        "procedure stringDefault : string()\n"
        "begin\n"
        "end procedure;\n"
        "begin\n"
        "    i := integerDefault();\n"
        "    f := floatDefault();\n"
        "    b := boolDefault();\n"
        "    s := stringDefault();\n"
        "end program.\n");
    captured_stdout capture;
    parser parsed(fixture.name());
    capture.restore();
    REQUIRE(parsed.frontend_valid());
    REQUIRE(parsed.can_generate_code());
    REQUIRE(parsed.ir_module().functions.size() == 14);

    const std::vector<std::variant<int, float, bool, std::string>> defaults = {
        0, 0.0F, false, std::string()
    };
    for (std::size_t index = 0; index < defaults.size(); index++)
    {
        const ir::Function &procedure = parsed.ir_module().functions[10 + index];
        const ir::BasicBlock &exit = procedure.blocks.back();
        REQUIRE(!exit.instructions.empty());
        const ir::Constant *constant = std::get_if<ir::Constant>(&exit.instructions.back());
        REQUIRE(constant != NULL);
        CHECK(constant->payload == defaults[index]);
        if (index == 1)
        {
            REQUIRE(std::holds_alternative<float>(constant->payload));
            CHECK_FALSE(std::signbit(std::get<float>(constant->payload)));
        }
        const ir::Terminator *terminator = std::get_if<ir::Terminator>(&exit.terminator);
        REQUIRE(terminator != NULL);
        const ir::ReturnTerminator *returned = std::get_if<ir::ReturnTerminator>(terminator);
        REQUIRE(returned != NULL);
        CHECK(returned->value == constant->result);
    }
    CHECK(ir::verify_module(parsed.ir_module()).valid);
}

TEST_CASE("Stage 6E defaults only the reachable procedure exit")
{
    temp_source_file fixture(
        "program paths is\n"
        "variable result : integer;\n"
        "procedure explicit : integer()\n"
        "begin\n"
        "    return 7;\n"
        "end procedure;\n"
        "procedure allarms : integer(variable flag : bool)\n"
        "begin\n"
        "    if (flag) then\n"
        "        return 1;\n"
        "    else\n"
        "        return 2;\n"
        "    end if;\n"
        "end procedure;\n"
        "procedure partial : integer(variable flag : bool)\n"
        "begin\n"
        "    if (flag) then\n"
        "        return 3;\n"
        "    end if;\n"
        "end procedure;\n"
        "begin\n"
        "    result := explicit();\n"
        "    result := allarms(true);\n"
        "    result := partial(false);\n"
        "end program.\n");
    captured_stdout capture;
    parser parsed(fixture.name());
    capture.restore();
    REQUIRE(parsed.frontend_valid());
    REQUIRE(parsed.can_generate_code());

    const ir::Function &explicit_procedure = parsed.ir_module().functions[10];
    CHECK(explicit_procedure.values.size() == 1);
    CHECK(explicit_procedure.blocks.size() == 1);
    const ir::Function &allarms = parsed.ir_module().functions[11];
    CHECK(allarms.blocks.size() == 3);
    CHECK(allarms.values.size() == 3); // condition load plus two explicit constants
    const ir::Function &partial = parsed.ir_module().functions[12];
    REQUIRE(partial.blocks.size() == 4);
    const ir::BasicBlock &partial_exit = partial.blocks.back();
    REQUIRE(partial_exit.instructions.size() == 1);
    const ir::Constant *implicit_zero =
        std::get_if<ir::Constant>(&partial_exit.instructions[0]);
    REQUIRE(implicit_zero != NULL);
    CHECK(implicit_zero->payload == std::variant<int, float, bool, std::string>(0));
    CHECK(ir::verify_module(parsed.ir_module()).valid);

    temp_source_file malformed_fixture(
        "program malformed is\n"
        "procedure broken : integer()\n"
        "begin\n"
        "begin\n"
        "end program.\n");
    captured_stdout malformed_capture;
    parser malformed(malformed_fixture.name());
    malformed_capture.restore();
    CHECK_FALSE(malformed.frontend_valid());
    CHECK(malformed.ir_status() == ir::ModuleStatus::FrontendError);
    CHECK(malformed.ir_module().functions.empty());
}

TEST_CASE("the final program period requires scanner-confirmed end of input")
{
    struct trailing_case
    {
        const char *suffix;
        const char *diagnostic;
        const char *additional_diagnostic;
    };
    const std::vector<trailing_case> invalid_cases = {
        {" trailing_identifier\n", "Unexpected token after final \".\"", ""},
        {".\n", "Unexpected token after final \".\"", ""},
        {";\n", "Unexpected token after final \".\"", ""},
        {"@\n", "Illegal character: '@'", ""},
        {"\"unterminated", "Unexpected token after final \".\"", "quotation left open"},
        {"/* unterminated", "Unclosed block comment detected", ""},
    };

    for (const trailing_case &test_case : invalid_cases)
    {
        temp_source_file fixture(
            std::string("program trailing is\nbegin\nend program.") + test_case.suffix);
        captured_stdout capture;
        parser parsed(fixture.name());
        capture.restore();

        CHECK_FALSE(parsed.frontend_valid());
        CHECK_FALSE(parsed.can_generate_code());
        CHECK(parsed.ir_status() == ir::ModuleStatus::FrontendError);
        CHECK(parsed.ir_module().functions.empty());
        CHECK(parsed.ir_module().storages.empty());
        CHECK(has_error(parsed, test_case.diagnostic));
        if (test_case.additional_diagnostic[0] != '\0')
        {
            CHECK(has_error(parsed, test_case.additional_diagnostic));
        }
    }

    temp_source_file valid_fixture(
        "program trailing is\n"
        "begin\n"
        "end program.  \t\n"
        "// trailing line comment with punctuation @ \" /*\n"
        "/* trailing block comment /* nested */ closed */\n");
    captured_stdout valid_capture;
    parser valid(valid_fixture.name());
    valid_capture.restore();

    CHECK(valid.frontend_valid());
    CHECK(valid.can_generate_code());
    CHECK(valid.ir_status() == ir::ModuleStatus::Ready);
    CHECK_FALSE(valid.ir_module().functions.empty());
}
