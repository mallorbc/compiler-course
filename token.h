#ifndef TOKEN_H
#define TOKEN_H
#include<string>

enum token
{
 T_SEMICOLON = ';',
 T_LPARAM = '(',
 T_RPARAM = ')',
 T_ASSIGN = '=',
 T_DIVIDE = '/',

 T_WHILE = 257,
 T_IF = 258,
 T_RETURN = 259,
 
 T_IDENTIFIER = 268,
 T_INTEGER = 269,
 T_DOUBLE = 270,
 T_STRING = 271



};

#endif // !TOKEN_H