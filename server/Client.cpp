#include "Client.hpp"
#include "../http/HttpResponse.hpp"
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <cstring>
#include <iostream>
#include <cstdlib>

Client::Client(int _fd, const ServerConfig* config) 
    : fd(_fd), 
      state(READING_REQUEST), 
      server_config(config),
      bytes_sent(0) {
    last_activity = std::time(NULL);
}

Client::~Client() {
    close();
}

bool Client::readRequest() {
    char buffer[4096];
    ssize_t bytes = recv(fd, buffer, sizeof(buffer), 0);
    
    if (bytes > 0) {
        last_activity = std::time(NULL);
        
        try {
            int parser_state = http_parser.parseHttpRequest(std::string(buffer, bytes));
            
            if (parser_state == COMPLETE)
                return true;
            else if (parser_state == ERROR) {
                buildErrorResponse(400, "Bad Request");
                state = SENDING_RESPONSE;
                return false;
            }
            return false;
            
        } catch (const HttpRequestException& e) {
            int error_code = 400;
            std::string error_msg = "Bad Request";
            
            switch(e.error_code()) {
                case INVALID_METHOD_NAME:
                    error_code = 405;
                    error_msg = "Method Not Allowed";
                    break;
                case MISSING_CONTENT_LENGTH:
                case INVALID_CONTENT_LENGTH:
                    error_code = 411;
                    error_msg = "Length Required";
                    break;
                case DUPLICATE_HEADER:
                    error_code = 400;
                    error_msg = "Bad Request - Duplicate Header";
                    break;
                default:
                    break;
            }
            
            buildErrorResponse(error_code, error_msg);
            state = SENDING_RESPONSE;
            return false;
        }
    }
    else if (bytes == 0) {
        state = CLOSING;
        return false;
    }
    else
        return false;
}

bool Client::sendResponse() {
    if (response_buffer.empty())
        return true;
    
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
        return false;
    }
    else if (bytes == 0)
        return false;
    else
        return false;
}

void Client::processRequest() {
    const HttpRequest& request = http_parser.getRequest();
    HttpResponse response;
    
    std::string method = request.getMethod();
    std::string path = request.getPath();
    std::string version = request.getVersion();
    
    LocationConfig* location = findMatchingLocation(path);
    
    if (!location) {
        response = HttpResponse::makeError(404);
        response_buffer = response.toString();
        state = SENDING_RESPONSE;
        return;
    }
    
    if (location->methods.find(method) == location->methods.end()) {
        response.setStatus(405);
        std::vector<std::string> allowed_methods(location->methods.begin(), 
                                                 location->methods.end());
        response.setAllow(allowed_methods);
        response.setContentType("text/html");
        std::stringstream html;
        html << "<!DOCTYPE html>\n";
        html << "<html><head><title>405 Method Not Allowed</title></head>\n";
        html << "<body><h1>405 Method Not Allowed</h1>\n";
        html << "<p>The requested method " << method << " is not allowed for this resource.</p>\n";
        html << "</body></html>\n";
        response.setBody(html.str());
        response_buffer = response.toString();
        state = SENDING_RESPONSE;
        return;
    }
    
    if (location->redirect.first > 0) {
        response = HttpResponse::makeRedirect(location->redirect.first, 
                                             location->redirect.second);
        response_buffer = response.toString();
        state = SENDING_RESPONSE;
        return;
    }
    
    if (method == "GET")
        handleGetRequest(request, location, response);
    else if (method == "POST")
        handlePostRequest(request, location, response);
    else if (method == "DELETE")
        handleDeleteRequest(request, location, response);
    response_buffer = response.toString();
    state = SENDING_RESPONSE;
}

