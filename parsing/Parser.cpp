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
        
        if (line.find("location") == std::string::npos) {
            size_t open_pos = line.find('{');
            while (open_pos != std::string::npos) {
                braceCount++;
                open_pos = line.find('{', open_pos + 1);
            }
        }
        
        size_t close_pos = line.find('}');
        while (close_pos != std::string::npos) {
            braceCount--;
            if (braceCount == 0) break;
            close_pos = line.find('}', close_pos + 1);
        }
        
        if (braceCount == 0) break;
        
        std::istringstream iss(line);
        std::string directive;
        iss >> directive;
        
        if (directive != "location" && line[line.length() - 1] != ';') {
            throw ConfigException("Missing semicolon after directive: " + directive);
        }
        
        if (directive == "listen") {
            std::string listen_value;
            iss >> listen_value;
            listen_value = Utils::removeSemicolon(listen_value);
            
            size_t colon_pos = listen_value.find(':');
            if (colon_pos != std::string::npos) {
                server.host = listen_value.substr(0, colon_pos);
                server.port = std::atoi(listen_value.substr(colon_pos + 1).c_str());
            } else
                server.port = std::atoi(listen_value.c_str());
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
            std::string path, extra;
            iss >> path >> extra;
            
            if (!extra.empty() && extra != "{")
                throw ConfigException("location directive takes only one path, found extra: " + extra);
            
            if (path.empty() || path[0] != '/')
                throw ConfigException("location path must start with '/': " + path);
            
            if (path.find("//") != std::string::npos)
                throw ConfigException("invalid path with consecutive slashes: " + path);
            
            bool braceOnSameLine = (line.find('{') != std::string::npos);
            
            server.locations.push_back(parseLocation(file, path, braceOnSameLine));
        }
    }
    
    return server;
}

std::vector<ServerConfig> Parser::parseConfigFile(const std::string& filename) {
    std::vector<ServerConfig> servers;
    std::ifstream file(filename.c_str());
    
    if (!file.is_open())
        throw ConfigException("cannot open config file: " + filename);

    std::string line;
    while (std::getline(file, line)) {
        line = Utils::trim(Utils::removeComment(line));
        if (line.empty()) continue;
        
        if (line == "server" || line == "server {") {
            if (line == "server {")
                servers.push_back(parseServer(file));
            else {
                std::string next_line;
                if (std::getline(file, next_line)) {
                    next_line = Utils::trim(Utils::removeComment(next_line));
                    if (next_line == "{")
                        servers.push_back(parseServer(file));
                    else
                        throw ConfigException("expected '{' after 'server' directive, found: " + next_line);
                }
                else
                    throw ConfigException("unexpected end of file after 'server' directive");
            }
        } else if (line.find("server") == 0)
            throw ConfigException("invalid server directive: " + line + ". use 'server' or 'server {'");
    }
    
    file.close();
    
    if (servers.empty())
        throw ConfigException("no valid server blocks found in configuration file");
    
    return servers;
}