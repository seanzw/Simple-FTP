#ifndef FTP_SERVER_H
#define FTP_SERVER_H

#include <map>
#include <regex>
#include <utility>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <dirent.h> 
#include "Server.hpp"
#define BUF_SIZE 1024

class FTPServer: public Server {
public:
    FTPServer(int port_num,
        const char *log_file
        ): Server(port_num, log_file) {

    }
    ~FTPServer() {}

protected:

    enum SessionStatus {
        SESSION_DOWN,
        SESSION_LOGIN,
        SESSION_USER,
        SESSION_WORKING
    };

    enum Command {
        ERR,
        USER,
        PASS,
        SYST,
        PWD,
        TYPE,
        PORT,
        LIST,
        RETR,
        STOR,
        CWD,
        ABOR
    };

    struct Message {
        Command command;
        string content;
        Message(Command command, string content) {
            this->command = command;
            this->content = content;
        }
    };

    class Session {
    public:
        Session(int client_fd, const string &pwd) {
            this->client_fd = client_fd;
            flush(replies.at(220));
            this->status = SESSION_LOGIN;
            this->pwd = pwd;
            this->socket_fd = -1;
        }
        Session() {
            this->client_fd = -1;
            this->status = SESSION_DOWN;
        }
        ~Session() {}

        /**
         * Handle the command.
         * Return 0 when everthing is fine.
         */
        int handle(const Message &msg) {

            string reply = "";

            switch (msg.command) {
            case USER:
                if (status == SESSION_LOGIN) {
                    flush(replies.at(331));
                    status = SESSION_USER;
                    return 0;
                } else {
                    return 1;
                }
                break;

            case PASS:
                if (status == SESSION_USER) {
                    flush(replies.at(230));
                    status = SESSION_WORKING;
                    return 0;
                } else {
                    status = SESSION_LOGIN;
                    return 1;
                }
                break;

            case SYST:
                flush(replies.at(215));
                return 0;

            case PWD:
                reply += "257 ";
                reply += pwd;
                reply += "\n";
                flush(reply);
                return 0;

            case TYPE:
                if (msg.content != "I") {
                    reply = "501 unknown type\n";
                } else {
                    reply = "200 switching to binary mode\n";
                }
                flush(reply);
                return 0;

            case PORT:
                if (socket_fd != -1) {

                }
                socket_fd = open_socket(msg.content);
                if (socket_fd != -1) {
                    flush(replies.at(200));
                } else {
                    flush(replies.at(421));
                }
                return 0;

            case LIST:
                if (socket_fd != -1) {
                    string reply = "";
                    if (get_list(pwd, reply) == 0) {
                        flush(replies.at(150));
                        cout << reply << endl;
                        send(socket_fd, reply.c_str(), reply.size(), 0);
                        flush(replies.at(226));
                    } else {
                        flush(replies.at(421));
                    }
                    close(socket_fd);
                    socket_fd = -1;
                }
                return 0;

            case RETR:
                if (socket_fd != -1) {
                    string fn = pwd;
                    fn += '/';
                    fn += msg.content;
                    cout << "RETR " << fn << endl;
                    ifstream file(fn);
                    if (file.is_open()) {
                        file.seekg(0, file.end);
                        int len = file.tellg();
                        file.seekg(0, file.beg);
                        char *buf = new char[len];
                        file.read(buf, len);
                        if (file) {
                            flush(replies.at(150));
                            send(socket_fd, buf, len, 0);
                            flush(replies.at(226));
                        } else {
                            flush(replies.at(421));
                        }
                        file.close();
                        delete [] buf;
                    } else {
                        flush(replies.at(501));
                    }
                    close(socket_fd);
                    socket_fd = -1;
                }
                return 0;

            case STOR:
                if (socket_fd != -1) {
                    string fn = pwd;
                    fn += '/';
                    fn += msg.content;
                    cout << "STOR " << fn << endl;
                    ofstream file(fn);
                    if (file.is_open()) {
                        flush(replies.at(125));
                        string data = get_data(socket_fd);
                        file << data;
                        flush(replies.at(226));
                        file.close();
                    } else {
                        flush(replies.at(501));
                    }
                    close(socket_fd);
                    socket_fd = -1;
                }
                return 0;

            case CWD: {
                string new_pwd;
                if (msg.content[0] == '/') {
                    new_pwd = msg.content;
                } else {
                    new_pwd = pwd;
                    new_pwd += "/";
                    new_pwd += msg.content;
                    char buf[BUF_SIZE];
                    realpath(new_pwd.c_str(), buf);
                    new_pwd = string(buf);
                }

                // Check if this directory exist.
                if (directory_exist(new_pwd)) {
                    flush(replies.at(200));
                    pwd = new_pwd;
                } else {
                    flush(replies.at(501));
                }
                return 0;
            }

            case ABOR:
                if (socket_fd != -1) {
                    close(socket_fd);
                }
                status = SESSION_DOWN;
                return -1;

            case ERR:
                flush(replies.at(500));
                return 0;

            default:
                return 1;
            }
            return 0;
        }

        int client_fd;
        string pwd;
        SessionStatus status;
        int socket_fd;

        /***************************************************************************
         * Utilities.
         ***************************************************************************/
        inline void flush(const string &reply) {
            send(client_fd, reply.c_str(), reply.size(), 0);
        }

