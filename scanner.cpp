#include "scanner.h"

scanner::scanner()
{
}

//usefuly for quick tests; pass in an empty file
void scanner::test()
{
    token test_token;
    std::string test_string;
    test_string = "";
    while (!source.eof())
    {
        test_token = Get_token();
        test_string = "The token is type: " + std::to_string(test_token.type);
        if (test_token.stringValue != "")
        {
            test_string = test_string + " with a value of: " + test_token.stringValue;
        }
        else if (test_token.charValue != '\0')
        {
            test_string = test_string + " with a value of: " + test_token.charValue;
        }
        //std::cout<<"Token is type: "<<test_token.type<<std::endl;
        std::cout << test_string << std::endl;
        test_string = "";
    }
}

int scanner::what_is_char(char test_char)
{
    if (isalpha(test_char))
    {
        return alpha_char;
    }
    else if (isdigit(test_char))
    {
        return number_char;
    }
    else if (Is_Reserved_Char(test_char))
    {
        return reserved_char;
    }
    else
    {
        return invalid_char;
    }
}

bool scanner::is_first_char()
{
    if (build_string.length() == 0)
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

bool scanner::Is_Reserved_Char(char test_char)
{
    //checks to see if the char is a reserved char
    if (symbol_table.is_reserved_char(test_char))
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

bool scanner::Is_Reserved_Word(std::string test_word)
{
    //checks to see if the string is already in the symbol table
    if (symbol_table.is_in_table(Tolower_string(test_word)))
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

scanner::scanner(std::string file)
{
    //Things done here to prepare the scanner
    InitScanner(file);
    //opens the file name to begin reading the file
    source.open(FileName);
}

void scanner::InitScanner(std::string file)
{
    //sets the name of the Filename in the object to the value passed into the program
    FileName = file;
    //sets the current line to 1
    current_line = 1;
}

//used for testing
void scanner::ReadFile()
{
    token read_token;
    //scanner_parser = new parser;
    //keeps running while not at the end of the file
    while (true)
    {
        //breaks loop if end of file is reached
        if (source.eof())
        {
            end_of_file = true;
            break;
        }
        //Gets a token and places it in the Current_token variable
        Current_token = new token;
        Get_token();

        //build token vector here?
        if (Current_token->type == 279)
        {
            std::cout << "Token is an integer with a value of: " << Current_token->intValue << " with a type of " << Current_token->type << std::endl;
        }
        else if (Current_token->type == 280)
        {
            std::cout << "Token a float with a value of: " << Current_token->floatValue << " with a type of " << Current_token->type << std::endl;
        }
        else if (Current_token->charValue != '\0')
        {
            std::cout << "Token is a char with a value of: " << Current_token->charValue << " with a type of " << Current_token->type << std::endl;
        }
        else if (Current_token->stringValue != "")
        {
            std::cout << "Token is a reserved word or ID with a value of: " << Current_token->stringValue << " with a type of " << Current_token->type << std::endl;
        }

        delete Current_token;
    }
}

token scanner::Get_token()
{
    Current_token = new token;
    token return_token;
    while (true)
    {
        //breaks loop if end of file is reached
        if (source.eof())
        {
            end_of_file = true;
            //this will be useful for getting the correct line if the file runs over
            *Current_token = last_sent_token;
            //sets this token type to an invalid token type
            Current_token->type = T_INVALID;
            break;
        }
        previous_char = current_char;
        current_char = next_char;
        if (is_first_char())
        {
            char_status = what_is_char(current_char);
            switch (char_status)
            {
            case 1:
                build_string_token();
                break;

            case 2:
                build_number_token();
                break;

            case 3:
                build_char_token();
                break;

            case 4:
                //needed to check for end lines and spaces
                invalid_char_test();
                break;
            }
        }
        build_string = "";

        //must be a valid token, and not either type of comment
        if (Current_token->type != 0 && !is_slash_comment && !is_nested_commented)
        {
            if (Current_token->line_found != last_sent_token.line_found)
            {
                Current_token->first_token_on_line = true;
            }
            break;
        }
        //tests if is a comment
        else if (is_slash_comment || is_nested_commented)
        {
            delete Current_token;
            Current_token = new token;
        }
        //else the token is invalid and a new one is needed
        else
        {
            delete Current_token;
            Current_token = new token;
        }
        //handles checking to see if the comment line has changed
        if (is_slash_comment)
        {
            if ((slash_comment_line != current_line) && (next_char == '\n' || current_char == '\n'))
            {
                end_line_handler();
            }
        }
    }
    return_token = *Current_token;
    delete Current_token;
    //used if the program breaks from hitting the eof
    last_sent_token = return_token;

    return return_token;
}

void scanner::build_char_token()
{
    //if the character is not in a quote, then it is a token
    if (!quote_status)
    {
        Current_token->charValue = current_char;
        Current_token->line_found = current_line;
        //Giant case statement here
        switch (current_char)
        {
        case ';':
            Current_token->type = T_SEMICOLON;
            break;

        case '(':
            Current_token->type = T_LPARAM;
            break;

        case ')':
            Current_token->type = T_RPARAM;
            break;

        case '=':
            Current_token->type = T_ASSIGN;
            break;

        //potentially part of a comment indicator
        case '/':
            comment_handler();
            if (nested_comment_stat_change)
            {
                nested_comment_stat_change = false;
            }
            else
            {
                Current_token->type = T_SLASH;
            }

            break;

        case '{':
            Current_token->type = T_LBRACE;
            break;

        case '}':
            Current_token->type = T_RBRACE;
            break;

        case '[':
            Current_token->type = T_LBRACKET;
            break;

        case ']':
            Current_token->type = T_RBRACKET;
            break;

        case ',':
            Current_token->type = T_COMMA;
            break;

        case '+':
            Current_token->type = T_PLUS;
            break;

        case '-':
            Current_token->type = T_MINUS;
            break;

        case '_':
            Current_token->type = T_UNDERSCORE;
            break;

        case '.':
            Current_token->type = T_PERIOD;
            break;

        case '>':
            Current_token->type = T_GREATER;
            break;

        case '<':
            Current_token->type = T_LESS;
            break;

        //pontentially part of a comment indicator
        case '*':
            comment_handler();
            if (nested_comment_stat_change)
            {
                nested_comment_stat_change = false;
            }
            else
            {
                Current_token->type = T_MULT;
            }
            break;

        case '"':
            //the value will be a quotation with a string value
            Current_token->type = T_QUOTE;
            //builds the string quoation
            string_value_builder();
            //build quote string here
            break;

        case '!':
            Current_token->type = T_EXCLAM;
            break;

        case ':':
            Current_token->type = T_COLON;
            break;

        case '|':
            Current_token->type = T_VERTICAL_BAR;
            break;

        case '&':
            Current_token->type = T_AMPERSAND;
            break;
        }
        if (debug)
        {
            std::cout << "DEBUG in build_char_token() of current char: " << current_char << std::endl;
        }
    }

    source.get(next_char);
}

void scanner::build_number_token()
{
    int token_int_value;
    float token_float_value;
    bool is_float = false;
    bool one_decimal = true;
    //numbers are valid until a non numbber character is used, or multiple decimals are used
    while (isdigit(next_char) || next_char == '.')
    {
        if (current_char == '.' && !one_decimal)
        {
            if (debug)
            {
                std::cout << "ERROR: The number has more than one decimal" << std::endl;
            }

            //throw error
            error_detected = true;
            return;
            //break;
        }
        if (current_char == '.')
        {
            one_decimal = false;
            is_float = true;
        }
        build_string = build_string + current_char;
        source.get(next_char);
        previous_char = current_char;
        current_char = next_char;
        //if end of the file breaks the loop
        if (source.eof())
        {
            end_of_file = true;
            break;
        }
        //increments line counter if end of the line
        if (current_char == '\n')
        {
            prev_line = current_line;
            current_line++;
            //is_slash_comment = false;
        }
    }
    if (is_float)
    {
        token_float_value = std::stof(build_string);
        //assign token type and value here
        Current_token->floatValue = token_float_value;
        Current_token->type = T_FLOAT_VALUE;
        Current_token->line_found = current_line;
    }
    else
    {
        token_int_value = std::stoi(build_string);
        //assign token type and value here
        Current_token->intValue = token_int_value;
        Current_token->type = T_INTEGER_VALUE;
        Current_token->line_found = current_line;
    }
}

//should probably return a string so that later checks on it can be done for reserved words
void scanner::build_string_token()
{
    //token is valid until a non letter or number is displayed
    while (isdigit(next_char) || isalpha(next_char) || next_char == '_')
    {
        build_string = build_string + current_char;
        source.get(next_char);
        previous_char = current_char;
        current_char = next_char;
        //increments line counter is the line ends
        if (current_char == '\n')
        {
            //current_line++;
        }
        if (source.eof())
        {
            end_of_file = true;
            break;
        }
    }
    //the values in the symbol table are lowercase convert these values to lower case for easy comparison
    build_string = Tolower_string(build_string);
    //checks to see whether the built string is either a reserved word or already in the symbol table
    if (symbol_table.is_in_table(build_string))
    {
        *Current_token = symbol_table.map[build_string];
        Current_token->line_found = current_line;
    }
    //if not in the symbol table it inserts the indentifier
    else
    {
        symbol_table.insert_stringValue(build_string, T_IDENTIFIER);
        *Current_token = symbol_table.map[build_string];
        Current_token->line_found = current_line;
    }
    if (debug)
    {
        std::cout << "DEBUG: Id or Reserved word is: " << build_string << std::endl;
    }

    build_string = "";
}

void scanner::invalid_char_test()
{
    if (current_char == '\n')
    {
        if (debug)
        {
            std::cout << "DEBUG: End of line character detected" << std::endl;
        }
        prev_line = current_line;
        current_line++;
        //is_slash_comment = false;
        last_char_was_end_line = true;
    }
    else if (isspace(current_char))
    {
        if (debug)
        {
            std::cout << "DEBUG: Space character detected" << std::endl;
        }
    }
    else
    {
        if (debug)
        {
            std::cout << "DEBUG: Invalid character detected" << std::endl;
        }

        error_detected = true;
    }
    source.get(next_char);
}

void scanner::string_value_builder()
{
    quote_status = true;
    while (true)
    {
        build_string = build_string + current_char;
        source.get(next_char);
        if (next_char == '"')
        {
            build_string = build_string + next_char;
            quote_status = false;
            break;
        }
        previous_char = current_char;
        current_char = next_char;
        if (current_char == '\n')
        {
            prev_line = current_line;
            current_line++;
            //is_slash_comment = false;
        }
        if (source.eof() && quote_status)
        {
            //no closing quotation mark;
            end_of_file = true;
            error_detected = true;
            break;
        }
    }
    if (debug)
    {
        std::cout << "DEBUG: The detected quotation is: " << build_string << std::endl;
    }

    Current_token->charValue = '\0';
    Current_token->stringValue = build_string;
    Current_token->line_found = current_line;
    Current_token->type = T_STRING_VALUE;
}

void scanner::comment_handler()
{
    char peek_char;
    peek_char = source.peek();
    if (current_char == '/')
    {
        if (peek_char == '/')
        {
            //will be a comment until the next line
            is_slash_comment = true;
            slash_comment_line = current_line;
            source.get(next_char);
            //source.get(next_char);
        }
        //enter block comment stack by one
        else if (peek_char == '*')
        {
            nested_comment_counter++;
            nested_comment_line = current_line;
            is_nested_commented = true;
            nested_comment_stat_change = true;
            //skips the next char since it is part of the block comment indicator
            source.get(next_char);
        }
    }
    else if (current_char == '*')
    {
        //exit block comment by one
        if (peek_char == '/')
        {
            nested_comment_counter--;
            //skips the next char since it is part of the block comment indicator
            source.get(next_char);
            if (nested_comment_counter <= 0)
            {
                is_nested_commented = false;
                nested_comment_stat_change = true;
            }
        }
        //do nothing
        else
        {
        }
    }
}

void scanner::end_line_handler()
{
    is_slash_comment = false;
    last_char_was_end_line = false;
}

void scanner::copy_SymbolTable(SymbolTable table_to_copy)
{
}

void scanner::put_SymbolTable()
{
}
