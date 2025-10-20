#include "CGIHandler.hpp"
#include <sys/wait.h>
#include <cstdlib>
#include <sys/types.h>
#include <unistd.h>
#include <cstring>
#include <sched.h>
#include <vector>
#include <sstream>
#include <cctype>

CGIHandler::CGIHandler(HttpRequest const& req, LocationConfig* location, std::string const & filepath, const ServerConfig* ServerConfig)
  : 
    _location(location),
    _request(req),
    _script_filename(filepath),
    _body(req.getBody()),
    servConfig(ServerConfig)
{}

std::string CGIHandler::getInterpreter(std::string const & extension)
{
  return _location->cgi[extension];
}

std::string convertHeaderName(const std::string& headerName) {
  if (headerName.empty()) return std::string("");
  std::string new_headerName;
  new_headerName.reserve(headerName.length() + 5);
  new_headerName = "HTTP_";
  for (size_t i = 0; i < headerName.length(); ++i) {
    if (isalpha(headerName[i]) && !isupper(headerName[i]))
      new_headerName += std::toupper(headerName[i]);
    else if (headerName[i] == '-')
      new_headerName += '_';
    else 
      new_headerName += headerName[i];
  }
  return new_headerName;
}

std::vector<std::string> CGIHandler::prepareEnv() {
    std::vector<std::string> env;
    env.push_back("GATEWAY_INTERFACE=CGI/1.0");
    env.push_back("SERVER_PROTOCOL=HTTP/1.0");
    env.push_back("SCRIPT_NAME=" + _script_filename);
    env.push_back("REQUEST_METHOD=" + _request.getMethod());
    env.push_back("QUERY_STRING=" + _request.getQueryString());
    env.push_back("SERVER_NAME=" + servConfig->server_name);
    
    std::stringstream port_ss;
    port_ss << servConfig->port;
    env.push_back("SERVER_PORT=" + port_ss.str());
    
    env.push_back("SERVER_PROTOCOL=" + _request.getVersion());
    env.push_back("REMOTE_ADDR=");
    if (_request.getMethod() == "POST" && _request.getContentlength() > 0) {
      env.push_back("CONTENT_TYPE=" + _request.getHeader("content-type"));
      
      std::stringstream length_ss;
      length_ss << _request.getContentlength();
      env.push_back("CONTENT_LENGTH=" + length_ss.str());
    }
    if (_request.hasHeaders()) {
      std::map<std::string, std::string> request_headers = _request.getHeadersMap();
      std::map<std::string, std::string>::const_iterator it;
      for (it = request_headers.begin(); it != request_headers.end(); ++it) {
        if (it->first != "content-type" && it->first != "content-length")
          env.push_back(convertHeaderName(it->first) + "=" + it->second);      
      }
    }
    return env;
}

char** CGIHandler::vectorToCharArray(const std::vector<std::string>& vec) {
    char** arr = new char*[vec.size() + 1];
    for (size_t i = 0; i < vec.size(); ++i) {
        arr[i] = new char[vec[i].length() + 1];
        std::strcpy(arr[i], vec[i].c_str());
    }
    arr[vec.size()] = NULL;
    return arr;
}

void CGIHandler::freeCharArray(char** arr) {
    for (int i = 0; arr[i] != NULL; ++i) {
        delete[] arr[i];
    }
    delete[] arr;
}

std::string CGIHandler::execute(std::string const & extension)
{
  int pipeIn[2];
  int pipeOut[2];
  if (pipe(pipeIn) == -1 || pipe(pipeOut) == -1) {
    return "HTTP/1.1 500 Internal Server Error\r\n\r\nPipe creation failed";
  }
  pid_t pid = fork();
  if (pid == -1)
  {
    close(pipeIn[0]);
    close(pipeIn[1]);
    close(pipeOut[0]);
    close(pipeOut[1]);
    return "HTTP/1.1 500 Internal Server Error\r\n\r\nFork failed";
  }
  if (pid == 0)
  {
    close(pipeIn[1]);
    close(pipeOut[0]);
    dup2(pipeIn[0], STDIN_FILENO);
    dup2(pipeOut[1], STDOUT_FILENO);
    close(pipeIn[0]);
    close(pipeOut[1]);
    std::vector<std::string> vec = prepareEnv();
    char **envp = vectorToCharArray(vec);
    std::string interpreter = getInterpreter(extension);
    if (interpreter.empty())
    {
      char* argv[] = { const_cast<char*>(_script_filename.c_str()), NULL };
      execve(_script_filename.c_str(), argv, envp);
      exit(1);
    }
    char* argv[] = { 
      const_cast<char*>(interpreter.c_str()), 
      const_cast<char*>(_script_filename.c_str()), 
      NULL 
    };
    execve(interpreter.c_str(), argv, envp);
    exit(1);
  }
  close(pipeIn[0]);
  close(pipeOut[1]);
  if (_request.getMethod() == "POST" && !_body.empty())
    write(pipeIn[1], _body.c_str(), _body.length());
  close(pipeIn[1]);
  std::string output;
  char buffer[4096];
  ssize_t bytes_read;
  while ((bytes_read = read(pipeOut[0], buffer, sizeof(buffer))) > 0)
    output.append(buffer, bytes_read);
  close(pipeOut[0]);
  int status;
  waitpid(pid, &status, 0);
  if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
    return output;
  }

  return "HTTP/1.0 500 Internal Server Error\r\n\r\nCGI execution failed";
}