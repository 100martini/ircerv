#ifndef UTILS_HPP
#define UTILS_HPP

#include <string>
#include <vector>

class Utils {
public:
    static std::string trim(const std::string& str);
    static std::string removeComment(const std::string& line);
    static std::vector<std::string> split(const std::string& str, char delim);
    static size_t parseSize(const std::string& str);
    static bool isNumber(const std::string& str);
};

#endif