#include "Server.hpp"
#include <iostream>
#include <cstring>
#include <signal.h>

static bool g_server_running = true;

static void signalHandler(int sig) {
    (void)sig;
    g_server_running = false;
    std::cout << "\nshutting down server." << std::endl;
}

Server::Server(const std::vector<ServerConfig>& _configs) 
    : configs(_configs), running(false) {
    
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    signal(SIGPIPE, SIG_IGN);
}

Server::~Server() {
    stop();
    
    for (size_t i = 0; i < listen_sockets.size(); i++)
        delete listen_sockets[i];
    
    for (std::map<int, Client*>::iterator it = clients.begin(); 
         it != clients.end(); ++it)
        delete it->second;
}

void Server::start() {
    std::cout << PURPLE << "\nstarting ircerv..." << RESET << std::endl;
    
    for (size_t i = 0; i < configs.size(); i++) {
        Socket* sock = new Socket();
        
        try {
            sock->create();
            sock->setReuseAddr();
            sock->setNonBlocking(); 
            sock->bind(configs[i].host, configs[i].port);
            sock->listen();
            
            listen_sockets.push_back(sock);
            fd_to_config[sock->getFd()] = &configs[i];
            event_manager.addFd(sock->getFd(), true, false);
            
            std::cout << "listening on " << YELLOW << configs[i].host 
                      << ":" << configs[i].port 
                      << RESET << " (fd=" << sock->getFd() << ")" << std::endl;
        }
        catch (const std::exception& e) {
            delete sock;
            throw std::runtime_error("failed to start server: " + std::string(e.what()));
        }
    }
    
    running = true;
    std::cout << GREEN << "server started successfully! <3" << RESET << std::endl;
}

void Server::run() {
    std::cout << "server is running. Ctrl+C if you wanna stop." << std::endl;
    
    while (running && g_server_running) {
        int num_events = event_manager.wait(100);
        
        if (num_events > 0) {
            const std::vector<EventManager::Event>& events = event_manager.getEvents();
            
            for (size_t i = 0; i < events.size(); i++) {
                const EventManager::Event& event = events[i];
                
                if (fd_to_config.find(event.fd) != fd_to_config.end())
                {
                    if (event.readable)
                        acceptNewClient(event.fd);
                }
                else if (clients.find(event.fd) != clients.end()) {
                    Client* client = clients[event.fd];
                    
                    if (event.error) {
                        removeClient(client);
                    }
                    else {
                        if (event.readable && client->getState() == Client::READING_REQUEST)
                            handleClientRead(client);
                        if (event.writable && client->getState() == Client::SENDING_RESPONSE)
                            handleClientWrite(client);
                            
                        if (client->getState() == Client::PROCESSING_REQUEST) {
                            client->processRequest();
                            event_manager.setReadMonitoring(client->getFd(), false);
                            event_manager.setWriteMonitoring(client->getFd(), true);
                        }
                    }
                }
                else if (active_cgis.find(event.fd) != active_cgis.end()) {
                    // Handle CGI pipe
                    handleGCIEventPipe(event.fd, event);
                }
            }
        }
        
        checkTimeouts();
    }
}

void Server::stop() {
    running = false;
    
    while (!clients.empty())
        removeClient(clients.begin()->second);
}

void Server::acceptNewClient(int listen_fd) {
    Socket* listen_socket = NULL;
    for (size_t i = 0; i < listen_sockets.size(); i++) {
        if (listen_sockets[i]->getFd() == listen_fd) {
            listen_socket = listen_sockets[i];
            break;
        }
    }
    
    if (!listen_socket)
        return;
    
    int accepted_count = 0;
    const int MAX_ACCEPT_PER_CYCLE = 10;
    
    while (accepted_count < MAX_ACCEPT_PER_CYCLE) {
        int client_fd = listen_socket->accept();
        if (client_fd == -1)
            break;
        
        if (clients.size() >= 1000) {
            ::close(client_fd);
            std::cout << "max clients reached, rejecting connection" << std::endl;
            break;
        }
        
        ServerConfig* config = fd_to_config[listen_fd];
        Client* client = new Client(client_fd, config);
        clients[client_fd] = client;
        
        event_manager.addFd(client_fd, true, false);
        
        std::cout << "new client connected: fd=" << client_fd 
                  << " on " << config->host << ":" << config->port << std::endl;
        
        accepted_count++;
    }
}

void Server::handleClientRead(Client* client) {
    if (!client) return;

    bool request_complete = client->readRequest();
    if (client->getState() == Client::CLOSING) {
        removeClient(client);
        return;
    }
    // added check for processing state before calling processRequest()
    if (request_complete && client->getState() == Client::PROCESSING_REQUEST) {
        client->processRequest();

        if (client->getState() == Client::CGI_IN_PROGRESS) {
            executeCGI(client);
            event_manager.setReadMonitoring(client->getFd(), false);
            event_manager.setWriteMonitoring(client->getFd(), false);
            return ;
        }

        event_manager.setReadMonitoring(client->getFd(), false);
        event_manager.setWriteMonitoring(client->getFd(), true);
    }
    else if (client->getState() == Client::SENDING_RESPONSE) {
        event_manager.setReadMonitoring(client->getFd(), false);
        event_manager.setWriteMonitoring(client->getFd(), true);
    }
}

