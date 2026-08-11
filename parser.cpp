#include "parser.h"

#include <algorithm>

namespace
{

token_and_status invalid_expression_result(bool syntax_valid = true)
{
    token_and_status result;
    result.valid_parse = syntax_valid;
    result.semantic_valid = false;
    return result;
}

lowered_expression invalid_lowered_expression(bool syntax_valid = true)
{
    lowered_expression result;
    result.semantics = invalid_expression_result(syntax_valid);
    return result;
}

ir::UnaryOp ir_unary_operation(semantic_operator operation)
{
    return operation == SEM_NOT ? ir::UnaryOp::Not : ir::UnaryOp::Negate;
}

ir::BinaryOp ir_binary_operation(semantic_operator operation)
{
    switch (operation)
    {
    case SEM_ADD: return ir::BinaryOp::Add;
    case SEM_SUBTRACT: return ir::BinaryOp::Subtract;
    case SEM_MULTIPLY: return ir::BinaryOp::Multiply;
    case SEM_DIVIDE: return ir::BinaryOp::Divide;
    case SEM_LESS: return ir::BinaryOp::Less;
    case SEM_LESS_EQUAL: return ir::BinaryOp::LessEqual;
    case SEM_GREATER: return ir::BinaryOp::Greater;
    case SEM_GREATER_EQUAL: return ir::BinaryOp::GreaterEqual;
    case SEM_EQUAL: return ir::BinaryOp::Equal;
    case SEM_NOT_EQUAL: return ir::BinaryOp::NotEqual;
    case SEM_AND: return ir::BinaryOp::And;
    case SEM_OR: return ir::BinaryOp::Or;
    case SEM_NOT:
    case SEM_NEGATE:
        break;
    }
    return ir::BinaryOp::Add;
}

bool ir_cast_operation(conversion_kind kind, ir::CastOp &operation)
{
    switch (kind)
    {
    case conversion_kind::IntToFloat: operation = ir::CastOp::IntToFloat; return true;
    case conversion_kind::FloatToInt: operation = ir::CastOp::FloatToInt; return true;
    case conversion_kind::BoolToInt: operation = ir::CastOp::BoolToInt; return true;
    case conversion_kind::IntToBool: operation = ir::CastOp::IntToBool; return true;
    case conversion_kind::Invalid:
    case conversion_kind::Exact:
        return false;
    }
    return false;
}

token_and_status typed_expression_result(Typechecker *checker, const value_shape &shape,
                                         const token &anchor)
{
    return checker->make_shaped_expression_result(shape, anchor);
}

token_and_status typed_expression_result(Typechecker *checker, data_types type,
                                         const token &anchor)
{
    value_shape shape;
    shape.element_type = type;
    return typed_expression_result(checker, shape, anchor);
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
    ir_builder = new ir::IRBuilder();
    Lexer = new scanner(parse_file);
    type_checker = new Typechecker(this);
    valid_parse = parse_program();
    ir_builder->finalize(valid_parse);
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
    if (ir_builder != NULL)
    {
        ir_builder->mark_frontend_error();
    }
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

bool parser::frontend_valid() const noexcept
{
    return error_reports.empty();
}

bool parser::can_generate_code() const noexcept
{
    return ir_builder != NULL && ir_builder->status() == ir::ModuleStatus::Ready;
}

ir::ModuleStatus parser::ir_status() const noexcept
{
    return ir_builder == NULL ? ir::ModuleStatus::InvalidIR : ir_builder->status();
}

const ir::Module &parser::ir_module() const noexcept
{
    static const ir::Module empty;
    return ir_builder == NULL ? empty : ir_builder->module();
}

const std::string &parser::ir_reason() const noexcept
{
    static const std::string empty;
    return ir_builder == NULL ? empty : ir_builder->reason();
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
        if (ir_builder != NULL)
        {
            ir_builder->emit_halt();
        }
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
        else if (ir_builder != NULL)
        {
            const SymbolRef program_ref{0, Current_parse_token.stringValue};
            ir_builder->register_program(program_ref, Current_parse_token.stringValue);
            ir_builder->seed_external_builtins();
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
    bool entered_ir_function = false;

    //A recovery/body scope is always created and later popped exactly once.
    //It remains in SymbolTable for later code generation even after exit.
    update_scopes(true);
    const int body_scope_id = current_scope_id;
    bool header_semantic_valid = true;
    const bool header_syntax_valid = parse_procedure_header(is_global, candidate,
                                                            parameters, header_symbols,
                                                            header_semantic_valid);
    bool duplicate = false;
    if (!candidate.stringValue.empty())
    {
        duplicate = Lexer->symbol_table.has_declared(target_scope_id,
                                                      candidate.stringValue);
        if (duplicate)
        {
            report_duplicate_declaration(candidate);
            header_semantic_valid = false;
        }
    }

    if (header_syntax_valid && header_semantic_valid && !duplicate)
    {
        std::vector<token> body_symbols = header_symbols;
        body_symbols.insert(body_symbols.end(), parameters.begin(), parameters.end());
        if (!Lexer->symbol_table.can_declare_all(body_scope_id, body_symbols))
        {
            generate_error_report("Duplicate declaration in procedure header",
                                  candidate.line_found);
            errors_occured = true;
            header_semantic_valid = false;
        }
        else if (!Lexer->symbol_table.declare_symbol(target_scope_id, candidate))
        {
            report_duplicate_declaration(candidate);
            header_semantic_valid = false;
        }
        else
        {
            const SymbolRef procedure_ref{target_scope_id, candidate.stringValue};
            Lexer->symbol_table.set_scope_owner(body_scope_id, procedure_ref);
            Lexer->symbol_table.declare_all(body_scope_id, body_symbols);
            if (ir_builder != NULL && ir_builder->emission_enabled())
            {
                token canonical_procedure;
                std::vector<std::pair<SymbolRef, value_shape>> ir_parameters;
                if (Lexer->symbol_table.lookup_declared(procedure_ref, canonical_procedure))
                {
                    for (const token &parameter : parameters)
                    {
                        token canonical_parameter;
                        const SymbolRef parameter_ref{body_scope_id, parameter.stringValue};
                        if (Lexer->symbol_table.lookup_declared(parameter_ref,
                                                                canonical_parameter))
                        {
                            ir_parameters.push_back(std::make_pair(parameter_ref,
                                                                   shape_of(canonical_parameter)));
                        }
                    }
                    const ir::FunctionId function = ir_builder->register_procedure(
                        procedure_ref, canonical_procedure.stringValue,
                        shape_of(canonical_procedure), ir_parameters);
                    if (function.valid())
                    {
                        entered_ir_function = ir_builder->enter_function(function);
                    }
                }
            }
        }
    }

    const bool body_valid = parse_procedure_body();
    if (ir_builder != NULL && entered_ir_function)
    {
        (void)ir_builder->leave_function();
    }
    parsing_statements = false;
    update_scopes(false);
    (void)parent_scope_id;
    //A header with a bad bound or a duplicate name has still consumed a complete
    //declaration.  Leave its recovery scope ownerless, but do not make the
    //enclosing declaration loop resynchronise across its terminating semicolon.
    return header_syntax_valid && body_valid;
}

bool parser::parse_procedure_header(bool is_global, token &candidate,
                                    std::vector<token> &parameters,
                                    std::vector<token> &header_symbols,
                                    bool &semantic_valid)
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
    bool parameter_semantic_valid = true;
    if (!parse_parameter_list(parameters, header_symbols, parameter_semantic_valid))
    {
        return false;
    }
    semantic_valid = semantic_valid && parameter_semantic_valid;
    for (const token &parameter : parameters)
    {
        candidate.procedure_params.push_back(shape_of(parameter));
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
        const bool suppress_unreachable = ir_builder != NULL &&
            ir_builder->current_function() != ir_builder->program_function() &&
            !ir_builder->current_block_is_open();
        if (suppress_unreachable)
        {
            ir_builder->begin_unreachable_statement();
        }
        valid_parse = parse_base_statement();
        if (suppress_unreachable)
        {
            ir_builder->end_unreachable_statement();
        }
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

bool parser::parse_parameter(token &parameter, std::vector<token> &header_symbols,
                             bool &semantic_valid)
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
    return parse_array_suffix(parameter, semantic_valid);
}

bool parser::parse_parameter_list(std::vector<token> &parameters,
                                  std::vector<token> &header_symbols,
                                  bool &semantic_valid)
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
        bool parameter_semantic_valid = true;
        if (!parse_parameter(parameter, header_symbols, parameter_semantic_valid))
        {
            return false;
        }
        semantic_valid = semantic_valid && parameter_semantic_valid;
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
    bool array_semantic_valid = true;
    if (!parse_array_suffix(candidate, array_semantic_valid))
    {
        return false;
    }
    if (!array_semantic_valid)
    {
        return true;
    }
    std::vector<token> symbols;
    symbols.push_back(candidate);
    symbols.insert(symbols.end(), enum_symbols.begin(), enum_symbols.end());
    if (!Lexer->symbol_table.declare_all(target_scope_id, symbols))
    {
        report_duplicate_declaration(candidate);
        return false;
    }
    if (ir_builder != NULL && ir_builder->emission_enabled())
    {
        token canonical;
        const SymbolRef reference{target_scope_id, candidate.stringValue};
        token scope_owner;
        const bool live_context = current_scope_id == 0 ||
                                  Lexer->symbol_table.lookup_scope_owner(current_scope_id,
                                                                          scope_owner);
        if (live_context && Lexer->symbol_table.lookup_declared(reference, canonical))
        {
            const value_shape shape = shape_of(canonical);
            if (!ir::is_ready_type(shape))
            {
                ir_builder->mark_unsupported("arrays or unresolved declaration types need runtime lowering");
            }
            else
            {
                const ir::StorageKind kind = target_scope_id == 0 ?
                                                 ir::StorageKind::Global :
                                                 ir::StorageKind::Local;
                (void)ir_builder->register_storage(reference, shape, kind);
            }
        }
    }
    return true;
}

bool parser::parse_bound(int &upper_bound, bool &semantic_valid)
{
    const token bound_token = Current_parse_token;
    if (Current_parse_token_type == T_MINUS)
    {
        Current_parse_token = Get_Valid_Token();
        if (Current_parse_token_type != T_INTEGER_VALUE &&
            Current_parse_token_type != T_FLOAT_VALUE)
        {
            generate_error_report("Missing expected number");
            errors_occured = true;
            return false;
        }
        Current_parse_token = Get_Valid_Token();
        generate_error_report("Array upper bound must be a non-negative integer",
                              bound_token.line_found);
        errors_occured = true;
        semantic_valid = false;
        return true;
    }
    if (Current_parse_token_type == T_INTEGER_VALUE)
    {
        upper_bound = Current_parse_token.intValue;
        Current_parse_token = Get_Valid_Token();
        if (upper_bound < 0)
        {
            generate_error_report("Array upper bound must be a non-negative integer",
                                  bound_token.line_found);
            errors_occured = true;
            semantic_valid = false;
        }
        return true;
    }
    if (Current_parse_token_type == T_FLOAT_VALUE)
    {
        Current_parse_token = Get_Valid_Token();
        generate_error_report("Array upper bound must be a non-negative integer",
                              bound_token.line_found);
        errors_occured = true;
        semantic_valid = false;
        return true;
    }
    generate_error_report("Missing expected number");
    errors_occured = true;
    return false;
}

bool parser::parse_array_suffix(token &candidate, bool &semantic_valid)
{
    if (Current_parse_token_type != T_LBRACKET)
    {
        return true;
    }
    candidate.is_array = true;
    candidate.array_upper_bound = -1;
    if (ir_builder != NULL && ir_builder->emission_enabled())
    {
        ir_builder->mark_unsupported("arrays require bounds-check lowering");
    }
    Current_parse_token = Get_Valid_Token();
    if (!parse_bound(candidate.array_upper_bound, semantic_valid))
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
    return true;
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
    if (ir_builder != NULL && (candidate.identifier_data_type == TYPE_NONE ||
                               !enum_symbols.empty()))
    {
        ir_builder->mark_unsupported("enum and unresolved types need runtime lowering");
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
        const token if_token = Current_parse_token;
        type_checker->set_statement_type(if_token);
        Current_parse_token = Get_Valid_Token();
        valid_parse = parse_if_statement(if_token);
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
        const token return_token = Current_parse_token;
        type_checker->set_statement_type(return_token);
        Current_parse_token = Get_Valid_Token();
        valid_parse = parse_return_statement(return_token);
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
    lowered_expression expression_parse;
    lowered_destination destination_parse;
    //this tracks the state of the parser
    bool valid_parse;
    destination_parse = parse_assignment_destination(destination_token);
    valid_parse = destination_parse.semantics.valid_parse;
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
            valid_parse = expression_parse.semantics.valid_parse;
            if (valid_parse && destination_parse.semantics.semantic_valid &&
                expression_parse.semantics.semantic_valid && !type_checker->statement_suppressed)
            {
                const conversion_plan assignment_plan =
                    type_checker->check_assignment_statement(destination_parse.semantics,
                                                             expression_parse.semantics);
                if (assignment_plan.valid && ir_builder != NULL &&
                    destination_parse.storage.valid() && expression_parse.value.valid())
                {
                    ir::ValueId value = expression_parse.value;
                    if (assignment_plan.requires_conversion)
                    {
                        ir::CastOp cast;
                        if (ir_cast_operation(assignment_plan.kind, cast))
                        {
                            value = ir_builder->emit_cast(cast, value);
                        }
                    }
                    if (value.valid())
                    {
                        (void)ir_builder->emit_store(destination_parse.storage, value);
                    }
                }
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
bool parser::parse_if_statement(const token &if_token)
{
    struct IfBlocks
    {
        ir::BlockId then_block;
        ir::BlockId else_block;
        ir::BlockId join_block;
        bool active = false;
        bool procedure_cfg = false;
    } blocks;

    const bool has_left_parenthesis = Current_parse_token_type == T_LPARAM;
    bool consumed_right_parenthesis = false;
    bool consumed_then = false;
    conversion_plan condition_plan;
    if (has_left_parenthesis)
    {
        Current_parse_token = Get_Valid_Token();
    }
    else
    {
        generate_error_report_previous_token("Missing \"(\" expected for if statment");
        errors_occured = true;
    }
    const lowered_expression condition = parse_expression();
    if (!condition.semantics.valid_parse)
    {
        //The expression production already emitted the focused syntax error.
        //Consume only up to this if's delimiter so a closed malformed
        //conditional remains structurally owned by this parser.
        while (Current_parse_token_type != T_RPARAM && Current_parse_token_type != T_THEN &&
               Current_parse_token_type != T_END && Current_parse_token_type != T_INVALID)
        {
            Current_parse_token = Get_Valid_Token();
        }
        if (Current_parse_token_type == T_RPARAM)
        {
            Current_parse_token = Get_Valid_Token();
        }
    }
    else if (Current_parse_token_type == T_RPARAM)
    {
        Current_parse_token = Get_Valid_Token();
        consumed_right_parenthesis = true;
        if (has_left_parenthesis && condition.semantics.semantic_valid &&
            !type_checker->statement_suppressed)
        {
            condition_plan = type_checker->check_condition_statement(
                condition.semantics, if_token, condition_context::If);
        }
    }
    else if (has_left_parenthesis)
    {
        //Do not resynchronise across THEN: it is the next grammar delimiter
        //and retaining it avoids the historical duplicate missing-then error.
        generate_error_report_previous_token("Missing \")\" expected for if statment");
        errors_occured = true;
    }

    if (Current_parse_token_type != T_THEN)
    {
        if (condition.semantics.valid_parse)
        {
            generate_error_report_previous_token("Missing expected keyword \"then\" for if statements");
            errors_occured = true;
        }
    }
    else
    {
        Current_parse_token = Get_Valid_Token();
        consumed_then = true;
    }

    //Only a fully recognized, semantically valid header opens CFG blocks.
    //Malformed/recovered headers still use the handwritten branch parser below
    //and are discarded atomically through the frontend-error status.
    if (has_left_parenthesis && consumed_right_parenthesis && consumed_then &&
        condition.semantics.valid_parse && condition.semantics.semantic_valid &&
        condition_plan.valid && !type_checker->statement_suppressed && ir_builder != NULL &&
        ir_builder->emission_enabled() && condition.value.valid())
    {
        const bool procedure_cfg =
            ir_builder->current_function() != ir_builder->program_function();
        {
            ir::ValueId condition_value = condition.value;
            if (condition_plan.requires_conversion)
            {
                ir::CastOp cast;
                if (ir_cast_operation(condition_plan.kind, cast))
                {
                    condition_value = ir_builder->emit_cast(cast, condition_value);
                }
            }
            if (ir_builder->emission_enabled() && condition_value.valid())
            {
                blocks.then_block = ir_builder->create_block();
                blocks.else_block = ir_builder->create_block();
                //Program CFGs keep their established eager three-target
                //shape.  A procedure creates its join only for an arm that
                //actually falls through, avoiding an unreachable join after
                //two early returns.
                if (!procedure_cfg)
                {
                    blocks.join_block = ir_builder->create_block();
                }
                if (blocks.then_block.valid() && blocks.else_block.valid() &&
                    (procedure_cfg || blocks.join_block.valid()) &&
                    ir_builder->emit_branch(condition_value, blocks.then_block,
                                            blocks.else_block) &&
                    ir_builder->select_block(blocks.then_block))
                {
                    blocks.active = true;
                    blocks.procedure_cfg = procedure_cfg;
                }
            }
        }
    }

    const auto parse_branch = [this]() -> bool {
        while (Current_parse_token_type != T_ELSE && Current_parse_token_type != T_END &&
               Current_parse_token_type != T_INVALID)
        {
            const std::size_t iteration_start = token_generation;
            const bool suppress_unreachable = ir_builder != NULL &&
                ir_builder->current_function() != ir_builder->program_function() &&
                !ir_builder->current_block_is_open();
            if (suppress_unreachable)
            {
                ir_builder->begin_unreachable_statement();
            }
            const bool child_valid = parse_base_statement();
            if (suppress_unreachable)
            {
                ir_builder->end_unreachable_statement();
            }
            if (Current_parse_token_type == T_SEMICOLON)
            {
                type_checker->clear_tokens(false);
                Current_parse_token = Get_Valid_Token();
                continue;
            }
            if (Current_parse_token_type == T_ELSE || Current_parse_token_type == T_END)
            {
                if (child_valid)
                {
                    generate_error_report_previous_token(
                        "Missing \";\" to end statement in if statement");
                    errors_occured = true;
                }
                continue;
            }
            //A child syntax error owns its own diagnostic; do not add a
            //secondary missing-semicolon report while seeking this branch's
            //next delimiter.
            if (child_valid)
            {
                generate_error_report_previous_token(
                    "Missing \";\" to end statement in if statement");
                errors_occured = true;
            }
            (void)resync_parser(S_IF_STATEMENT);
            if (Current_parse_token_type == T_SEMICOLON)
            {
                Current_parse_token = Get_Valid_Token();
            }
            if (Current_parse_token_type == T_INVALID)
            {
                return false;
            }
            if (token_generation == iteration_start && Current_parse_token_type != T_ELSE &&
                Current_parse_token_type != T_END)
            {
                Current_parse_token = Get_Valid_Token();
            }
        }
        return Current_parse_token_type != T_INVALID;
    };

    if (!parse_branch())
    {
        return false;
    }
    if (blocks.active && ir_builder != NULL && ir_builder->emission_enabled())
    {
        if (!blocks.procedure_cfg)
        {
            (void)ir_builder->emit_jump(blocks.join_block);
        }
        else if (ir_builder->current_block_is_open())
        {
            blocks.join_block = ir_builder->create_block();
            if (blocks.join_block.valid())
            {
                (void)ir_builder->emit_jump(blocks.join_block);
            }
        }
    }
    bool consumed_optional_else = false;
    bool reported_repeated_else = false;
    while (Current_parse_token_type == T_ELSE)
    {
        const bool first_else = !consumed_optional_else;
        if (consumed_optional_else && !reported_repeated_else)
        {
            generate_error_report("Unexpected repeated \"else\" in if statement");
            errors_occured = true;
            reported_repeated_else = true;
        }
        consumed_optional_else = true;
        if (blocks.active && first_else && ir_builder != NULL && ir_builder->emission_enabled())
        {
            (void)ir_builder->select_block(blocks.else_block);
        }
        Current_parse_token = Get_Valid_Token();
        if (!parse_branch())
        {
            return false;
        }
        if (blocks.active && first_else && ir_builder != NULL && ir_builder->emission_enabled())
        {
            if (!blocks.procedure_cfg)
            {
                (void)ir_builder->emit_jump(blocks.join_block);
            }
            else if (ir_builder->current_block_is_open())
            {
                if (!blocks.join_block.valid())
                {
                    blocks.join_block = ir_builder->create_block();
                }
                if (blocks.join_block.valid())
                {
                    (void)ir_builder->emit_jump(blocks.join_block);
                }
            }
        }
    }
    if (blocks.active && !consumed_optional_else && ir_builder != NULL &&
        ir_builder->emission_enabled())
    {
        if (ir_builder->select_block(blocks.else_block))
        {
            if (blocks.procedure_cfg && !blocks.join_block.valid())
            {
                blocks.join_block = ir_builder->create_block();
            }
            (void)ir_builder->emit_jump(blocks.join_block);
        }
    }

    if (Current_parse_token_type != T_END)
    {
        generate_error_report_previous_token("Missing keyword \"end\" to end if statement");
        errors_occured = true;
        return false;
    }
    Current_parse_token = Get_Valid_Token();
    if (Current_parse_token_type != T_IF)
    {
        generate_error_report_previous_token("Missing keyword \"if\"to end if statement");
        errors_occured = true;
        return false;
    }
    Current_parse_token = Get_Valid_Token();
    if (blocks.active && blocks.join_block.valid() && ir_builder != NULL &&
        ir_builder->emission_enabled())
    {
        (void)ir_builder->select_block(blocks.join_block);
    }
    return true;
}

//ready to test
//consumes for token before entering this function
//refactored 2 times
bool parser::parse_loop_statement()
{
    struct LoopBlocks
    {
        ir::BlockId condition;
        ir::BlockId body;
        ir::BlockId exit;
        bool prepared = false;
        bool active = false;
    } blocks;

    lowered_expression expression_parse;
    parser_state state = S_LOOP_STATEMENT;
    bool valid_parse = false;
    bool initializer_valid_parse = false;
    bool condition_valid_parse = false;
    bool has_internal_semicolon = false;
    bool has_right_parenthesis = false;
    bool closed_loop = false;
    //A malformed loop header still owns its matching `end for`.  Consume that
    //boundary without asking the broad resynchronizer to guess whether a
    //nested statement's terminator belongs to this loop or its caller.  The
    //helper balances the two statement forms that own `end` (`if` and `for`)
    //but deliberately stops at enclosing program/procedure terminators or a
    //mismatched close, leaving their established recovery intact.
    const auto recover_to_own_end_for = [this]() -> bool {
        std::vector<int> nested_statements;
        while (Current_parse_token_type != T_INVALID)
        {
            if (Current_parse_token_type == T_END)
            {
                if (Next_parse_token_type != T_IF && Next_parse_token_type != T_FOR)
                {
                    return false;
                }
                const int closing_statement = Next_parse_token_type;
                if (nested_statements.empty())
                {
                    if (closing_statement != T_FOR)
                    {
                        return false;
                    }
                    Current_parse_token = Get_Valid_Token();
                    Current_parse_token = Get_Valid_Token();
                    return true;
                }
                if (nested_statements.back() != closing_statement)
                {
                    return false;
                }
                Current_parse_token = Get_Valid_Token();
                Current_parse_token = Get_Valid_Token();
                nested_statements.pop_back();
                continue;
            }
            if (Current_parse_token_type == T_IF || Current_parse_token_type == T_FOR)
            {
                nested_statements.push_back(Current_parse_token_type);
            }
            Current_parse_token = Get_Valid_Token();
        }
        return false;
    };
    if (Current_parse_token_type == T_LPARAM)
    {
        Current_parse_token = Get_Valid_Token();
        if (Current_parse_token_type == T_IDENTIFIER)
        {
            const token destination_occurrence = Current_parse_token;
            type_checker->set_statement_type(destination_occurrence);
            token destination;
            const bool destination_resolved = resolve_identifier_use(destination_occurrence,
                                                                     destination);
            Current_parse_token = Get_Valid_Token();
            initializer_valid_parse = parse_assignment_statement(
                destination_resolved ? destination : destination_occurrence);
            valid_parse = initializer_valid_parse;
            if (!initializer_valid_parse)
            {
                if (recover_to_own_end_for())
                {
                    return true;
                }
                return false;
            }
            if (Current_parse_token_type == T_SEMICOLON)
            {
                has_internal_semicolon = true;
                Current_parse_token = Get_Valid_Token();
                type_checker->begin_loop_condition(Current_parse_token);

                //The initializer belongs to the current/preheader block.
                //Select the condition block before parsing its expression so
                //loads and calls are re-evaluated on every loop backedge.
                if (initializer_valid_parse && has_internal_semicolon && ir_builder != NULL &&
                    ir_builder->emission_enabled())
                {
                    blocks.condition = ir_builder->create_block();
                    blocks.body = ir_builder->create_block();
                    blocks.exit = ir_builder->create_block();
                    if (blocks.condition.valid() && blocks.body.valid() &&
                        blocks.exit.valid() && ir_builder->emit_jump(blocks.condition) &&
                        ir_builder->select_block(blocks.condition))
                    {
                        blocks.prepared = true;
                    }
                }

                expression_parse = parse_expression();
                condition_valid_parse = expression_parse.semantics.valid_parse;
                valid_parse = condition_valid_parse;
                if (Current_parse_token_type == T_RPARAM)
                {
                    has_right_parenthesis = true;
                    Current_parse_token = Get_Valid_Token();
                    conversion_plan loop_condition_plan;
                    if (condition_valid_parse &&
                        expression_parse.semantics.semantic_valid &&
                        !type_checker->statement_suppressed)
                    {
                        loop_condition_plan =
                            type_checker->check_loop_statement(expression_parse.semantics);
                    }
                    if (blocks.prepared && has_internal_semicolon && has_right_parenthesis &&
                        condition_valid_parse &&
                        expression_parse.semantics.semantic_valid && loop_condition_plan.valid &&
                        !type_checker->statement_suppressed && ir_builder != NULL &&
                        ir_builder->emission_enabled() && expression_parse.value.valid())
                    {
                        ir::ValueId condition_value = expression_parse.value;
                        if (loop_condition_plan.requires_conversion)
                        {
                            ir::CastOp cast;
                            if (ir_cast_operation(loop_condition_plan.kind, cast))
                            {
                                condition_value = ir_builder->emit_cast(cast, condition_value);
                            }
                        }
                        if (condition_value.valid() && ir_builder->emission_enabled() &&
                            ir_builder->emit_branch(condition_value, blocks.body, blocks.exit) &&
                            ir_builder->select_block(blocks.body))
                        {
                            blocks.active = true;
                        }
                    }
                    while (Current_parse_token_type != T_END)
                    {
                        std::size_t iteration_start = token_generation;
                        const bool suppress_unreachable = ir_builder != NULL &&
                            ir_builder->current_function() != ir_builder->program_function() &&
                            !ir_builder->current_block_is_open();
                        if (suppress_unreachable)
                        {
                            ir_builder->begin_unreachable_statement();
                        }
                        valid_parse = parse_base_statement();
                        if (suppress_unreachable)
                        {
                            ir_builder->end_unreachable_statement();
                        }
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
                            closed_loop = true;
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
                    if (closed_loop && blocks.active && ir_builder != NULL &&
                        ir_builder->emission_enabled())
                    {
                        if (ir_builder->current_block_is_open())
                        {
                            (void)ir_builder->emit_jump(blocks.condition);
                        }
                        if (ir_builder->emission_enabled() && !ir_builder->current_block_is_open())
                        {
                            (void)ir_builder->select_block(blocks.exit);
                        }
                    }
                }
                else
                {
                    generate_error_report_previous_token("Missing \")\" for loop declaration");
                    errors_occured = true;
                    if (recover_to_own_end_for())
                    {
                        return true;
                    }
                    return false;
                }
            }
            else
            {
                generate_error_report_previous_token("Missing \";\" for loop assignment statement");
                errors_occured = true;
                if (recover_to_own_end_for())
                {
                    return true;
                }
                return false;
            }
        }
        else
        {
            generate_error_report("Missing expeceted identifier for assignment statement");
            errors_occured = true;
            if (recover_to_own_end_for())
            {
                return true;
            }
        }
    }
    else
    {
        generate_error_report_previous_token("Missing \"(\" required for loop");
        errors_occured = true;
        if (recover_to_own_end_for())
        {
            return true;
        }
    }

    return valid_parse;
}

//ready to test
//consumes return token before entering function
//refactored 1 time
bool parser::parse_return_statement(const token &return_token)
{
    const lowered_expression expression_parse = parse_expression();
    if (!expression_parse.semantics.valid_parse)
    {
        return false;
    }
    if (!expression_parse.semantics.semantic_valid || type_checker->statement_suppressed)
    {
        return true;
    }
    token owner;
    if (Lexer->symbol_table.lookup_scope_owner(current_scope_id, owner))
    {
        const conversion_plan return_plan = type_checker->check_return_statement(
            expression_parse.semantics, owner, return_token);
        if (return_plan.valid && ir_builder != NULL && expression_parse.value.valid())
        {
            ir::ValueId value = expression_parse.value;
            if (return_plan.requires_conversion)
            {
                ir::CastOp cast;
                if (ir_cast_operation(return_plan.kind, cast))
                {
                    value = ir_builder->emit_cast(cast, value);
                }
            }
            if (value.valid())
            {
                (void)ir_builder->emit_return(value);
            }
        }
    }
    else if (current_scope_id == 0)
    {
        generate_error_report("Return statements are only valid inside procedures",
                              return_token.line_found);
        errors_occured = true;
        type_checker->mark_current_statement_invalid();
    }
    //A retained recovery scope can be ownerless after a malformed procedure
    //header.  Its body remains structurally parseable, but it has no return
    //contract to validate and must not generate a second error.
    return true;
}

//ready to test
//already consumes identifier before parsing
//refactored 1 time
lowered_destination parser::parse_assignment_destination(token destination_token)
{
    lowered_destination destination_parse;
    const bool resolved_variable = destination_token.identifer_type == I_VARIABLE;
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
        destination_parse.semantics = typed_expression_result(type_checker,
                                                               shape_of(destination_token),
                                                               destination_token);
        if (ir_builder != NULL)
        {
            destination_parse.storage = ir_builder->storage_for(
                SymbolRef{destination_token.scope_id, destination_token.stringValue});
        }
    }
    const value_shape base_shape = shape_of(destination_token);
    lowered_expression indexed_destination;
    indexed_destination.semantics = destination_parse.semantics;
    if (!parse_optional_index(destination_token, resolved_variable, base_shape,
                              indexed_destination))
    {
        destination_parse.semantics = indexed_destination.semantics;
        return destination_parse;
    }
    destination_parse.semantics = indexed_destination.semantics;
    if (!resolved_variable)
    {
        destination_parse.semantics.valid_parse = true;
        destination_parse.semantics.semantic_valid = false;
        destination_parse.semantics.resolved_token = token();
    }
    return destination_parse;
}

bool parser::parse_optional_index(const token &base_occurrence, bool base_resolved,
                                  const value_shape &base_shape,
                                  lowered_expression &base_result)
{
    if (Current_parse_token_type != T_LBRACKET)
    {
        return true;
    }
    if (ir_builder != NULL)
    {
        ir_builder->mark_unsupported("array indexes require runtime bounds lowering");
    }
    Current_parse_token = Get_Valid_Token();
    const lowered_expression index_parse = parse_expression();
    if (Current_parse_token_type != T_RBRACKET)
    {
        generate_error_report_previous_token(
            "Missing closing right bracket to the identifier expression");
        errors_occured = true;
        base_result = invalid_lowered_expression(false);
        return false;
    }
    Current_parse_token = Get_Valid_Token();
    base_result.semantics.valid_parse = index_parse.semantics.valid_parse;
    if (!index_parse.semantics.valid_parse || !base_resolved ||
        !base_result.semantics.semantic_valid || !index_parse.semantics.semantic_valid ||
        type_checker->statement_suppressed)
    {
        base_result.semantics.semantic_valid = false;
        base_result.semantics.resolved_token = token();
        return index_parse.semantics.valid_parse;
    }
    if (!type_checker->validate_array_index(base_occurrence, base_shape,
                                            index_parse.semantics))
    {
        base_result.semantics.semantic_valid = false;
        base_result.semantics.resolved_token = token();
        return true;
    }
    value_shape element_shape;
    element_shape.element_type = base_shape.element_type;
    base_result.semantics = typed_expression_result(type_checker, element_shape, base_occurrence);
    return true;
}

//ready to test
//consumes a token before entering this function
//all expressions start be thought to start with a ArithOp?
lowered_expression parser::parse_expression()
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
    lowered_expression expression_parse;
    lowered_expression right_parse;
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
        return invalid_lowered_expression(false);
    }

    if (Current_parse_token_type == T_NOT)
    {
        has_leading_not = true;
        not_token = Current_parse_token;
        Current_parse_token = Get_Valid_Token();
    }

    expression_parse = parse_arithOp();
    if (has_leading_not && expression_parse.semantics.valid_parse)
    {
        if (expression_parse.semantics.semantic_valid && !type_checker->statement_suppressed)
        {
            const token_and_status checked = type_checker->check_unary_expression(
                SEM_NOT, not_token, expression_parse.semantics);
            expression_parse.semantics = checked;
            if (checked.semantic_valid && ir_builder != NULL && expression_parse.value.valid())
            {
                const value_shape input_shape = shape_of(expression_parse.semantics.resolved_token);
                if (ir::is_ready_type(input_shape))
                {
                    expression_parse.value = ir_builder->emit_unary(ir_unary_operation(SEM_NOT),
                                                                      expression_parse.value);
                }
                else
                {
                    ir_builder->mark_unsupported("array or unresolved unary expression");
                    expression_parse.value = ir::ValueId();
                }
            }
        }
        else
        {
            expression_parse.semantics.semantic_valid = false;
            expression_parse.semantics.resolved_token = token();
        }
    }

    //The grammar gives '&' and '|' equal precedence.  Each right operand is
    //<arithOp>, not a new expression; the double-operator recovery above is
    //the sole deliberate exception for a focused missing-left diagnostic.
    while (expression_parse.semantics.valid_parse &&
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
        if (!right_parse.semantics.valid_parse)
        {
            expression_parse.semantics.valid_parse = false;
            expression_parse.semantics.semantic_valid = false;
            expression_parse.semantics.resolved_token = token();
            break;
        }
        if (expression_parse.semantics.semantic_valid && right_parse.semantics.semantic_valid &&
            !type_checker->statement_suppressed)
        {
            const value_shape left_shape = shape_of(expression_parse.semantics.resolved_token);
            const value_shape right_shape = shape_of(right_parse.semantics.resolved_token);
            const token_and_status checked = type_checker->check_binary_expression(
                operation, operator_token, expression_parse.semantics, right_parse.semantics);
            expression_parse.semantics = checked;
            if (checked.semantic_valid && ir_builder != NULL)
            {
                if (ir::is_ready_type(left_shape) && left_shape == right_shape &&
                    expression_parse.value.valid() && right_parse.value.valid())
                {
                    expression_parse.value = ir_builder->emit_binary(
                        ir_binary_operation(operation), expression_parse.value, right_parse.value);
                }
                else
                {
                    ir_builder->mark_unsupported("mixed or array binary expression");
                    expression_parse.value = ir::ValueId();
                }
            }
        }
        else
        {
            expression_parse.semantics.semantic_valid = false;
            expression_parse.semantics.resolved_token = token();
            expression_parse.value = ir::ValueId();
        }
    }

    if (!expression_parse.semantics.valid_parse && !type_checker->statement_suppressed)
    {
        generate_error_report("Error in expression");
        errors_occured = true;
    }
    return expression_parse;
}

//ready to test
//consumes a token before entering this function
//all arithOps can be thought to starts with relations?
lowered_expression parser::parse_arithOp()
{
    lowered_expression arithop_parse;
    lowered_expression right_parse;

    if (Current_parse_token_type == T_PLUS)
    {
        const token operator_token = Current_parse_token;
        Current_parse_token = Get_Valid_Token();
        (void)parse_relation();
        generate_error_report("Missing left operand before \"+\" operator",
                              operator_token.line_found);
        errors_occured = true;
        return invalid_lowered_expression(false);
    }

    arithop_parse = parse_relation();
    while (arithop_parse.semantics.valid_parse &&
           (Current_parse_token_type == T_PLUS || Current_parse_token_type == T_MINUS))
    {
        const token operator_token = Current_parse_token;
        const semantic_operator operation = Current_parse_token_type == T_PLUS ?
                                                SEM_ADD : SEM_SUBTRACT;
        Current_parse_token = Get_Valid_Token();
        right_parse = parse_relation();
        if (!right_parse.semantics.valid_parse)
        {
            arithop_parse.semantics.valid_parse = false;
            arithop_parse.semantics.semantic_valid = false;
            arithop_parse.semantics.resolved_token = token();
            break;
        }
        if (arithop_parse.semantics.semantic_valid && right_parse.semantics.semantic_valid &&
            !type_checker->statement_suppressed)
        {
            const value_shape left_shape = shape_of(arithop_parse.semantics.resolved_token);
            const value_shape right_shape = shape_of(right_parse.semantics.resolved_token);
            const token_and_status checked = type_checker->check_binary_expression(
                operation, operator_token, arithop_parse.semantics, right_parse.semantics);
            arithop_parse.semantics = checked;
            if (checked.semantic_valid && ir_builder != NULL)
            {
                if (ir::is_ready_type(left_shape) && left_shape == right_shape &&
                    arithop_parse.value.valid() && right_parse.value.valid())
                {
                    arithop_parse.value = ir_builder->emit_binary(
                        ir_binary_operation(operation), arithop_parse.value, right_parse.value);
                }
                else
                {
                    ir_builder->mark_unsupported("mixed or array binary expression");
                    arithop_parse.value = ir::ValueId();
                }
            }
        }
        else
        {
            arithop_parse.semantics.semantic_valid = false;
            arithop_parse.semantics.resolved_token = token();
            arithop_parse.value = ir::ValueId();
        }
    }
    return arithop_parse;
}

//ready to test
//consumes a token before entering this function
//all arithOps can be thought to starts with terms?
lowered_expression parser::parse_relation()
{
    lowered_expression relation_parse;
    lowered_expression right_parse;

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
        return invalid_lowered_expression(false);
    }

    relation_parse = parse_term();
    while (relation_parse.semantics.valid_parse && is_relation_start(Current_parse_token_type))
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
            relation_parse.semantics.valid_parse = false;
            relation_parse.semantics.semantic_valid = false;
            relation_parse.semantics.resolved_token = token();
            break;
        }
        right_parse = parse_term();
        if (!right_parse.semantics.valid_parse)
        {
            relation_parse.semantics.valid_parse = false;
            relation_parse.semantics.semantic_valid = false;
            relation_parse.semantics.resolved_token = token();
            break;
        }
        if (relation_parse.semantics.semantic_valid && right_parse.semantics.semantic_valid &&
            !type_checker->statement_suppressed)
        {
            const value_shape left_shape = shape_of(relation_parse.semantics.resolved_token);
            const value_shape right_shape = shape_of(right_parse.semantics.resolved_token);
            const token_and_status checked = type_checker->check_binary_expression(
                operation, operator_token, relation_parse.semantics, right_parse.semantics);
            relation_parse.semantics = checked;
            if (checked.semantic_valid && ir_builder != NULL)
            {
                if (ir::is_ready_type(left_shape) && left_shape == right_shape &&
                    relation_parse.value.valid() && right_parse.value.valid())
                {
                    relation_parse.value = ir_builder->emit_binary(
                        ir_binary_operation(operation), relation_parse.value, right_parse.value);
                }
                else
                {
                    ir_builder->mark_unsupported("mixed or array binary expression");
                    relation_parse.value = ir::ValueId();
                }
            }
        }
        else
        {
            relation_parse.semantics.semantic_valid = false;
            relation_parse.semantics.resolved_token = token();
            relation_parse.value = ir::ValueId();
        }
    }
    return relation_parse;
}

