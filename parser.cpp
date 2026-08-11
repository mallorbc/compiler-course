#include "parser.h"

namespace
{

token_and_status invalid_expression_result(bool syntax_valid = true)
{
    token_and_status result;
    result.valid_parse = syntax_valid;
    result.semantic_valid = false;
    return result;
}

token_and_status typed_expression_result(Typechecker *checker, data_types type,
                                         const token &anchor)
{
    token_and_status result;
    result.valid_parse = true;
    result.semantic_valid = true;
    result.resolved_token = checker->make_expression_result(type, anchor);
    return result;
}

bool is_relation_start(int token_type)
{
    return token_type == T_LESS || token_type == T_GREATER ||
           token_type == T_ASSIGN || token_type == T_EXCLAM;
}

} // namespace

//ready for testing
parser::parser(std::string file_to_parse)
{
    //initalized the Current_parse_token type to 9999 as a flag to say that nothing has been loaded yet
    Current_parse_token.type = 9999;
    bool valid_parse;
    parse_file = file_to_parse;
    Lexer = new scanner(parse_file);
    type_checker = new Typechecker(this);
    valid_parse = parse_program();
    if (valid_parse)
    {
        if (!error_reports.empty())
        {
            std::cout << "The program parsed successfully with errors" << std::endl;
        }
        else
        {
            std::cout << "The program parsed successfully with no errors" << std::endl;
        }
    }
    else
    {
        std::cout << "The program had parsing errors" << std::endl;
    }
    print_errors();
}

//ready for testing; May have issues at the end of the program
token parser::Get_Valid_Token()
{
    token first_token;
    //if first token in the list;  may need a similar condition for the last token
    if (Current_parse_token.type == 9999)
    {
        Current_parse_token = Lexer->Get_token();
        collect_scanner_diagnostics();
        Next_parse_token = Lexer->Get_token();
        collect_scanner_diagnostics();
        Current_parse_token_type = Current_parse_token.type;
        Next_parse_token_type = Next_parse_token.type;
    }
    //if not the first token
    else
    {
        prev_token = Current_parse_token;
        prev_token_type = Current_parse_token_type;
        Current_parse_token = Next_parse_token;
        Current_parse_token_type = Next_parse_token_type;
        Next_parse_token = Lexer->Get_token();
        collect_scanner_diagnostics();
        Next_parse_token_type = Next_parse_token.type;
        //for some reason random junk sometimes appears
        while (Next_parse_token_type > T_INVALID || Next_parse_token_type < 0)
        {
            Next_parse_token = Lexer->Get_token();
            collect_scanner_diagnostics();
            Next_parse_token_type = Next_parse_token.type;
        }
    }
    token_generation++;
    // Lexer->symbol_table.update_token_scope_id(Current_parse_token, current_scope_id);
    return Current_parse_token;
}

void parser::collect_scanner_diagnostics()
{
    std::vector<scanner_diagnostic> collected = Lexer->take_diagnostics();
    for (size_t i = 0; i < collected.size(); i++)
    {
        add_error_report("Error on line " +
                         std::to_string(collected[i].line_found) + ": " +
                         collected[i].message);
    }
}

//ready for testing
void parser::add_error_report(std::string error_report)
{
    error_reports.push_back(error_report);
}

//ready for testing
void parser::generate_error_report(std::string error_message)
{
    int line_number = Current_parse_token.line_found;
    if (line_number < 1)
    {
        line_number = prev_token.line_found;
    }
    generate_error_report(error_message, line_number);
}

void parser::generate_error_report(std::string error_message, int line_number)
{
    std::string full_error_message = "";
    if (!resync_status)
    {
        if (line_number < 1)
        {
            line_number = 1;
        }
        full_error_message = "Error on line " + std::to_string(line_number) + ": ";
        full_error_message = full_error_message + error_message;
        add_error_report(full_error_message);
    }
}

void parser::generate_error_report_previous_token(std::string error_message)
{
    int line_number = prev_token.line_found;
    //An unterminated string is represented by one token at its opening line,
    //but the omitted delimiter follows the physical end of that token.  The
    //EOF token is the only source position that preserves that end line.
    if (prev_token.type == T_STRING_VALUE && Lexer->quote_status &&
        Current_parse_token.type == T_INVALID)
    {
        line_number = Current_parse_token.line_found;
    }
    if (line_number < 1)
    {
        line_number = Current_parse_token.line_found;
    }
    generate_error_report(error_message, line_number);
}

//ready for testing
void parser::print_errors()
{
    for (std::size_t i = 0; i < error_reports.size(); i++)
    {
        std::cout << error_reports[i] << std::endl
                  << std::endl;
    }
}

int parser::error_count()
{
    return error_reports.size();
}

//ready for testing
//refactored 1 time
bool parser::parse_program()
{
    //this tracks the state of the parser
    bool valid_parse;
    valid_parse = parse_program_header();
    valid_parse = parse_program_body();
    if (Current_parse_token_type == T_PERIOD)
    {
        valid_parse = true;
    }
    else
    {
        generate_error_report_previous_token("Missing \".\" to end the program");
        errors_occured = true;
        valid_parse = false;
    }
    if (debugging && !valid_parse)
    {
        std::cout << "parser failed on parse_proram()" << std::endl;
    }
    //resync_parser(state);
    return valid_parse;
}

//ready for testing
//refactored 1
bool parser::parse_program_header()
{
    bool valid_parse;
    Current_parse_token = Get_Valid_Token();
    if (Current_parse_token_type == T_PROGRAM)
    {
        valid_parse = true;
        Current_parse_token = Get_Valid_Token();
    }
    else
    {
        generate_error_report("Expected keyword \"Program\" not found");
        errors_occured = true;
        if (debugging)
        {
            std::cout << "parser failed on parse_program_header()" << std::endl;
        }
        return false;
    }
    if (Current_parse_token_type == T_IDENTIFIER)
    {
        Current_parse_token.identifer_type = I_PROGRAM_NAME;
        Current_parse_token.global_scope = true;
        valid_parse = Lexer->symbol_table.declare_symbol(0, Current_parse_token);
        if (!valid_parse)
        {
            report_duplicate_declaration(Current_parse_token);
        }
        Current_parse_token = Get_Valid_Token();
    }
    else
    {
        generate_error_report("Expected \"identifier\" not found");
        errors_occured = true;
        if (debugging)
        {
            std::cout << "parser failed on parse_program_header()" << std::endl;
        }
        return false;
    }
    if (Current_parse_token_type == T_IS)
    {
        valid_parse = true;
        Current_parse_token = Get_Valid_Token();
    }
    else
    {
        generate_error_report_previous_token("Expected keyword \"is\" is not found");
        errors_occured = true;
        if (debugging)
        {
            std::cout << "parser failed on parse_program_header()" << std::endl;
        }
        return false;
    }

    return valid_parse;
}

//ready to test
//refactored 1 time
bool parser::parse_program_body()
{
    //this tracks the state of the parser
    parser_state state = S_PROGRAM_BODY;
    bool valid_parse = false;
    //keeps parsing until keyword begin is found
    while (Current_parse_token_type != T_BEGIN)
    {
        std::size_t iteration_start = token_generation;
        valid_parse = parse_base_declaration();
        //required a semicolon after parse
        if (Current_parse_token_type == T_SEMICOLON)
        {
            Current_parse_token = Get_Valid_Token();
        }
        else if (!valid_parse &&
                 (Current_parse_token_type == T_PROCEDURE ||
                  Current_parse_token_type == T_VARIABLE ||
                  Current_parse_token_type == T_TYPE ||
                  Current_parse_token_type == T_GLOBAL ||
                  Current_parse_token_type == T_BEGIN ||
                  Current_parse_token_type == T_END ||
                  Current_parse_token_type == T_PROGRAM ||
                  Current_parse_token_type == T_PERIOD))
        {
            //A malformed declaration stopped at the start of its sibling or
            //at an enclosing-body delimiter.  The next loop/production owns it.
            valid_parse = true;
        }
        else
        {
            if (valid_parse)
            {
                generate_error_report_previous_token("Missing \";\" to complete declaration");
                errors_occured = true;
            }
            valid_parse = resync_parser(state);
            //if have run out of tokens
            if (Current_parse_token_type == T_INVALID)
            {
                if (Lexer->is_nested_commented)
                {
                    generate_error_report("Unclosed block comment detected", Lexer->nested_comment_line);
                    errors_occured = true;
                }
                return false;
            }
        }
        if (Current_parse_token_type == T_END ||
            Current_parse_token_type == T_PROGRAM ||
            Current_parse_token_type == T_PERIOD)
        {
            break;
        }
        //happens after trying to resync
        if (valid_parse)
        {
            //breaks out on tokens indicating begin was aborbed
            //these all indicate statements or eof
            if (Current_parse_token_type == T_IF || Current_parse_token_type == T_RETURN || Current_parse_token_type == T_FOR || Current_parse_token_type == T_IDENTIFIER || Current_parse_token_type == T_INVALID)
            {
                break;
            }
        }
        //else parse_base_declaration failed
        else
        {
            if (debugging)
            {
                std::cout << "parser failed on parse_program_body()" << std::endl;
            }
            valid_parse = resync_parser(state);
            if (Current_parse_token_type == T_INVALID)
            {
                return false;
            }
        }
        parsing_statements = false;
        if (token_generation == iteration_start && Current_parse_token_type != T_INVALID &&
            Current_parse_token_type != T_BEGIN && Current_parse_token_type != T_END &&
            Current_parse_token_type != T_PROGRAM && Current_parse_token_type != T_PERIOD &&
            Current_parse_token_type != T_PROCEDURE && Current_parse_token_type != T_VARIABLE &&
            Current_parse_token_type != T_TYPE && Current_parse_token_type != T_GLOBAL)
        {
            Current_parse_token = Get_Valid_Token();
        }
    }
    //once the begin token is recieved
    if (Current_parse_token_type == T_BEGIN)
    {
        Current_parse_token = Get_Valid_Token();
    }
    else
    {
        generate_error_report_previous_token("Missing keyword \"begin\" to begin program statements");
        errors_occured = true;
        valid_parse = resync_parser(state);
        if (Current_parse_token_type == T_INVALID)
        {
            return false;
        }
        //return false;
    }
    while (Current_parse_token_type != T_END &&
           Current_parse_token_type != T_PROGRAM &&
           Current_parse_token_type != T_PERIOD)
    {
        std::size_t iteration_start = token_generation;
        valid_parse = parse_base_statement();
        if (Current_parse_token_type == T_SEMICOLON)
        {
            //clear out the tokens at the end of a statement
            type_checker->clear_tokens(false);
            Current_parse_token = Get_Valid_Token();
        }
        else
        {
            if (valid_parse)
            {
                generate_error_report_previous_token("Missing \";\" to end program statement");
                errors_occured = true;
            }
            valid_parse = resync_parser(state);
            //if have run out of tokens
            if (Current_parse_token_type == T_INVALID)
            {
                if (Lexer->is_nested_commented)
                {
                    generate_error_report("Unclosed block comment detected", Lexer->nested_comment_line);
                    errors_occured = true;
                }
                return false;
            }
        }
        //conditions to break loop
        if (valid_parse)
        {
            if (Current_parse_token_type == T_PROGRAM || Current_parse_token_type == T_PERIOD || Current_parse_token_type == T_INVALID)
            {
                break;
            }
        }
        else
        {
            valid_parse = resync_parser(state);
        }
        if (token_generation == iteration_start && Current_parse_token_type != T_INVALID &&
            Current_parse_token_type != T_END && Current_parse_token_type != T_PROGRAM &&
            Current_parse_token_type != T_PERIOD)
        {
            Current_parse_token = Get_Valid_Token();
        }
    }
    //once the end token is recieveds
    if (Current_parse_token_type == T_END)
    {
        Current_parse_token = Get_Valid_Token();
    }
    else
    {
        generate_error_report_previous_token("Missing keyword \"end\" to end program");
        errors_occured = true;
        valid_parse = resync_parser(state);
    }

    if (Current_parse_token_type == T_PROGRAM)
    {
        Current_parse_token = Get_Valid_Token();
    }
    else
    {
        generate_error_report_previous_token("Missing keyworkd \"program\" to end program");
        errors_occured = true;
        valid_parse = resync_parser(state);
        //return false;
    }

    return valid_parse;
}

