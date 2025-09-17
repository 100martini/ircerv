#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/epoll.h>


#define PORT_NUMBER 8081
#define MAX_EVENTS 5
#define BACKLOG 5
#define BUFFER_SIZE 1024

#include <vector>
#include "HttpParser.hpp"

struct ClientContext {
    int fd;
    std::string buffer;           
    HttpParser* parser;            
    bool close_after_response;
};

void setnonblocking (int fd) {
    int flag = fcntl(fd, F_GETFL, 0);
    if (flag == -1) {
        perror("fcntl");
        exit(EXIT_FAILURE);
    }
    if (fcntl(fd, F_SETFL, flag | O_NONBLOCK) == -1) {
        perror("fcntl");
        exit(EXIT_FAILURE);
    }
}

void cleanup_client(int epoll_fd, ClientContext* ctx) {
    if (ctx) {
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, ctx->fd, NULL);
        close(ctx->fd);
        delete ctx;
    }
}

// void handle_client_request(int client_sock_fd) {
//     // read the client (HTTP) request
//     char request_buffer[1024 + 1];
//     ssize_t bytes_reseived = recv(
//         client_sock_fd, 
//         request_buffer, 
//         1024, 0
//     );
//     if (bytes_reseived < 0) {
//         perror("resv failed");
//         exit (EXIT_FAILURE);
//     }
//     request_buffer[bytes_reseived] = '\0';
//     printf("\n\n-------REQUEST------\n%s----------------\n", request_buffer);

//     // sent an HTTP response
//     char response[] =   "HTTP/1.1 200 OK\r\n"
//                         "Content-Type: text/html\r\n\n"
//                         "<html><body><h1>Hello World!</h1></body></html>";
//     send(client_sock_fd, response, sizeof(response), 0);
//     close(client_sock_fd);
// }

std::vector<std::string> splitIntoChunks(const std::string& input, size_t chunkSize) {
    std::vector<std::string> chunks;
    for (size_t i = 0; i < input.size(); i += chunkSize) {
        chunks.push_back(input.substr(i, chunkSize));
    }
    return chunks;
}


bool testChunked(const std::string& fullRequest, ClientContext* ctx, size_t chunkSize) {
    std::vector<std::string> chunkedRequest = splitIntoChunks(fullRequest, chunkSize);
    for (size_t i = 0; i < chunkedRequest.size(); ++i) {
        int ParserResult = ctx->parser->parseHttpRequest(chunkedRequest[i]);
        if (ParserResult == COMPLETE)
            return true;
    }
    return false;
}

int main (void) {

    printf("SERVER STARTED..\n");

    // creating the socket
    // set up the server adress structure
    // connect the socket with a PORT NUMBER
    struct addrinfo  hints, *res;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;              // use IPV4
    hints.ai_socktype = SOCK_STREAM; 
    
    getaddrinfo("127.0.1.2", "8081", &hints, &res);
    
    int socket_descriptor = socket(
        res->ai_family,                 // IPV4
        res->ai_socktype,               // TCP
        res->ai_protocol                // Protocol, 0 let the system choose
    );

    if (socket_descriptor < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    if (bind(socket_descriptor, res->ai_addr, res->ai_addrlen) < 0) {
        perror("bind failed");
        return EXIT_FAILURE;
    }

    // put the socket into listening mode
    printf("Listening on port %d\n", PORT_NUMBER);
    if (listen(socket_descriptor, BACKLOG) < 0) {
        perror("listen failed");
        return EXIT_FAILURE;
    }

    struct epoll_event ev[MAX_EVENTS], event;
    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd < 0) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }

    event.events = EPOLLIN;      // monitor the incoming data
    event.data.fd = socket_descriptor;

    // add the listening socket to epoll
    // EPOLL_CTL_ADD to add a new socket
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, socket_descriptor, &event) < 0) {
        perror("epoll_ctl");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in client_address;
    socklen_t addrlen = sizeof(client_address);

    while (1) {
        int events_number = epoll_wait(epoll_fd, ev, MAX_EVENTS, -1);
        if (events_number < 0) {
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }

        // loop through the events epoll_wail returns
        for (int itr = 0; itr < events_number; ++itr) {
            // new client want to connect
            if (ev[itr].data.fd == socket_descriptor) {
                // handl the new client
                // accept client connection
                int client_socket_descriptor = accept(
                    socket_descriptor,                          // listening socket descriptor
                    (struct sockaddr *)&client_address,         // client address structor 
                    &addrlen                                    // client address size
                );
                
                if (client_socket_descriptor < 0){
                    perror("accept failed");
                    exit (EXIT_FAILURE);
                }

                // make the client socket non blocking
                setnonblocking(client_socket_descriptor);

                //  parser stuff
                ClientContext* ctx = new ClientContext;
                ctx->fd = client_socket_descriptor;
                ctx->parser = new HttpParser();
                ctx->close_after_response = false;
                ctx->buffer = "";

                event.events = EPOLLIN;
                event.data.ptr = ctx;

                // add the client socket to the epoll monitoring
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_socket_descriptor, &event) < 0) {
                    perror("epoll_ctl");
                    exit (EXIT_FAILURE);
                }
            } else {   // existing clinet sent data
                // handle the HTTP request sent by the client
                //handle_client_request(ev[itr].data.fd);
                ClientContext* ctx = static_cast<ClientContext*>(ev[itr].data.ptr);

                if (ev[itr].events & EPOLLERR || ev[itr].events & EPOLLHUP) {
                    // Error or hangup
                    cleanup_client(epoll_fd, ctx);
                    continue;
                }

                char tmp_buffer[BUFFER_SIZE];
                ssize_t received = recv(ctx->fd, tmp_buffer, sizeof(tmp_buffer), 0);

                if (received <= 0) {
                    // Handle disconnect or error
                    close(ctx->fd);
                    delete ctx;
                    continue;
                }

                tmp_buffer[received] = '\0';
                
                std::cout << "----------------------------------------------------\n";
                std::cout << "REQUEST :\n" << tmp_buffer << "\n\n"; 
                std::cout << "----------------------------------------------------\n\n";
               

                
                // int done = ctx->parser->parseHttpRequest(tmp_buffer);
                bool done = testChunked(tmp_buffer, ctx, 10);


                if (done) {
                    // Respond to client
                    const char* response = 
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: text/html\r\n\r\n"
                        "<html><body><h1>Hello World!</h1></body></html>";
                    send(ctx->fd, response, strlen(response), 0);

                    // Clean up
                    close(ctx->fd);
                    delete ctx;
                } 
            }
        }
    }

    // close the connection
    close(socket_descriptor);

    return 0;
}