#include "HttpParser.hpp"

HttpParser::HttpParser() 
    : buffer_(""), state_(PARSING_REQUEST_LINE), bytes_read_(0) {}
HttpParser::~HttpParser() {}

int HttpParser::parseHttpRequest(const std::string& RequestData) {
    std::cout << "\n-----------------HTTP_PARSER--------------------\n";
    std::cout << "RECEIVED DATA:\n";
    //std::cout << RequestData << "\n\n";
    
    //added for debugging
    std::cout << "Received " << RequestData.length() << " bytes\n";
    buffer_ += RequestData;
    while (state_ != COMPLETE && state_ != ERROR && HttpParser::hasEnoughData()) {
        std::cout << "State: " << state_ << ", Buffer size: " << buffer_.length() << "\n";
        switch (state_)
        {
            case PARSING_REQUEST_LINE:
                if (HttpParser::parseRequestLine()) {
                    state_ = PARSING_HEADERS;
                }
                else 
                    break; // need more data
                break;
            case PARSING_HEADERS:
                if (HttpParser::parseHeaders()) {
                    if (httpRequest_.method_ == "POST" && httpRequest_.content_length_ > 0)
                        state_ = PARSING_BODY;
                    else {
                        state_ = COMPLETE;
                    }
                }
                else
                    break;
                break;
            case PARSING_BODY:
                if (HttpParser::parseBody())
                    state_ = COMPLETE;
                else 
                    break;
                break;
            case COMPLETE:
            case ERROR:
                break;
        }
    }
    std::cout << "\nPARSER OUT:\n";
    std::cout << "NO ERRORS\n";
    std::cout << "----------------------------------------------------\n";
    if (state_ == COMPLETE) {
        std::cout << "------------------------------------\n";
        std::cout << "\tCOMPLETED\n";
        std::cout << httpRequest_ << "\n\n";
        std::cout << "------------------------------------\n";
    }   
    std::cout << "Parser state: " << state_ << "\n";
    return state_;
}

bool HttpParser::parseRequestLine() {
    size_t pos = buffer_.find("\r\n");
    if (pos == std::string::npos) {
        return false;
    }

    std::string line = buffer_.substr(0, pos);
    buffer_ = buffer_.substr(pos + 2);

    // count occurrences of space characters
    if (std::count(line.begin(), line.end(), ' ') != 2)
       throw MalformedRequestLineException("");

    size_t firstSpace = line.find(' ');
    if (firstSpace == std::string::npos)
        throw MalformedRequestLineException(""); // throw exception
    size_t secondSpace = line.find(' ', firstSpace + 1);
    if (secondSpace == std::string::npos)
        throw MalformedRequestLineException("");

    httpRequest_.setMethod(line.substr(0, firstSpace));
    httpRequest_.handleURI(line.substr(firstSpace + 1, secondSpace - firstSpace - 1));
    httpRequest_.setVersion(line.substr(secondSpace + 1));

    return true;
}

bool HttpParser::parseHeaders() {
    while (true) {
        size_t pos = buffer_.find("\r\n");
        if (pos == std::string::npos)
            return false;

        std::string line = buffer_.substr(0, pos);
        buffer_ = buffer_.substr(pos + 2);

        if (line.empty()) {
            // Empty line end of headers
            return true;
        }

        // parse header, key, value
        size_t colonPos = line.find(':');
        if (colonPos == std::string::npos)
            throw MalformedHeaderException("");
        std::string key = line.substr(0, colonPos);
        std::string value = line.substr(colonPos + 1);
        httpRequest_.addHeader(key, value);
    }
}

bool HttpParser::parseBody() {
    size_t neededLength = httpRequest_.content_length_ - bytes_read_;
    size_t available = buffer_.length();

    if (available >= neededLength) {
        // Take only what we need from the buffer
        httpRequest_.body_ += buffer_.substr(0, neededLength);
        // Remove the consumed data from buffer
        buffer_ = buffer_.substr(neededLength);
        bytes_read_ += neededLength;
        return true;  // Body parsing complete
    } else {
        // Take all available data
        httpRequest_.body_ += buffer_;
        bytes_read_ += available;
        buffer_.clear();
        return false; // Need more data
    }
}

bool HttpParser::hasEnoughData() {
    // For body parsing, we don't need newlines - any data is enough to process
    if (state_ == PARSING_BODY) {
        return !buffer_.empty();
    }
    
    // For headers and request line, we need newlines
    size_t newLineEx = buffer_.find("\r\n");
    return newLineEx != std::string::npos;
}