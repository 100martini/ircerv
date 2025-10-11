#include "Methods.hpp"
#include "../cgi/CGIHandler.hpp"
#include "HelpersMethods.hpp"
#include <sys/stat.h>
#include <sstream>
#include <cstdlib>
#include <cerrno>
#include <cstdio>

void handleGet(const HttpRequest& request, LocationConfig* location, HttpResponse& response) {
    std::string request_path = request.getPath();
    std::string full_path;
    
    if (location->path != "/" && request_path.find(location->path) == 0) {
        std::string relative_path = request_path.substr(location->path.length());
        if (relative_path.empty())
            relative_path = "/";
        else if (relative_path[0] != '/')
            relative_path = "/" + relative_path;
        full_path = location->root + relative_path;
    } else {
        full_path = location->root + request_path;
    }

    if (location->cgi_enabled && isCGIScript(full_path, location->cgi_extension)) {
        CGIHandler cgi(request, *location);
        if (cgi.execute(full_path, response))
            return;
        else {
            response = HttpResponse::makeError(500, "CGI execution failed");
            return;
        }
    }
    
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
                    serveFile(index_path, response, request, location);
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
        serveFile(full_path, response, request, location);
}

void handlePost(const HttpRequest& request, LocationConfig* location, const ServerConfig* server_config, HttpResponse& response) {
    std::string content_type = request.getHeader("Content-Type");
    
    std::string request_path = request.getPath();
    std::string full_path = location->root + request_path;
    if (location->cgi_enabled && isCGIScript(full_path, location->cgi_extension)) {
        CGIHandler cgi(request, *location);
        if (cgi.execute(full_path, response))
            return;
        else {
            response = HttpResponse::makeError(500, "CGI execution failed");
            return;
        }
    }
    
    if (location->upload_path.empty() && content_type.find("multipart/form-data") != std::string::npos) {
        response.setStatus(403);
        response.setContentType("text/html; charset=utf-8");
        response.setBody("<html><body><h1>403 Forbidden</h1><p>File uploads not allowed</p></body></html>");
        return;
    }
    
    if (location->upload_path.empty()) {
        response.setStatus(200);
        response.setContentType("text/html; charset=utf-8");
        response.setBody("<html><body><h1>POST Request Received</h1><p>Data accepted</p></body></html>");
        return;
    }
    std::string content_length_str = request.getHeader("Content-Length");
    if (content_length_str.empty()) {
        response = HttpResponse::makeError(411, "Length Required");
        return;
    }
    size_t content_length = std::atoi(content_length_str.c_str());
    size_t max_body_size = location->has_body_count ? 
                          location->client_max_body_size : 
                          server_config->client_max_body_size;

    if (content_length > max_body_size) {
        response = HttpResponse::makeError(413, "Payload Too Large");
        return;
    }
    if (!ensureUploadDirectory(location->upload_path, response))
        return;
    std::string body = request.getBody();
    if (body.length() != content_length) {
        response = HttpResponse::makeError(400, "Incomplete request body");
        return;
    }
    
    if (content_type.find("multipart/form-data") != std::string::npos) {
        std::string boundary = extractBoundary(content_type);
        if (boundary.empty()) {
            response = HttpResponse::makeError(400, "Missing boundary");
            return;
        }
        if (!parseMultipartFormData(body, boundary, location->upload_path, response))
            return;
    } else if (content_type.find("application/x-www-form-urlencoded") != std::string::npos) {
        response.setStatus(200);
        response.setContentType("text/html; charset=utf-8");
        response.setBody("<html><body><h1>Form Data Received</h1></body></html>");
    } else {
        std::stringstream ss;
        ss << location->upload_path << "/upload_" << std::time(NULL) << ".dat";
        std::string filepath = ss.str();
        
        if (saveUploadedFile(filepath, body)) {
            response.setStatus(201);
            response.setContentType("text/html");
            std::stringstream html;
            html << "<html><body><h1>File Uploaded</h1>";
            html << "<p>File saved to: " << filepath << "</p>";
            html << "<p>Size: " << body.length() << " bytes</p>";
            html << "</body></html>";
            response.setBody(html.str());
        } else
            response = HttpResponse::makeError(500, "Failed to save file");
    }
}

void handleDelete(const HttpRequest& request, LocationConfig* location, HttpResponse& response) {
    std::string path = request.getPath();
    std::string full_path;
    
    if (location->path != "/" && path.find(location->path) == 0) {
        std::string relative_path = path.substr(location->path.length());
        if (relative_path.empty())
            relative_path = "/";
        else if (relative_path[0] != '/')
            relative_path = "/" + relative_path;
        full_path = location->root + relative_path;
    } else {
        full_path = location->root + path;
    }
    
    if (!isPathSafe(full_path, location->root)) {
        response = HttpResponse::makeError(403, "Forbidden");
        return;
    }
    struct stat file_stat;
    if (stat(full_path.c_str(), &file_stat) == -1) {
        if (errno == ENOENT)
            response = HttpResponse::makeError(404, "File not found");
        else
            response = HttpResponse::makeError(403, "Access denied");
        return;
    }
    if (S_ISDIR(file_stat.st_mode)) {
        response = HttpResponse::makeError(403, "Cannot delete directories");
        return;
    }
    if (remove(full_path.c_str()) == 0) {
        response.setStatus(200);
        response.setContentType("text/html; charset=utf-8");
        std::stringstream html;
        html << "<html><body><h1>File Deleted</h1>";
        html << "<p>Successfully deleted: " << path << "</p>";
        html << "</body></html>";
        response.setBody(html.str());
    } else {
        if (errno == EACCES)
            response = HttpResponse::makeError(403, "Permission denied");
        else
            response = HttpResponse::makeError(500, "Failed to delete file");
    }
}
