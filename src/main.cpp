#include <iostream>
#include <fstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
using std::ofstream;
using std::fstream;
using std::cout;
using std::endl;

void write2Log(char *path, float duration, float tput, float avg, int bitrate, char* server_ip, char* chunk_name) {
    ofstream log_file;

    log_file.open(path, fstream::out | fstream::app);
    if(!log_file)
    {
        cout<<"Error in creating file!!!";
        exit(2);
    }
    cout << "Opening log file " << path << endl;
    log_file << duration << " ";
    log_file << tput << " ";
    log_file << avg << " ";
    log_file << bitrate << " ";
    log_file << server_ip << " ";
    log_file << chunk_name << endl;

    log_file.close();

}

void startServer(int port) {
    int sockfd, new_fd, sin_size, valread;
    int yes = 1;
    struct sockaddr_in address;
    char* finack = "FINACK";

    cout << "Opening server with port number " << port << endl;
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Error: Failed to establish the socket for the server");
        exit(1);
    }

    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
        perror("Error: set socket option");
        exit(1);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr *) &address, sizeof(address)) == -1) {
        perror("Error: server failed to bind");
        exit(1);
    }

    if (listen(sockfd, 3) < 0) {
        perror("Error: failed to listen to assigned port");
        exit(1);
    }

    sin_size = sizeof(address);
    if ((new_fd = accept(sockfd, (struct sockaddr *) &address, (socklen_t *) &sin_size)) == -1) {
        perror("Error: failed to receive connection from client");
        exit(1);
    }

    while (1) {
        char buffer[1024] = {0};
        valread = recv(new_fd , buffer, 1024, 0);
        if (valread > 0) {
            cout << buffer << endl;
        }
    }
}

int main(int argc, char** argv) {
    char *log_path, *www_ip;
    int port;
    float alpha;

    if (argc != 5) {
        perror("Error: please input the correct number of argument.");
        exit(1);
    }

    log_path = argv[1];
    alpha = atof(argv[2]);
    port = atoi(argv[3]);
    www_ip = argv[4];

    startServer(port);


    return 0;
}