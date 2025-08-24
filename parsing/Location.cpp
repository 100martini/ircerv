#include "Parser.hpp"
#include "Utils.hpp"
#include <sstream>
#include <cstdlib>

LocationConfig Parser::parseLocation(std::ifstream& file, const std::string& path) {
    LocationConfig location;
    location.path = path;
    std::string line;
    int braceCount = 0;
    
    std::getline(file, line);
    line = Utils::trim(Utils::removeComment(line));
    if (line.find('{') != std::string::npos) braceCount = 1;
    
    while (std::getline(file, line) && braceCount > 0) {
        line = Utils::trim(Utils::removeComment(line));
        if (line.empty()) continue;
        
        if (line.find('{') != std::string::npos) braceCount++;
        if (line.find('}') != std::string::npos) {
            braceCount--;
            if (braceCount == 0) break;
            continue;
        }
        
        std::istringstream iss(line);
        std::string directive;
        iss >> directive;
        
        if (directive == "methods") {
            std::string method;
            while (iss >> method) {
                method = Utils::removeSemicolon(method);
                if (!method.empty())
                    location.methods.insert(method);
            }
        }
        else if (directive == "root") {
            iss >> location.root;
            location.root = Utils::removeSemicolon(location.root);
        }
        else if (directive == "index") {
            std::string idx;
            location.index.clear();
            while (iss >> idx) {
                idx = Utils::removeSemicolon(idx);
                if (!idx.empty()) {
                    if (!location.index.empty()) location.index += " ";
                    location.index += idx;
                }
            }
        }
        else if (directive == "autoindex") {
            std::string value;
            iss >> value;
            value = Utils::removeSemicolon(value);
            location.autoindex = (value == "on");
        }
        else if (directive == "upload_path") {
            iss >> location.upload_path;
            location.upload_path = Utils::removeSemicolon(location.upload_path);
        }
        else if (directive == "return") {
            iss >> location.redirect.first >> location.redirect.second;
            location.redirect.second = Utils::removeSemicolon(location.redirect.second);
        }
        else if (directive == "cgi") {
            std::string ext, handler;
            iss >> ext >> handler;
            handler = Utils::removeSemicolon(handler);
            location.cgi[ext] = handler;
        }
        else if (directive == "client_max_body_count") {
            std::string size;
            iss >> size;
            size = Utils::removeSemicolon(size);
            location.client_max_body_count = Utils::parseSize(size);
            location.has_body_count = true;
        }
    }
    
    if (location.methods.empty())
        location.methods.insert("GET");
    
    if (location.index.empty())
        location.index = "index.html"; 
    
    return location;
}