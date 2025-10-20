#include "Client.hpp"
#include "../http/HttpResponse.hpp"
#include "../http/Methods.hpp"
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fstream>
#include <sstream>
#include <cstring>
#include <iostream>
#include <cstdlib>

Client::Client(int _fd, const ServerConfig* config) 
    : fd(_fd), 
      state(READING_REQUEST), 
      server_config(config),
      bytes_sent(0),
      keep_alive(false) {
    last_activity = std::time(NULL);
}

Client::~Client() {
    close();
}

bool Client::readRequest() {
    char buffer[8192];
    ssize_t bytes = recv(fd, buffer, sizeof(buffer), 0);
    
    if (bytes > 0) {
        last_activity = std::time(NULL);
        
        try {
            int parser_state = http_parser.parseHttpRequest(std::string(buffer, bytes));
            
            if (parser_state == COMPLETE) {
                state = PROCESSING_REQUEST;
                return true;
            }
            else if (parser_state == ERROR) {
                buildErrorResponse(400, "Bad Request");
                state = SENDING_RESPONSE;
                return false;
            }
            return false;
            
        } catch (const MissingContentLengthException& e) {
            buildErrorResponse(411, "Length Required");
            state = SENDING_RESPONSE;
            return false;
        } catch (const InvalidContentLengthException& e) {
            buildErrorResponse(400, "Invalid Content-Length");
            state = SENDING_RESPONSE;
            return false;
        } catch (const InvalidMethodName& e) {
            buildErrorResponse(405, "Method Not Allowed");
            state = SENDING_RESPONSE;
            return false;
        } catch (const HttpRequestException& e) {
            buildErrorResponse(400, "Bad Request");
            state = SENDING_RESPONSE;
            return false;
        }
    }
    else if (bytes == 0) {
        state = CLOSING;
        return false;
    }
    else {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            state = CLOSING;
        return false;
    }
}

bool Client::sendResponse() {
    if (response_buffer.empty())
        return true;
    
    ssize_t bytes = send(fd, response_buffer.c_str() + bytes_sent, response_buffer.length() - bytes_sent, 0);

    if (bytes > 0) {
        bytes_sent += bytes;
        last_activity = std::time(NULL);
        
        if (bytes_sent >= response_buffer.length()) {
            response_buffer.clear();
            bytes_sent = 0;
            
            if (keep_alive) {
                http_parser.reset();
                state = READING_REQUEST;
                return false;
            }
            return true;
        }
        return false;
    }
    else if (bytes == 0) {
        return false;
    }
    else {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            state = CLOSING;
        return false;
    }
}

