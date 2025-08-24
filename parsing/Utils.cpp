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

std::string Utils::removeSemicolon(const std::string& str) {
    size_t pos = str.find(';');
    if (pos != std::string::npos)
        return str.substr(0, pos);
    return str;
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
    if (str.empty()) {
        throw std::runtime_error("Empty size string");
    }
    
    size_t value = 0;
    std::string num_str;
    char unit = 0;
    
    size_t i = 0;
    while (i < str.length() && (std::isdigit(str[i]) || str[i] == '.')) {
        num_str += str[i];
        i++;
    }
    
    if (i < str.length())
        unit = str[i];
    
    std::stringstream ss(num_str);
    ss >> value;
    
    if (ss.fail())
        throw std::runtime_error("Invalid size format: " + str);
    
    switch (std::toupper(unit)) {
        case 'G': value *= 1024UL * 1024UL * 1024UL; break;
        case 'M': value *= 1024UL * 1024UL; break;
        case 'K': value *= 1024UL; break;
        case '\0':
        case 'B':
            break;
        default:
            throw std::runtime_error("Invalid size unit: " + std::string(1, unit));
    }
    
    return value;
}

bool Utils::isNumber(const std::string& str) {
    if (str.empty()) return false;
    for (size_t i = 0; i < str.length(); i++) {
        if (!std::isdigit(str[i])) return false;
    }
    return true;
}