//ready to test
//refactored 1 time
bool parser::parse_base_declaration()
{
    //tracks whether base declaration is global or not
    bool is_global_declaration = false;
    //this tracks the state of the parser
    bool valid_parse;
    if (Current_parse_token_type == T_GLOBAL)
    {
        //Do work for global declarations here
        is_global_declaration = true;
        Current_parse_token = Get_Valid_Token();

        if (Current_parse_token_type == T_PROCEDURE)
        {
            Current_parse_token = Get_Valid_Token();
            valid_parse = parse_procedure_declaration(is_global_declaration);
        }
        else if (Current_parse_token_type == T_VARIABLE)
        {
            Current_parse_token = Get_Valid_Token();
            valid_parse = parse_variable_declaration(is_global_declaration);
        }
        else if (Current_parse_token_type == T_TYPE)
        {
            Current_parse_token = Get_Valid_Token();
            valid_parse = parse_type_declaration(is_global_declaration);
        }
        else
        {
            if (debugging)
            {
                std::cout << "parser failed on parse_base_declaration()" << std::endl;
            }
            generate_error_report("Expected keywords \"procedure\",\"variable\" \"type\", or \"begin\" not found");
            errors_occured = true;
            return false;
        }
    }
    else
    {
        if (Current_parse_token_type == T_PROCEDURE)
        {
            Current_parse_token = Get_Valid_Token();
            valid_parse = parse_procedure_declaration(is_global_declaration);
        }
        else if (Current_parse_token_type == T_VARIABLE)
        {
            Current_parse_token = Get_Valid_Token();
            valid_parse = parse_variable_declaration(is_global_declaration);
        }
        else if (Current_parse_token_type == T_TYPE)
        {
            Current_parse_token = Get_Valid_Token();
            valid_parse = parse_type_declaration(is_global_declaration);
        }
        else
        {
            if (debugging)
            {
                std::cout << "parser failed on parse_base_declaration()" << std::endl;
            }
            generate_error_report("Expected keywords \"procedure\",\"variable\" \"type\", or \"begin\" not found");
            errors_occured = true;
            return false;
        }
    }

    return valid_parse;
}

bool parser::parse_procedure_declaration(bool is_global)
{
    const int parent_scope_id = current_scope_id;
    const int target_scope_id = declaration_scope(is_global);
    token candidate;
    std::vector<token> parameters;
    std::vector<token> header_symbols;

    //A recovery/body scope is always created and later popped exactly once.
    //It remains in SymbolTable for later code generation even after exit.
    update_scopes(true);
    const int body_scope_id = current_scope_id;
    bool header_valid = parse_procedure_header(is_global, candidate, parameters,
                                               header_symbols);
    bool duplicate = false;
    if (!candidate.stringValue.empty())
    {
        duplicate = Lexer->symbol_table.has_declared(target_scope_id,
                                                      candidate.stringValue);
        if (duplicate)
        {
            report_duplicate_declaration(candidate);
            header_valid = false;
        }
    }

    if (header_valid && !duplicate)
    {
        std::vector<token> body_symbols = header_symbols;
        body_symbols.insert(body_symbols.end(), parameters.begin(), parameters.end());
        if (!Lexer->symbol_table.can_declare_all(body_scope_id, body_symbols))
        {
            generate_error_report("Duplicate declaration in procedure header",
                                  candidate.line_found);
            errors_occured = true;
            header_valid = false;
        }
        else if (!Lexer->symbol_table.declare_symbol(target_scope_id, candidate))
        {
            report_duplicate_declaration(candidate);
            header_valid = false;
        }
        else
        {
            const SymbolRef procedure_ref{target_scope_id, candidate.stringValue};
            Lexer->symbol_table.set_scope_owner(body_scope_id, procedure_ref);
            Lexer->symbol_table.declare_all(body_scope_id, body_symbols);
        }
    }

    const bool body_valid = parse_procedure_body();
    parsing_statements = false;
    update_scopes(false);
    (void)parent_scope_id;
    return header_valid && body_valid;
}

bool parser::parse_procedure_header(bool is_global, token &candidate,
                                    std::vector<token> &parameters,
                                    std::vector<token> &header_symbols)
{
    bool valid_parse = true;
    if (Current_parse_token_type == T_IDENTIFIER)
    {
        candidate = Current_parse_token;
        candidate.identifer_type = I_PROCEDURE;
        candidate.global_scope = declaration_target(is_global);
        candidate.scope_id = declaration_scope(is_global);
        Current_parse_token = Get_Valid_Token();
    }
    else
    {
        generate_error_report("Procedure must be named a valid identifier");
        errors_occured = true;
        valid_parse = false;
    }

    if (Current_parse_token_type == T_COLON)
    {
        Current_parse_token = Get_Valid_Token();
    }
    else
    {
        generate_error_report_previous_token("Expected \":\" before type mark declaration");
        errors_occured = true;
        valid_parse = false;
    }

    data_types return_type = TYPE_NONE;
    std::vector<token> return_enum_symbols;
    if (!parse_declared_type(return_type, return_enum_symbols))
    {
        valid_parse = false;
    }
    candidate.identifier_data_type = return_type;
    header_symbols.insert(header_symbols.end(), return_enum_symbols.begin(),
                          return_enum_symbols.end());

    if (Current_parse_token_type != T_LPARAM)
    {
        generate_error_report_previous_token("Missing \"(\" needed to for procedure declaration");
        errors_occured = true;
        return false;
    }
    Current_parse_token = Get_Valid_Token();
    if (Current_parse_token_type == T_RPARAM)
    {
        Current_parse_token = Get_Valid_Token();
        return valid_parse;
    }
    if (!parse_parameter_list(parameters, header_symbols))
    {
        return false;
    }
    for (const token &parameter : parameters)
    {
        candidate.procedure_params.push_back(parameter.identifier_data_type);
    }
    return valid_parse;
}

