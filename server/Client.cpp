#include "Client.hpp"
#include <unistd.h>
#include <sys/socket.h>
#include <sstream>
#include <cstring>
#include <iostream>
#include <cstdlib>

Client::Client(int _fd, const ServerConfig* config) 
    : fd(_fd), 
      state(READING_REQUEST), 
      server_config(config),
      bytes_sent(0),
      headers_complete(false),
      content_length(0) {
    last_activity = std::time(NULL);
}

Client::~Client() {
    close();
}

bool Client::readRequest() {
    char buffer[4096];
    
    while (true) {
        ssize_t bytes = recv(fd, buffer, sizeof(buffer), 0);
        
        if (bytes > 0) {
            request_buffer.append(buffer, bytes);
            last_activity = std::time(NULL);
            
            if (!headers_complete) {
                if (checkHeaders()) {
                    headers_complete = true;
                    content_length = getContentLength();
                    
                    if (content_length == 0 && 
                        request_buffer.find("POST") != 0 && 
                        request_buffer.find("PUT") != 0) {
                        return true;
                    }
                }
            }
            
            if (headers_complete) {
                size_t headers_end = request_buffer.find("\r\n\r\n");
                if (headers_end != std::string::npos) {
                    size_t body_start = headers_end + 4;
                    size_t body_size = request_buffer.length() - body_start;
                    
                    if (body_size > server_config->client_max_body_size) {
                        buildErrorResponse(413, "request entity too large");
                        state = SENDING_RESPONSE;
                        return false;
                    }
                    
                    if (body_size >= content_length)
                        return true;
                }
            }
        }
        else if (bytes == 0) {
            state = CLOSING;
            return false;
        }
        else
            //with non-blocking sockets, we can't distinguish between EAGAIN/EWOULDBLOCK vs real errors, so i dont know if we need to close the connection also here to be safe
            return false;
    }
}

bool Client::sendResponse() {
    if (response_buffer.empty())
        return true;
    
    while (bytes_sent < response_buffer.length()) {
        ssize_t bytes = send(fd, response_buffer.c_str() + bytes_sent, 
                            response_buffer.length() - bytes_sent, 0);
        if (bytes > 0) {
            bytes_sent += bytes;
            last_activity = std::time(NULL);
            
            if (bytes_sent >= response_buffer.length()) {
                response_buffer.clear();
                bytes_sent = 0;
                return true;
            }
        }
        else if (bytes == 0)
            //this shouldn't happen with send() anyways walakin sir 3lah
            return false;
        else
            return false;
    }
    return true;
}

void Client::processRequest() {
    std::string method, path, version;
    std::istringstream iss(request_buffer);
    iss >> method >> path >> version;
    
    std::cout << "Request: " << method << " " << path << " " << version << std::endl;
    
    std::stringstream html;
    html << "<!DOCTYPE html>\n";
    html << "<html><head><title>ircerv</title></head>\n";
    html << "<body>\n";
    html << "<h1>Our server is running!</h1>\n";
    html << "<p>You requested: " << path << "</p>\n";
    html << "<p>Method: " << method << "</p>\n";
    html << "<p>Server: " << server_config->server_name << "</p>\n";
    html << "<p>Port: " << server_config->port << "</p>\n";
    html << "</body></html>\n";
    
    buildSimpleResponse(html.str());
    state = SENDING_RESPONSE;
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

void Client::buildErrorResponse(int code, const std::string& msg) {
    std::stringstream html;
    html << "<!DOCTYPE html>\n";
    html << "<html><head><title>" << code << " " << msg << "</title></head>\n";
    html << "<body>\n";
    html << "<h1>" << code << " " << msg << "</h1>\n";
    html << "<hr><p>webserv</p>\n";
    html << "</body></html>\n";
    
    std::stringstream response;
    response << "HTTP/1.1 " << code << " " << msg << "\r\n";
    response << "Content-Type: text/html\r\n";
    response << "Content-Length: " << html.str().length() << "\r\n";
    response << "Connection: close\r\n";
    response << "\r\n";
    response << html.str();
    
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

bool Client::isTimedOut(time_t timeout_seconds) const {
    return (std::time(NULL) - last_activity) > timeout_seconds;
}

void Client::close() {
    if (fd != -1) {
        ::close(fd);
        fd = -1;
    }
}