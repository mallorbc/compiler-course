#include "scanner.h"

scanner::scanner(){
    
}

void scanner::Build_Reserved_Words_Table(){
    Reserved_Words.push_back("program");
    Reserved_Words.push_back("is");
    Reserved_Words.push_back("begin");
    Reserved_Words.push_back("end");
    Reserved_Words.push_back("global");
    Reserved_Words.push_back("procedure");
    Reserved_Words.push_back("variable");
    Reserved_Words.push_back("type");
    Reserved_Words.push_back("integer");
    Reserved_Words.push_back("float");
    Reserved_Words.push_back("string");
    Reserved_Words.push_back("bool");
    Reserved_Words.push_back("enum");
    Reserved_Words.push_back("if");
    Reserved_Words.push_back("then");
    Reserved_Words.push_back("else");
    Reserved_Words.push_back("for");
    Reserved_Words.push_back("return");
    Reserved_Words.push_back("not");
    Reserved_Words.push_back("true");
    Reserved_Words.push_back("false");

}

void scanner::Build_Reserved_Char_Table(){
    Reserved_Chars.push_back('(');
    Reserved_Chars.push_back(')');
    Reserved_Chars.push_back('[');
    Reserved_Chars.push_back(']');
    Reserved_Chars.push_back(',');
    Reserved_Chars.push_back('/');
    Reserved_Chars.push_back(';');
    Reserved_Chars.push_back('{');
    Reserved_Chars.push_back('}');
    Reserved_Chars.push_back('=');
    Reserved_Chars.push_back('+');
    Reserved_Chars.push_back('_');
    Reserved_Chars.push_back('.');
    Reserved_Chars.push_back('>');
    Reserved_Chars.push_back('<');
    Reserved_Chars.push_back('*');
    Reserved_Chars.push_back('"');
}

bool scanner::Is_Reserved_Char(char test_char){
    for(int i = 0; i<Reserved_Chars.size(); i++){
        if(Reserved_Chars[i] == test_char){
            return true;
        }
    }
    return false;

}

bool scanner::Is_Reserved_Word(std::string test_word){
    for(int i = 0; i<Reserved_Words.size(); i++){
        if(Reserved_Words[i] == test_word){
            return true;
        }
    }
    return false;
}

std::string scanner::Tolower_string(std::string data){
    std::string return_value;
    std::transform(data.begin(), data.end(), data.begin(), ::tolower);
    return data;

}


scanner::scanner(std::string file){
    //Things done here to prepare the scanner
    InitScanner(file);
}

void scanner::InitScanner(std::string file){
    //sets the name of the Filename in the object to the value passed into the program
    FileName = file;
    //Builds vector of reserved words
    Build_Reserved_Words_Table();
    //Builds vector of special characters
    Build_Reserved_Char_Table();
    //sets the current line tracker
    current_line = 1;
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
    std::cout<<Is_Reserved_Word(Tolower_string("If"));
}