//ready to test
bool parser::parse_procedure_body()
{
    //this tracks the state of the parser
    parser_state state = S_PROCEDURE_BODY;
    bool valid_parse = false;
    //must be able to parse declarations until T_BEGIN is found
    while (Current_parse_token_type != T_BEGIN)
    {
        std::size_t iteration_start = token_generation;
        valid_parse = parse_base_declaration();
        if (Current_parse_token_type == T_SEMICOLON)
        {
            Current_parse_token = Get_Valid_Token();
        }
        else if (!valid_parse &&
                 (Current_parse_token_type == T_PROCEDURE ||
                  Current_parse_token_type == T_VARIABLE ||
                  Current_parse_token_type == T_TYPE ||
                  Current_parse_token_type == T_GLOBAL ||
                  Current_parse_token_type == T_BEGIN ||
                  Current_parse_token_type == T_END))
        {
            //Keep a sibling declaration or enclosing-body delimiter available
            //to this declaration loop's caller.
            valid_parse = true;
        }
        else
        {
            if (valid_parse)
            {
                generate_error_report_previous_token("Missing \";\" to complete declaration");
                errors_occured = true;
            }
            valid_parse = resync_parser(state);
            //if have run out of tokens
            if (Current_parse_token_type == T_INVALID)
            {
                if (Lexer->is_nested_commented)
                {
                    generate_error_report("Unclosed block comment detected", Lexer->nested_comment_line);
                    errors_occured = true;
                }
                return false;
            }
        }
        if (Current_parse_token_type == T_END)
        {
            break;
        }
        if (token_generation == iteration_start && Current_parse_token_type != T_INVALID &&
            Current_parse_token_type != T_BEGIN && Current_parse_token_type != T_END &&
            Current_parse_token_type != T_PROCEDURE &&
            Current_parse_token_type != T_VARIABLE &&
            Current_parse_token_type != T_TYPE &&
            Current_parse_token_type != T_GLOBAL)
        {
            Current_parse_token = Get_Valid_Token();
        }
        if (valid_parse)
        {
            if (Current_parse_token_type == T_BEGIN || Current_parse_token_type == T_IF || Current_parse_token_type == T_FOR || Current_parse_token_type == T_RETURN || Current_parse_token_type == T_END)
            {
                break;
            }
        }
        //else parse_base_declaration failed
        else
        {
            if (debugging)
            {
                std::cout << "parser failed on parse_procedure_body()" << std::endl;
            }
            valid_parse = resync_parser(state);
            if (Current_parse_token_type == T_INVALID)
            {
                return false;
            }
        }
    }

    //after doen parsing any and all declarations, must start parsing statements
    //need to parse more than one base statement
    //add this if statement in the case that never enters while loop
    if (Current_parse_token_type == T_BEGIN)
    {
        Current_parse_token = Get_Valid_Token();
    }
    else
    {
        generate_error_report_previous_token("Missing keyword \"begin\" to begin procedure statements");
        errors_occured = true;
        valid_parse = resync_parser(state);
        if (Current_parse_token_type == T_INVALID)
        {
            return false;
        }
    }
    parsing_statements = true;
    while (Current_parse_token_type != T_END)
    {
        std::size_t iteration_start = token_generation;
        valid_parse = parse_base_statement();
        if (Current_parse_token_type == T_SEMICOLON)
        {
            //clear out the tokens at the end of a statement
            type_checker->clear_tokens(false);
            Current_parse_token = Get_Valid_Token();
        }
        else
        {
            if (valid_parse)
            {
                generate_error_report_previous_token("Missing \";\" to end program statement");
                errors_occured = true;
            }
            valid_parse = resync_parser(state);
            //if have run out of tokens
            if (Current_parse_token_type == T_INVALID)
            {
                if (Lexer->is_nested_commented)
                {
                    generate_error_report("Unclosed block comment detected", Lexer->nested_comment_line);
                    errors_occured = true;
                }
                return false;
            }
        }
        if (token_generation == iteration_start)
        {
            //A second begin while parsing procedure statements belongs to the
            //enclosing body after a malformed/missing "end procedure".  Leave
            //it for that caller; other stalled tokens are discarded once.
            if (Current_parse_token_type == T_BEGIN ||
                Current_parse_token_type == T_PROCEDURE)
            {
                break;
            }
            if (Current_parse_token_type != T_INVALID && Current_parse_token_type != T_END)
            {
                Current_parse_token = Get_Valid_Token();
            }
        }
        if (valid_parse)
        {
            if (Current_parse_token_type == T_PROCEDURE || Current_parse_token_type == T_INVALID)
            {
                break;
            }
        }
        else
        {
            valid_parse = resync_parser(state);
            if (Current_parse_token_type == T_INVALID)
            {
                return false;
            }
        }
    }
    bool consumed_end = false;
    if (Current_parse_token_type == T_END)
    {
        Current_parse_token = Get_Valid_Token();
        consumed_end = true;
    }
    else
    {
        generate_error_report_previous_token("Missing keyword \"end\" to close procedure body");
        errors_occured = true;
        valid_parse = false;
        if (Current_parse_token_type != T_BEGIN &&
            Current_parse_token_type != T_PROCEDURE)
        {
            resync_parser(state);
        }
    }
    if (consumed_end && Current_parse_token_type == T_PROCEDURE)
    {
        Current_parse_token = Get_Valid_Token();
    }
    else
    {
        generate_error_report_previous_token("Missing keyword \"procedure\" to close procedure body");
        errors_occured = true;
        valid_parse = false;
        if (consumed_end && Current_parse_token_type != T_BEGIN &&
            Current_parse_token_type != T_PROCEDURE)
        {
            resync_parser(state);
        }
    }

    return valid_parse;
}


bool parser::parse_declared_type(data_types &resolved_type, std::vector<token> &enum_symbols)
{
    resolved_type = TYPE_NONE;
    if (Current_parse_token_type == T_INTEGER_TYPE)
    {
        resolved_type = TYPE_INT;
        Current_parse_token = Get_Valid_Token();
        return true;
    }
    if (Current_parse_token_type == T_FLOAT_TYPE)
    {
        resolved_type = TYPE_FLOAT;
        Current_parse_token = Get_Valid_Token();
        return true;
    }
    if (Current_parse_token_type == T_STRING_TYPE)
    {
        resolved_type = TYPE_STRING;
        Current_parse_token = Get_Valid_Token();
        return true;
    }
    if (Current_parse_token_type == T_BOOL_TYPE)
    {
        resolved_type = TYPE_BOOL;
        Current_parse_token = Get_Valid_Token();
        return true;
    }
    if (Current_parse_token_type == T_IDENTIFIER)
    {
        token type_symbol;
        if (!Lexer->symbol_table.resolve_name(Current_parse_token.stringValue,
                                              current_scope_id, type_symbol) ||
            type_symbol.identifer_type != I_TYPE)
        {
            generate_error_report("Expected declared type \"" +
                                      Current_parse_token.stringValue + "\"",
                                  Current_parse_token.line_found);
            errors_occured = true;
            Current_parse_token = Get_Valid_Token();
            return false;
        }
        resolved_type = type_symbol.identifier_data_type;
        Current_parse_token = Get_Valid_Token();
        return true;
    }
    if (Current_parse_token_type != T_ENUM)
    {
        generate_error_report("Missing valid type mark");
        errors_occured = true;
        return false;
    }

    Current_parse_token = Get_Valid_Token();
    if (Current_parse_token_type != T_LBRACE)
    {
        generate_error_report_previous_token("Expected \"{\" to begin enumeration");
        errors_occured = true;
        return false;
    }
    Current_parse_token = Get_Valid_Token();
    bool expect_identifier = true;
    while (Current_parse_token_type != T_RBRACE &&
           Current_parse_token_type != T_INVALID)
    {
        if (expect_identifier)
        {
            if (Current_parse_token_type != T_IDENTIFIER)
            {
                generate_error_report("Expected identifier as part of Enum");
                errors_occured = true;
                return false;
            }
            token enumerator = Current_parse_token;
            enumerator.identifer_type = I_TYPE;
            enumerator.identifier_data_type = TYPE_NONE;
            enum_symbols.push_back(enumerator);
            Current_parse_token = Get_Valid_Token();
            expect_identifier = false;
        }
        else if (Current_parse_token_type == T_COMMA)
        {
            Current_parse_token = Get_Valid_Token();
            expect_identifier = true;
        }
        else
        {
            generate_error_report("Enumeration list must be either a comma or a identifier");
            errors_occured = true;
            return false;
        }
    }
    if (expect_identifier || Current_parse_token_type != T_RBRACE)
    {
        generate_error_report_previous_token("Missing expected identifier after comma in enumeration list");
        errors_occured = true;
        return false;
    }
    Current_parse_token = Get_Valid_Token();
    return true;
}

bool parser::parse_parameter(token &parameter, std::vector<token> &header_symbols)
{
    if (Current_parse_token_type != T_IDENTIFIER)
    {
        generate_error_report("Missing identifier for variable declaration");
        errors_occured = true;
        return false;
    }
    parameter = Current_parse_token;
    parameter.identifer_type = I_VARIABLE;
    Current_parse_token = Get_Valid_Token();
    if (Current_parse_token_type != T_COLON)
    {
        generate_error_report_previous_token("Missing colon for delcaration of variable type");
        errors_occured = true;
        return false;
    }
    Current_parse_token = Get_Valid_Token();
    std::vector<token> enum_symbols;
    if (!parse_declared_type(parameter.identifier_data_type, enum_symbols))
    {
        return false;
    }
    header_symbols.insert(header_symbols.end(), enum_symbols.begin(), enum_symbols.end());
    if (Current_parse_token_type == T_LBRACKET)
    {
        parameter.is_array = true;
        Current_parse_token = Get_Valid_Token();
        if (!parse_bound())
        {
            return false;
        }
        if (Current_parse_token_type != T_RBRACKET)
        {
            generate_error_report_previous_token("Missing \"]\" to close the array declaration");
            errors_occured = true;
            return false;
        }
        Current_parse_token = Get_Valid_Token();
    }
    return true;
}

bool parser::parse_parameter_list(std::vector<token> &parameters,
                                  std::vector<token> &header_symbols)
{
    while (true)
    {
        if (Current_parse_token_type != T_VARIABLE)
        {
            generate_error_report("Expected keyword \"variable\" in procedure parameter list");
            errors_occured = true;
            return false;
        }
        Current_parse_token = Get_Valid_Token();
        token parameter;
        if (!parse_parameter(parameter, header_symbols))
        {
            return false;
        }
        parameters.push_back(parameter);
        if (Current_parse_token_type != T_COMMA)
        {
            break;
        }
        Current_parse_token = Get_Valid_Token();
        if (Current_parse_token_type == T_RPARAM)
        {
            generate_error_report("Missing parameter after comma in procedure parameter list");
            errors_occured = true;
            return false;
        }
    }
    if (Current_parse_token_type != T_RPARAM)
    {
        generate_error_report_previous_token("Missing \")\" to close procedure parameter list");
        errors_occured = true;
        return false;
    }
    Current_parse_token = Get_Valid_Token();
    return true;
}

bool parser::parse_variable_declaration(bool is_global)
{
    if (Current_parse_token_type != T_IDENTIFIER)
    {
        generate_error_report("Missing identifier for variable declaration");
        errors_occured = true;
        return false;
    }
    token candidate = Current_parse_token;
    candidate.identifer_type = I_VARIABLE;
    candidate.global_scope = declaration_target(is_global);
    const int target_scope_id = declaration_scope(is_global);
    Current_parse_token = Get_Valid_Token();
    if (Current_parse_token_type != T_COLON)
    {
        generate_error_report_previous_token("Missing colon for delcaration of variable type");
        errors_occured = true;
        return false;
    }
    Current_parse_token = Get_Valid_Token();
    std::vector<token> enum_symbols;
    if (!parse_declared_type(candidate.identifier_data_type, enum_symbols))
    {
        return false;
    }
    if (Current_parse_token_type == T_LBRACKET)
    {
        candidate.is_array = true;
        Current_parse_token = Get_Valid_Token();
        if (!parse_bound())
        {
            return false;
        }
        if (Current_parse_token_type != T_RBRACKET)
        {
            generate_error_report_previous_token("Missing \"]\" to close the array declaration");
            errors_occured = true;
            return false;
        }
        Current_parse_token = Get_Valid_Token();
    }
    std::vector<token> symbols;
    symbols.push_back(candidate);
    symbols.insert(symbols.end(), enum_symbols.begin(), enum_symbols.end());
    if (!Lexer->symbol_table.declare_all(target_scope_id, symbols))
    {
        report_duplicate_declaration(candidate);
        return false;
    }
    return true;
}

bool parser::parse_bound()
{
    if (Current_parse_token_type == T_INTEGER_VALUE ||
        Current_parse_token_type == T_FLOAT_VALUE)
    {
        Current_parse_token = Get_Valid_Token();
        return true;
    }
    generate_error_report("Missing expected number");
    errors_occured = true;
    return false;
}

