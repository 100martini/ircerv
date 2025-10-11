#include "HelpersMethods.hpp"
#include "HttpRequest.hpp"
#include <sys/stat.h>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <limits.h>

void serveFile(const std::string& filepath, HttpResponse& response, HttpRequest const & request, LocationConfig *location) {
    std::ifstream file(filepath.c_str(), std::ios::binary);
    if (!file) {
        response = HttpResponse::makeError(500, "Cannot open file");
        return;
    }
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::string extension = getFileExtension(filepath);
    std::string mime_type = HttpResponse::getMimeType(extension);
    if (!extension.empty() && extension == ".py")
    {
      CGIHandler cgi(request, location, filepath);
      content = cgi.execute(extension);
      mime_type = "text/html; charset=utf-8";
    }
    response.setStatus(200);
    response.setContentType(mime_type);
    // std::cout << mime_type << std::endl;
    response.setBody(content);

    struct stat file_stat;

    if (stat(filepath.c_str(), &file_stat) == 0)
        response.setLastModified(file_stat.st_mtime);
}

std::string getFileExtension(const std::string& filepath) {
    size_t dot_pos = filepath.rfind('.');
    if (dot_pos != std::string::npos)
        return filepath.substr(dot_pos);
    return "";
}

std::string generateDirectoryListing(const std::string& dir_path, const std::string& uri_path) {
    std::stringstream html;
    html << "<!DOCTYPE html>\n";
    html << "<html>\n";
    html << "<head>\n";
    html << "<meta charset=\"UTF-8\">\n";
    html << "<title>Index of " << uri_path << "</title></head>\n";
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

bool ensureUploadDirectory(const std::string& path, HttpResponse& response) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        if (!S_ISDIR(st.st_mode)) {
            response = HttpResponse::makeError(500, "Upload path is not a directory");
            return false;
        }
        if (access(path.c_str(), W_OK) != 0) {
            response = HttpResponse::makeError(500, "Upload directory not writable");
            return false;
        }
        return true;
    }
    
    if (mkdir(path.c_str(), 0755) != 0) {
        response = HttpResponse::makeError(500, "Cannot create upload directory");
        return false;
    }
    return true;
}

bool parseMultipartFormData(const std::string& body, const std::string& boundary, const std::string& upload_path, HttpResponse& response) {
    std::string delimiter = "--" + boundary;
    std::vector<std::string> uploaded_files;
    size_t pos = 0;
    
    while (pos < body.length()) {
        size_t part_start = body.find(delimiter, pos);
        if (part_start == std::string::npos)
            break;
        
        part_start += delimiter.length();
        if (body.substr(part_start, 2) == "--")
            break;
        
        if (body.substr(part_start, 2) == "\r\n")
            part_start += 2;
        else if (body.substr(part_start, 1) == "\n")
            part_start += 1;
        
        size_t part_end = body.find(delimiter, part_start);
        if (part_end == std::string::npos)
            break;
        
        std::string part = body.substr(part_start, part_end - part_start);
        
        size_t headers_end = part.find("\r\n\r\n");
        if (headers_end == std::string::npos)
            headers_end = part.find("\n\n");
        
        if (headers_end != std::string::npos) {
            std::string headers = part.substr(0, headers_end);
            size_t data_start = (part.substr(headers_end, 4) == "\r\n\r\n") ? headers_end + 4 : headers_end + 2;
            std::string file_data = part.substr(data_start);
            
            if (file_data.length() >= 2 && file_data.substr(file_data.length() - 2) == "\r\n")
                file_data = file_data.substr(0, file_data.length() - 2);
            else if (file_data.length() >= 1 && file_data.substr(file_data.length() - 1) == "\n")
                file_data = file_data.substr(0, file_data.length() - 1);
            
            size_t filename_pos = headers.find("filename=\"");
            if (filename_pos != std::string::npos) {
                filename_pos += 10;
                size_t filename_end = headers.find("\"", filename_pos);
                std::string filename = headers.substr(filename_pos, filename_end - filename_pos);
                
                if (!filename.empty()) {
                    std::string filepath = upload_path + "/" + filename;
                    if (saveUploadedFile(filepath, file_data))
                        uploaded_files.push_back(filename);
                }
            }
        }
        pos = part_end;
    }
    
    if (uploaded_files.empty()) {
        response = HttpResponse::makeError(400, "No files uploaded");
        return false;
    }
    response.setStatus(201);
    response.setContentType("text/html; charset=utf-8");
    std::stringstream html;
    html << "<!DOCTYPE html>\n"
         << "<html>\n"
         << "<head><meta charset=\"UTF-8\"><title>Success</title>\n"
         << "<style>body{background:#2ecc71;color:white;font-family:Arial;display:flex;"
         << "flex-direction:column;align-items:center;justify-content:center;height:100vh;"
         << "margin:0;font-size:24px;text-align:center;}"
         << ".filename{font-size:18px;margin-top:20px;opacity:0.9;}</style>\n"
         << "</head>\n"
         << "<body><div><b>[ successfully uploaded ]</b></div>";

    for (size_t i = 0; i < uploaded_files.size(); i++)
        html << "<div class=\"filename\">" << uploaded_files[i] << "</div>";
    html << "</body>\n</html>";
    response.setBody(html.str());
    return true;
}

bool saveUploadedFile(const std::string& filepath, const std::string& content) {
    std::ofstream file(filepath.c_str(), std::ios::binary);
    if (!file)
        return false;
    
    file.write(content.c_str(), content.length());
    file.close();

    return file.good();
}

std::string extractBoundary(const std::string& content_type) {
    size_t boundary_pos = content_type.find("boundary=");
    if (boundary_pos == std::string::npos)
        return "";
    
    std::string boundary = content_type.substr(boundary_pos + 9);
    if (!boundary.empty() && boundary[0] == '"') {
        size_t end_quote = boundary.find('"', 1);
        if (end_quote != std::string::npos)
            boundary = boundary.substr(1, end_quote - 1);
    }
    return boundary;
}

bool isPathSafe(const std::string& full_path, const std::string& root) {
    char resolved_path[PATH_MAX];
    char resolved_root[PATH_MAX];
    
    if (realpath(root.c_str(), resolved_root) == NULL)
        return false;
    
    std::string parent_path = full_path;
    size_t last_slash = parent_path.rfind('/');

    if (last_slash != std::string::npos)
        parent_path = parent_path.substr(0, last_slash);
    if (realpath(parent_path.c_str(), resolved_path) == NULL)
        return false;
    
    std::string path_str(resolved_path);
    std::string root_str(resolved_root);
    return path_str.find(root_str) == 0;
}
