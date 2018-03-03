#include <iostream>
#include <fstream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <vector>
#include <sys/select.h>
#include <unistd.h>
#include <sys/types.h>
#include <netdb.h>
#include <algorithm>
#include <cassert>
#include <sys/time.h>
#include <cstdio>

using std::ofstream;
using std::fstream;
using std::cout;
using std::endl;

const int buffer_size = 8192;

void write2Log(char* path, float duration, float tput, float avg, int bitrate, char* server_ip, char* chunk_name) {
    ofstream log_file;

    log_file.open(path, fstream::out | fstream::app);
    if(!log_file) {
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

typedef struct client_data
{
    char http_request[8192];
    char chunk_name[50];
    char* server_ip;
    client_data* server_pointer;
    std::vector<int> bitrate_vector;
    struct timeval start_receive;
    struct timeval end_receive;
    double average_throughput;
    int server_fd;
    int is_server;
    int content_length;
    int total_received;
    int request_bitrate;
    bool is_f4m;
    bool is_chunk;
    bool first_f4m;
}client_data;

typedef struct client_data_fd
{
    client_data* cli_data;
    int fd;

}client_data_fd;

void start_client(client_data_fd* fd, char* buffer, int bytesRecvd) {
    char *f4m;
    char *chunk;
    chunk=strcasestr(buffer,"-Frag");
    f4m=strcasestr(buffer,".f4m");
    if (f4m!=NULL) {
        fd->cli_data->is_f4m=1;
        fd->cli_data->server_pointer->is_f4m=1;
        fd->cli_data->is_chunk=0;
        fd->cli_data->server_pointer->is_chunk=0;
        memcpy(fd->cli_data->http_request,buffer,bytesRecvd);
    }
    else if (chunk!=NULL) {
        fd->cli_data->is_chunk=1;
        fd->cli_data->server_pointer->is_chunk=1;
        fd->cli_data->is_f4m=0;
        fd->cli_data->server_pointer->is_f4m=0;
        int bitrate_browser;
        if(fd->cli_data->server_pointer->bitrate_vector.size()==0) {
            std::cout << "no bitrate in vector" <<std::endl;
        }
        else {
            float bitrate_constraint=(fd->cli_data->server_pointer->average_throughput)/1.5;
            if (fd->cli_data->server_pointer->bitrate_vector[0]>=bitrate_constraint) {
                bitrate_browser=fd->cli_data->server_pointer->bitrate_vector[0];
            }
            else {
                for (int j = 0; j < fd->cli_data->server_pointer->bitrate_vector.size(); ++j) {
                    if (fd->cli_data->server_pointer->bitrate_vector[j]<bitrate_constraint) {
                        bitrate_browser=fd->cli_data->server_pointer->bitrate_vector[j];
                    }
                    else {
                        break;
                    }
                }
            }
        }
        fd->cli_data->request_bitrate=bitrate_browser;
        char * replace_bitrate_pointer_left;
        char * replace_bitrate_pointer_right;
        char * chunk_name_pointer_right;
        replace_bitrate_pointer_left=strstr(buffer,"Seg");
        replace_bitrate_pointer_right=strstr(buffer,"Seg");
        while(*replace_bitrate_pointer_left!='/') {
            replace_bitrate_pointer_left=replace_bitrate_pointer_left-1;
        }
        replace_bitrate_pointer_left=replace_bitrate_pointer_left+1;
        chunk_name_pointer_right=strstr(replace_bitrate_pointer_right," ");
        strncpy(fd->cli_data->server_pointer->chunk_name,replace_bitrate_pointer_left,(chunk_name_pointer_right-replace_bitrate_pointer_left));
        *replace_bitrate_pointer_left='\0';
        char buffer_again[buffer_size];
        bzero(buffer_again,buffer_size);
        snprintf(buffer_again,buffer_size,"%s%d%s",buffer,bitrate_browser,replace_bitrate_pointer_right);
        memcpy(buffer,buffer_again,strlen(buffer_again));
        //timer start
        gettimeofday(&fd->cli_data->start_receive, NULL);
        fd->cli_data->server_pointer->start_receive=fd->cli_data->start_receive;
    }
    else {
        fd->cli_data->is_chunk=0;
        fd->cli_data->server_pointer->is_chunk=0;
        fd->cli_data->is_f4m=0;
        fd->cli_data->server_pointer->is_f4m=0;
    }

    if (send(fd->cli_data->server_fd, buffer,bytesRecvd , 0)== -1) {
        std::cout << "error in browser send to server" <<std::endl;
        exit(1);
    }
}

void start_server(client_data_fd* fd, char* buffer, int bytesRecvd, float alpha, char* path) {
    char* content_length_start;
    char* content_length_end;
    char* body;

    if (strstr(buffer,"\r\n\r\n")!=NULL)//has header
    {
        body=strstr(buffer,"\r\n\r\n");
        //parse
        content_length_start=strstr(buffer,"Content-Length: ");
        content_length_end=strstr(content_length_start,"\r\n");
        char content_length[25];
        strncpy(content_length,(content_length_start+16),(content_length_end-content_length_start-16));
        content_length[(content_length_end-content_length_start-16)]='\0';
        fd->cli_data->content_length=atoi(content_length);
        fd->cli_data->total_received=bytesRecvd;
        if (fd->cli_data->is_f4m)//f4m file
        {
            if (fd->cli_data->first_f4m==1)
            {
                if (fd->cli_data->total_received>fd->cli_data->content_length)// all are received
                {
                    //parse f4m
                    char* xml_pointer;
                    char* media_pointer;
                    char* bitrate_pointer;
                    xml_pointer=strstr(body,"<?xml");
                    media_pointer=strstr(xml_pointer,"<media");
                    while(media_pointer!=NULL)
                    {
                        bitrate_pointer=strstr(media_pointer,"bitrate=");
                        if (bitrate_pointer!=NULL)
                        {
                            int bitrate;
                            if (std::sscanf(bitrate_pointer,"bitrate=\"%d\"", &bitrate)>=1)
                            {
                                //insert into vector
                                fd->cli_data->bitrate_vector.push_back(bitrate);
                            }
                        }
                        media_pointer=strstr(media_pointer+1,"<media");
                    }
                    fd->cli_data->first_f4m=0;
                }
                char* replace_f4m_pointer;
                replace_f4m_pointer=strstr(fd->cli_data->server_pointer->http_request,".f4m");
                int total_len=strlen(fd->cli_data->server_pointer->http_request);
                int before_len=replace_f4m_pointer-fd->cli_data->server_pointer->http_request;
                int after_len=total_len-before_len;
                memmove(replace_f4m_pointer+12,replace_f4m_pointer+5,after_len);
                memmove(replace_f4m_pointer,"_nolist.f4m ",strlen("_nolist.f4m "));
                if (send(fd->fd, fd->cli_data->server_pointer->http_request,strlen(fd->cli_data->server_pointer->http_request), 0)== -1)
                {
                    std::cout << "error in server send to browser" <<std::endl;
                    exit(1);
                }
            }
            else
            {
                if (send(fd->cli_data->server_fd, buffer,bytesRecvd , 0)== -1)
                {
                    std::cout << "error in server send to browser" <<std::endl;
                    exit(1);
                }
            }
        }
        else //other html header response
        {
            if (send(fd->cli_data->server_fd, buffer,bytesRecvd , 0)== -1)
            {
                std::cout << "error in server send to browser" <<std::endl;
                exit(1);
            }
        }
    }
    else
    {
        if (fd->cli_data->is_chunk==1)
        {
            //chunk body data
            fd->cli_data->total_received+=bytesRecvd;
            if (fd->cli_data->total_received>fd->cli_data->content_length)// all are received
            {
                gettimeofday(&fd->cli_data->end_receive, NULL);
                double elapsedTime = (fd->cli_data->end_receive.tv_sec - fd->cli_data->start_receive.tv_sec);
                elapsedTime += (fd->cli_data->end_receive.tv_usec - fd->cli_data->start_receive.tv_usec) / 1000000.0;
                double new_throughput=(fd->cli_data->content_length)*8/(elapsedTime*1000);//kb/s
                fd->cli_data->average_throughput = alpha * new_throughput + (fd->cli_data->average_throughput)*(1-alpha);
                // TODO LOG STUFF
                write2Log(path, elapsedTime, new_throughput, fd->cli_data->average_throughput, fd->cli_data->server_pointer->request_bitrate, fd->cli_data->server_pointer->server_ip, fd->cli_data->chunk_name);
            }
            if (send(fd->cli_data->server_fd, buffer,bytesRecvd , 0)== -1)
            {
                std::cout << "error in server send to browser" <<std::endl;
                exit(1);
            }
        }
        else
        {
            if (send(fd->cli_data->server_fd, buffer,bytesRecvd , 0)== -1)
            {
                std::cout << "error in server send to browser" <<std::endl;
                exit(1);
            }
        }
    }
}


int main(int argc, char** argv) {
    char *path, *www_ip;
    int server_port, server_sock, client_sock, valread, valsend;
    float alpha;
    fd_set readfds;

    if (argc != 5) {
        perror("Error: please input the correct number of argument.");
        exit(1);
    }

    path = argv[1];
    alpha = atof(argv[2]);
    server_port = atoi(argv[3]);
    www_ip = argv[4];

    int browser_listen_sd;
    struct sockaddr_in sever_addr, client_addr;
    if ((browser_listen_sd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        std::cout << "Error in opening TCP socket\n";
        return -1;
    }

    memset(&sever_addr, 0, sizeof (sever_addr));
    sever_addr.sin_family = AF_INET;
    sever_addr.sin_addr.s_addr = INADDR_ANY;
    sever_addr.sin_port = htons(server_port);

    if (bind(browser_listen_sd, (struct sockaddr *) &sever_addr, sizeof (sever_addr)) < 0) {
        std::cout << "Cannot bind socket to address\n";
        return -1;
    }
    //listen the port
    if (listen(browser_listen_sd, 10) < 0) {
        std::cout << "Error: Cannot bind socket to address\n";
        return -1;
    }
    memset(&client_addr, 0, sizeof (client_addr));
    socklen_t client_addr_len = sizeof(client_addr);
    int ip_sd;
    fd_set read_set;
    std::vector<client_data_fd*> fds;
    int client_sd;

    while (1) {
        int maxfd = 0;
        FD_ZERO(&read_set);
        FD_SET(browser_listen_sd, &read_set);
        for(int i = 0; i < (int) fds.size(); ++i) {
            FD_SET(fds[i]->fd, &read_set);
            if (maxfd < fds[i]->fd) {
                maxfd=fds[i]->fd;
            }
        }
        maxfd = std::max(maxfd, browser_listen_sd);
        // maxfd + 1 is important
        int number_fd = select(maxfd + 1, &read_set, NULL, NULL, NULL);
        if(number_fd== -1) {
            std::cout << "Error on select" << std::endl;
        }
        if(FD_ISSET(browser_listen_sd, &read_set)) {
            client_sd = accept(browser_listen_sd, (struct sockaddr *) &client_addr, &client_addr_len);
            if(client_sd == -1) {
                std::cout << "Error on accept" << std::endl;
            }
            else {
                // add client to vector
                /*connect to www_ip*/
                struct addrinfo hints;
                struct addrinfo* result;
                memset(&hints, 0, sizeof (hints));
                hints.ai_family = AF_INET;
                hints.ai_socktype = SOCK_STREAM;
                hints.ai_flags = AI_PASSIVE;
                if (getaddrinfo(www_ip, "80", &hints, &result) < 0) {
                    std::cout << "Error on getaddrinfo" << std::endl;
                    return -1;
                }
                int cli_2_server_sd;
                if((cli_2_server_sd = socket(result->ai_family, result->ai_socktype,result->ai_protocol))<0) {
                    std::cout << "Error on client to server socket" << std::endl;
                    return -1;
                }
                if (connect(cli_2_server_sd, result->ai_addr, result->ai_addrlen) == -1) {
                    std::cout << "Error on client to server connect" << std::endl;
                    return -1;
                }
                ip_sd=cli_2_server_sd;
                client_data* ip_client_data;
                ip_client_data=(client_data*)malloc(sizeof(struct client_data));
                memset(ip_client_data->http_request,  0, buffer_size);
                memset(ip_client_data->chunk_name,  0, 50);
                ip_client_data->server_ip=www_ip;
                ip_client_data->server_fd=client_sd;
                ip_client_data->is_server=1;
                ip_client_data->average_throughput=0.0;
                ip_client_data->first_f4m=1;
                ip_client_data->request_bitrate=0;

                client_data_fd* ip_client_data_fd;
                ip_client_data_fd=(client_data_fd*)malloc(sizeof(struct client_data_fd));
                ip_client_data_fd->cli_data=ip_client_data;
                ip_client_data_fd->fd=ip_sd;
                client_data* client_client_data;
                client_client_data=(client_data*)malloc(sizeof(struct client_data));
                memset(client_client_data->http_request,  0, buffer_size);
                memset(client_client_data->chunk_name,  0, 50);
                client_client_data->server_fd=ip_sd;
                client_client_data->is_server=0;
                client_client_data->server_pointer=ip_client_data;
                client_client_data->average_throughput=0.0;
                client_client_data->first_f4m=1;
                ip_client_data->request_bitrate=0;
                client_client_data->server_ip=client_client_data->server_pointer->server_ip;
                client_data_fd* client_client_data_fd;
                client_client_data_fd=(client_data_fd*)malloc(sizeof(struct client_data_fd));
                client_client_data_fd->cli_data=client_client_data;
                client_client_data_fd->fd=client_sd;
                ip_client_data->server_pointer=client_client_data;
                fds.push_back(ip_client_data_fd);
                fds.push_back(client_client_data_fd);
            }
        }
        for(int i = 0; i < (int) fds.size(); i++) {
            if (FD_ISSET(fds[i]->fd, &read_set)) {
                char buffer[buffer_size];
                bzero(buffer, buffer_size);
                int blen = buffer_size;
                int bytesRecvd = recv(fds[i]->fd, &buffer, blen, 0);
                if (bytesRecvd < 0) {
                    std::cout << "Error: Error recving bytes" << std::endl;
                    exit(1);
                } else if (bytesRecvd == 0) {
                    std::cout << "Connection closed" << std::endl;
                    fds.erase(fds.begin() + i);
                    i = i - 1;
                }
                cout << buffer << endl;
                if (!fds[i]->cli_data->is_server) {
                    start_client(fds[i], buffer, bytesRecvd);
                } else {
                    start_server(fds[i], buffer, bytesRecvd, alpha, path);

                }
            }
        }

    }

    return 0;
}