bool parser::parse_type_declaration(bool is_global)
{
    if (Current_parse_token_type != T_IDENTIFIER)
    {
        generate_error_report("Missing required identifier for type declaration");
        errors_occured = true;
        return false;
    }
    token candidate = Current_parse_token;
    candidate.identifer_type = I_TYPE;
    candidate.global_scope = declaration_target(is_global);
    const int target_scope_id = declaration_scope(is_global);
    Current_parse_token = Get_Valid_Token();
    if (Current_parse_token_type != T_IS)
    {
        generate_error_report_previous_token("Missing required \"is\" for type declaration");
        errors_occured = true;
        return false;
    }
    Current_parse_token = Get_Valid_Token();
    std::vector<token> enum_symbols;
    if (!parse_declared_type(candidate.identifier_data_type, enum_symbols))
    {
        return false;
    }
    std::vector<token> symbols;
    symbols.push_back(candidate);
    symbols.insert(symbols.end(), enum_symbols.begin(), enum_symbols.end());
    if (!Lexer->symbol_table.declare_all(target_scope_id, symbols))
    {
        report_duplicate_declaration(candidate);
        return false;
    }
    return true;
}

//ready to test
//refactored 1 time
bool parser::parse_base_statement()
{
    //this tracks the state of the parser
    bool valid_parse;
    //an identifier means it will be an assignment statement
    if (Current_parse_token_type == T_IDENTIFIER)
    {
        const token destination_occurrence = Current_parse_token;
        type_checker->set_statement_type(destination_occurrence);
        token destination;
        const bool destination_resolved = resolve_identifier_use(destination_occurrence,
                                                                 destination);
        Current_parse_token = Get_Valid_Token();
        valid_parse = parse_assignment_statement(destination_resolved ? destination :
                                                                       destination_occurrence);
    }
    else if (Current_parse_token_type == T_IF)
    {
        //sets the typchecker up to handle if statements
        type_checker->set_statement_type(Current_parse_token);
        Current_parse_token = Get_Valid_Token();
        valid_parse = parse_if_statement();
    }
    else if (Current_parse_token_type == T_FOR)
    {
        //sets the typchecker up to handle loop statements
        type_checker->set_statement_type(Current_parse_token);
        Current_parse_token = Get_Valid_Token();
        valid_parse = parse_loop_statement();
    }
    else if (Current_parse_token_type == T_RETURN)
    {
        //sets the typchecker up to handle return statements
        type_checker->set_statement_type(Current_parse_token);
        Current_parse_token = Get_Valid_Token();
        valid_parse = parse_return_statement();
    }
    else
    {
        if (debugging)
        {
            std::cout << "parser failed on parse_base_statement()" << std::endl;
        }
        generate_error_report("Invalid statement; Not an assignment, if, loop, or return");
        errors_occured = true;
        return false;
    }

    return valid_parse;
}

//ready to test
bool parser::parse_number()
{
    //this tracks the state of the parser
    bool valid_parse;
    //the token will be either an integer or a float, or and error
    if (Current_parse_token_type == T_INTEGER_VALUE)
    {
        valid_parse = true;
        Current_parse_token = Get_Valid_Token();
    }
    else if (Current_parse_token_type == T_FLOAT_VALUE)
    {
        valid_parse = true;
        Current_parse_token = Get_Valid_Token();
    }
    //not an integer, not a float, so is an error
    else
    {
        if (debugging)
        {
            std::cout << "parser failed on parse_number()" << std::endl;
        }
        generate_error_report("Missing expected float or integer");
        errors_occured = true;
        return false;
    }
    return valid_parse;
}

//ready to test
//consumes an identifer before parsing
//refactored 1 time
bool parser::parse_assignment_statement(token destination_token)
{
    token_and_status expression_parse;
    token_and_status destination_parse;
    token destination_parse_token;
    token expression_parse_token;
    //this tracks the state of the parser
    bool valid_parse;
    destination_parse = parse_assignment_destination(destination_token);
    destination_parse_token = destination_parse.resolved_token;
    valid_parse = destination_parse.valid_parse;
    if (valid_parse)
    {
        if (Current_parse_token_type == T_COLON)
        {
            Current_parse_token = Get_Valid_Token();
        }
        else
        {
            generate_error_report_previous_token("Missing \":\" needed for assignment statement");
            errors_occured = true;
            return false;
        }
        if (Current_parse_token_type == T_ASSIGN)
        {
            Current_parse_token = Get_Valid_Token();
            expression_parse = parse_expression();
            expression_parse_token = expression_parse.resolved_token;
            valid_parse = expression_parse.valid_parse;
            if (valid_parse && destination_parse.semantic_valid &&
                expression_parse.semantic_valid && !type_checker->statement_suppressed)
            {
                (void)type_checker->check_assignment_statement(destination_parse_token,
                                                               expression_parse_token);
            }
        }
        else
        {
            generate_error_report_previous_token("Missing \"=\" needed for assignment statement");
            errors_occured = true;
            return false;
        }
    }
    //not a valid parse from parse assignment_destination
    return valid_parse;
}

//ready for testing
//consumes if token before parsing
//refactored 1 time
bool parser::parse_if_statement()
{
    token updated_token;
    token_and_status expression_parse;
    //this tracks the state of the parser
    parser_state state = S_IF_STATEMENT;
    bool valid_parse;
    if (Current_parse_token_type == T_LPARAM)
    {
        Current_parse_token = Get_Valid_Token();
        expression_parse = parse_expression();
        updated_token = expression_parse.resolved_token;
        if (expression_parse.valid_parse && expression_parse.semantic_valid &&
            !type_checker->statement_suppressed)
        {
            type_checker->check_if_statement(updated_token);
        }
        type_checker->clear_tokens(false);
        valid_parse = expression_parse.valid_parse;
        if (Current_parse_token_type == T_RPARAM)
        {
            Current_parse_token = Get_Valid_Token();
        }
        else
        {
            generate_error_report_previous_token("Missing \")\" expected for if statment");
            errors_occured = true;
            valid_parse = resync_parser(state);
            //return false;
        }
        if (Current_parse_token_type == T_THEN)
        {
            Current_parse_token = Get_Valid_Token();
        }
        else
        {
            generate_error_report_previous_token("Missing expected keyword \"then\" for if statements");
            errors_occured = true;
            valid_parse = resync_parser(state);
            if (Current_parse_token_type == T_INVALID)
            {
                return false;
            }
        }
        //may need to remove this if statement
        while (Current_parse_token_type != T_END)
        {
            std::size_t iteration_start = token_generation;
            if (Current_parse_token_type == T_ELSE)
            {
                Current_parse_token = Get_Valid_Token();
            }
            valid_parse = parse_base_statement();
            if (Current_parse_token_type == T_SEMICOLON)
            {
                //clear out the tokens at the end of a statement
                type_checker->clear_tokens(false);
                Current_parse_token = Get_Valid_Token();
            }
            else
            {
                if (valid_parse)
                {
                    generate_error_report_previous_token("Missing \";\" to end statement in if statement");
                    errors_occured = true;
                }
                valid_parse = resync_parser(state);
                //if have run out of tokens
                if (Current_parse_token_type == T_INVALID)
                {
                    if (Lexer->is_nested_commented)
                    {
                        generate_error_report("Unclosed block comment detected", Lexer->nested_comment_line);
                        errors_occured = true;
                    }
                    return false;
                }
            }
            if (token_generation == iteration_start && Current_parse_token_type != T_INVALID &&
                Current_parse_token_type != T_END && Current_parse_token_type != T_IF)
            {
                Current_parse_token = Get_Valid_Token();
            }
            //conditions to break loop
            if (valid_parse)
            {
                if (Current_parse_token_type == T_END || Current_parse_token_type == T_INVALID)
                {
                    break;
                }
                if (Current_parse_token_type == T_IF && Next_parse_token_type != T_LPARAM)
                {
                    break;
                }
            }
            else
            {
                valid_parse = resync_parser(state);
                if (Current_parse_token_type == T_INVALID)
                {
                    return false;
                }
            }
        }
        if (Current_parse_token_type == T_END)
        {
            Current_parse_token = Get_Valid_Token();
        }
        else
        {
            generate_error_report_previous_token("Missing keyword \"end\" to end if statement");
            errors_occured = true;
            //return false;
        }
        if (Current_parse_token_type == T_IF)
        {
            Current_parse_token = Get_Valid_Token();
        }
        else
        {
            generate_error_report_previous_token("Missing keyword \"if\"to end if statement");
            errors_occured = true;
            //return false;
        }
    }
    else
    {
        if (debugging)
        {
            std::cout << "parser failed on parse_if_statement()" << std::endl;
        }
        generate_error_report_previous_token("Missing \"(\" expected for if statment");
        errors_occured = true;
        return false;
    }

    return valid_parse;
}