//ready to test
//already consumes a token before being parsed
lowered_expression parser::parse_term()
{
    lowered_expression term_parse;
    lowered_expression right_parse;
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
        return invalid_lowered_expression(false);
    }
    term_parse = parse_factor();

    // <term> ::= <term> (*|/) <factor> | <factor>.  Consume every valid
    // following multiplicative operator left-to-right.
    while (term_parse.semantics.valid_parse &&
           (Current_parse_token_type == T_MULT || Current_parse_token_type == T_SLASH))
    {
        const token operator_token = Current_parse_token;
        const semantic_operator operation = Current_parse_token_type == T_MULT ?
                                                SEM_MULTIPLY : SEM_DIVIDE;
        Current_parse_token = Get_Valid_Token();
        right_parse = parse_factor();
        if (!right_parse.semantics.valid_parse)
        {
            term_parse.semantics.valid_parse = false;
            term_parse.semantics.semantic_valid = false;
            term_parse.semantics.resolved_token = token();
            break;
        }
        if (term_parse.semantics.semantic_valid && right_parse.semantics.semantic_valid &&
            !type_checker->statement_suppressed)
        {
            const value_shape left_shape = shape_of(term_parse.semantics.resolved_token);
            const value_shape right_shape = shape_of(right_parse.semantics.resolved_token);
            const token_and_status checked = type_checker->check_binary_expression(
                operation, operator_token, term_parse.semantics, right_parse.semantics);
            term_parse.semantics = checked;
            if (checked.semantic_valid && ir_builder != NULL)
            {
                if (ir::is_ready_type(left_shape) && left_shape == right_shape &&
                    term_parse.value.valid() && right_parse.value.valid())
                {
                    term_parse.value = ir_builder->emit_binary(
                        ir_binary_operation(operation), term_parse.value, right_parse.value);
                }
                else
                {
                    ir_builder->mark_unsupported("mixed or array binary expression");
                    term_parse.value = ir::ValueId();
                }
            }
        }
        else
        {
            term_parse.semantics.semantic_valid = false;
            term_parse.semantics.resolved_token = token();
            term_parse.value = ir::ValueId();
        }
    }
    return term_parse;
}