void Server::handleClientWrite(Client* client) {
    if (!client) return;
    
    bool response_complete = client->sendResponse();
    
    if (client->getState() == Client::CLOSING) {
        removeClient(client);
        return;
    }
    if (response_complete)
        removeClient(client); // done, close connection
    else if (client->getState() == Client::READING_REQUEST) {
        // back to reading
        event_manager.setWriteMonitoring(client->getFd(), false);
        event_manager.setReadMonitoring(client->getFd(), true);
    }
}

void Server::removeClient(Client* client) {
    if (!client) return;
    
    int fd = client->getFd();
    if (fd < 0) {
        delete client;
        return;
    }
    std::cout << "client disconnected: fd=" << fd << std::endl;
    event_manager.removeFd(fd);
    // just making sure we erase from map
    std::map<int, Client*>::iterator it = clients.find(fd);
    if (it != clients.end())
        clients.erase(it);
    delete client;
}

void Server::checkTimeouts() {
    std::vector<Client*> timed_out;
    
    for (std::map<int, Client*>::iterator it = clients.begin(); 
         it != clients.end(); ++it)
        if (it->second->isTimedOut(60))
            timed_out.push_back(it->second);
    
    for (size_t i = 0; i < timed_out.size(); i++) {
        std::cout << "client timed out: fd=" << timed_out[i]->getFd() << std::endl;
        removeClient(timed_out[i]);
    }
}

ServerConfig* Server::getConfigForListenFd(int fd) {
    std::map<int, ServerConfig*>::iterator it = fd_to_config.find(fd);
    if (it != fd_to_config.end())
        return it->second;
    return NULL;
}

// Need more refining 
void Server::executeCGI(Client* client) {
    int pipeIn[2];
    int pipeOut[2];
    pid_t pid;

    if (pipe(pipeIn) == -1 || pipe(pipeOut) == -1)
        throw std::runtime_error("pipe failed");   

    if (!setFdNonBlocking(pipeOut[0]) || !setFdNonBlocking(pipeIn[1])) {
        close(pipeIn[0]); close(pipeIn[1]);
        close(pipeOut[0]); close(pipeOut[1]);
        throw std::runtime_error("fcntl failed");
    }

    pid = fork();
    if (pid == -1) {
        close(pipeIn[0]); close(pipeIn[1]);
        close(pipeOut[0]); close(pipeOut[1]);
        throw std::runtime_error("fork failed");
    }
    if (pid == 0) {
        // CHILD PROCESS
        close(pipeIn[1]);
        close(pipeOut[0]);

        dup2(pipeIn[0], STDIN_FILENO);
        dup2(pipeOut[1], STDOUT_FILENO);

        close(pipeIn[0]);
        close(pipeOut[1]);

        const std::string& fullPath = client->getCGIFullPath();
        std::cerr << "CGI FULL PATH : " << fullPath << std::endl; 
        std::vector<std::string> env_vect = prepareEnv(client->getCGIRequest(), client->getServerConfig(), fullPath);
        char **envp = vectorToCharArray(env_vect);
        std::string interpreter = client->getCGILocation()->cgi[client->getCGIExtension()];
        if (interpreter.empty()) {
            char* argv[] = { const_cast<char*>(fullPath.c_str()), NULL };
            execve(fullPath.c_str(), argv, envp);
        } else {
            char* argv[] = {
                const_cast<char*>(interpreter.c_str()),
                const_cast<char*>(fullPath.c_str()),
                NULL
            };
            execve(interpreter.c_str(), argv, envp);
        }
        freeCharArray(envp);
        exit(1);
    }
    // PARENT PROCESS
    close(pipeIn[0]);
    close(pipeOut[1]);

    // Create CGI tracker
    CGIProcess *cgi = new CGIProcess();
    cgi->pid = pid;
    cgi->pipeIn = pipeIn[1];
    cgi->pipeOut = pipeOut[0];
    cgi->client_fd = client->getFd();
    cgi->post_body = client->getCGIRequest()->getBody();
    cgi->bytes_written = 0;
    cgi->cgi_output = "";
    cgi->start_time = time(NULL);
    cgi->stdin_closed = false;
    cgi->error = false;
    
    // Add pipes to epoll
    active_cgis[pipeOut[0]] = cgi;
    event_manager.addFd(pipeOut[0], true, false);
    if (!cgi->post_body.empty()) {
        active_cgis[pipeIn[1]] = cgi;
        event_manager.addFd(pipeIn[1], false, true);
    }
}

