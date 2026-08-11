//Unit tests for the scanner.  The scanner is the one stage of this compiler
//that is stable enough to pin down with exact expectations, so what is asserted
//here is meant to be its contract: it should survive the typechecker rebuild.
//
//Scanner regressions cite their audit id where one exists.  The expectations
//describe the repaired behavior rather than preserving known failures.
#include "../vendor/doctest.h"
#include "../../scanner.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{

//the scanner can only read a file, so every fixture is written to a private
//temporary file that is deleted again when this object goes out of scope
class temp_source_file
{
public:
    explicit temp_source_file(const std::string &contents)
    {
        const char *temp_dir = std::getenv("TMPDIR");
        std::string name_template = std::string(temp_dir ? temp_dir : "/tmp") + "/compiler_unit_XXXXXX";
        //mkstemp fills the name in place, so it needs a writable buffer
        file_name.assign(name_template.begin(), name_template.end());
        file_name.push_back('\0');
        int file_descriptor = mkstemp(&file_name[0]);
        REQUIRE(file_descriptor != -1);
        //written through the descriptor mkstemp handed us so the fixture is
        //never briefly readable under a name somebody else could have taken
        std::FILE *fixture = fdopen(file_descriptor, "w");
        REQUIRE(fixture != NULL);
        if (!contents.empty())
        {
            REQUIRE(std::fwrite(contents.data(), 1, contents.size(), fixture) == contents.size());
        }
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

//scans a whole fixture and hands back every token, T_INVALID sentinel included
std::vector<token> scan_all(const std::string &contents)
{
    temp_source_file fixture(contents);
    scanner lexer(fixture.name());
    std::vector<token> tokens;
    //The bound only guards a fixture that never reaches EOF.
    while (tokens.size() < 128)
    {
        token scanned = lexer.Get_token();
        tokens.push_back(scanned);
        if (scanned.type == T_INVALID)
        {
            break;
        }
    }
    return tokens;
}

struct scan_result
{
    std::vector<token> tokens;
    std::vector<scanner_diagnostic> diagnostics;
};

scan_result scan_all_with_diagnostics(scanner &lexer)
{
    scan_result result;
    while (result.tokens.size() < 128)
    {
        token scanned = lexer.Get_token();
        result.tokens.push_back(scanned);
        std::vector<scanner_diagnostic> latest = lexer.take_diagnostics();
        result.diagnostics.insert(result.diagnostics.end(), latest.begin(), latest.end());
        if (scanned.type == T_INVALID)
        {
            break;
        }
    }
    return result;
}

scan_result scan_all_with_diagnostics(const std::string &contents)
{
    temp_source_file fixture(contents);
    scanner lexer(fixture.name());
    return scan_all_with_diagnostics(lexer);
}

} // namespace

TEST_CASE("scanner lowercases identifiers and matches reserved words in any case")
{
    std::vector<token> tokens = scan_all("Foo BAR_baz IF\n");

    REQUIRE(tokens.size() == 4);
    CHECK(tokens[0].type == T_IDENTIFIER);
    CHECK(tokens[0].stringValue == "foo");
    CHECK(tokens[1].type == T_IDENTIFIER);
    CHECK(tokens[1].stringValue == "bar_baz");
    //the language is case insensitive, so "IF" is the reserved word
    CHECK(tokens[2].type == T_IF);
    CHECK(tokens[2].stringValue == "if");
    CHECK(tokens[3].type == T_INVALID);
}

TEST_CASE("scanner reads integer and float literals with their values")
{
    std::vector<token> tokens = scan_all("x := 42;\ny := 3.5;\n");

    REQUIRE(tokens.size() == 11);
    //":=" is two tokens; the parser is what pairs them up
    CHECK(tokens[1].type == T_COLON);
    CHECK(tokens[2].type == T_ASSIGN);
    CHECK(tokens[3].type == T_INTEGER_VALUE);
    CHECK(tokens[3].intValue == 42);
    CHECK(tokens[3].line_found == 1);
    CHECK(tokens[8].type == T_FLOAT_VALUE);
    CHECK(tokens[8].floatValue == doctest::Approx(3.5));
    CHECK(tokens[8].line_found == 2);
}

TEST_CASE("scanner accepts the grammar's underscore number separators")
{
    scan_result result = scan_all_with_diagnostics("1_000 3.1_4 7_.__\n");

    REQUIRE(result.tokens.size() == 4);
    CHECK(result.tokens[0].type == T_INTEGER_VALUE);
    CHECK(result.tokens[0].intValue == 1000);
    CHECK(result.tokens[1].type == T_FLOAT_VALUE);
    CHECK(result.tokens[1].floatValue == doctest::Approx(3.14));
    //The recovered grammar permits underscores in both portions, including
    //immediately before or after the decimal point.
    CHECK(result.tokens[2].type == T_FLOAT_VALUE);
    CHECK(result.tokens[2].floatValue == doctest::Approx(7.0));
    CHECK(result.tokens[3].type == T_INVALID);
    CHECK(result.diagnostics.empty());
}

TEST_CASE("scanner rejects a malformed underscored numeric run as one token")
{
    scan_result result = scan_all_with_diagnostics("1_2.3_4.5; after\n");

    REQUIRE(result.tokens.size() == 4);
    CHECK(result.tokens[0].type == T_FLOAT_VALUE);
    CHECK(result.tokens[0].floatValue == doctest::Approx(0.0));
    CHECK(result.tokens[1].type == T_SEMICOLON);
    CHECK(result.tokens[2].stringValue == "after");
    CHECK(result.tokens[3].type == T_INVALID);
    REQUIRE(result.diagnostics.size() == 1);
    CHECK(result.diagnostics[0].line_found == 1);
    CHECK(result.diagnostics[0].message ==
          "Malformed numeric literal: multiple decimal points");
}

TEST_CASE("scanner reports integer and float overflow and reaches EOF")
{
    SUBCASE("integer overflow")
    {
        scan_result result = scan_all_with_diagnostics(
            "99999999999999999999999999999999999999999;");

        REQUIRE(result.tokens.size() == 3);
        CHECK(result.tokens[0].type == T_INTEGER_VALUE);
        CHECK(result.tokens[0].intValue == 0);
        CHECK(result.tokens[1].type == T_SEMICOLON);
        CHECK(result.tokens[2].type == T_INVALID);
        REQUIRE(result.diagnostics.size() == 1);
        CHECK(result.diagnostics[0].line_found == 1);
        CHECK(result.diagnostics[0].message == "Numeric literal is out of range");
    }

    SUBCASE("float overflow")
    {
        scan_result result = scan_all_with_diagnostics(
            "99999999999999999999999999999999999999999.0;");

        REQUIRE(result.tokens.size() == 3);
        CHECK(result.tokens[0].type == T_FLOAT_VALUE);
        CHECK(result.tokens[0].floatValue == doctest::Approx(0.0));
        CHECK(result.tokens[1].type == T_SEMICOLON);
        CHECK(result.tokens[2].type == T_INVALID);
        REQUIRE(result.diagnostics.size() == 1);
        CHECK(result.diagnostics[0].line_found == 1);
        CHECK(result.diagnostics[0].message == "Numeric literal is out of range");
    }
}

TEST_CASE("scanner consumes and reports a multi-decimal numeric literal")
{
    scan_result result = scan_all_with_diagnostics("1.2.3; 4\n");

    REQUIRE(result.tokens.size() == 4);
    CHECK(result.tokens[0].type == T_FLOAT_VALUE);
    CHECK(result.tokens[0].floatValue == doctest::Approx(0.0));
    CHECK(result.tokens[1].type == T_SEMICOLON);
    CHECK(result.tokens[2].type == T_INTEGER_VALUE);
    CHECK(result.tokens[2].intValue == 4);
    CHECK(result.tokens[3].type == T_INVALID);
    REQUIRE(result.diagnostics.size() == 1);
    CHECK(result.diagnostics[0].line_found == 1);
    CHECK(result.diagnostics[0].message ==
          "Malformed numeric literal: multiple decimal points");
}

TEST_CASE("scanner keeps the delimiters and the opening line of a string literal")
{
    SUBCASE("single line")
    {
        std::vector<token> tokens = scan_all("putString(\"hi there\");\n");

        REQUIRE(tokens.size() == 6);
        CHECK(tokens[2].type == T_STRING_VALUE);
        //the quotation marks are part of stringValue
        CHECK(tokens[2].stringValue == "\"hi there\"");
        CHECK(tokens[2].line_found == 1);
    }

    SUBCASE("spanning two lines")
    {
        std::vector<token> tokens = scan_all("\"multi\nline\"\nafter\n");

        REQUIRE(tokens.size() == 3);
        CHECK(tokens[0].type == T_STRING_VALUE);
        CHECK(tokens[0].stringValue == "\"multi\nline\"");
        //the reported line is the opening quote's, not the closing quote's
        CHECK(tokens[0].line_found == 1);
        CHECK(tokens[1].stringValue == "after");
        CHECK(tokens[1].line_found == 3);
    }
}

TEST_CASE("scanner emits single character operators as their own ASCII code")
{
    const std::string operator_chars = "+-*/<>=!:;,()[]{}|&.";
    //spaced apart so no two of them can pair into a comment marker
    std::string source_text;
    for (size_t i = 0; i < operator_chars.size(); i++)
    {
        source_text += operator_chars[i];
        source_text += ' ';
    }

    std::vector<token> tokens = scan_all(source_text + "\n");

    REQUIRE(tokens.size() == operator_chars.size() + 1);
    for (size_t i = 0; i < operator_chars.size(); i++)
    {
        CAPTURE(operator_chars[i]);
        CHECK(tokens[i].type == operator_chars[i]);
        CHECK(tokens[i].charValue == operator_chars[i]);
    }
    CHECK(tokens[operator_chars.size()].type == T_INVALID);
}

TEST_CASE("scanner does not combine two character operators")
{
    std::vector<token> tokens = scan_all("a <= b;\nc == d;\ne != f;\ng >= h;\n");

    REQUIRE(tokens.size() == 21);
    CHECK(tokens[1].type == T_LESS);
    CHECK(tokens[2].type == T_ASSIGN);
    CHECK(tokens[6].type == T_ASSIGN);
    CHECK(tokens[7].type == T_ASSIGN);
    CHECK(tokens[11].type == T_EXCLAM);
    CHECK(tokens[12].type == T_ASSIGN);
    CHECK(tokens[16].type == T_GREATER);
    CHECK(tokens[17].type == T_ASSIGN);
}

TEST_CASE("scanner skips // comments to the end of the line")
{
    std::vector<token> tokens = scan_all("// leading comment\nalpha\n// trailing comment\nbeta\n");

    REQUIRE(tokens.size() == 3);
    CHECK(tokens[0].stringValue == "alpha");
    CHECK(tokens[0].line_found == 2);
    CHECK(tokens[1].stringValue == "beta");
    CHECK(tokens[1].line_found == 4);
    CHECK(tokens[2].type == T_INVALID);
}

TEST_CASE("scanner skips block comments")
{
    SUBCASE("spanning several lines")
    {
        std::vector<token> tokens = scan_all("/* block\n comment */\nalpha\n");

        REQUIRE(tokens.size() == 2);
        CHECK(tokens[0].stringValue == "alpha");
        CHECK(tokens[0].line_found == 3);
    }

    SUBCASE("nested one level deep")
    {
        std::vector<token> tokens = scan_all("/* outer /* inner */ still commented */\nalpha\n");

        REQUIRE(tokens.size() == 2);
        CHECK(tokens[0].stringValue == "alpha");
        CHECK(tokens[0].line_found == 2);
    }
}

TEST_CASE("scanner consumes comment contents before ordinary tokenization")
{
    SUBCASE("line comment contents are inert through the newline")
    {
        std::string source_text =
            "// ignored_line 1.2.3 999999999999999999999999999999 \" @ /* */ ";
        source_text += '\xff';
        source_text += '\0';
        source_text += "\nline_live\n";

        temp_source_file fixture(source_text);
        scanner lexer(fixture.name());
        scan_result result = scan_all_with_diagnostics(lexer);

        REQUIRE(result.tokens.size() == 2);
        CHECK(result.tokens[0].stringValue == "line_live");
        CHECK(result.tokens[0].line_found == 2);
        CHECK(result.tokens[1].type == T_INVALID);
        CHECK(result.diagnostics.empty());
        CHECK(lexer.error_detected == false);
        CHECK(lexer.symbol_table.map.find("ignored_line") == lexer.symbol_table.map.end());
        CHECK(lexer.symbol_table.map.find("line_live") != lexer.symbol_table.map.end());
    }

    SUBCASE("block comment contents are inert except nested block delimiters")
    {
        std::string source_text =
            "/* ignored_block 1.2.3 999999999999999999999999999999 \" @ // ";
        source_text += '\xff';
        source_text += '\0';
        source_text += "\n/* ignored_nested \" @ // ";
        source_text += '\xff';
        source_text += '\0';
        source_text += " */ still_ignored */\nblock_live\n";

        temp_source_file fixture(source_text);
        scanner lexer(fixture.name());
        scan_result result = scan_all_with_diagnostics(lexer);

        REQUIRE(result.tokens.size() == 2);
        CHECK(result.tokens[0].stringValue == "block_live");
        CHECK(result.tokens[0].line_found == 3);
        CHECK(result.tokens[1].type == T_INVALID);
        CHECK(result.diagnostics.empty());
        CHECK(lexer.error_detected == false);
        CHECK(lexer.symbol_table.map.find("ignored_block") == lexer.symbol_table.map.end());
        CHECK(lexer.symbol_table.map.find("ignored_nested") == lexer.symbol_table.map.end());
        CHECK(lexer.symbol_table.map.find("still_ignored") == lexer.symbol_table.map.end());
        CHECK(lexer.symbol_table.map.find("block_live") != lexer.symbol_table.map.end());
    }
}

TEST_CASE("scanner treats string contents as one literal")
{
    std::string source_text = "\"@ /* // */ ";
    source_text += '\xff';
    source_text += '\0';
    source_text += "\" string_live\n";
    const std::string expected_string = source_text.substr(0, source_text.find(" string_live"));

    scan_result result = scan_all_with_diagnostics(source_text);

    REQUIRE(result.tokens.size() == 3);
    CHECK(result.tokens[0].type == T_STRING_VALUE);
    CHECK(result.tokens[0].stringValue == expected_string);
    CHECK(result.tokens[1].stringValue == "string_live");
    CHECK(result.tokens[2].type == T_INVALID);
    CHECK(result.diagnostics.empty());
}

TEST_CASE("scanner returns the T_INVALID sentinel at end of file, and keeps returning it")
{
    SUBCASE("empty file")
    {
        temp_source_file fixture("");
        scanner lexer(fixture.name());

        //the parser's loops depend on the sentinel being repeatable
        for (int i = 0; i < 3; i++)
        {
            token sentinel = lexer.Get_token();
            CHECK(sentinel.type == T_INVALID);
            CHECK(sentinel.line_found == 1);
        }
    }

    SUBCASE("sentinel copies the last real token")
    {
        std::vector<token> tokens = scan_all("alpha\n");

        REQUIRE(tokens.size() == 2);
        //by design (scanner.cpp Get_token): the sentinel is the previous token
        //with the type overwritten, so line/name information survives past EOF
        CHECK(tokens[1].type == T_INVALID);
        CHECK(tokens[1].stringValue == "alpha");
    }
}

TEST_CASE("scanner tracks line numbers across blank lines")
{
    std::vector<token> tokens = scan_all("one\ntwo\n\nthree\n");

    REQUIRE(tokens.size() == 4);
    CHECK(tokens[0].line_found == 1);
    CHECK(tokens[1].line_found == 2);
    CHECK(tokens[2].line_found == 4);
    //every one of them opens its line
    CHECK(tokens[0].first_token_on_line == true);
    CHECK(tokens[1].first_token_on_line == true);
    CHECK(tokens[2].first_token_on_line == true);
}

TEST_CASE("scanner does not let an identifier start with an underscore")
{
    std::vector<token> tokens = scan_all("_lead a_1\n");

    REQUIRE(tokens.size() == 4);
    CHECK(tokens[0].type == T_UNDERSCORE);
    CHECK(tokens[1].type == T_IDENTIFIER);
    CHECK(tokens[1].stringValue == "lead");
    //an underscore inside the identifier is fine
    CHECK(tokens[2].type == T_IDENTIFIER);
    CHECK(tokens[2].stringValue == "a_1");
}

TEST_CASE("scanner does not start a block comment inside a // comment (LX-1)")
{
    scan_result result = scan_all_with_diagnostics(
        "// note: /* not really a comment\nalpha\nbeta\n");

    REQUIRE(result.tokens.size() == 3);
    CHECK(result.tokens[0].stringValue == "alpha");
    CHECK(result.tokens[0].line_found == 2);
    CHECK(result.tokens[1].stringValue == "beta");
    CHECK(result.tokens[1].line_found == 3);
    CHECK(result.tokens[2].type == T_INVALID);
    CHECK(result.diagnostics.empty());
}

TEST_CASE("scanner reports a stray */ and keeps later nested comments closed (LX-2)")
{
    scan_result result = scan_all_with_diagnostics(
        "a */ b\n/* outer /* inner */ leaked */ c\n");

    REQUIRE(result.tokens.size() == 4);
    CHECK(result.tokens[0].stringValue == "a");
    CHECK(result.tokens[1].stringValue == "b");
    CHECK(result.tokens[2].stringValue == "c");
    CHECK(result.tokens[3].type == T_INVALID);
    REQUIRE(result.diagnostics.size() == 1);
    CHECK(result.diagnostics[0].line_found == 1);
    CHECK(result.diagnostics[0].message == "Stray block comment terminator detected");
}

TEST_CASE("scanner stamps a number with its opening line (LX-6)")
{
    std::vector<token> tokens = scan_all("5\nafter\n");

    REQUIRE(tokens.size() == 3);
    CHECK(tokens[0].type == T_INTEGER_VALUE);
    CHECK(tokens[0].intValue == 5);
    CHECK(tokens[0].line_found == 1);
    CHECK(tokens[1].stringValue == "after");
    CHECK(tokens[1].line_found == 2);
}

TEST_CASE("scanner handles an underscore-separated number at physical EOF")
{
    scan_result result = scan_all_with_diagnostics("1_000");

    REQUIRE(result.tokens.size() == 2);
    CHECK(result.tokens[0].type == T_INTEGER_VALUE);
    CHECK(result.tokens[0].intValue == 1000);
    CHECK(result.tokens[0].line_found == 1);
    CHECK(result.tokens[1].type == T_INVALID);
    CHECK(result.tokens[1].line_found == 1);
    CHECK(result.diagnostics.empty());
}

TEST_CASE("scanner reports real illegal characters without a priming false-positive (SIL-8)")
{
    SUBCASE("empty file")
    {
        temp_source_file fixture("");
        scanner lexer(fixture.name());
        CHECK(lexer.Get_token().type == T_INVALID);
        CHECK(lexer.error_detected == false);
        CHECK(lexer.take_diagnostics().empty());
    }

    SUBCASE("well formed input")
    {
        temp_source_file fixture("alpha;\n");
        scanner lexer(fixture.name());
        while (lexer.Get_token().type != T_INVALID)
        {
        }
        CHECK(lexer.error_detected == false);
        CHECK(lexer.take_diagnostics().empty());
    }

    SUBCASE("an actual illegal character")
    {
        scan_result result = scan_all_with_diagnostics("alpha @ beta\n");

        REQUIRE(result.tokens.size() == 3);
        CHECK(result.tokens[0].stringValue == "alpha");
        CHECK(result.tokens[1].stringValue == "beta");
        CHECK(result.tokens[2].type == T_INVALID);
        REQUIRE(result.diagnostics.size() == 1);
        CHECK(result.diagnostics[0].line_found == 1);
        CHECK(result.diagnostics[0].message == "Illegal character: '@'");
    }

    SUBCASE("a non-printable byte is rendered without invalid diagnostic text")
    {
        const std::string source_text("alpha \xff beta\n", 13);
        scan_result result = scan_all_with_diagnostics(source_text);

        REQUIRE(result.tokens.size() == 3);
        CHECK(result.tokens[0].stringValue == "alpha");
        CHECK(result.tokens[1].stringValue == "beta");
        CHECK(result.tokens[2].type == T_INVALID);
        REQUIRE(result.diagnostics.size() == 1);
        CHECK(result.diagnostics[0].line_found == 1);
        CHECK(result.diagnostics[0].message == "Illegal character: 0xFF");
    }

    SUBCASE("an embedded NUL byte outside a comment is diagnosed")
    {
        std::string source_text = "before";
        source_text += '\0';
        source_text += "after\n";
        scan_result result = scan_all_with_diagnostics(source_text);

        REQUIRE(result.tokens.size() == 3);
        CHECK(result.tokens[0].stringValue == "before");
        CHECK(result.tokens[1].stringValue == "after");
        CHECK(result.tokens[2].type == T_INVALID);
        REQUIRE(result.diagnostics.size() == 1);
        CHECK(result.diagnostics[0].line_found == 1);
        CHECK(result.diagnostics[0].message == "Illegal character: 0x00");
    }
}