void Client::processRequest() {
    const HttpRequest& request = http_parser.getRequest();
    HttpResponse response;
    
    std::string method = request.getMethod();
    std::string path = request.getPath();
    std::string version = request.getVersion();
    std::string connection = request.getHeader("Connection");
    
    keep_alive = (version == "HTTP/1.1" && connection != "close") ||
                 (version == "HTTP/1.0" && connection == "keep-alive");
    
    LocationConfig* location = findMatchingLocation(path);
    
    if (!location) {
        location = findMatchingLocation("/");
        if (!location) {
            response = HttpResponse::makeError(404);
            response_buffer = response.toString();
            state = SENDING_RESPONSE;
            return;
        }
    }
    
    if (location->redirect.first > 0) {
        response = HttpResponse::makeRedirect(location->redirect.first, 
                                             location->redirect.second);
        response_buffer = response.toString();
        state = SENDING_RESPONSE;
        return;
    }
    
    if (location->methods.find(method) == location->methods.end()) {
        response.setStatus(405);
        std::vector<std::string> allowed_methods(location->methods.begin(), 
                                                location->methods.end());
        response.setAllow(allowed_methods);
        response.setContentType("text/html; charset=utf-8");
        std::stringstream html;
        html << "<!DOCTYPE html>\n";
        html << "<html>\n";
        html << "<head><meta charset=\"UTF-8\"><title>405 Method Not Allowed</title>\n";
        html << "<style>body{background:#e74c3c;color:white;font-family:Arial;display:flex;"
            << "flex-direction:column;align-items:center;justify-content:center;height:100vh;"
            << "margin:0;font-size:24px;text-align:center;}"
            << ".code{font-size:48px;font-weight:bold;margin-bottom:10px;}"
            << ".message{font-size:20px;opacity:0.9;}</style>\n";
        html << "</head>\n";
        html << "<body>\n";
        html << "<div class=\"code\">405</div>\n";
        html << "<div class=\"message\">Method Not Allowed</div>\n";
        html << "</body>\n";
        html << "</html>\n";
        response.setBody(html.str());
        
        if (server_config->error_pages.find(405) != server_config->error_pages.end()) {
            std::string error_page_path = server_config->error_pages.find(405)->second;
            std::ifstream file(error_page_path.c_str());
            if (file) {
                std::string content((std::istreambuf_iterator<char>(file)),
                                  std::istreambuf_iterator<char>());
                response.setBody(content);
            }
        }
        
        response_buffer = response.toString();
        state = SENDING_RESPONSE;
        return;
    }
    
    if (method == "GET")
        handleGet(request, location, response, server_config);
    else if (method == "POST")
        handlePost(request, location, server_config, response);
    else if (method == "DELETE")
        handleDelete(request, location, response);
    
    if (response.isError() && server_config->error_pages.find(response.getStatusCode()) != server_config->error_pages.end()) {
        std::string error_page_path = server_config->error_pages.find(response.getStatusCode())->second;
        std::ifstream file(error_page_path.c_str());
        if (file) {
            std::string content((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
            response.setBody(content);
        }
    }
    
    if (!keep_alive)
        response.setConnection("close");
    else
        response.setConnection("keep-alive");
        
    response_buffer = response.toString();
    state = SENDING_RESPONSE;
}

LocationConfig* Client::findMatchingLocation(const std::string& path) {
    LocationConfig* best_match = NULL;
    size_t best_match_length = 0;
    
    for (size_t i = 0; i < server_config->locations.size(); ++i) {
        const std::string& loc_path = server_config->locations[i].path;
        
        if (loc_path == path) {
            return const_cast<LocationConfig*>(&server_config->locations[i]);
        }
        
        if (path.find(loc_path) == 0) {
            if (loc_path == "/" || 
                (path.length() > loc_path.length() && path[loc_path.length()] == '/') ||
                path.length() == loc_path.length()) {
                
                if (loc_path.length() > best_match_length) {
                    best_match = const_cast<LocationConfig*>(&server_config->locations[i]);
                    best_match_length = loc_path.length();
                }
            }
        }
    }
    return best_match;
}

void Client::buildErrorResponse(int code, const std::string& msg) {
    HttpResponse response = HttpResponse::makeError(code, msg);
    
    std::map<int, std::string>::const_iterator it = server_config->error_pages.find(code);
    if (it != server_config->error_pages.end()) {
        std::string error_page_path = it->second;
        std::ifstream file(error_page_path.c_str());
        if (file) {
            std::string content((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
            response.setBody(content);
        }
    }
    response_buffer = response.toString();
    bytes_sent = 0;
}

void Client::buildSimpleResponse(const std::string& content) {
    std::stringstream response;
    response << "HTTP/1.1 200 OK\r\n";
    response << "Content-Type: text/html\r\n";
    response << "Content-Length: " << content.length() << "\r\n";
    response << "Connection: close\r\n";
    response << "\r\n";
    response << content;
    response_buffer = response.str();
    bytes_sent = 0;
}

bool Client::checkHeaders() {
    return request_buffer.find("\r\n\r\n") != std::string::npos ||
           request_buffer.find("\n\n") != std::string::npos;
}

size_t Client::getContentLength() const {
    size_t pos = request_buffer.find("Content-Length:");
    if (pos == std::string::npos)
        pos = request_buffer.find("content-length:");
    if (pos != std::string::npos) {
        size_t end = request_buffer.find("\r\n", pos);
        if (end == std::string::npos)
            end = request_buffer.find("\n", pos);
        
        std::string length_str = request_buffer.substr(pos + 15, end - pos - 15);
        size_t first = length_str.find_first_not_of(" \t");
        if (first != std::string::npos)
            length_str = length_str.substr(first);
        return std::atoi(length_str.c_str());
    }
    return 0;
}

size_t Client::getBodySize() const {
    size_t headers_end = request_buffer.find("\r\n\r\n");
    
    if (headers_end != std::string::npos)
        return request_buffer.length() - (headers_end + 4);
    headers_end = request_buffer.find("\n\n");
    if (headers_end != std::string::npos)
        return request_buffer.length() - (headers_end + 2);
    return 0;
}

bool Client::isTimedOut(time_t timeout_seconds) const {
    return (std::time(NULL) - last_activity) > timeout_seconds;
}

void Client::close() {
    if (fd != -1) {
        ::close(fd);
        fd = -1;
    }
}