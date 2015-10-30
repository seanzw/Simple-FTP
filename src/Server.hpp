/****************************************************
Server base class.
*****************************************************/

#ifndef SERVER_H
#define SERVER_H

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fstream>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace std;

class Server {
public:
    Server(
        int port_num,
        const char * log_file
        ) : port(port_num) {

        log.open(log_file, ios::app);
        if (!log.is_open()) {
            throw "Failed opening log file.\n";
        }

        // Create the socket.
        socket_fd = socket(PF_INET, SOCK_STREAM, 0);
        if (socket_fd == -1) {
            log << "Server > Failed creating socket\n";
            throw "Failed creating socket.\n";
        }

        log << "Server > Create socket." << endl;

        // Set the socket options so that we can reuse the server immediately.
        int optval = 1;
        setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval, sizeof(int));

        // Set the server address.
        struct sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons((unsigned short)port);
        server_addr.sin_addr.s_addr = INADDR_ANY;

        // Bind the server to the port.
        if (bind(socket_fd, (sockaddr *)&server_addr, sizeof(server_addr))) {
            throw "Failed binding socket to the port.\n";
        }

        log << "Server > Bind socket to port " << port << endl;

    }

    ~Server() {

    }

    virtual int start() {


        // Listen to the port.
        if (listen(socket_fd, 5)) {
            log << "Server > Failed listening to port." << endl;
            throw "Failed listening to port.\n";
        }

        log << "Server > Listening to port " << port << endl;

        // Initialize the file descriptor set.
        FD_ZERO(&active_fd_set);
        FD_SET(socket_fd, &active_fd_set);

        // Main loop.
        while (true) {

            read_fd_set = active_fd_set;

            // Set timeout.
            timeval tv;
            tv.tv_sec = 1;
            tv.tv_usec = 0;

            if (select(FD_SETSIZE, &read_fd_set, NULL, NULL, &tv) < 0) {
                log << "Server > Failed selecting file descriptor set." << endl;
                throw "Failed selecting fd_set.\n";
            }

            for (int i = 0; i < FD_SETSIZE; ++i) {
                if (FD_ISSET(i, &read_fd_set)) {

                    // If this is the socket, then there is a new connection.
                    if (i == socket_fd) {

                        sockaddr_in client_addr;
                        socklen_t size = sizeof(client_addr);

                        // Get the new fd for the new connection.
                        int client_fd = accept(socket_fd, (sockaddr *)&client_addr, &size);
                        if (client_fd < 0) {
                            stop();
                        }

                        log << "Server > New connection from " << inet_ntoa(client_addr.sin_addr)
                            << " Port: " << ntohs(client_addr.sin_port)
                            << " FD: " << client_fd << endl;

                        FD_SET(client_fd, &active_fd_set);

                        // If the server what to do something with the new client.
                        handle_new_client(client_fd);

                    } else {

                        log << "Server > Serving client " << i << endl;

                        // Handle the existing client.
                        handle_exist_client(i);

                    }
                }
            }
        }

        return 0;
    }

    // Stop the server.
    int stop() {

        if (log.is_open())
            log.close();
        if (socket_fd > 0)
            close(socket_fd);

        return 0;
    }

protected:

    // Handle the request.
    virtual int handle_exist_client(int client_fd) = 0;
    virtual int handle_new_client(int client_fd) = 0;

    int port;
    int socket_fd;

    ofstream log;

    fd_set active_fd_set, read_fd_set;

    void close_client(int client_fd) {
        log << "Server > Close client " << client_fd << endl;
        FD_CLR(client_fd, &active_fd_set);
        close(client_fd);
    }

};

#endif