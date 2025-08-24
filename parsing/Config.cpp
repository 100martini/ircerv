#include "Config.hpp"
#include "Parser.hpp"
#include <fstream>
#include <sstream>

LocationConfig::LocationConfig() 
    : autoindex(false), 
      redirect(0, ""), 
      client_max_body_count(0), 
      has_body_count(false) {}

ServerConfig::ServerConfig() 
    : port(80), 
      host("0.0.0.0"), 
      client_max_body_count(1048576) {}

Config::Config() {}

Config::Config(const std::string& config_file) : _config_file(config_file) {
    parse();
}

Config::~Config() {}

void Config::parse() {
    if (_config_file.empty())
        throw ConfigException("No configuration file specified");
    parse(_config_file);
}

void Config::parse(const std::string& config_file) {
    _config_file = config_file;
    _servers = Parser::parseConfigFile(config_file);
    validate();
}

void Config::validate() {
    if (_servers.empty())
        throw ConfigException("No server blocks found in configuration");
    
    std::set<std::pair<std::string, int> > addresses;
    for (size_t i = 0; i < _servers.size(); i++) {
        if (_servers[i].port < 1 || _servers[i].port > 65535) {
            std::stringstream ss;
            ss << "Invalid port number: " << _servers[i].port;
            throw ConfigException(ss.str());
        }
        
        std::pair<std::string, int> addr(_servers[i].host, _servers[i].port);
        if (addresses.find(addr) != addresses.end()) {
            std::stringstream ss;
            ss << "Duplicate listen address: " << _servers[i].host << ":" << _servers[i].port;
            throw ConfigException(ss.str());
        }
        addresses.insert(addr);
        
        if (_servers[i].locations.empty())
            throw ConfigException("Server block must have at least one location");
        
        std::set<std::string> location_paths;
        for (size_t j = 0; j < _servers[i].locations.size(); j++) {
            if (location_paths.find(_servers[i].locations[j].path) != location_paths.end()) {
                std::stringstream ss;
                ss << "Duplicate location path in server: " << _servers[i].locations[j].path;
                throw ConfigException(ss.str());
            }
            location_paths.insert(_servers[i].locations[j].path);
        }
    }
}

const std::vector<ServerConfig>& Config::getServers() const {
    return _servers;
}

std::vector<ServerConfig>& Config::getServers() {
    return _servers;
}

ConfigException::ConfigException(const std::string& msg) 
    : _msg("Config Error: " + msg) {}

ConfigException::~ConfigException() throw() {}

const char* ConfigException::what() const throw() {
    return _msg.c_str();
}