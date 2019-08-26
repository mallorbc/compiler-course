#include "SymbolTable.h"

SymbolTable::SymbolTable()
{
    init_reserved_words();
    init_reserved_chars();
}

bool SymbolTable::init_reserved_words()
{
    //Inserts keywords into the symbol table with the string value and the key
    insert_stringValue("program", T_PROGRAM);
    insert_stringValue("is", T_IS);
    insert_stringValue("begin", T_BEGIN);
    insert_stringValue("end", T_END);
    insert_stringValue("global", T_GLOBAL);
    insert_stringValue("procedure", T_PROCEDURE);
    insert_stringValue("variable", T_VARIABLE);
    insert_stringValue("type", T_TYPE);
    insert_stringValue("integer", T_INTEGER_TYPE);
    insert_stringValue("float", T_FLOAT_TYPE);
    insert_stringValue("string", T_STRING_TYPE);
    insert_stringValue("bool", T_BOOL_TYPE);
    insert_stringValue("enum", T_ENUM);
    insert_stringValue("if", T_IF);
    insert_stringValue("then", T_THEN);
    insert_stringValue("else", T_ELSE);
    insert_stringValue("for", T_FOR);
    insert_stringValue("return", T_RETURN);
    insert_stringValue("not", T_NOT);
    insert_stringValue("true", T_TRUE);
    insert_stringValue("false", T_FALSE);

    return 1;
}

bool SymbolTable::insert_stringValue(std::string stringValue, token_type type_of_token)
{
    //stores
    token *new_token;
    new_token = new token;
    new_token->type = type_of_token;
    new_token->stringValue = stringValue;
    insert_string_token(*new_token);

    return 1;
}

bool SymbolTable::insert_string_token(token new_token)
{
    std::string key_value = new_token.stringValue;
    map[key_value] = new_token;

    return 1;
}

bool SymbolTable::is_in_table(std::string test_string)
{
    if (map.find(test_string) == map.end())
    {
        return 0;
    }
    else
    {
        return 1;
    }
}

bool SymbolTable::init_reserved_chars()
{
    insert_char_table('(', T_LPARAM);
    insert_char_table(')', T_RPARAM);
    insert_char_table('[', T_LBRACKET);
    insert_char_table(']', T_RBRACKET);
    insert_char_table(',', T_COMMA);
    insert_char_table('/', T_SLASH);
    insert_char_table('{', T_LBRACE);
    insert_char_table('}', T_RBRACE);
    insert_char_table('=', T_ASSIGN);
    insert_char_table('+', T_PLUS);
    insert_char_table('_', T_UNDERSCORE);
    insert_char_table('.', T_PERIOD);
    insert_char_table('>', T_GREATER);
    insert_char_table('<', T_LESS);
    insert_char_table('*', T_MULT);
    insert_char_table('"', T_QUOTE);
    insert_char_table('!', T_EXCLAM);
    insert_char_table(';', T_SEMICOLON);
    insert_char_table(':', T_COLON);
    insert_char_table('|', T_VERTICAL_BAR);
    insert_char_table('&', T_AMPERSAND);
    insert_char_table('-', T_MINUS);

    return 1;
}

bool SymbolTable::insert_char_table(char reserved_char, token_type type_of_token)
{
    reserved_chars[reserved_char] = type_of_token;
    return 1;
}

bool SymbolTable::is_reserved_char(char test_char)
{
    if (reserved_chars.find(test_char) == reserved_chars.end())
    {
        return 0;
    }
    else
        return 1;
}

bool SymbolTable::make_token_global(token global_token)
{
    bool return_value;
    //used as the temporary hold value
    token temp_token;
    //finds the token value using the string key
    temp_token = map[global_token.stringValue];
    //changes the value to global
    temp_token.global_scope = true;
    //puts the modified value back in the map
    map[temp_token.stringValue] = temp_token;
    //resyncs the token
    bool resync_status = resync_tables(-1, global_token);
    return resync_status;
}

bool SymbolTable::is_global_token(token token_to_check)
{
    bool is_global;
    is_global = token_to_check.global_scope;
    return is_global;
}