//ready to test
//consumes for token before entering this function
//refactored 2 times
bool parser::parse_loop_statement()
{
    token updated_token;
    token_and_status expression_parse;
    //this tracks the state of the parser
    parser_state state = S_LOOP_STATEMENT;
    bool valid_parse = false;
    if (Current_parse_token_type == T_LPARAM)
    {
        //grabs what should be an identifier
        Current_parse_token = Get_Valid_Token();
        if (Current_parse_token_type == T_IDENTIFIER)
        {
            const token destination_occurrence = Current_parse_token;
            token destination;
            const bool destination_resolved = resolve_identifier_use(destination_occurrence,
                                                                     destination);
            Current_parse_token = Get_Valid_Token();
            valid_parse = parse_assignment_statement(destination_resolved ? destination :
                                                                            destination_occurrence);
            if (Current_parse_token_type == T_SEMICOLON)
            {
                Current_parse_token = Get_Valid_Token();
                //COME BACK
                expression_parse = parse_expression();
                updated_token = expression_parse.resolved_token;
                if (expression_parse.valid_parse && expression_parse.semantic_valid &&
                    !type_checker->statement_suppressed)
                {
                    type_checker->check_if_statement(updated_token);
                }
                valid_parse = expression_parse.valid_parse;
                if (Current_parse_token_type == T_RPARAM)
                {
                    Current_parse_token = Get_Valid_Token();
                    while (Current_parse_token_type != T_END)
                    {
                        std::size_t iteration_start = token_generation;
                        valid_parse = parse_base_statement();
                        if (Current_parse_token_type == T_SEMICOLON)
                        {
                            //clear out the tokens at the end of a statement
                            type_checker->clear_tokens(false);
                            Current_parse_token = Get_Valid_Token();
                        }
                        else
                        {
                            if (valid_parse)
                            {
                                generate_error_report_previous_token("Missing \";\" to end statement in loop statement");
                                errors_occured = true;
                            }
                            valid_parse = resync_parser(state);
                            //if have run out of tokens
                            if (Current_parse_token_type == T_INVALID)
                            {
                                if (Lexer->is_nested_commented)
                                {
                                    generate_error_report("Unclosed block comment detected", Lexer->nested_comment_line);
                                    errors_occured = true;
                                }
                                return false;
                            }
                        }
                        if (token_generation == iteration_start && Current_parse_token_type != T_INVALID &&
                            Current_parse_token_type != T_END && Current_parse_token_type != T_FOR)
                        {
                            Current_parse_token = Get_Valid_Token();
                        }
                        if (valid_parse)
                        {
                            if (Current_parse_token_type == T_END || Current_parse_token_type == T_INVALID)
                            {
                                break;
                            }
                            if (Current_parse_token_type == T_FOR && Next_parse_token_type != T_LPARAM)
                            {
                                break;
                            }
                        }
                        else
                        {
                            valid_parse = resync_parser(state);
                            if (Current_parse_token_type == T_INVALID)
                            {
                                return false;
                            }
                        }
                    }
                    if (Current_parse_token_type == T_END)
                    {
                        Current_parse_token = Get_Valid_Token();
                        if (Current_parse_token_type == T_FOR)
                        {
                            Current_parse_token = Get_Valid_Token();
                        }
                        else
                        {
                            generate_error_report_previous_token("Missing expected keyword \"for\" for end of statement");
                            errors_occured = true;
                        }
                    }
                    else
                    {
                        generate_error_report_previous_token("Missing expected keyword \"end\" for end of statement");
                        errors_occured = true;
                    }
                }
                else
                {
                    generate_error_report_previous_token("Missing \")\" for loop declaration");
                    errors_occured = true;
                }
            }
            else
            {
                generate_error_report_previous_token("Missing \";\" for loop assignment statement");
                errors_occured = true;
            }
        }
        else
        {
            generate_error_report("Missing expeceted identifier for assignment statement");
            errors_occured = true;
        }
    }
    else
    {
        generate_error_report_previous_token("Missing \"(\" required for loop");
        errors_occured = true;
        //return false;
    }

    return valid_parse;
}

//ready to test
//consumes return token before entering function
//refactored 1 time
bool parser::parse_return_statement()
{
    token updated_token;
    token_and_status expression_parse;
    //this tracks the state of the parser
    bool valid_parse;
    expression_parse = parse_expression();
    updated_token = expression_parse.resolved_token;
    if (expression_parse.valid_parse && expression_parse.semantic_valid &&
        !type_checker->statement_suppressed)
    {
        token owner;
        if (Lexer->symbol_table.lookup_scope_owner(current_scope_id, owner))
        {
            type_checker->check_return_statement(updated_token, owner);
        }
    }
    valid_parse = expression_parse.valid_parse;
    return valid_parse;
}

//ready to test
//already consumes identifier before parsing
//refactored 1 time
token_and_status parser::parse_assignment_destination(token destination_token)
{
    token_and_status destination_parse;
    token_and_status expression_parse;
    bool valid_parse;
    const bool resolved_variable = destination_token.identifer_type == I_VARIABLE;
    const bool indexed = Current_parse_token_type == T_LBRACKET;
    if (resolved_variable && destination_token.identifier_data_type == TYPE_NONE)
    {
        generate_error_report("Identifier \"" + destination_token.stringValue +
                                  "\" has no resolved type",
                              destination_token.line_found);
        errors_occured = true;
        type_checker->suppress_current_statement();
    }
    if (resolved_variable && destination_token.identifier_data_type != TYPE_NONE &&
        !type_checker->statement_suppressed)
    {
        destination_parse = typed_expression_result(type_checker,
                                                    destination_token.identifier_data_type,
                                                    destination_token);
    }
    //this means that the optional bracketed expression should exist
    if (Current_parse_token_type == T_LBRACKET)
    {
        //Indexed uses are occurrences, not declarations.  Array declaration
        //metadata remains canonical and is not mutated by an expression use.
        Current_parse_token = Get_Valid_Token();
        expression_parse = parse_expression();
        valid_parse = expression_parse.valid_parse;
        //after parsing the expression, it should have a right bracket
        if (Current_parse_token_type == T_RBRACKET)
        {
            Current_parse_token = Get_Valid_Token();
        }
        //required right bracket missing
        else
        {
            if (debugging)
            {
                std::cout << "parser failed on parse_assignment_destination()" << std::endl;
            }
            generate_error_report_previous_token("Missing closing right bracket to the identifier expression");
            errors_occured = true;
            destination_parse.valid_parse = false;
            return destination_parse;
        }
    }
    else
    {
        valid_parse = true;
    }
    destination_parse.valid_parse = valid_parse;
    destination_parse.semantic_valid = destination_parse.semantic_valid &&
                                       (!indexed || expression_parse.semantic_valid) &&
                                       !type_checker->statement_suppressed;
    if (!destination_parse.semantic_valid)
    {
        destination_parse.resolved_token = token();
    }
    return destination_parse;
}

//ready to test
//consumes a token before entering this function
//all expressions start be thought to start with a ArithOp?
token_and_status parser::parse_expression()
{
    expression_depth++;
    struct expression_depth_guard
    {
        std::size_t &depth;
        ~expression_depth_guard()
        {
            depth--;
        }
    } depth_guard{expression_depth};
    token_and_status expression_parse;
    token_and_status right_parse;
    bool has_leading_not = false;
    token not_token;

    //A leading logical operator has no left operand.  Consume the attempted
    //right side so recovery can continue, but never offer the prefix token to
    //the semantic layer.
    if (Current_parse_token_type == T_AMPERSAND || Current_parse_token_type == T_VERTICAL_BAR)
    {
        const token operator_token = Current_parse_token;
        const bool is_and = Current_parse_token_type == T_AMPERSAND;
        Current_parse_token = Get_Valid_Token();
        const bool nested_logical_prefix = Current_parse_token_type == T_AMPERSAND ||
                                           Current_parse_token_type == T_VERTICAL_BAR;
        if (nested_logical_prefix)
        {
            (void)parse_expression();
        }
        else
        {
            (void)parse_arithOp();
        }
        generate_error_report(is_and ? "Missing left operand before \"&\" operator" :
                                       "Missing left operand before \"|\" operator",
                              operator_token.line_found);
        errors_occured = true;
        if (!nested_logical_prefix && expression_depth == 1 &&
            !type_checker->statement_suppressed)
        {
            generate_error_report("Error in expression", operator_token.line_found);
        }
        return invalid_expression_result(false);
    }

    if (Current_parse_token_type == T_NOT)
    {
        has_leading_not = true;
        not_token = Current_parse_token;
        Current_parse_token = Get_Valid_Token();
    }

    expression_parse = parse_arithOp();
    if (has_leading_not && expression_parse.valid_parse)
    {
        if (expression_parse.semantic_valid && !type_checker->statement_suppressed)
        {
            expression_parse = type_checker->check_unary_expression(SEM_NOT, not_token,
                                                                       expression_parse.resolved_token);
        }
        else
        {
            expression_parse.semantic_valid = false;
            expression_parse.resolved_token = token();
        }
    }

    //The grammar gives '&' and '|' equal precedence.  Each right operand is
    //<arithOp>, not a new expression; the double-operator recovery above is
    //the sole deliberate exception for a focused missing-left diagnostic.
    while (expression_parse.valid_parse &&
           (Current_parse_token_type == T_AMPERSAND || Current_parse_token_type == T_VERTICAL_BAR))
    {
        const token operator_token = Current_parse_token;
        const semantic_operator operation = Current_parse_token_type == T_AMPERSAND ?
                                                SEM_AND : SEM_OR;
        Current_parse_token = Get_Valid_Token();
        if (Current_parse_token_type == T_AMPERSAND || Current_parse_token_type == T_VERTICAL_BAR)
        {
            right_parse = parse_expression();
        }
        else
        {
            right_parse = parse_arithOp();
        }
        if (!right_parse.valid_parse)
        {
            expression_parse.valid_parse = false;
            expression_parse.semantic_valid = false;
            expression_parse.resolved_token = token();
            break;
        }
        if (expression_parse.semantic_valid && right_parse.semantic_valid &&
            !type_checker->statement_suppressed)
        {
            expression_parse = type_checker->check_binary_expression(
                operation, operator_token, expression_parse.resolved_token,
                right_parse.resolved_token);
        }
        else
        {
            expression_parse.semantic_valid = false;
            expression_parse.resolved_token = token();
        }
    }

    if (!expression_parse.valid_parse && !type_checker->statement_suppressed)
    {
        generate_error_report("Error in expression");
        errors_occured = true;
    }
    return expression_parse;
}

//ready to test
//consumes a token before entering this function
//all arithOps can be thought to starts with relations?
token_and_status parser::parse_arithOp()
{
    token_and_status arithop_parse;
    token_and_status right_parse;

    if (Current_parse_token_type == T_PLUS)
    {
        const token operator_token = Current_parse_token;
        Current_parse_token = Get_Valid_Token();
        (void)parse_relation();
        generate_error_report("Missing left operand before \"+\" operator",
                              operator_token.line_found);
        errors_occured = true;
        return invalid_expression_result(false);
    }

    arithop_parse = parse_relation();
    while (arithop_parse.valid_parse &&
           (Current_parse_token_type == T_PLUS || Current_parse_token_type == T_MINUS))
    {
        const token operator_token = Current_parse_token;
        const semantic_operator operation = Current_parse_token_type == T_PLUS ?
                                                SEM_ADD : SEM_SUBTRACT;
        Current_parse_token = Get_Valid_Token();
        right_parse = parse_relation();
        if (!right_parse.valid_parse)
        {
            arithop_parse.valid_parse = false;
            arithop_parse.semantic_valid = false;
            arithop_parse.resolved_token = token();
            break;
        }
        if (arithop_parse.semantic_valid && right_parse.semantic_valid &&
            !type_checker->statement_suppressed)
        {
            arithop_parse = type_checker->check_binary_expression(
                operation, operator_token, arithop_parse.resolved_token,
                right_parse.resolved_token);
        }
        else
        {
            arithop_parse.semantic_valid = false;
            arithop_parse.resolved_token = token();
        }
    }
    return arithop_parse;
}

