#include "CustomFunctions.h"

std::string Tolower_string(std::string data){
    //converts the string to all lowercase
    std::transform(data.begin(), data.end(), data.begin(), ::tolower);
    return data;

}