bool SymbolTable::scope_map_exists(int scope_id)
{
    //checks if a scope table of that id exists
    if (scope_table.find(scope_id) == scope_table.end())
    {
        return false;
    }
    else
    {
        return true;
    }
}

bool SymbolTable::create_new_scope_table(int scope_id)
{
    //creates a new object and inserts it into the table
    ScopeTable *table_to_make;
    table_to_make = new ScopeTable(scope_id);
    scope_table[scope_id] = *table_to_make;
}

bool SymbolTable::resync_tables(int scope_id, token token_to_sync)
{
    bool token_is_procedure_identifier;
    //this array will hold at least one scope id, more will be added in some cases
    std::vector<int> list_of_scopes;
    //adds the first scope id
    list_of_scopes.push_back(scope_id);
    //if the identifer is a procedure, it is visible on its own scope as well as the one above
    if (token_to_sync.identifer_type == I_PROCEDURE)
    {
        scope_table[scope_id].procedure_token = token_to_sync;
        if (scope_id > 0)
        {
            list_of_scopes.push_back(scope_id - 1);
        }
    }
    //holds the id of the current scope in the case we need to add multiple scopes
    int current_scope;
    for (int i = 0; i < list_of_scopes.size(); i++)
    {
        current_scope = list_of_scopes[i];
        //temp variableused to update the values of the tokens and map
        std::unordered_map<std::string, token> temp_map;
        //creates scope table if it doesnt exist
        if (!scope_map_exists(current_scope))
        {
            create_new_scope_table(current_scope);
        }

        //checks if the token is not in the scope table
        if (!scope_table[current_scope].is_in_table(token_to_sync.stringValue))
        {
            //creates the token if it isn't in the table
            scope_table[current_scope].insert_string_token(token_to_sync);
            return true;
        }
        else
        {
            //finds the appropriate map based on the scope id
            temp_map = scope_table[current_scope].scope_map;
            //making sure to add context on what scope the token is in
            token_to_sync.scope_id = current_scope;
            //the new token will have the same string value but different properties that will be synced
            temp_map[token_to_sync.stringValue] = token_to_sync;
            //writes the changes back
            scope_table[current_scope].scope_map = temp_map;
            //return true;
        }
    }
    return true;
    //return false;
}

bool SymbolTable::remove_scope(int scope_id)
{
    if (scope_map_exists(scope_id))
    {
        scope_table.erase(scope_id);
        return true;
    }
    else
    {
        return false;
    }
}

bool SymbolTable::update_token_scope_id(token token_to_update, int scope_id)
{
    bool return_value;

    token_to_update.scope_id = scope_id;
    token_to_update.procedure_params.clear();
    map[token_to_update.stringValue] = token_to_update;
    //creates a hash entry for the new scope if it doesn't already exist
    if (!scope_map_exists(scope_id))
    {
        create_new_scope_table(scope_id);
    }
    bool resync_status = resync_tables(scope_id, token_to_update);
    return resync_status;
}

bool SymbolTable::update_identifier_type(token token_to_update, int scope_id)
{
    identifier_types temp_identifier_type = I_NONE;
    token temp_token = token_to_update;
    bool array_status = false;
    if (scope_table[scope_id].is_in_table(token_to_update.stringValue))
    {
        token_to_update.scope_id = scope_id;
        scope_table[scope_id].scope_map[token_to_update.stringValue] = token_to_update;
    }
    else
    {
        token_to_update.scope_id = scope_id;
        scope_table[scope_id].insert_string_token(token_to_update);
    }
    // temp_identifier_type = token_to_update.identifer_type;
    // array_status = token_to_update.is_array;
    // if (token_is_in_scope_table(token_to_update.stringValue, scope_id))
    // {
    //     token_to_update = scope_table[scope_id].scope_map[token_to_update.stringValue];
    //     token_to_update.scope_id = scope_id;
    //     token_to_update.identifer_type = temp_identifier_type;
    //     token_to_update.is_array = array_status;
    // }
    // //updates the token
    // map[token_to_update.stringValue] = token_to_update;
    // //updates the token on the scope maps
    bool resync_status = resync_tables(scope_id, token_to_update);
    return resync_status;
}