//ready to test
//consumes a token before entering this function
//all arithOps can be thought to starts with terms?
token_and_status parser::parse_relation()
{
    token_and_status relation_parse;
    token_and_status right_parse;

    //Prefix relations are always syntax errors.  Still consume the complete
    //operator spelling and one attempted term so statement recovery advances.
    if (is_relation_start(Current_parse_token_type))
    {
        const token operator_token = Current_parse_token;
        const int operator_type = Current_parse_token_type;
        Current_parse_token = Get_Valid_Token();
        bool valid_operator = true;
        std::string message;
        if (operator_type == T_LESS || operator_type == T_GREATER)
        {
            if (Current_parse_token_type == T_ASSIGN)
            {
                Current_parse_token = Get_Valid_Token();
            }
            message = operator_type == T_LESS ?
                          "Missing left operand before \"<\" operator" :
                          "Missing left operand before \">\" operator";
        }
        else if (operator_type == T_ASSIGN)
        {
            if (Current_parse_token_type == T_ASSIGN)
            {
                Current_parse_token = Get_Valid_Token();
                message = "Missing left operand before \"==\" operator";
            }
            else
            {
                valid_operator = false;
                message = "\"=\" is not a valid relational operator, did you mean \"==\"";
            }
        }
        else
        {
            if (Current_parse_token_type == T_ASSIGN)
            {
                Current_parse_token = Get_Valid_Token();
                message = "Missing left operand before \"!=\" operator";
            }
            else
            {
                valid_operator = false;
                message = "Invalid relational operator detected";
            }
        }
        if (valid_operator)
        {
            (void)parse_term();
        }
        generate_error_report(message, operator_token.line_found);
        errors_occured = true;
        return invalid_expression_result(false);
    }

    relation_parse = parse_term();
    while (relation_parse.valid_parse && is_relation_start(Current_parse_token_type))
    {
        const token operator_token = Current_parse_token;
        const int operator_type = Current_parse_token_type;
        semantic_operator operation = SEM_LESS;
        bool valid_operator = true;
        Current_parse_token = Get_Valid_Token();

        if (operator_type == T_LESS)
        {
            operation = SEM_LESS;
            if (Current_parse_token_type == T_ASSIGN)
            {
                operation = SEM_LESS_EQUAL;
                Current_parse_token = Get_Valid_Token();
            }
        }
        else if (operator_type == T_GREATER)
        {
            operation = SEM_GREATER;
            if (Current_parse_token_type == T_ASSIGN)
            {
                operation = SEM_GREATER_EQUAL;
                Current_parse_token = Get_Valid_Token();
            }
        }
        else if (operator_type == T_ASSIGN)
        {
            if (Current_parse_token_type == T_ASSIGN)
            {
                operation = SEM_EQUAL;
                Current_parse_token = Get_Valid_Token();
            }
            else
            {
                valid_operator = false;
                generate_error_report("Not a valid relational operator", operator_token.line_found);
            }
        }
        else if (Current_parse_token_type == T_ASSIGN)
        {
            operation = SEM_NOT_EQUAL;
            Current_parse_token = Get_Valid_Token();
        }
        else
        {
            valid_operator = false;
            generate_error_report("Invalid relational operator detected", operator_token.line_found);
        }

        if (!valid_operator)
        {
            errors_occured = true;
            relation_parse.valid_parse = false;
            relation_parse.semantic_valid = false;
            relation_parse.resolved_token = token();
            break;
        }
        right_parse = parse_term();
        if (!right_parse.valid_parse)
        {
            relation_parse.valid_parse = false;
            relation_parse.semantic_valid = false;
            relation_parse.resolved_token = token();
            break;
        }
        if (relation_parse.semantic_valid && right_parse.semantic_valid &&
            !type_checker->statement_suppressed)
        {
            relation_parse = type_checker->check_binary_expression(
                operation, operator_token, relation_parse.resolved_token,
                right_parse.resolved_token);
        }
        else
        {
            relation_parse.semantic_valid = false;
            relation_parse.resolved_token = token();
        }
    }
    return relation_parse;
}

//ready to test
//already consumes a token before being parsed
token_and_status parser::parse_term()
{
    token_and_status term_parse;
    token_and_status right_parse;
    if (Current_parse_token_type == T_MULT || Current_parse_token_type == T_SLASH)
    {
        const token operator_token = Current_parse_token;
        const int operator_type = Current_parse_token_type;
        Current_parse_token = Get_Valid_Token();

        //Consume repeated multiplicative prefixes before parsing the attempted
        //right-hand factor.  This preserves forward progress for inputs such
        //as `* * 2` without treating any invalid prefix as a semantic token.
        while (Current_parse_token_type == T_MULT || Current_parse_token_type == T_SLASH)
        {
            Current_parse_token = Get_Valid_Token();
        }
        (void)parse_factor();
        if (operator_type == T_MULT)
        {
            generate_error_report("Missing left operand before \"*\" operator",
                                  operator_token.line_found);
        }
        else
        {
            generate_error_report("Missing left operand before \"/\" operator",
                                  operator_token.line_found);
        }
        errors_occured = true;
        return invalid_expression_result(false);
    }
    term_parse = parse_factor();

    // <term> ::= <term> (*|/) <factor> | <factor>.  Consume every valid
    // following multiplicative operator left-to-right.
    while (term_parse.valid_parse &&
           (Current_parse_token_type == T_MULT || Current_parse_token_type == T_SLASH))
    {
        const token operator_token = Current_parse_token;
        const semantic_operator operation = Current_parse_token_type == T_MULT ?
                                                SEM_MULTIPLY : SEM_DIVIDE;
        Current_parse_token = Get_Valid_Token();
        right_parse = parse_factor();
        if (!right_parse.valid_parse)
        {
            term_parse.valid_parse = false;
            term_parse.semantic_valid = false;
            term_parse.resolved_token = token();
            break;
        }
        if (term_parse.semantic_valid && right_parse.semantic_valid &&
            !type_checker->statement_suppressed)
        {
            term_parse = type_checker->check_binary_expression(
                operation, operator_token, term_parse.resolved_token,
                right_parse.resolved_token);
        }
        else
        {
            term_parse.semantic_valid = false;
            term_parse.resolved_token = token();
        }
    }
    return term_parse;
}

//ready to test
//already consumes a token before being parsed
token_and_status parser::parse_factor()
{
    token_and_status expression_parse;
    token_and_status factor_parse;
    token identifier_token;
    if (Current_parse_token_type == T_LPARAM)
    {
        Current_parse_token = Get_Valid_Token();
        expression_parse = parse_expression();
        if (Current_parse_token_type == T_RPARAM)
        {
            Current_parse_token = Get_Valid_Token();
            //Parentheses are syntax only; their expression type is the inner
            //result, including its source anchor.
            return expression_parse;
        }
        generate_error_report_previous_token("Missing \")\" to close expresssion factor");
        errors_occured = true;
        return invalid_expression_result(false);
    }
    if (Current_parse_token_type == T_IDENTIFIER)
    {
        if (Next_parse_token_type == T_LPARAM)
        {
            const token callee_occurrence = Current_parse_token;
            token callee;
            bool callee_resolved = false;
            token_and_status callee_result;
            callee_result.valid_parse = true;
            if (resolve_procedure_use(callee_occurrence, callee))
            {
                callee_resolved = true;
                if (callee.identifier_data_type == TYPE_NONE)
                {
                    if (!type_checker->statement_suppressed)
                    {
                        generate_error_report("Procedure \"" + callee_occurrence.stringValue +
                                                  "\" has no resolved type",
                                              callee_occurrence.line_found);
                        errors_occured = true;
                        type_checker->suppress_current_statement();
                    }
                }
                else
                {
                    callee_result = typed_expression_result(type_checker,
                                                            callee.identifier_data_type,
                                                            callee_occurrence);
                }
            }
            Current_parse_token = Get_Valid_Token();
            return parse_procedure_call(callee_occurrence, callee, callee_resolved,
                                        callee_result);
        }
        identifier_token = Current_parse_token;
        Current_parse_token = Get_Valid_Token();
        return parse_name(identifier_token);
    }
    if (Current_parse_token_type == T_MINUS)
    {
        const token operator_token = Current_parse_token;
        Current_parse_token = Get_Valid_Token();
        if (Current_parse_token_type == T_INTEGER_VALUE || Current_parse_token_type == T_FLOAT_VALUE)
        {
            factor_parse = typed_expression_result(
                type_checker,
                Current_parse_token_type == T_INTEGER_VALUE ? TYPE_INT : TYPE_FLOAT,
                Current_parse_token);
            Current_parse_token = Get_Valid_Token();
            return type_checker->check_unary_expression(SEM_NEGATE, operator_token,
                                                         factor_parse.resolved_token);
        }
        if (Current_parse_token_type == T_IDENTIFIER)
        {
            identifier_token = Current_parse_token;
            Current_parse_token = Get_Valid_Token();
            factor_parse = parse_name(identifier_token);
            if (!factor_parse.valid_parse)
            {
                return factor_parse;
            }
            if (factor_parse.semantic_valid && !type_checker->statement_suppressed)
            {
                return type_checker->check_unary_expression(SEM_NEGATE, operator_token,
                                                             factor_parse.resolved_token);
            }
            return invalid_expression_result(true);
        }
        generate_error_report("Unexpected negative factor is not a name or a number",
                              operator_token.line_found);
        errors_occured = true;
        return invalid_expression_result(false);
    }
    if (Current_parse_token_type == T_INTEGER_VALUE || Current_parse_token_type == T_FLOAT_VALUE)
    {
        factor_parse = typed_expression_result(
            type_checker,
            Current_parse_token_type == T_INTEGER_VALUE ? TYPE_INT : TYPE_FLOAT,
            Current_parse_token);
        Current_parse_token = Get_Valid_Token();
        return factor_parse;
    }
    if (Current_parse_token_type == T_STRING_VALUE)
    {
        if (Lexer->quote_status)
        {
            generate_error_report("quotation left open", Lexer->quote_opener);
        }
        factor_parse = typed_expression_result(type_checker, TYPE_STRING, Current_parse_token);
        Current_parse_token = Get_Valid_Token();
        return factor_parse;
    }
    if (Current_parse_token_type == T_TRUE || Current_parse_token_type == T_FALSE)
    {
        factor_parse = typed_expression_result(type_checker, TYPE_BOOL, Current_parse_token);
        Current_parse_token = Get_Valid_Token();
        return factor_parse;
    }
    generate_error_report("Invalid token for factor discovered");
    errors_occured = true;
    return invalid_expression_result(false);
}

