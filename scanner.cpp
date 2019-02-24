#include "scanner.h"

scanner::scanner(){
    
}

int scanner::what_is_char(char test_char){
    if(isalpha(test_char)){
        return alpha_char;
    }
    else if(isdigit(test_char)){
        return number_char;
    }
    else if(Is_Reserved_Char(test_char)){
        return reserved_char;
    }
    else{
        return invalid_char;
    }


}

bool scanner::is_first_char(){
    if(build_string.length() == 0){
        return 1;
    }
    else{
        return 0;
    }
}



bool scanner::Is_Reserved_Char(char test_char){
    //checks to see if the char is a reserved char
    if(symbol_table.is_reserved_char(test_char)){
        return 1;
    }
    else{
        return 0;
    }
}

bool scanner::Is_Reserved_Word(std::string test_word){
    //checks to see if the string is already in the symbol table
    if(symbol_table.is_in_table(Tolower_string(test_word))){
        return 1;
    }
    else{
        return 0;
    }

}




scanner::scanner(std::string file){
    //Things done here to prepare the scanner
    InitScanner(file);
    //opens the file name to begin reading the file
    source.open(FileName);
}

void scanner::InitScanner(std::string file){
    //sets the name of the Filename in the object to the value passed into the program
    FileName = file;
    //sets the current line to 1
    current_line = 1;
    //Starts reading the actual file
    ReadFile();
}

void scanner::ReadFile(){
    //keeps running while not at the end of the file
    // while(source.get(next_char)){
    while(true){
        //breaks loop if end of file is reached
        if(source.eof()){
            break;
        }
        //Gets a token and places it in the Current_token variable
        //Current_token = new token;
        Get_token();
        //build token vector here?
        if(Current_token.type == 279){
            std::cout<<"Token is an integer with a value of: "<<Current_token.intValue<<" with a type of "<<Current_token.type<<std::endl;

        }
        else if(Current_token.type == 280){
            std::cout<<"Token a float with a value of: "<<Current_token.floatValue<<" with a type of "<<Current_token.type<<std::endl;
        }
        else if(Current_token.charValue!=NULL){
            std::cout<<"Token is a char with a value of: "<<Current_token.charValue<<" with a type of "<<Current_token.type<<std::endl;

        }
        else{
        std::cout<<"Token is a reserved word or ID with a value of: "<<Current_token.stringValue<<" with a type of "<<Current_token.type<<std::endl;
        }
        //Current_token = NULL;
    }

}

//usefuly for quick tests; pass in an empty file
void scanner::test(){
    token *test_tok;
    test_tok = new token;
    *test_tok = symbol_table.map["if"];
    std::cout<<"Token is: "<<test_tok->stringValue<<" of type: "<<test_tok->type<<std::endl;
}

void scanner::Get_token(){
    while(true){
        //breaks loop if end of file is reached
        if(source.eof()){
            break;
        }
        current_char = next_char;
        if(is_first_char()){
            char_status = what_is_char(current_char);
            switch(char_status){
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
                invalid_char_test();
                break;

            }

        }
        build_string = "";
        break;
    }
}

void scanner::build_char_token(){
    //if the character is not in a quote, then it is a token
    if(!quote_status){
        Current_token.charValue = current_char;
        //Giant case statement here
        switch(current_char){

        }
        std::cout<<current_char<<std::endl;

    }
    // //else the character is part of a string
    // else{
    //     build_string + build_string + current_char;
    // }
    source.get(next_char);
}

void scanner::build_number_token(){
    int token_int_value;
    float token_float_value;
    bool is_float = false;
    bool one_decimal = true;
    //numbers are valid until a non numbber character is used, or multiple decimals are used
    // while(isdigit(next_char) || (next_char=='.' && one_decimal)){
    while(isdigit(next_char) || next_char=='.'){
        if(current_char == '.' && one_decimal){
            std::cout<<"The number has more than one decimal"<<std::endl;
            //throw error
            return;
            break;
        }
        if(current_char == '.'){
            one_decimal = false;
        }
        build_string = build_string + current_char;
        source.get(next_char);
        current_char = next_char;
        if(source.eof()){
            break;
        }
    }
    if(is_float){
        token_float_value = std::stof(build_string);
        //assign token type and value here
        Current_token.floatValue = token_float_value;
        Current_token.type = T_FLOAT_VALUE;

    }
    else{
        token_int_value = std::stoi(build_string);
        //assign token type and value here
        Current_token.intValue = token_int_value;
        Current_token.type = T_INTEGER_VALUE;

    }

}
//should probably return a string so that later checks on it can be done for reserved words
void scanner::build_string_token(){
    //token is valid until a non letter or number is displayed
    while(isdigit(next_char) || isalpha(next_char)){
        build_string = build_string + current_char;
        source.get(next_char);
        current_char = next_char;
        if(source.eof()){
            break;
        }
    }
    //checks to see whether the built string is either a reserved word or already in the symbol table
    if(symbol_table.is_in_table(build_string)){
        Current_token = symbol_table.map[build_string];

    }
    //if not in the symbol table it inserts the indentifier
    else{
        symbol_table.insert_stringValue(build_string,T_IDENTIFIER);
    }
    std::cout<<build_string<<std::endl;
    build_string = "";

}

void scanner::invalid_char_test(){
    //if current making a string
    if(quote_status){
        while(true){
            build_string = build_string + current_char;
            if(current_char == '"'){
                break;
            }
            //if a next line character is detected it increments the current_line counter
            if(current_char = '\n'){
                current_line++;
            }
            source.get(next_char);
            current_char = next_char;
            if(source.eof()){
                std::cout<<"Error, there was never a closing quotation"<<std::endl;
                break;
                //error never had a closing quote
            }
        }
        std::cout<<"The quotation is: "<<build_string<<std::endl;
        //sets the token type to a string and assigns the value
        Current_token.type = T_STRING_VALUE;
        Current_token.stringValue = build_string;
        Current_token.line_found = current_line;

    }
    //if not currently making a string, means either invalid or will be ignored
    else{
        if(current_char == '\n'){
            std::cout<<"End of line character detected"<<std::endl;
            current_line++;
        }
        else if(isspace(current_char)){
            std::cout<<"Space character detected"<<std::endl;
        }
        else{
        std::cout<<"Invalid character detected"<<std::endl;
        //throw an error
        }
        source.get(next_char);
    }
}
 