void Client::handleGetRequest(const HttpRequest& request, 
                              LocationConfig* location, 
                              HttpResponse& response) {
    std::string full_path = location->root + request.getPath();
    
    struct stat file_stat;
    if (stat(full_path.c_str(), &file_stat) == -1) {
        response = HttpResponse::makeError(404);
        return;
    }
    
    if (S_ISDIR(file_stat.st_mode)) {
        if (full_path[full_path.length() - 1] != '/')
            full_path += '/';
        bool index_served = false;
        if (!location->index.empty()) {
            std::istringstream iss(location->index);
            std::string index_file;
            while (iss >> index_file) {
                std::string index_path = full_path + index_file;
                if (stat(index_path.c_str(), &file_stat) == 0 && !S_ISDIR(file_stat.st_mode)) {
                    serveFile(index_path, response);
                    index_served = true;
                    break;
                }
            }
        }
        
        if (!index_served) {
            if (location->autoindex) {
                std::string listing = generateDirectoryListing(full_path, request.getPath());
                response.setStatus(200);
                response.setContentType("text/html");
                response.setBody(listing);
            } else
                response = HttpResponse::makeError(403, "Directory listing forbidden");
        }
    } else
        serveFile(full_path, response);
}

void Client::handlePostRequest(const HttpRequest& request,
                               LocationConfig* location,
                               HttpResponse& response) {
    (void)request;
    (void)location;
    response.setStatus(501);
    response.setContentType("text/html");
    response.setBody("<html><body><h1>501 Not Implemented</h1><p>POST requests are not yet implemented</p></body></html>");
}

void Client::handleDeleteRequest(const HttpRequest& request,
                                 LocationConfig* location,
                                 HttpResponse& response) {
    (void)request;
    (void)location;
    response.setStatus(501);
    response.setContentType("text/html");
    response.setBody("<html><body><h1>501 Not Implemented</h1><p>DELETE requests are not yet implemented</p></body></html>");
}

void Client::serveFile(const std::string& filepath, HttpResponse& response) {
    std::ifstream file(filepath.c_str(), std::ios::binary);
    if (!file) {
        response = HttpResponse::makeError(500, "Cannot open file");
        return;
    }
    std::string content((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
    std::string extension = getFileExtension(filepath);
    std::string mime_type = HttpResponse::getMimeType(extension);
    
    response.setStatus(200);
    response.setContentType(mime_type);
    response.setBody(content);
    
    struct stat file_stat;
    if (stat(filepath.c_str(), &file_stat) == 0)
        response.setLastModified(file_stat.st_mtime);
}

std::string Client::getFileExtension(const std::string& filepath) {
    size_t dot_pos = filepath.rfind('.');
    if (dot_pos != std::string::npos)
        return filepath.substr(dot_pos);
    return "";
}

LocationConfig* Client::findMatchingLocation(const std::string& path) {
    LocationConfig* best_match = NULL;
    size_t best_match_length = 0;
    
    for (size_t i = 0; i < server_config->locations.size(); ++i) {
        const std::string& loc_path = server_config->locations[i].path;
        if (path.find(loc_path) == 0) {
            if (loc_path.length() > best_match_length) {
                best_match = const_cast<LocationConfig*>(&server_config->locations[i]);
                best_match_length = loc_path.length();
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

std::string Client::generateDirectoryListing(const std::string& dir_path, 
                                            const std::string& uri_path) {
    std::stringstream html;
    html << "<!DOCTYPE html>\n";
    html << "<html>\n";
    html << "<head><title>Index of " << uri_path << "</title></head>\n";
    html << "<body>\n";
    html << "<h1>Index of " << uri_path << "</h1>\n";
    html << "<hr>\n";
    html << "<pre>\n";
    DIR* dir = opendir(dir_path.c_str());
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            std::string name = entry->d_name;
            if (name == ".") continue;

            std::string full_entry_path = dir_path + "/" + name;
            struct stat entry_stat;
            
            if (stat(full_entry_path.c_str(), &entry_stat) == 0) {
                if (S_ISDIR(entry_stat.st_mode))
                    html << "<a href=\"" << uri_path << "/" << name << "/\">" 
                         << name << "/</a>\n";
                else
                    html << "<a href=\"" << uri_path << "/" << name << "\">" 
                         << name << "</a>    " 
                         << entry_stat.st_size << " bytes\n";
            }
        }
        closedir(dir);
    }
    
    html << "</pre>\n";
    html << "<hr>\n";
    html << "</body>\n";
    html << "</html>\n";
    return html.str();
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