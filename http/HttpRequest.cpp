#include "HttpRequest.hpp"

HttpRequest::HttpRequest() 
    : method_(""), path_(""), version_(""), query_string_(""),
         body_(""), content_length_found_(false){
    NoneDupHeaders.insert("content_length");
    NoneDupHeaders.insert("authorization");
    NoneDupHeaders.insert("form");
    NoneDupHeaders.insert("if-modified-since");
    NoneDupHeaders.insert("referer");
    NoneDupHeaders.insert("user_agent");
    NoneDupHeaders.insert("content_type");
}
HttpRequest::~HttpRequest(){}

std::string HttpRequest::getMethod() const { return method_; }
std::string HttpRequest::getPath() const { return path_; }
std::string HttpRequest::getVersion() const { return version_; }
std::string HttpRequest::getQueryString() const { return query_string_; }
std::string HttpRequest::getBody() const { return body_; }

bool HttpRequest::hasHeaders() const {
    if (!headers_.empty())
        return true; 
    return false;
};

std::string HttpRequest::getHeader(const std::string& headerName) const {
    if (!this->hasHeaders())
        return NULL;
    std::map<std::string, std::string>::const_iterator it;
    for (it = headers_.begin(); it != headers_.end(); ++it) {
        if (it->first == headerName)
            return it->second;
    }
    return NULL;
}

void HttpRequest::setMethod(const std::string& method) {
    // TODO: validate that the request method is permitted by the configuration
    if (method != "GET" && method != "POST" && method != "DELETE") {
        throw InvalidMethodName("");
    }
    method_ = method;
}

void HttpRequest::setVersion(const std::string& version) {
    if (version != "HTTP/1.1" && version != "HTTP/1.0")
        throw MalformedRequestLineException("");
    version_ = version;
}

void HttpRequest::handleURI(const std::string& uri) {
    if (uri.length() > MAX_URI_SIZE)
        throw ;
    if (uri.empty() || uri[0] != '/')
        throw MalformedRequestLineException("");
    std::string allowedChars = "$_-.!*'(),%:@&=+/;?";
    // if (uri.find_first_not_of(allowedChars) != std::string::npos)
    //     throw ;
    for (size_t i = 0; i < uri.length(); i++) {
        if (!std::isalnum(uri[i]))
        {
            if (allowedChars.find(uri[i]) == std::string::npos)
                throw MalformedRequestLineException("");
        }    
    }
    size_t queryPos = uri.find('?');
    if (queryPos == std::string::npos) {
        this->setPath(uri);
        return ;
    }
    this->setPath(uri.substr(0, queryPos));
    this->setQueryString(uri.substr(queryPos + 1));
}

void HttpRequest::setPath(const std::string& path) {
    // TODO: normalize the incoming path (e.g., handling cases like /../../hdhddhd). 
    // After normalization, verify that the resolved path still points inside the configured root directory
    path_ = HttpRequest::percentDecode(path);
}

void HttpRequest::setQueryString(const std::string& query) {
    query_string_ = HttpRequest::percentDecode(query);
}

std::string HttpRequest::percentDecode(const std::string& encoded) {
    std::string result = "";
    for (size_t i = 0; i < encoded.length(); i++) {
        if (encoded[i] == '%') {
            if (i + 2 > encoded.length())
                throw MalformedRequestLineException("");
            std::string hexPart = encoded.substr(i + 1, 2);
            if (!std::isxdigit(hexPart[0]) || !std::isxdigit(hexPart[1])) 
                throw MalformedRequestLineException("");
            int hexPartInt = std::strtol(hexPart.c_str(), NULL, 16);
            result += static_cast<char>(hexPartInt);
            i += 2;
        } else {
            result += encoded[i];
        }
    }
    return result;
}   

void HttpRequest::addHeader(const std::string& key, const std::string& value) {
    std::string newKey = HttpRequest::trim(key);
    std::string newValue = HttpRequest::trim(value);
    
    if (newKey.empty() || newValue.empty())
        throw MalformedHeaderException("");
    
    // Convert key to lowercase for comparison
    std::string lowerKey = newKey;
    for (size_t i = 0; i < lowerKey.length(); i++) {
        lowerKey[i] = std::tolower(lowerKey[i]);
    }
    
    // Handle Content-Length (case insensitive)
    if (lowerKey == "content-length") {
        content_length_found_ = true;
        char *tolEnd;
        signed long contentLen = std::strtol(newValue.c_str(), &tolEnd, 10);
        if (*tolEnd != '\0' || contentLen < 0)
            throw InvalidContentLengthException("");
        content_length_ = contentLen;
    }
    
    // Check for duplicates
    if (HttpRequest::isDublicate(newKey))
        throw DuplicateHeaderException("");
    
    headers_.insert(std::make_pair(newKey, newValue));
}

bool HttpRequest::isDublicate(const std::string& name) {
    std::map<std::string, std::string>::const_iterator map_it;
    std::set<std::string>::iterator set_itr = NoneDupHeaders.find(name);
    for (map_it = headers_.begin(); map_it != headers_.end(); ++map_it) {
        if (name == map_it->first && set_itr == NoneDupHeaders.end())
            return true;
    }
    return false;
}

std::ostream& operator<<(std::ostream& os, const HttpRequest& request) {
    os << "---------------------REQUEST-------------------\n";
    os << "     --------------REQUEST LINE------------    \n";
    os << "Method [" << request.method_ << "]\n";
    os << "Path [" << request.path_ << "]\n";
    os << "Query String [" << request.query_string_ << "]\n";
    os << "Version: [" << request.version_ << "]\n";
    os << "     ----------------HEADERS---------------    \n";
    request.printHeaders();
    os << "-----------------------------------------------\n";
    return os;
}

void HttpRequest::printHeaders() const {
    if (!this->hasHeaders()) {
        std::cout << "No Headers!\n";
        return ;
    }
    std::map<std::string, std::string>::const_iterator it;
    for (it = headers_.begin(); it != headers_.end(); ++it) {
        std::cout << it->first << ": " << it->second << "\n";
    }
}

std::string HttpRequest::trim(const std::string& str) {
    size_t start = 0;
    while (start < str.size() && std::isspace(str[start]))
        start++;
    if (start == str.size())
        return "";
    size_t end = str.size() - 1;
    while (end > start && std::isspace(str[end]))
        end--;
    return str.substr(start, end - start + 1);
}