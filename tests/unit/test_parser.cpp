//Focused invariants for parser recovery.  Process-level timeout coverage lives
//in tests/test_cli.py; these tests inspect state that is intentionally public in
//the original parser design.
#include "../vendor/doctest.h"
#include "../../parser.h"

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
