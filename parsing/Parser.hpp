#ifndef PARSER_HPP
#define PARSER_HPP

#include "Config.hpp"
#include <fstream>
#include <string>
#include <vector>

class Parser {
public:
    static std::vector<ServerConfig> parseConfigFile(const std::string& filename);
    static ServerConfig parseServer(std::ifstream& file);
    static LocationConfig parseLocation(std::ifstream& file, const std::string& path);
};

#endif