//ready to test
//already consumes indentifier token before being parsed
token_and_status parser::parse_name(token identifier_token)
{
    token_and_status expression_parse;
    token_and_status name_parse;
    token resolved_identifier;
    const bool resolved = resolve_identifier_use(identifier_token, resolved_identifier);
    if (resolved)
    {
        if (resolved_identifier.identifier_data_type == TYPE_NONE)
        {
            if (!type_checker->statement_suppressed)
            {
                generate_error_report("Identifier \"" + identifier_token.stringValue +
                                          "\" has no resolved type",
                                      identifier_token.line_found);
                errors_occured = true;
                type_checker->suppress_current_statement();
            }
        }
        else
        {
            name_parse = typed_expression_result(type_checker,
                                                 resolved_identifier.identifier_data_type,
                                                 identifier_token);
        }
    }
    const bool indexed = Current_parse_token_type == T_LBRACKET;
    if (indexed)
    {
        Current_parse_token = Get_Valid_Token();
        expression_parse = parse_expression();
        if (Current_parse_token_type == T_RBRACKET)
        {
            Current_parse_token = Get_Valid_Token();
        }
        else
        {
            generate_error_report_previous_token("Missing require \"]\" for the end of optional expression for name");
            errors_occured = true;
            return invalid_expression_result(false);
        }
        name_parse.valid_parse = expression_parse.valid_parse;
        name_parse.semantic_valid = name_parse.semantic_valid && expression_parse.semantic_valid &&
                                    !type_checker->statement_suppressed;
        if (!name_parse.semantic_valid)
        {
            name_parse.resolved_token = token();
        }
    }
    if (!resolved)
    {
        name_parse.valid_parse = true;
        name_parse.semantic_valid = false;
        name_parse.resolved_token = token();
    }
    return name_parse;
}

//ready to test
//consumes one token before starting
bool parser::parse_argument_list(std::vector<token_and_status> &arguments)
{
    token_and_status expression_parse;
    bool valid_parse = true;
    expression_parse = parse_expression();
    arguments.push_back(expression_parse);
    valid_parse = expression_parse.valid_parse;
    while (Current_parse_token_type == T_COMMA)
    {
        Current_parse_token = Get_Valid_Token();
        expression_parse = parse_expression();
        arguments.push_back(expression_parse);
        valid_parse = expression_parse.valid_parse && valid_parse;
    }

    return valid_parse;
}

//ready to test
//already consumes identifier token before parsing
token_and_status parser::parse_procedure_call(const token &callee_occurrence,
                                              const token &canonical_callee,
                                              bool callee_resolved,
                                              const token_and_status &callee_result)
{
    token_and_status call_parse;
    call_parse.valid_parse = true;
    std::vector<token_and_status> arguments;
    if (Current_parse_token_type == T_LPARAM)
    {
        Current_parse_token = Get_Valid_Token();
        if (Current_parse_token_type == T_RPARAM)
        {
            Current_parse_token = Get_Valid_Token();
        }
        else
        {
            call_parse.valid_parse = parse_argument_list(arguments);
            if (Current_parse_token_type == T_RPARAM)
            {
                Current_parse_token = Get_Valid_Token();
            }
            //missing needed right param for the end of a procedure call
            else
            {
                generate_error_report_previous_token("Missing required \")\" for the end of a procedure call");
                errors_occured = true;
                return invalid_expression_result(false);
            }
        }
    }
    //missing needed left param for the start of the procedure call
    else
    {
        generate_error_report_previous_token("Missing required \"(\" for the end of a procedure call");
        errors_occured = true;
        return invalid_expression_result(false);
    }
    if (!call_parse.valid_parse || !callee_resolved ||
        canonical_callee.identifer_type != I_PROCEDURE || !callee_result.semantic_valid ||
        type_checker->statement_suppressed)
    {
        call_parse.semantic_valid = false;
        call_parse.resolved_token = token();
        return call_parse;
    }
    for (std::size_t i = 0; i < arguments.size(); i++)
    {
        if (!arguments[i].valid_parse || !arguments[i].semantic_valid)
        {
            call_parse.semantic_valid = false;
            call_parse.resolved_token = token();
            return call_parse;
        }
    }
    if (!type_checker->validate_procedure_call(canonical_callee, callee_occurrence,
                                               arguments))
    {
        call_parse.semantic_valid = false;
        call_parse.resolved_token = token();
        return call_parse;
    }
    //The call return remains a synthetic expression result.  The declaration
    //identity and signature stay in canonical_callee for validation only.
    call_parse = callee_result;
    call_parse.resolved_token.line_found = callee_occurrence.line_found;
    call_parse.resolved_token.column_found = callee_occurrence.column_found;
    call_parse.resolved_token.first_token_on_line = callee_occurrence.first_token_on_line;
    return call_parse;
}

