//Unit tests for the scanner.  The scanner is the one stage of this compiler
//that is stable enough to pin down with exact expectations, so what is asserted
//here is meant to be its contract: it should survive the typechecker rebuild.
//
//Cases tagged KNOWN-BUG assert what the scanner does *today* (with the audit id
//from docs/audit/AUDIT.md where one exists), not what it should do.  They are
//documentation of the defect, and they are supposed to fail loudly when the
//defect is finally fixed.
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
    //the bound only guards a fixture that never reaches EOF; note that a
    //multi-decimal number (LX-3) spins inside Get_token itself and would hang
    //regardless, which is why no fixture here contains one
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

TEST_CASE("KNOWN-BUG LX-1: a /* inside a // comment swallows the rest of the file")
{
    std::vector<token> tokens = scan_all("// note: /* not really a comment\nalpha\nbeta\n");

    //comment_handler runs even while a // comment is active, so the block
    //comment counter is opened and never closed and everything after is lost
    REQUIRE(tokens.size() == 1);
    CHECK(tokens[0].type == T_INVALID);
}

TEST_CASE("KNOWN-BUG LX-2: a stray */ makes a later nested comment leak tokens")
{
    std::vector<token> tokens = scan_all("a */ b\n/* outer /* inner */ leaked */ c\n");

    //the stray */ drives nested_comment_counter to -1 and it is never floored,
    //so the inner */ closes the block one level early
    REQUIRE(tokens.size() == 5);
    CHECK(tokens[0].stringValue == "a");
    CHECK(tokens[1].stringValue == "b");
    CHECK(tokens[2].stringValue == "leaked");
    CHECK(tokens[3].stringValue == "c");
}

TEST_CASE("KNOWN-BUG LX-6: a number ending a line is reported one line late")
{
    std::vector<token> tokens = scan_all("5\n");

    REQUIRE(tokens.size() == 2);
    CHECK(tokens[0].type == T_INTEGER_VALUE);
    CHECK(tokens[0].intValue == 5);
    //build_number_token counts the terminating newline before it stamps the
    //token, so line_found is 2 where every other token type would report 1
    CHECK(tokens[0].line_found == 2);
}

TEST_CASE("KNOWN-BUG SIL-8: error_detected is set even for a clean file")
{
    SUBCASE("empty file")
    {
        temp_source_file fixture("");
        scanner lexer(fixture.name());
        CHECK(lexer.Get_token().type == T_INVALID);
        //next_char is primed to '\0', which the first pass through Get_token
        //classifies as an invalid character before any real input is read
        CHECK(lexer.error_detected == true);
    }

    SUBCASE("well formed input")
    {
        temp_source_file fixture("alpha;\n");
        scanner lexer(fixture.name());
        while (lexer.Get_token().type != T_INVALID)
        {
        }
        CHECK(lexer.error_detected == true);
    }
}