bool SymbolTable::add_procedure_valid_inputs(std::string procedure_name, data_types valid_input_type, int scope_id)
{
    //used to hold the scope symbol table
    std::unordered_map<std::string, token> temp_scope_map;
    //temporary token to help change us change the map
    token procedure_identifier_token;
    //checks to see if the procedure is already in the scope table, if it is we grab it, else its new and we wipe allowed inputs
    if (!token_is_in_scope_table(procedure_name, scope_id))
    {
        //grads the token based on the name of the procedure
        procedure_identifier_token = map[procedure_name];
        procedure_identifier_token.scope_id = scope_id;
        procedure_identifier_token.procedure_params.clear();
    }
    else
    {
        //grabs the already existing token in the appropriate scope
        temp_scope_map = scope_table[scope_id].scope_map;
        procedure_identifier_token = temp_scope_map[procedure_name];
        procedure_identifier_token.scope_id = scope_id;
    }
    //adds the valid type to the procedure identifier
    procedure_identifier_token.procedure_params.push_back(valid_input_type);
    //adds the modified value back to the map
    map[procedure_name] = procedure_identifier_token;
    //we need to resync this change to the scope tables
    resync_tables(scope_id, procedure_identifier_token);
    return true;
}

bool SymbolTable::update_identifier_data_type(std::string identifier_name, data_types data_type, int scope_id)
{
    //used to hold the scope symbol table
    // std::unordered_map<std::string, token> temp_scope_map;
    // //grabs the token from the main table
    token token_to_update;
    // token_to_update = map[identifier_name];
    // //first makes sure that the scope of the token is updated
    // token_to_update.scope_id = scope_id;
    // //make sure that the data type of the token is updated
    // token_to_update.identifier_data_type = data_type;
    // //update the main map symboltable
    // map[token_to_update.stringValue] = token_to_update;

    if (scope_table[scope_id].is_in_table(identifier_name))
    {
        token_to_update = scope_table[scope_id].scope_map[identifier_name];
        token_to_update.scope_id = scope_id;
        token_to_update.identifier_data_type = data_type;
        scope_table[scope_id].scope_map[identifier_name] = token_to_update;
        //token_to_update = scope_table[scope_id].scope_map[identifier_name];
        //token_to_update.identifier_data_type = data_type;
    }
    else
    {
        token_to_update.scope_id = scope_id;
        token_to_update.identifier_data_type = data_type;
        //scope_table[scope_id].scope_map[identifier_name] = token_to_update;
        scope_table[scope_id].insert_string_token(token_to_update);
    }
    //resyncs the tables
    resync_tables(token_to_update.scope_id, token_to_update);

    return true;
}

//not needed?
bool SymbolTable::update_procedure_return_type(std::string procedure_name, data_types return_type, int scope_id)
{
    //temp token for manipulation
    token token_to_update;
    // //used to hold the scope symbol table
    // std::unordered_map<std::string, token> temp_scope_map;
    // temp_token = map[procedure_name];
    // temp_token.scope_id = scope_id;
    // temp_token.identifier_data_type = return_type;
    // //writes changes back
    // map[procedure_name] = temp_token;

    if (scope_table[scope_id].is_in_table(procedure_name))
    {
        token_to_update = scope_table[scope_id].scope_map[procedure_name];
        token_to_update.scope_id = scope_id;
        token_to_update.identifier_data_type = return_type;
        scope_table[scope_id].scope_map[procedure_name] = token_to_update;
    }
    else
    {
        token_to_update.scope_id = scope_id;
        token_to_update.identifier_data_type = return_type;
        scope_table[scope_id].insert_string_token(token_to_update);
    }
    //resyncs the tables
    resync_tables(token_to_update.scope_id, token_to_update);

    return true;
}

bool SymbolTable::token_is_in_scope_table(std::string token_string, int scope_id)
{
    std::unordered_map<std::string, token> temp_scope_map;
    temp_scope_map = scope_table[scope_id].scope_map;

    if (temp_scope_map.find(token_string) == temp_scope_map.end())
    {
        return false;
    }
    else
    {
        return true;
    }
}