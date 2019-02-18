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

    //opens the file name to begin reading the file
    source.open(FileName);
    //keeps running while not at the end of the file
    while(!source.eof()){
    //gets the next character in the file
    source.get(next_char);
    //check to make sure that we are not at the end of the line, not at a space, and not at the EOF
    if(next_char == '\n' || isspace(next_char) || source.eof()){
        //outputs the token if the line wasn't blank
        if (build_string.length()>0){
            std::cout<<"\nToken was: "<<build_string;
        }
        //increments the scanner line tracker if at the end of the line
        if(next_char = '\n'){
            current_line++;
        }
        //clears the string to build the next token
        std::cout<<"\nEnd of token";
        build_string="";
    }
    //else the character is part of a valid token
    else{
        if(build_string.length() == 0){
            first_char = true;
        }
        current_char = next_char;
        if(isalpha(current_char)){
            //lowers any capital letters to lower case to ease process; Doesn't affect non letters
            //current_char = tolower(next_char);
            //TO DO: Make sure that the character is not a bracket or operation first
            //progressively builds up a string
            build_string = build_string + current_char;

        }
        else if(isdigit(current_char)){
            //if the first character is a number, then that means
            //the first character in the token is a number,
            //making it either a float or integer
            if(first_char){
                first_char_is_num = true;
            }

        }
        //if not a reserved character, then the character is invalid
        // else if(Is_Reserved_Char(current_char)){
        //     //the character is neither a number or letter, making it a special character or illegal
            
        // }
        // else{
        //     //make an error
        // }
        }
 

    }

}

//usefuly for quick tests; pass in an empty file
void scanner::test(){
    std::cout<<Is_Reserved_Word("fOr");
}

// token scanner::Get_token(){

// }

