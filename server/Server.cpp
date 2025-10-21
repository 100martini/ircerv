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