        /***************************************************************************
         * Open the connection to the client in active mode.
         * Target is something like "127,0,0,1,117,117".
         *
         * Returns the socket_fd.
         ***************************************************************************/
        int open_socket(string target) const {
            cout << target << ";" << endl;

            replace(target.begin(), target.end(), ',', '.');
            cout << target << ';' << endl;

            regex ip_reg("([[:digit:]]{1,3}[.]){3}[[:digit:]]{1,3}");
            smatch ip_match;
            regex_search(target, ip_match, ip_reg);

            if (ip_match.size() == 0) {
                return -1;
            }

            string ip(ip_match[0]);
            string port = target.substr(ip.size() + 1);
            unsigned short portnum;

            size_t separator = port.find('.');
            if (separator == string::npos) {
                return -1;
            } else {
                portnum = (stoi(port.substr(0, separator)) << 8) + stoi(port.substr(separator + 1));
            }

            cout << ip << ":" << portnum << endl;

            int ret = socket(PF_INET, SOCK_STREAM, 0);
            if (ret == -1) {
                cout << "FTPServer > Failed to create socket" << endl;
                return ret;
            }

            struct sockaddr_in client;
            memset(&client, 0, sizeof(client));

            client.sin_family = AF_INET;
            client.sin_addr.s_addr = inet_addr(ip.c_str());
            client.sin_port = htons(portnum);

            if (connect(ret, (sockaddr *)&client, sizeof(client)) < 0) {
                return -1;
            }

            return ret;
        }

        int get_list(const string &path, string &out) const {

            DIR *dir;
            dirent *dirInfo;

            if ((dir = opendir(path.c_str())) == NULL) {
                return -1;
            } else {
                while ((dirInfo = readdir(dir)) != NULL) {
                    if ((dirInfo->d_type == DT_DIR || dirInfo->d_type == DT_REG) &&
                        (dirInfo->d_name[0] != '.')
                    ) {
                        string name(dirInfo->d_name);
                        out += name;
                        out += "\n";
                    }
                }
                closedir(dir);
                out.replace(out.size() - 1, 1, "\0");
                return 0;
            }
        }

        bool directory_exist(const string &path) const {
            DIR *dir = opendir(path.c_str());
            return dir != NULL;
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


    };

    map<int, Session*> sessions;
    

    virtual int handle_new_client(int client_fd) {
        sessions[client_fd] = new Session(client_fd, "/home/sean");
        return 0;
    }
    virtual int handle_exist_client(int client_fd) {
        Message msg = get_message(client_fd);
        if (msg.command == ERR) {
            log << "ERROR : " << msg.content << endl;
        }
        int ret = sessions[client_fd]->handle(msg);
        if (ret == -1) {
            close_client(client_fd);
            delete sessions[client_fd];
        }
        return 0;
    }

private:

    /******************************************************************************
     * Parse the message from the client.
     *
     * Return the Message.
     *
     *****************************************************************************/
    Message get_message(int client_fd) {

        char buf[BUF_SIZE];
        int read_n = get_line(client_fd, buf, sizeof(buf));
        if (read_n <= 0) {
            return Message(ERR, "");
        }
        string raw(buf);
        string command;
        string content;
        size_t separator = raw.find_first_of(' ');
        if (separator == string::npos) {
            command = raw.substr(0, raw.size() - 1);
            content = "";
        } else {
            command = raw.substr(0, separator);
            content = raw.substr(separator + 1, raw.size() - separator - 2);
        }
        auto iter = dict.find(command);
        if (iter != dict.end()) {
            return Message(iter->second, content);
        } else {
            return Message(ERR, raw);
        }
    } 


    /******************************************************************************
     * Get a line from the socket.
     *
     * socket_fd: socket descriptor
     * buf: pointer to buffer
     * size: size of the buffer
     * 
     * Returns the number of chars stored, excluding null.
     ******************************************************************************/
    int get_line(int socket_fd, char *buf, int size) {

        int idx = 0, read_n;
        char c = '\0';
        while ((idx < size - 1) && (c != '\n')) {

            // Read one character from the socket.
            read_n = recv(socket_fd, &c, 1, 0);
            if (read_n > 0) {

                // If this is a CR.
                if (c == '\r') {

                    // Peek the next char.
                    read_n = recv(socket_fd, &c, 1, MSG_PEEK);
                    if ((read_n > 0) && (c == '\n')) {

                        // If this is a LF, then eat it.
                        recv(socket_fd, &c, 1, 0);

                    } else {
                        c = '\n';
                    }
                }
                buf[idx] = c;
                idx++;
            } else {

                // Something goes wrong, return an empty line.
                c = '\n';

            }
        }

        // Terminate the line with a NULL character.
        buf[idx] = '\0';
        return idx;
    }

    const static map<string, Command> dict;
    const static map<int, string> replies;
};

int main() {
    FTPServer server(9999, "log.txt");
    server.start();
    return 0;
}

const map<string, FTPServer::Command> FTPServer::dict {
    make_pair(  "USER",   USER  ),
    make_pair(  "PASS",   PASS  ),
    make_pair(  "SYST",   SYST  ),
    make_pair(  "PWD",    PWD   ),
    make_pair(  "TYPE",   TYPE  ),
    make_pair(  "PORT",   PORT  ),
    make_pair(  "LIST",   LIST  ),
    make_pair(  "RETR",   RETR  ),
    make_pair(  "STOR",   STOR  ),
    make_pair(  "CWD",    CWD   ),
    make_pair(  "ABOR",   ABOR  )
};

const map<int, string> FTPServer::replies {
    make_pair(125, "125 transfer starting\n"),
    make_pair(150, "150 data coming\n"),
    make_pair(200, "200 done\n"),
    make_pair(215, "215 UNIX Type: L8\n"),
    make_pair(220, "220 waiting for input\n"),
    make_pair(226, "226 data sent\n"),
    make_pair(230, "230 user logged in\n"),
    make_pair(331, "331 user name ok, need password\n"),
    make_pair(421, "421 shut down\n"),
    make_pair(500, "500 command unrecognized\n"),
    make_pair(501, "501 syntax error in arguments\n")
};

#endif