//ready to test
//already consumes a token before being parsed
lowered_expression parser::parse_factor()
{
    lowered_expression expression_parse;
    lowered_expression factor_parse;
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
        return invalid_lowered_expression(false);
    }
    if (Current_parse_token_type == T_IDENTIFIER)
    {
        if (Next_parse_token_type == T_LPARAM)
        {
            const token callee_occurrence = Current_parse_token;
            token callee;
            bool callee_resolved = false;
            lowered_expression callee_result;
            callee_result.semantics.valid_parse = true;
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
                    callee_result.semantics = typed_expression_result(
                        type_checker, shape_of(callee), callee_occurrence);
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
            factor_parse.semantics = typed_expression_result(
                type_checker,
                Current_parse_token_type == T_INTEGER_VALUE ? TYPE_INT : TYPE_FLOAT,
                Current_parse_token);
            if (ir_builder != NULL && ir_builder->emission_enabled())
            {
                const token literal = Current_parse_token;
                value_shape shape = shape_of(factor_parse.semantics.resolved_token);
                factor_parse.value = ir_builder->emit_constant(
                    shape, literal.type == T_INTEGER_VALUE ?
                               std::variant<int, float, bool, std::string>(literal.intValue) :
                               std::variant<int, float, bool, std::string>(literal.floatValue));
            }
            Current_parse_token = Get_Valid_Token();
            const token_and_status checked = type_checker->check_unary_expression(
                SEM_NEGATE, operator_token, factor_parse.semantics);
            factor_parse.semantics = checked;
            if (checked.semantic_valid && ir_builder != NULL && factor_parse.value.valid())
            {
                factor_parse.value = ir_builder->emit_unary(ir_unary_operation(SEM_NEGATE),
                                                             factor_parse.value);
            }
            return factor_parse;
        }
        if (Current_parse_token_type == T_IDENTIFIER)
        {
            identifier_token = Current_parse_token;
            Current_parse_token = Get_Valid_Token();
            factor_parse = parse_name(identifier_token);
            if (!factor_parse.semantics.valid_parse)
            {
                return factor_parse;
            }
            if (factor_parse.semantics.semantic_valid && !type_checker->statement_suppressed)
            {
                const token_and_status checked = type_checker->check_unary_expression(
                    SEM_NEGATE, operator_token, factor_parse.semantics);
                factor_parse.semantics = checked;
                if (checked.semantic_valid && ir_builder != NULL && factor_parse.value.valid())
                {
                    factor_parse.value = ir_builder->emit_unary(ir_unary_operation(SEM_NEGATE),
                                                                 factor_parse.value);
                }
                return factor_parse;
            }
            return invalid_lowered_expression(true);
        }
        generate_error_report("Unexpected negative factor is not a name or a number",
                              operator_token.line_found);
        errors_occured = true;
        return invalid_lowered_expression(false);
    }
    if (Current_parse_token_type == T_INTEGER_VALUE || Current_parse_token_type == T_FLOAT_VALUE)
    {
        factor_parse.semantics = typed_expression_result(
            type_checker,
            Current_parse_token_type == T_INTEGER_VALUE ? TYPE_INT : TYPE_FLOAT,
            Current_parse_token);
        if (ir_builder != NULL)
        {
            const token literal = Current_parse_token;
            factor_parse.value = ir_builder->emit_constant(
                shape_of(factor_parse.semantics.resolved_token),
                literal.type == T_INTEGER_VALUE ?
                    std::variant<int, float, bool, std::string>(literal.intValue) :
                    std::variant<int, float, bool, std::string>(literal.floatValue));
        }
        Current_parse_token = Get_Valid_Token();
        return factor_parse;
    }
    if (Current_parse_token_type == T_STRING_VALUE)
    {
        if (Lexer->quote_status)
        {
            generate_error_report("quotation left open", Lexer->quote_opener);
        }
        factor_parse.semantics = typed_expression_result(type_checker, TYPE_STRING,
                                                         Current_parse_token);
        if (ir_builder != NULL)
        {
            factor_parse.value = ir_builder->emit_constant(
                shape_of(factor_parse.semantics.resolved_token), Current_parse_token.stringValue);
        }
        Current_parse_token = Get_Valid_Token();
        return factor_parse;
    }
    if (Current_parse_token_type == T_TRUE || Current_parse_token_type == T_FALSE)
    {
        factor_parse.semantics = typed_expression_result(type_checker, TYPE_BOOL,
                                                         Current_parse_token);
        if (ir_builder != NULL)
        {
            factor_parse.value = ir_builder->emit_constant(
                shape_of(factor_parse.semantics.resolved_token),
                Current_parse_token_type == T_TRUE);
        }
        Current_parse_token = Get_Valid_Token();
        return factor_parse;
    }
    generate_error_report("Invalid token for factor discovered");
    errors_occured = true;
    return invalid_lowered_expression(false);
}

