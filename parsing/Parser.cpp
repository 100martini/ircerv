// Parser.cpp - Improved version
#include "Parser.hpp"
#include "Utils.hpp"
#include <sstream>
#include <cstdlib>

ServerConfig Parser::parseServer(std::ifstream& file) {
    ServerConfig server;
    std::string line;
    int braceCount = 1;
    
    while (std::getline(file, line) && braceCount > 0) {
        line = Utils::trim(Utils::removeComment(line));
        if (line.empty()) continue;
        
        if (line.find('{') != std::string::npos && line.find("location") == std::string::npos) 
            braceCount++;
        if (line.find('}') != std::string::npos) {
            braceCount--;
            if (braceCount == 0) break;
            continue;
        }
        
        std::istringstream iss(line);
        std::string directive;
        iss >> directive;
        
        if (directive == "listen") {
            std::string listen_value;
            iss >> listen_value;
            size_t semicolon_pos = listen_value.find(';');
            if (semicolon_pos != std::string::npos)
                listen_value = listen_value.substr(0, semicolon_pos);
            
            size_t colon_pos = listen_value.find(':');
            if (colon_pos != std::string::npos) {
                server.host = listen_value.substr(0, colon_pos);
                server.port = std::atoi(listen_value.substr(colon_pos + 1).c_str());
            } else {
                server.port = std::atoi(listen_value.c_str());
            }
        }
        else if (directive == "host") {
            iss >> server.host;
            server.host = Utils::removeSemicolon(server.host);
        }
        else if (directive == "server_name") {
            iss >> server.server_name;
            server.server_name = Utils::removeSemicolon(server.server_name);
        }
        else if (directive == "error_page") {
            std::vector<int> codes;
            std::string token, page;
            
            while (iss >> token) {
                if (token[0] == '/') {
                    page = Utils::removeSemicolon(token);
                    break;
                }
                codes.push_back(std::atoi(token.c_str()));
            }
            
            for (size_t i = 0; i < codes.size(); i++)
                server.error_pages[codes[i]] = page;
        }
        else if (directive == "client_max_body_count") {  
            std::string size;
            iss >> size;
            size = Utils::removeSemicolon(size);
            server.client_max_body_count = Utils::parseSize(size);
        }
        else if (directive == "location") {
            std::string path;
            iss >> path;
            
            if (path.empty() || path[0] != '/')
                throw ConfigException("Location path must start with '/': " + path);
            
            server.locations.push_back(parseLocation(file, path));
        }
    }
    
    return server;
}

std::vector<ServerConfig> Parser::parseConfigFile(const std::string& filename) {
    std::vector<ServerConfig> servers;
    std::ifstream file(filename.c_str());
    
    if (!file.is_open())
        throw ConfigException("Cannot open config file: " + filename);

    std::string line;
    while (std::getline(file, line)) {
        line = Utils::trim(Utils::removeComment(line));
        if (line.empty()) continue;
        
        if (line.find("server") == 0) {
            size_t brace_pos = line.find('{');
            if (brace_pos != std::string::npos)
                servers.push_back(parseServer(file));
            else {
                std::string next_line;
                if (std::getline(file, next_line)) {
                    next_line = Utils::trim(Utils::removeComment(next_line));
                    if (next_line == "{")
                        servers.push_back(parseServer(file));
                }
            }
        }
    }
    
    file.close();
    
    if (servers.empty())
        throw ConfigException("No valid server blocks found in configuration file");
    
    return servers;
}