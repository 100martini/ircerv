#include "Utils.hpp"
#include <sstream>
#include <cctype>
#include <cstdlib>

std::string Utils::trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

std::string Utils::removeComment(const std::string& line) {
    size_t pos = line.find('#');
    if (pos != std::string::npos)
        return line.substr(0, pos);
    return line;
}

std::vector<std::string> Utils::split(const std::string& str, char delim) {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, delim)) {
        token = trim(token);
        if (!token.empty())
            tokens.push_back(token);
    }
    return tokens;
}

size_t Utils::parseSize(const std::string& str) {
    size_t value;
    char unit = 0;
    std::stringstream ss(str);
    
    ss >> value;
    if (!ss.eof())
        ss >> unit;
    
    switch (toupper(unit)) {
        case 'G': 
            value *= 1024;
            value *= 1024;
            value *= 1024;
            break;
        case 'M': 
            value *= 1024;
            value *= 1024;
            break;
        case 'K': 
            value *= 1024;
            break;
        default: 
            break;
    }
    return value;
}

bool Utils::isNumber(const std::string& str) {
    if (str.empty()) return false;
    for (size_t i = 0; i < str.length(); i++) {
        if (!isdigit(str[i])) return false;
    }
    return true;
}