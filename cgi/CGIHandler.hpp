#pragma once
#include <iostream>
#include "../http/HttpRequest.hpp"
#include "../parsing/Config.hpp"
#include <vector>
#include <fcntl.h>
#include <poll.h>
class CGIHandler
{
  private:
    LocationConfig *_location;
    const HttpRequest& _request;
    std::string _script_filename;
    std::string _body;
    const ServerConfig* servConfig;
    //size_t timeout_;
    
    std::vector<std::string> prepareEnv();
    char** vectorToCharArray(const std::vector<std::string>& vec);
    void freeCharArray(char** arr);
  public:
    CGIHandler(HttpRequest const& req, LocationConfig *location, std::string const & filepath, const ServerConfig* serverConfig);
    std::string getInterpreter(std::string const & extension);
    std::string execute(std::string const & extension);
};