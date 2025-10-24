#include "CGIHelpers.hpp"

std::string extractValueAfterColon(const std::string& line) {
    std::string value;
    size_t colon_pos = line.find(":");
    if (colon_pos == std::string::npos) return "";
    value = line.substr(colon_pos + 1);
    value = HttpRequest::trim(value);
    return value;
}

void setCGIResponseHeaders(HttpResponse& response, const std::string& headers) {
    std::string content_type;
    std::string location;
    size_t content_length = 0;
    int status_code = 200;
    
    std::istringstream iss(headers);
    std::string line;

    while (std::getline(iss, line)) {
        if (line == "\r" || line.empty()) // momkin ERROR hna !!!
            break;
        if (line.find("status:", 0) != std::string::npos)
            status_code = atoi(extractValueAfterColon(line).c_str());
        else if (line.find("content-length:", 0) != std::string::npos) {
            content_length = atoi(extractValueAfterColon(line).c_str());
            response.setContentLength(content_length); 
        } else if (line.find("content-type:", 0) != std::string::npos) {
            content_type = extractValueAfterColon(line);
            response.setContentType(content_type);
        } else if (line.find("location:", 0), std::string::npos) {
            location = extractValueAfterColon(line);
            if (!location.empty()) response.setLocation(location);
        } else {
            size_t colon_pos = line.find(':');
            std::string name = line.substr(0, colon_pos);
            std::string value = extractValueAfterColon(value);
            response.setHeader(name, value);
        }
    }

    // Set default if not found
    if (!response.getStatusCode()) response.setStatus(status_code);
    if (response.getHeader("Content-Length").empty()) 
        response.setContentLength(response.getBody().length());
    if (response.getHeader("Content-Type").empty())
        response.setContentType("text/html; charset=utf-8");
}

// validate that the CGI headers comply with HTTP/1.0 formatting rules!!!
HttpResponse processCGIOutput(const std::string& cgi_output) {
    HttpResponse cgi_response;
    size_t headers_end = cgi_output.find("\r\n\r\n");
    if (headers_end == std::string::npos) 
        headers_end = cgi_output.find("\n\n");

    if (headers_end == std::string::npos) {
        std::string headers = "";
        std::string body = cgi_output;
        cgi_response.setStatus(200);
        cgi_response.setContentType("text/html; charset=utf-8");
        cgi_response.setBody(body);
        return cgi_response;
    }

    std::string headers = cgi_output.substr(0, headers_end);
    std::string body = cgi_output.substr(headers_end + 1);

    cgi_response.setBody(body);
    setCGIResponseHeaders(cgi_response, headers);
    return cgi_response;
}

bool setFdNonBlocking(int fd) {
    int flags;
    flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
       return false;
    }
    return true;
} 

std::string convertHeaderName(const std::string& headerName) {
  if (headerName.empty()) return std::string("");
  std::string new_headerName;
  new_headerName.reserve(headerName.length() + 5);
  new_headerName.replace(0, 5, "HTTP_");
  for (size_t i = 0; i < headerName.size(); ++i) {
    if (isalpha(headerName[i]) && !isupper(headerName[i]))
      new_headerName[i + 5] = std::toupper(headerName[i]);
    else if (headerName[i] == '-')
      new_headerName[i + 5] = '_';
    else 
      new_headerName[i + 5] = headerName[i];
  }
  return new_headerName;
}

std::vector<std::string> prepareEnv(const HttpRequest* _request, const ServerConfig* servConfig, const std::string& _script_filename) {
    std::vector<std::string> env;
    env.push_back("GATEWAY_INTERFACE=CGI/1.0");
    env.push_back("SERVER_PROTOCOL=HTTP/1.0");
    env.push_back("SCRIPT_NAME=" + _script_filename);
    env.push_back("REQUEST_METHOD=" + _request->getMethod());
    env.push_back("QUERY_STRING=" + _request->getQueryString());
    env.push_back("SERVER_NAME=" + servConfig->server_name);
    // env.push_back("SERVER_PORT=" + servConfig->port);
    env.push_back("SERVER_PROTOCOL=" + _request->getVersion());
    env.push_back("REMOTE_ADDR=");        // clinet ip address
    if (_request->getMethod() == "POST") {
      env.push_back("CONTENT_TYPE=" + _request->getHeader("content-type"));
      env.push_back("CONTENT_LENGTH=" + _request->getHeader("content-length"));
    }
    if (!_request->hasHeaders()) {
      std::map<std::string, std::string> request_headers = _request->getHeadersMap();
      std::map<std::string, std::string>::const_iterator it;
      for (it = request_headers.begin(); it != request_headers.end(); ++it) {
        if (it->first != "content-type" && it->first != "content-length")
          env.push_back(convertHeaderName(it->first) + it->second);      
      }
    }
  return env;
}

char** vectorToCharArray(const std::vector<std::string>& vec) {
    char** arr = new char*[vec.size() + 1];
    for (size_t i = 0; i < vec.size(); ++i) {
        arr[i] = new char[vec[i].length() + 1];
        std::strcpy(arr[i], vec[i].c_str());
    }
    arr[vec.size()] = NULL;
    return arr;
}

void freeCharArray(char** arr) {
    for (int i = 0; arr[i] != NULL; ++i) {
        delete[] arr[i];
    }
    delete[] arr;
}