//ready to test
//already consumes indentifier token before being parsed
lowered_expression parser::parse_name(token identifier_token)
{
    lowered_expression name_parse;
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
            name_parse.semantics = typed_expression_result(type_checker,
                                                           shape_of(resolved_identifier),
                                                           identifier_token);
            if (ir_builder != NULL && ir_builder->emission_enabled())
            {
                const value_shape shape = shape_of(resolved_identifier);
                if (ir::is_ready_type(shape))
                {
                    const ir::StorageId storage = ir_builder->storage_for(
                        SymbolRef{resolved_identifier.scope_id,
                                  resolved_identifier.stringValue});
                    if (storage.valid())
                    {
                        name_parse.value = ir_builder->emit_load(storage);
                    }
                    else
                    {
                        ir_builder->mark_invalid("resolved scalar name has no IR storage");
                    }
                }
                else
                {
                    ir_builder->mark_unsupported("arrays or unresolved values need runtime lowering");
                }
            }
        }
    }
    if (!parse_optional_index(identifier_token, resolved, shape_of(resolved_identifier), name_parse))
    {
        return name_parse;
    }
    if (!resolved)
    {
        name_parse.semantics.valid_parse = true;
        name_parse.semantics.semantic_valid = false;
        name_parse.semantics.resolved_token = token();
    }
    return name_parse;
}