bool parser::resync_parser(parser_state state)
{
    int temp_token_type;
    resync_status = true;
    struct resync_status_guard
    {
        bool &status;
        ~resync_status_guard()
        {
            status = false;
        }
    } status_guard{resync_status};
    //state to return
    bool return_state = false;
    //may be used to call proper parse function if needed
    parser_state new_state = state;
    //will be used to store the original state
    parser_state original_state;
    original_state = state;
    //consumes tokens until it can resync
    switch (state)
    {
    //1
    //not possible?
    case S_PROGRAM:

        break;

    //2
    case S_PROGRAM_HEADER:

        break;

    //3
    case S_PROGRAM_BODY:
        while (Current_parse_token_type != T_SEMICOLON && Current_parse_token_type != T_INVALID)
        {

            if (Current_parse_token_type == T_BEGIN)
            {
                new_state = original_state;
                break;
            }
            else if (Current_parse_token_type == T_END)
            {
                new_state = original_state;
                break;
            }
            else if (Current_parse_token_type == T_PROGRAM)
            {
                new_state = original_state;
                break;
            }
            else if (Current_parse_token_type == T_PERIOD)
            {
                new_state = original_state;
                break;
            }
            //DECLARATIONS

            if (prev_token_type == T_PROCEDURE &&
                Current_parse_token_type != T_PROCEDURE &&
                Current_parse_token_type != T_VARIABLE &&
                Current_parse_token_type != T_TYPE &&
                Current_parse_token_type != T_GLOBAL &&
                Current_parse_token_type != T_BEGIN &&
                Current_parse_token_type != T_END)
            {
                new_state = S_PROCEDURE_DECLARATION;
                break;
            }
            else if (prev_token_type == T_VARIABLE)
            {
                new_state = S_VARIABLE_DECLARATION;
                break;
            }
            else if (prev_token_type == T_TYPE)
            {
                new_state = S_TYPE_DECLARATION;
                break;
            }
            else if (prev_token_type == T_GLOBAL)
            {
                new_state = S_BASE_DECLARATION;
                break;
            }

            if (Current_parse_token_type == T_PROCEDURE)
            {
                new_state = S_BASE_DECLARATION;
                break;
            }
            else if (Current_parse_token_type == T_VARIABLE)
            {
                new_state = S_BASE_DECLARATION;
                break;
            }
            else if (Current_parse_token_type == T_TYPE)
            {
                new_state = S_BASE_DECLARATION;
            }

            //STATEMENTS

            //is a an assignment statement
            if (Current_parse_token_type == T_IDENTIFIER)
            {
                new_state = S_BASE_STATEMENT;
                break;
            }
            else if (Current_parse_token_type == T_IF)
            {
                new_state = S_BASE_STATEMENT;
                break;
            }
            else if (Current_parse_token_type == T_FOR)
            {
                new_state = S_BASE_STATEMENT;
                break;
            }
            else if (Current_parse_token_type == T_RETURN)
            {
                new_state = S_BASE_STATEMENT;
                break;
            }

            //SEMICOLONS
            Current_parse_token = Get_Valid_Token();
            if (Current_parse_token_type == T_SEMICOLON)
            {
                Current_parse_token = Get_Valid_Token();
                new_state = original_state;
                break;
            }
        }

        break;

    //4
    case S_BASE_DECLARATION:

        break;

    //5
    case S_PROCEDURE_DECLARATION:

        break;

    //6
    case S_PROCEDURE_HEADER:
        new_state = original_state;

        break;

    //7
    case S_PARAMETER_LIST:

        break;

    //8
    case S_PARAMETER:

        break;

    //9
    case S_PROCEDURE_BODY:
        while (Current_parse_token_type != T_SEMICOLON && Current_parse_token_type != T_INVALID)
        {

            if (Current_parse_token_type == T_BEGIN)
            {
                new_state = original_state;
                break;
            }
            else if (Current_parse_token_type == T_END)
            {
                new_state = original_state;
                break;
            }
            else if (Current_parse_token_type == T_PROCEDURE)
            {
                new_state = original_state;
                break;
            }

            //DECLARATIONS

            if (prev_token_type == T_PROCEDURE &&
                Current_parse_token_type != T_PROCEDURE &&
                Current_parse_token_type != T_VARIABLE &&
                Current_parse_token_type != T_TYPE &&
                Current_parse_token_type != T_GLOBAL &&
                Current_parse_token_type != T_BEGIN &&
                Current_parse_token_type != T_END)
            {
                new_state = S_PROCEDURE_DECLARATION;
                break;
            }
            else if (prev_token_type == T_VARIABLE)
            {
                new_state = S_VARIABLE_DECLARATION;
                break;
            }
            else if (prev_token_type == T_TYPE)
            {
                new_state = S_TYPE_DECLARATION;
                break;
            }
            else if (prev_token_type == T_GLOBAL)
            {
                new_state = S_BASE_DECLARATION;
                break;
            }

            if (Current_parse_token_type == T_PROCEDURE)
            {
                new_state = S_BASE_DECLARATION;
                break;
            }
            else if (Current_parse_token_type == T_VARIABLE)
            {
                if (prev_token_type == T_LPARAM)
                {
                    new_state = S_PARAMETER_LIST;
                    break;
                }
                else
                {
                    new_state = S_BASE_DECLARATION;
                    break;
                }
            }
            else if (Current_parse_token_type == T_TYPE)
            {
                new_state = S_BASE_DECLARATION;
                break;
            }

            //STATEMENTS

            //is a an assignment statement
            if (Current_parse_token_type == T_IDENTIFIER)
            {
                new_state = S_BASE_STATEMENT;
                break;
            }
            else if (Current_parse_token_type == T_IF)
            {
                new_state = S_BASE_STATEMENT;
                break;
            }
            else if (Current_parse_token_type == T_FOR)
            {
                new_state = S_BASE_STATEMENT;
                break;
            }
            else if (Current_parse_token_type == T_RETURN)
            {
                new_state = S_BASE_STATEMENT;
                break;
            }

            //SEMICOLONS
            Current_parse_token = Get_Valid_Token();
            if (Current_parse_token_type == T_SEMICOLON)
            {
                Current_parse_token = Get_Valid_Token();
                new_state = original_state;
                break;
            }
        }

        break;

    //10
    case S_VARIABLE_DECLARATION:

        break;

    //11
    case S_TYPE_DECLARATION:

        break;

    //12
    case S_TYPE_MARK:

        break;

    //13
    case S_BOUND:

        break;

    //14
    case S_BASE_STATEMENT:

        break;

    //15
    case S_PROCEDURE_CALL:

        break;

    //16
    case S_ASSIGNMENT_STATMENT:

        break;

    //17
    case S_ASSIGNMENT_DESTINATION:

        break;

    //18
    case S_IF_STATEMENT:
        while (Current_parse_token_type != T_SEMICOLON && Current_parse_token_type != T_INVALID)
        {
            if (Current_parse_token_type == T_IF)
            {
                new_state = S_BASE_STATEMENT;
                break;
            }
            // else if(Current_parse_token_type == T_THEN){
            //     Cure
            // }
            else if (Current_parse_token_type == T_FOR)
            {
                new_state = S_BASE_STATEMENT;
                break;
            }
            else if (Current_parse_token_type == T_RETURN)
            {
                new_state = S_BASE_STATEMENT;
                break;
            }
            // else if(Current_parse_token_type ==T_IDENTIFIER){
            //     new_state = S_BASE_DECLARATION;
            //     break;
            // }
            else if (Current_parse_token_type == T_END)
            {
                return true;
            }
            else if (Current_parse_token_type == T_IDENTIFIER)
            {
                new_state = S_BASE_STATEMENT;
                break;
            }

            Current_parse_token = Get_Valid_Token();
        }

        break;

    //19
    case S_LOOP_STATEMENT:
        while (Current_parse_token_type != T_SEMICOLON && Current_parse_token_type != T_INVALID)
        {
            if (Current_parse_token_type == T_IF)
            {
                new_state = S_BASE_STATEMENT;
                break;
            }
            else if (Current_parse_token_type == T_FOR)
            {
                new_state = S_BASE_STATEMENT;
                break;
            }
            else if (Current_parse_token_type == T_RETURN)
            {
                new_state = S_BASE_STATEMENT;
                break;
            }
            else if (Current_parse_token_type == T_END)
            {
                return true;
            }
            else if (Current_parse_token_type == T_IDENTIFIER)
            {
                new_state = S_BASE_STATEMENT;
                break;
            }

            Current_parse_token = Get_Valid_Token();
            // if(Current_parse_token_type ==T_SEMICOLON){
            //     Current_parse_token = Get_Valid_Token();
            //     return true;
            // }
        }

        break;

    //20
    case S_RETURN_STATEMENT:

        break;

    //21
    case S_EXPRESSION:

        break;

    //22
    case S_ARITH_OP:

        break;

    //23
    case S_RELATION:

        break;

    //24
    case S_TERM:

        break;

    //25
    case S_FACTOR:

        break;

    //26
    case S_NAME:

        break;

    //27
    case S_ARGUMENT_LIST:

        break;

    //28
    case S_NUMBER:

        break;

    //This should never happen
    default:
        std::cout << "Error in resync start state" << std::endl;
        break;
    }

    //Scanning is complete before a recovered production is dispatched, so its
    //own diagnostics must not be suppressed as recovery-internal noise.
    resync_status = false;
    //calls appropriate parse function based on the new state
    switch (new_state)
    {
    //1
    //not possible?
    case S_PROGRAM:

        break;

    //2
    case S_PROGRAM_HEADER:

        break;

    //3
    case S_PROGRAM_BODY:
        //return_state = parse_program_body();
        return_state = true;

        break;

    //4
    case S_BASE_DECLARATION:
        temp_token_type = Current_parse_token_type;
        return_state = parse_base_declaration();
        if (return_state)
        {
            if ((Current_parse_token_type == T_SEMICOLON) || (Current_parse_token_type == T_RPARAM && temp_token_type == T_VARIABLE))
            {
                Current_parse_token = Get_Valid_Token();
            }
            else
            {
                generate_error_report_previous_token("Missing \";\" to complete declaration");
                errors_occured = true;
                return_state = false;
            }
        }
        else
        {
        }

        break;

    //5
    case S_PROCEDURE_DECLARATION:
        return_state = parse_procedure_declaration(false);
        if (return_state)
        {
            if (Current_parse_token_type == T_SEMICOLON)
            {
                Current_parse_token = Get_Valid_Token();
            }
            else
            {
                generate_error_report_previous_token("Missing \";\" to complete declaration");
                errors_occured = true;
                return_state = false;
            }
        }
        else
        {
        }

        break;

    //6
    case S_PROCEDURE_HEADER:
        //Current_parse_token = Get_Valid_Token();
        return true;
        // return_state = parse_procedure_header();

        break;

    //7
    case S_PARAMETER_LIST:
        //THESE USED STILL?
    {
        std::vector<token> recovered_parameters;
        std::vector<token> recovered_header_symbols;
        return_state = parse_parameter_list(recovered_parameters, recovered_header_symbols);
    }
        return_state = parse_procedure_body();

        break;

    //8
    case S_PARAMETER:

        break;

    //9
    case S_PROCEDURE_BODY:

        break;

    //10
    case S_VARIABLE_DECLARATION:
        return_state = parse_variable_declaration(false);
        if (return_state)
        {
            if (Current_parse_token_type == T_SEMICOLON)
            {
                Current_parse_token = Get_Valid_Token();
            }
            else
            {
                generate_error_report_previous_token("Missing \";\" to complete declaration");
                errors_occured = true;
                return_state = false;
            }
        }
        else
        {
        }

        break;

    //11
    case S_TYPE_DECLARATION:
        return_state = parse_type_declaration(false);
        if (return_state)
        {
            if (Current_parse_token_type == T_SEMICOLON)
            {
                Current_parse_token = Get_Valid_Token();
            }
            else
            {
                generate_error_report_previous_token("Missing \";\" to complete declaration");
                errors_occured = true;
                return_state = false;
            }
        }
        else
        {
        }

        break;

    //12
    case S_TYPE_MARK:

        break;

    //13
    case S_BOUND:

        break;

    //14
    case S_BASE_STATEMENT:
        return_state = parse_base_statement();
        if (return_state)
        {
            if (Current_parse_token_type == T_SEMICOLON)
            {
                Current_parse_token = Get_Valid_Token();
            }
            else
            {
                generate_error_report_previous_token("Missing \";\" to end program statement");
                errors_occured = true;
                return_state = false;
            }
        }

        break;

    //15
    case S_PROCEDURE_CALL:

        break;

    //16
    case S_ASSIGNMENT_STATMENT:

        break;

    //17
    case S_ASSIGNMENT_DESTINATION:

        break;

    //18
    case S_IF_STATEMENT:

        break;

    //19
    case S_LOOP_STATEMENT:

        break;

    //20
    case S_RETURN_STATEMENT:

        break;

    //21
    case S_EXPRESSION:

        break;

    //22
    case S_ARITH_OP:

        break;

    //23
    case S_RELATION:

        break;

    //24
    case S_TERM:

        break;

    //25
    case S_FACTOR:

        break;

    //26
    case S_NAME:

        break;

    //27
    case S_ARGUMENT_LIST:

        break;

    //28
    case S_NUMBER:

        break;

    //This should never happen
    default:
        std::cout << "Error in resync start state" << std::endl;
        break;
    }
    return return_state;
}

void parser::update_scopes(bool increment_scope_id)
{
    if (increment_scope_id)
    {
        const int parent_scope_id = active_scope_ids.empty() ? 0 : active_scope_ids.back();
        const int new_scope_id = next_scope_id++;
        if (!Lexer->symbol_table.create_scope(new_scope_id, parent_scope_id, true))
        {
            generate_error_report("Unable to create a unique procedure scope");
            errors_occured = true;
            return;
        }
        active_scope_ids.push_back(new_scope_id);
    }
    else if (active_scope_ids.size() > 1)
    {
        active_scope_ids.pop_back();
    }
    current_scope_id = active_scope_ids.empty() ? 0 : active_scope_ids.back();
    number_of_scopes = active_scope_ids.empty() ? 0 :
                                             static_cast<int>(active_scope_ids.size()) - 1;
}

bool parser::declaration_target(bool explicitly_global) const
{
    return explicitly_global || current_scope_id == 0;
}

int parser::declaration_scope(bool explicitly_global) const
{
    return declaration_target(explicitly_global) ? 0 : current_scope_id;
}

void parser::report_duplicate_declaration(const token &occurrence)
{
    generate_error_report("Duplicate declaration for \"" + occurrence.stringValue + "\"",
                          occurrence.line_found);
    errors_occured = true;
}

bool parser::resolve_identifier_use(const token &occurrence, token &resolved_token,
                                    const std::string &kind)
{
    if (!Lexer->symbol_table.resolve_name(occurrence.stringValue, current_scope_id,
                                          resolved_token))
    {
        generate_error_report("Undeclared " + kind + " \"" + occurrence.stringValue + "\"",
                              occurrence.line_found);
        errors_occured = true;
        type_checker->suppress_current_statement();
        return false;
    }
    if (resolved_token.identifer_type != I_VARIABLE)
    {
        generate_error_report("Identifier \"" + occurrence.stringValue +
                                  "\" is not a variable",
                              occurrence.line_found);
        errors_occured = true;
        type_checker->suppress_current_statement();
        return false;
    }
    resolved_token.line_found = occurrence.line_found;
    resolved_token.column_found = occurrence.column_found;
    return true;
}

bool parser::resolve_procedure_use(const token &occurrence, token &resolved_token)
{
    if (!Lexer->symbol_table.resolve_name(occurrence.stringValue, current_scope_id,
                                          resolved_token))
    {
        generate_error_report("Undeclared procedure \"" + occurrence.stringValue + "\"",
                              occurrence.line_found);
        errors_occured = true;
        type_checker->suppress_current_statement();
        return false;
    }
    if (resolved_token.identifer_type != I_PROCEDURE)
    {
        generate_error_report("Identifier \"" + occurrence.stringValue +
                                  "\" is not a procedure",
                              occurrence.line_found);
        errors_occured = true;
        type_checker->suppress_current_statement();
        return false;
    }
    resolved_token.line_found = occurrence.line_found;
    resolved_token.column_found = occurrence.column_found;
    return true;
}
