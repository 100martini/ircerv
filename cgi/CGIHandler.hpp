#pragma once
#include <iostream>
#include "../http/HttpRequest.hpp"
#include <vector>
class CGIHandler
{
  private:
    std::string _request_method;
    std::string _query_string;
    std::string _content_length;
    std::string _content_type;
    std::string _script_filename;
    std::string _body;
    std::vector<std::string> prepareEnv();
    char** vectorToCharArray(const std::vector<std::string>& vec);
    void freeCharArray(char** arr);
  public:
    CGIHandler(HttpRequest const& req, std::map<std::string, std::string>headers_);
    std::string getInterpreter();
    std::string execute();
};
