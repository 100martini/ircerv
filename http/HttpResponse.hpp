#pragma once

#include <unistd.h>
#include "HttpExceptions.hpp"
#include "HttpRequest.hpp"

// 2xx: Success
#define HTTP_OK                 200
#define HTTP_CREATED            201
#define HTTP_NO_CONTENT         204

// 4xx: Client errors
#define HTTP_BAD_REQUEST        400
#define HTTP_NOT_FOUND          404
#define HTTP_METHOD_NOT_ALLOWED 405

// 5xx: Server errors
#define HTTP_INTERNAL_SERVER_ERROR 500
#define HTTP_NOT_IMPLEMENTED       501
#define HTTP_SERVICE_UNAVAILABLE   503

// 600 to be desided
#define TO_BE_DECIDED 600

struct headers
{
  
};

class HttpResponse
{
  private:
    char _status;
    HttpRequest _request;
    std::string _response;
  public:
    const std::string getResponse() const;
    void retrieve_status();
    void form_response();
};
