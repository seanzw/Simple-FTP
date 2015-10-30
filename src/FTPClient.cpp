#include <iostream>
#include <sstream>
#include <fstream>
#include <string>

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUF_SIZE 1024

using namespace std;

struct Reply {
    int reply;
    string content;
    Reply(int r, const string &c) {
        reply = r;
        content = c;
    }
};

Reply get_response(int command_fd) {
    char buf[BUF_SIZE];
    while (true) {
        int read_n = read(command_fd, buf, sizeof(buf));
        if (read_n < 0) {
            return Reply(-1, "");
        } else if (read_n > 0) {
            string response(buf, read_n);
            size_t separator = response.find_first_of(" ");
            if (separator == string::npos) {
                return Reply(-1, "");
            } else {
                string content = response.substr(separator + 1, response.size() - separator - 2);
                return Reply(stoi(response.substr(0, separator)), content);
            }
        }
    }
}

string get_data(int data_fd) {
    stringstream ss;
    char buf[BUF_SIZE];
    int read_n;
    while ((read_n = read(data_fd, buf, sizeof(buf))) > 0) {
        ss << string(buf, read_n);
    }
    return ss.str();
}

void send_request(int command_fd, const string &request) {
    write(command_fd, request.c_str(), request.size());
}

void print_data(const string &content) {
    cout << "--------------\n";
    cout << content << endl;
    cout << "--------------\n";
}

void print_command_list() {
    cout << "--------------\n";
    cout << "get : get a file from server\n";
    cout << "put : upload a file to server\n";
    cout << "pwd : print current directory\n";
    cout << "dir : list the file in the directory\n";
    cout << "cd  : change directory\n";
    cout << "?   : show command list\n";
    cout << "quit: quit\n";
    cout << "--------------\n";
}

int establish_data_link(int command_fd, int listening_fd) {

    // Set the server address.
    struct sockaddr_in server_addr;
    socklen_t len = sizeof(server_addr);

    getsockname(listening_fd, (struct sockaddr *)&server_addr, &len);
    unsigned short data_port = ntohs(server_addr.sin_port);

    send_request(command_fd, "TYPE I\n");
    Reply reply = get_response(command_fd);
    if (reply.reply != 200) {
        cout << "Failed set image mode\n";
        return -1;
    } else {
        print_data(reply.content);
    }

    string request = "PORT 127,0,0,1,";
    request += to_string(data_port >> 8);
    request += ",";
    request += to_string(data_port & 0xFF);
    request += "\n";
    send_request(command_fd, request);
    socklen_t clilen;
    sockaddr_in cli_addr;
    clilen = sizeof(cli_addr);
    int data_fd = accept(listening_fd, (struct sockaddr *) &cli_addr, &clilen);
    if (data_fd <= 0) {
        return -1;
    }

    if (get_response(command_fd).reply != 200) {
        cout << "Failed port command\n";
        return -1;
    }
    return data_fd;
}

