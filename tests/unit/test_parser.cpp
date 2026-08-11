//Focused invariants for parser recovery.  Process-level timeout coverage lives
//in tests/test_cli.py; these tests inspect state that is intentionally public in
//the original parser design.
#include "../vendor/doctest.h"
#include "../../parser.h"

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
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
    CHECK(has_error(parsed, "Missing expected keyword \"then\" for if statements"));
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

TEST_CASE("recovery-dispatched procedure parsing does not own an enclosing scope")
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

    parsed.parse_procedure_declaration(false, false);

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
        "    result := three(1, 2, 3);\n"
        "end program.\n"};
    const std::vector<std::string> procedure_names = {"one", "two", "three"};
    const std::vector<std::vector<data_types>> expected_parameters = {
        {TYPE_INT},
        {TYPE_INT, TYPE_FLOAT},
        {TYPE_INT, TYPE_FLOAT, TYPE_BOOL}};

    for (std::size_t i = 0; i < programs.size(); i++)
    {
        temp_source_file fixture(programs[i]);
        captured_stdout capture;
        parser parsed(fixture.name());
        capture.restore();

        CHECK(parsed.error_count() == 0);
        CHECK(parsed.current_scope_id == 0);
        CHECK(parsed.number_of_scopes == 0);
        CHECK(parsed.Lexer->symbol_table.scope_table[0].is_in_table(procedure_names[i]));
        CHECK(parsed.Lexer->symbol_table.scope_table[0]
                  .scope_map[procedure_names[i]]
                  .procedure_params == expected_parameters[i]);
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
