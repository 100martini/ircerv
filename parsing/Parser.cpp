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
            iss >> server.port;
        }
        else if (directive == "host") {
            iss >> server.host;
            server.host = server.host.substr(0, server.host.find(';'));
        }
        else if (directive == "server_name") {
            iss >> server.server_name;
            server.server_name = server.server_name.substr(0, server.server_name.find(';'));
        }
        else if (directive == "error_page") {
            std::vector<int> codes;
            std::string token, page;
            
            while (iss >> token) {
                if (token[0] == '/') {
                    page = token.substr(0, token.find(';'));
                    break;
                }
                codes.push_back(std::atoi(token.c_str()));
            }
            
            for (size_t i = 0; i < codes.size(); i++) {
                server.error_pages[codes[i]] = page;
            }
        }
        else if (directive == "client_max_body_count") {
            std::string size;
            iss >> size;
            size = size.substr(0, size.find(';'));
            server.client_max_body_count = Utils::parseSize(size);
        }
        else if (directive == "location") {
            std::string path;
            iss >> path;
            server.locations.push_back(parseLocation(file, path));
        }
    }
    
    return server;
}

std::vector<ServerConfig> Parser::parseConfigFile(const std::string& filename) {
    std::vector<ServerConfig> servers;
    std::ifstream file(filename.c_str());
    
    if (!file.is_open()) {
        throw ConfigException("Cannot open config file: " + filename);
    }
    
    std::string line;
    while (std::getline(file, line)) {
        line = Utils::trim(Utils::removeComment(line));
        if (line.empty()) continue;
        
        if (line == "server {" || (line.find("server") == 0 && line.find('{') != std::string::npos)) {
            servers.push_back(parseServer(file));
        }
    }
    
    file.close();
    return servers;
}