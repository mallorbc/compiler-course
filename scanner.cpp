#include "scanner.h"

scanner::scanner(){
    
}
scanner::scanner(std::string file){
    //Things done here to prepare the scanner
    InitScanner(file);
}

void scanner::InitScanner(std::string file){
    //sets the name of the Filename in the object to the value passed into the program
    FileName = file;
    //Starts reading the actual file
    ReadFile();
}

void scanner::ReadFile(){
    //Creates an text input stream
    std::ifstream source;
    //used to track whether or not the character is the first character in the token
    bool first_char;
    //checks whether or not the first char is a number; if so, can't be an identifier
    bool first_char_is_num;
    //used to check the next variable; so to make sure that it isn't the end of the line, a space, of the EOF
    char next_char;
    //used to store the current character
    char current_char;
    //used to build a string from the characters after checking them.  Ease of use
    std::string build_string;
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
        else{
            //the character is neither a number or letter, making it a special character or illegal
            switch(current_char){
                case '(':
                break;

            }
        }
        }
 

    }

}

//usefuly for quick tests; pass in an empty file
void scanner::test(){
    char test_char = 'a';
    char output;
    output = tolower(test_char);
    std::cout<<output<<std::endl;
}

