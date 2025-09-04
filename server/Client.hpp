#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <string>
#include <vector>
#include <ctime>
#include "../parsing/Config.hpp"

class Client {
public:
    enum State {
        READING_REQUEST,
        PROCESSING_REQUEST,
        SENDING_RESPONSE,
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
    bool headers_complete;
    size_t content_length;
    
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
};

#endif