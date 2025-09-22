#include "./HttpResponse.hpp"

void HttpResponse::retrieve_status()
{
  if (_request.getMethod() == "GET")
  {
    if (std::access(_request.getPath(), F_OK) == 0)
      _status = HTTP_OK;
    else
      _status = HTTP_NOT_FOUND;
  }
  else
    _status = TO_BE_DECIDED;
}

int get_content_length()
{
  for ()
  int len;
}

void status_line(std::string & response, HttpRequest request)
{
  response += "HTTP/" + request.getVersion() + " ";
  switch (_status) {
    case HTTP_OK:
      reponse += "200 OK\r\n";
      break;
    case HTTP_NOT_FOUND:
      reponse += "404 Not Found\r\n";
      break;
    default:
      break;
  }
}

void HttpResponse::form_response()
{
}