void Server::handleGCIEventPipe(int fd, const EventManager::Event& event) {
    CGIProcess* cgi_ptr = NULL;
    for (std::map<int, CGIProcess*>::iterator it = active_cgis.begin();
        it != active_cgis.end(); ++it) {
        if (it->second->pipeOut == fd || it->second->pipeIn == fd) {
            cgi_ptr = it->second;
            break;
        }
    }
    if (!cgi_ptr) return;
  
    // Hadchi lahma 3raft ach andir fih
    // if (event.error) {
    //     // std::cerr << "ER!!!!!!!!!!!!!\n";
    //     // completeCGI(cgi);
    //     // return ; // khasni wa9ila ndir chi tmajnina hna     
    // }

    if (event.readable)
        readCGIOutput(cgi_ptr);

    if (event.writable)
        writeCGIInput(cgi_ptr);

    // Check if process exited
    int status;
    pid_t result = waitpid(cgi_ptr->pid, &status, WNOHANG);
    if (result > 0) {
        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            cgi_ptr->error = true;
            cgi_ptr->error_code = WEXITSTATUS(status);
        }
        completeCGI(cgi_ptr);
    }
}

void Server::readCGIOutput(CGIProcess* cgi) {
    char buffer[8192];
    ssize_t bytes = read(cgi->pipeOut, buffer, sizeof(buffer));
    if (bytes > 0)
        cgi->cgi_output.append(buffer, bytes);
    else if (bytes == 0) {
        // EOF : CGI closed output
        close(cgi->pipeOut);
        event_manager.removeFd(cgi->pipeOut);
    }
    else if (errno != EAGAIN && errno != EWOULDBLOCK) {
        // fatal error
        // makhsnach n3ayto 3la errno wra read/write ms khasni nchecki -1
        return;
    }
}

// testi POST b http://localhost:8080/form.html
void Server::writeCGIInput(CGIProcess* cgi) {
    if (cgi->stdin_closed || cgi->bytes_written >= cgi->post_body.length()) {
        if (!cgi->stdin_closed) {
            close(cgi->pipeIn);
            event_manager.removeFd(cgi->pipeIn);
            cgi->stdin_closed = true;
        }
        return ;
    }

    size_t remaining = cgi->post_body.length() - cgi->bytes_written;
    const char* data = cgi->post_body.c_str() + cgi->bytes_written;
    ssize_t written = write(cgi->pipeIn, data, remaining);
    
    if (written > 0) {
        cgi->bytes_written += written;
        if (cgi->bytes_written >= cgi->post_body.length()) {
            close(cgi->pipeIn);
            event_manager.removeFd(cgi->pipeIn);
            cgi->stdin_closed = true;
        }
    }
    else if (written == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
        // fatal error
        // makhsnach n3ayto 3la errno wra read/write ms khasni nchecki -1
        return ;
    }
}

// handle cgi execution failed : done
void Server::completeCGI(CGIProcess* cgi) {
    Client* client = clients[cgi->client_fd];
    HttpResponse response;
    
    event_manager.removeFd(cgi->pipeOut);
    event_manager.removeFd(cgi->pipeIn);
    close(cgi->pipeOut);
    close(cgi->pipeIn);

    // Errors check !
    if (cgi->error || cgi->cgi_output.find("HTTP/1.0 500") == 0) {
        if (cgi->error_code == 126)
            response = HttpResponse::makeError(403, "CGI Permission denied");
        else if (cgi->error_code == 127)
            response = HttpResponse::makeError(404, "CGI Script not found");
        response = HttpResponse::makeError(500, "CGI execution failed");
    }
    else {
        // Parse CGI Output
        response = processCGIOutput(cgi->cgi_output);
    }
    
    client->setResponseBuffer(response.toString());
    client->setState(Client::SENDING_RESPONSE);
    event_manager.setWriteMonitoring(client->getFd(), true);
    event_manager.setReadMonitoring(client->getFd(), false);

    active_cgis.erase(cgi->pipeOut);
    active_cgis.erase(cgi->pipeIn);

    delete cgi;
}

// Need more testing !! (ma3t lach matidkholch bzzf lhad l function)
void Server::checkCGITimeout() {
    time_t timeout_ms = 30;
    time_t current_time = time(NULL);

    std::map<int, CGIProcess*>::iterator it = active_cgis.begin();
    while (it != active_cgis.end()) {
        CGIProcess* cgi = it->second;
        time_t elapsed = current_time - cgi->start_time;

        if (elapsed >= timeout_ms) {
            std::map<int, CGIProcess*>::iterator next_it = it;
            ++next_it;
            // TIMEOUT: terminate CGI process
            kill(cgi->pid, SIGKILL);
            waitpid(cgi->pid, NULL, 0);

            // Close pipes
            close(cgi->pipeIn);
            close(cgi->pipeOut);
            event_manager.removeFd(cgi->pipeIn);
            event_manager.removeFd(cgi->pipeOut);

            // Send 500 response to client
            Client* client = clients[cgi->client_fd];
            HttpResponse response = HttpResponse::makeError(504, "CGI timeout");
            client->setResponseBuffer(response.toString());
            client->setState(Client::SENDING_RESPONSE);
            event_manager.setWriteMonitoring(client->getFd(), true);
            event_manager.setReadMonitoring(client->getFd(), false);
            active_cgis.erase(cgi->pipeOut);
            active_cgis.erase(cgi->pipeIn);
            
            delete cgi;
            it = next_it;
        } 
        else 
            ++it;
    }
}