#include "scanner.h"

scanner::scanner(){
    
}


// void scanner::Build_Reserved_Char_Table(){
//     //builds vector of reserved characters
//     Reserved_Chars.push_back('(');
//     Reserved_Chars.push_back(')');
//     Reserved_Chars.push_back('[');
//     Reserved_Chars.push_back(']');
//     Reserved_Chars.push_back(',');
//     Reserved_Chars.push_back('/');
//     Reserved_Chars.push_back(';');
//     Reserved_Chars.push_back('{');
//     Reserved_Chars.push_back('}');
//     Reserved_Chars.push_back('=');
//     Reserved_Chars.push_back('+');
//     Reserved_Chars.push_back('_');
//     Reserved_Chars.push_back('.');
//     Reserved_Chars.push_back('>');
//     Reserved_Chars.push_back('<');
//     Reserved_Chars.push_back('*');
//     Reserved_Chars.push_back('"');
//     Reserved_Chars.push_back('!');
// }

// bool scanner::Is_Reserved_Char(char test_char){
//     //checks all alues in the vector to see if is a reserved char; if it is return true
//     for(int i = 0; i<Reserved_Chars.size(); i++){
//         if(Reserved_Chars[i] == test_char){
//             return true;
//         }
//     }
//     return false;

// }

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
    //Builds vector of special characters
    //Build_Reserved_Char_Table();
    //sets the current line tracker
    current_line = 1;
    //Starts reading the actual file
    ReadFile();
}

void scanner::ReadFile(){
    //keeps running while not at the end of the file
    while(source.get(next_char)){
        //breaks loop if end of file is reached
        if(source.eof()){
            break;
        }
        //Gets a token and places it in the Current_token variable
        Get_token();
        //build token vector here?
        std::cout<<"Token is: "<<Current_token.stringValue<<" of type: "<<Current_token.type<<std::endl;
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
    //token *new_token;
    //new_token = new token;
    //if we are at
    while(next_char != '\n' && !isspace(next_char)){


        //breaks loop if end of file is reached
        if(source.eof()){
            break;
        }
        current_char = next_char;
        //check for special characters here
        if(build_string.size() == 0){
            //first_char = true;
            if(isdigit(current_char)){
                first_char_is_num = true;
            }
            //this will either lead to a reserved word for a token or an identifier
            else if(isalpha(current_char)){
                first_char_is_alpha = true;
            }

        }
        build_string = build_string + current_char;
        source.get(next_char);
        //std::cout<<"Build String is "<<build_string<<std::endl;        
    }
    //since the first character was a letter, the only options are an identifier or a keyword
    if(first_char_is_alpha){
        //if not in table, and then must be a identifier
        if(!symbol_table.is_in_table(build_string)){
            symbol_table.insert_stringValue(build_string,T_IDENTIFIER);
            Current_token = symbol_table.map[build_string];

        }
        //else the token is a key word
        else{
            Current_token = symbol_table.map[build_string];
            //*new_token = symbol_table.map[build_string];
            //return *new_token;
        }

    }
    else
    
        //new_token->stringValue = build_string;
        build_string = "";
        //return *new_token;
    if(next_char == '\n'){
        //std::cout<<"new line detected"<<std::endl;
    }
    // while(next_char != '\n' && !isspace(next_char)){
    //     build_string = build_string + next_char;
    // }
    // std::cout<<build_string<<std::endl;
//    while(next_char != '\n' && !isspace(next_char) && !source.eof()){
//         while(next_char != '\n' && !isspace(next_char) && !source.get(next_char).eof()){
//     source.get(next_char);
//     //check to make sure that we are not at the end of the line, not at a space, and not at the EOF
//     // if(next_char == '\n' || isspace(next_char) || source.eof()){
//     //     //outputs the token if the line wasn't blank
//     //     if (build_string.length()>0){
//     //         std::cout<<"\nToken was: "<<build_string;
//     //         return;
//     //     }
//     //     //increments the scanner line tracker if at the end of the line
//     //     if(next_char = '\n'){
//     //         current_line++;
//     //     }
//     //     //clears the string to build the next token
//     //     std::cout<<"\nEnd of token";
//     //     build_string="";
//     // }
//     //else the character is part of a valid token
//     //else{
//         if(build_string.length() == 0){
//             first_char = true;
//         }
//         current_char = next_char;
//         if(isalpha(current_char)){
//             //lowers any capital letters to lower case to ease process; Doesn't affect non letters
//             //current_char = tolower(next_char);
//             //TO DO: Make sure that the character is not a bracket or operation first
//             //progressively builds up a string
//             build_string = build_string + current_char;
//             std::cout<<build_string;

//         }
//         else if(isdigit(current_char)){
//             //if the first character is a number, then that means
//             //the first character in the token is a number,
//             //making it either a float or integer
//             if(first_char){
//                 first_char_is_num = true;
//             }

//         }
//         //if not a reserved character, then the character is invalid
//         // else if(Is_Reserved_Char(current_char)){
//         //     //the character is neither a number or letter, making it a special character or illegal
            
//         // }
//         // else{
//         //     //make an error
//         // }
//         //}
// }
//return Current_token;
        //resets variables used to track what is being scanned
        first_char_is_alpha = false;
        first_char_is_num = false;
}

