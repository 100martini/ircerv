#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <string>
#include <vector>
#include <ctime>
#include "../parsing/Config.hpp"
#include "../http/HttpParser.hpp"

class HttpResponse;

class Client {
public:
    enum State {
        READING_REQUEST,
        PROCESSING_REQUEST,
        SENDING_RESPONSE,
        CGI_IN_PROGRESS,
        CLOSING
    };
    
private:
    int fd;
    State state;
    std::string request_buffer;
    std::string response_buffer;
    time_t last_activity;
    const ServerConfig* server_config;
    size_t bytes_sent;
    HttpParser http_parser;
    bool keep_alive;
    bool cgi_requested;
    
public:
    Client(int _fd, const ServerConfig* config);
    ~Client();
    
    bool readRequest();
    bool sendResponse();
    void setState(State _state) { state = _state; }
    State getState() const { return state; }
    void processRequest();
    void buildErrorResponse(int code, const std::string& msg);
    void buildSimpleResponse(const std::string& content);
    int getFd() const { return fd; }
    time_t getLastActivity() const { return last_activity; }
    bool isTimedOut(time_t timeout_seconds = 60) const;
    bool hasDataToSend() const { return !response_buffer.empty(); }
    void close();
    
private:
    bool checkHeaders();
    size_t getContentLength() const;
    size_t getBodySize() const;
    LocationConfig* findMatchingLocation(const std::string& path);

// CGI
private:
    LocationConfig* cgi_location;
    const HttpRequest* cgi_request; 
    std::string cgi_full_path;
    std::string cgi_extension;

public:
    LocationConfig* getCGILocation() { return cgi_location; }
    std::string getCGIFullPath() { return cgi_full_path; }
    std::string getCGIExtension() { return cgi_extension; }
    const HttpRequest* getCGIRequest() { return cgi_request; }
    const ServerConfig* getServerConfig() { return server_config; }

    // FOR TESTING
    void setResponseBuffer(std::string response_) { this->response_buffer = response_; }
    std::string getResponseBuffer() { return response_buffer; }
};

#endif