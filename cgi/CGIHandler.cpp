#include "CGIHandler.hpp"
#include <sys/wait.h>
#include <cstdlib>
#include <sys/types.h>
#include <unistd.h>
#include <cstring>
#include <sched.h>
#include <vector>

CGIHandler::CGIHandler(HttpRequest const& req, LocationConfig* location, std::string const & filepath)
  : 
    _location(location),
    _request_method(req.getMethod()),
    _query_string(req.getQueryString()),
    _content_length(req.getHeader("content-length")),
    _content_type(req.getHeader("content-type")),
    _script_filename(filepath),
    _body(req.getBody())
{}

std::string CGIHandler::getInterpreter(std::string const & extension)
{
  return _location->cgi[extension];
}

std::vector<std::string> CGIHandler::prepareEnv() {
    std::vector<std::string> env;
    
    env.push_back("REQUEST_METHOD=" + _request_method);
    env.push_back("SCRIPT_FILENAME=" + _script_filename);
    env.push_back("QUERY_STRING=" + _query_string);
    env.push_back("CONTENT_TYPE=" + _content_type);
    env.push_back("CONTENT_LENGTH=" + _content_length);
    env.push_back("GATEWAY_INTERFACE=CGI/1.0");
    env.push_back("SERVER_PROTOCOL=HTTP/1.0");
    env.push_back("SERVER_SOFTWARE=BasicHTTPServer/1.0");
    env.push_back("REDIRECT_STATUS=200");

    // Add other headers with HTTP_ prefix
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
  if (_request_method == "POST" && !_body.empty())
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
    // if (output.find("Content-Type:") != std::string::npos ||
    //     output.find("Content-type:") != std::string::npos) {
    //   return "HTTP/1.0 200 OK\r\n" + output;
    // } else {
    //   return "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\n" + output;
    // }
  }

  return "HTTP/1.0 500 Internal Server Error\r\n\r\nCGI execution failed";
}