int handle_cmd(int command_fd, int listening_fd, const string &cmd) {

    if (cmd.substr(0, 3) == "dir") {

        int data_fd = establish_data_link(command_fd, listening_fd);
        if (data_fd <= 0) {
            cout << "Failed establish data link\n";
            return -1;
        }
        send_request(command_fd, "LIST\n");
        if (get_response(command_fd).reply == 150) {
            if (get_response(command_fd).reply == 226) {
                string list = get_data(data_fd);
                print_data(list);
                close(data_fd);
                return 0;
            }
        }
        cout << "Failed getting list\n";
        close(data_fd);
        return -1;
    }

    else if (cmd.substr(0, 3) == "pwd") {
        send_request(command_fd, "PWD\n");
        Reply reply = get_response(command_fd);
        if (reply.reply == 257) {
            print_data(reply.content);
            return 0;
        } else {
            cout << "Failed getting the working directory\n";
            return -1;
        }
    }

    else if (cmd.substr(0, 3) == "get") {

        string fn = cmd.substr(4);

        int data_fd = establish_data_link(command_fd, listening_fd);
        if (data_fd <= 0) {
            cout << "Failed establish data link\n";
            return -1;
        }
        string request = "RETR ";
        request += fn;
        request += "\n";
        send_request(command_fd, request);
        if (get_response(command_fd).reply == 150) {
            if (get_response(command_fd).reply == 226) {
                string data = get_data(data_fd);
                ofstream file(fn);
                if (file.is_open()) {
                    file << data;
                    file.close();
                    print_data("Succeed retrieving file\n");
                    file.close();
                    close(data_fd);
                    return 0;
                }
            }
        }

        cout << "Failed retrieving the file\n";
        close(data_fd);
        return 0;
    }

    else if (cmd.substr(0, 3) == "put") {

        string fn = cmd.substr(4);
        int data_fd = establish_data_link(command_fd, listening_fd);
        if (data_fd <= 0) {
            cout << "Failed establish data link\n";
            return -1;
        }
        string request = "STOR ";
        request += fn;
        request += "\n";
        send_request(command_fd, request);
        if (get_response(command_fd).reply == 125) {
            ifstream file(fn);
            if (file.is_open()) {
                file.seekg(0, file.end);
                int len = file.tellg();
                file.seekg(0, file.beg);
                char *buf = new char[len];
                file.read(buf, len);
                if (file) {
                    send(data_fd, buf, len, 0);
                    close(data_fd);
                    if (get_response(command_fd).reply == 226) {
                        cout << "Succeed uploading " << fn << endl;
                        delete [] buf;
                        file.close();
                        return 0;
                    }
                }
                file.close();
                delete [] buf;
            }
        }

        cout << "Failed uploading the file\n";
        close(data_fd);
        return 0;
    }

    else if (cmd.substr(0, 2) == "cd") {
        string path = cmd.substr(3);
        string request = "CWD ";
        request += path;
        request += "\n";
        send_request(command_fd, request);
        Reply reply = get_response(command_fd);
        print_data(reply.content);
        return 0;
    }

    else if (cmd.substr(0, 4) == "quit") {
        return -1;
    }

    else {
        print_command_list();
        return 0;
    }

}

int main(int argc, char *argv[]) {

    if (argc < 3) {
        cout << "usage: ip port" << endl;
        return -1;
    }

    cout << argv[1] << ":" << argv[2] << endl;

    int command_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (command_fd == -1) {
        cout << "FTPServer > Failed to create socket" << endl;
        return -1;
    }

    struct sockaddr_in sever;
    memset(&sever, 0, sizeof(sever));

    sever.sin_family = AF_INET;
    sever.sin_addr.s_addr = inet_addr(argv[1]);
    sever.sin_port = htons(atoi(argv[2]));

    if (connect(command_fd, (sockaddr *)&sever, sizeof(sever)) < 0) {
        return -1;
    }

    if (get_response(command_fd).reply != 220) {
        return -1;
    }

    send_request(command_fd, "USER sean\n");

    if (get_response(command_fd).reply != 331) {
        return -1;
    }

    send_request(command_fd, "PASS sean\n");

    if (get_response(command_fd).reply != 230) {
        cout << "Failed logging in\n";
        return -1;
    }

    send_request(command_fd, "SYST\n");
    Reply reply = get_response(command_fd);
    if (reply.reply != 215) {
        return -1;
    } else {
        print_data(reply.content);
    }

    // Create the data socket.
    int listening_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (listening_fd == -1) {
        cout << "Failed creating socket.\n";
        return -1;
    }

    // Set the socket options so that we can reuse the server immediately.
    int optval = 1;
    setsockopt(listening_fd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval, sizeof(int));

    // Set the server address.
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = 0;
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // Bind the server to the port.
    if (bind(listening_fd, (sockaddr *)&server_addr, sizeof(server_addr))) {
        cout << "Failed binding socket to the port.\n";
        return -1;
    }

    if (listen(listening_fd, 5)) {
        cout << "Failed listening to port." << endl;
        return -1;
    }

    string command;
    while (getline(cin, command)) {
        if (handle_cmd(command_fd, listening_fd, command) < 0) {
            cout << "Quiting" << endl;
            send_request(command_fd, "ABOR\n");
            close(listening_fd);
            close(command_fd);
            break;
        }
    }

    return 0;
}