//ready to test
//consumes one token before starting
bool parser::parse_argument_list(std::vector<lowered_expression> &arguments)
{
    lowered_expression expression_parse;
    bool valid_parse = true;
    expression_parse = parse_expression();
    arguments.push_back(expression_parse);
    valid_parse = expression_parse.semantics.valid_parse;
    while (Current_parse_token_type == T_COMMA)
    {
        Current_parse_token = Get_Valid_Token();
        expression_parse = parse_expression();
        arguments.push_back(expression_parse);
        valid_parse = expression_parse.semantics.valid_parse && valid_parse;
    }

    return valid_parse;
}

//ready to test
//already consumes identifier token before parsing
lowered_expression parser::parse_procedure_call(const token &callee_occurrence,
                                                const token &canonical_callee,
                                                bool callee_resolved,
                                                const lowered_expression &callee_result)
{
    lowered_expression call_parse;
    call_parse.semantics.valid_parse = true;
    std::vector<lowered_expression> arguments;
    if (Current_parse_token_type == T_LPARAM)
    {
        Current_parse_token = Get_Valid_Token();
        if (Current_parse_token_type == T_RPARAM)
        {
            Current_parse_token = Get_Valid_Token();
        }
        else
        {
            call_parse.semantics.valid_parse = parse_argument_list(arguments);
            if (Current_parse_token_type == T_RPARAM)
            {
                Current_parse_token = Get_Valid_Token();
            }
            //missing needed right param for the end of a procedure call
            else
            {
                generate_error_report_previous_token("Missing required \")\" for the end of a procedure call");
                errors_occured = true;
            return invalid_lowered_expression(false);
            }
        }
    }
    //missing needed left param for the start of the procedure call
    else
    {
        generate_error_report_previous_token("Missing required \"(\" for the end of a procedure call");
        errors_occured = true;
        return invalid_lowered_expression(false);
    }
    if (!call_parse.semantics.valid_parse || !callee_resolved ||
        canonical_callee.identifer_type != I_PROCEDURE || !callee_result.semantics.semantic_valid ||
        type_checker->statement_suppressed)
    {
        call_parse.semantics.semantic_valid = false;
        call_parse.semantics.resolved_token = token();
        return call_parse;
    }
    for (std::size_t i = 0; i < arguments.size(); i++)
    {
        if (!arguments[i].semantics.valid_parse || !arguments[i].semantics.semantic_valid)
        {
            call_parse.semantics.semantic_valid = false;
            call_parse.semantics.resolved_token = token();
            return call_parse;
        }
    }
    std::vector<token_and_status> argument_semantics;
    for (const lowered_expression &argument : arguments)
    {
        argument_semantics.push_back(argument.semantics);
    }
    if (!type_checker->validate_procedure_call(canonical_callee, callee_occurrence,
                                               argument_semantics))
    {
        call_parse.semantics.semantic_valid = false;
        call_parse.semantics.resolved_token = token();
        return call_parse;
    }
    //The call return remains a synthetic expression result.  The declaration
    //identity and signature stay in canonical_callee for validation only.
    call_parse.semantics = callee_result.semantics;
    call_parse.semantics.resolved_token.line_found = callee_occurrence.line_found;
    call_parse.semantics.resolved_token.column_found = callee_occurrence.column_found;
    call_parse.semantics.resolved_token.first_token_on_line = callee_occurrence.first_token_on_line;
    if (ir_builder != NULL && ir_builder->emission_enabled())
    {
        std::vector<ir::ValueId> values;
        for (const lowered_expression &argument : arguments)
        {
            values.push_back(argument.value);
        }
        const ir::FunctionId callee = ir_builder->function_for(
            SymbolRef{canonical_callee.scope_id, canonical_callee.stringValue});
        if (callee.valid() && std::all_of(values.begin(), values.end(),
                                          [](ir::ValueId value) { return value.valid(); }))
        {
            call_parse.value = ir_builder->emit_call(callee, values);
        }
        else
        {
            ir_builder->mark_invalid("validated procedure call has no IR callee or argument value");
        }
    }
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
            else if (Current_parse_token_type == T_ELSE)
            {
                //The current if owns ELSE.  Keep it available to the branch
                //parser instead of scanning through it as recovery noise.
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
        bool recovered_semantic_valid = true;
        return_state = parse_parameter_list(recovered_parameters, recovered_header_symbols,
                                            recovered_semantic_valid);
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
