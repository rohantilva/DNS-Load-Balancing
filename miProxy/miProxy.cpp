#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>
#include "DNSHeader.h"
#include "DNSQuestion.h"
#include "DNSRecord.h"
#include "Structs.h"
#include <arpa/inet.h>
#define BACKLOG 10

using namespace std;

bool check_video_data(string str);
string get_value(string str, string key);
string recv_response(int server_sd);
string get_chunkname(string request);

DNSQuery* send_dns_query(string host, ushort id) {
    DNSQuery* ret = new DNSQuery();
    memset(ret, 0, sizeof(DNSQuery));
    ret->question.QTYPE = 1; // QTYPe, qclass
    ret->question.QCLASS = 1;
    ret->header.ID = id;
    strcpy(ret->question.QNAME, (const char *) host.c_str());
    return ret;
}

int main(int argc, char const *argv[]) {
    if (argc != 6 && argc != 7) {
        perror("./miProxy <log> <alpha> <listen-port> <dns-ip> <dns-port> [<www-ip>]");
        return -1;
    }

    /* Parse cmd arguments */
    int dns_sd;
    ushort dns_id = 0;
    char * log_path = (char *) argv[1];
    float alpha = atof(argv[2]);
    int listen_port = atoi(argv[3]);
    char * dns_ip = (char *) argv[4];
    char * dns_port = (char *) argv[5];
    char * www_ip;
    if (argc == 7) {
        www_ip = (char *) argv[6];
    } else {
        www_ip = (char *) "video.cs.jhu.edu";
    }

    int new_www_ip = 0;

    /* Create a socket */
    int sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd < 0) {
        printf("%s %d\n", __FUNCTION__, __LINE__);
        perror("socket");
        return -1;
    }

    /* Create sockaddr */
    struct sockaddr_in proxy_addr;
    proxy_addr.sin_family = AF_INET;
    proxy_addr.sin_addr.s_addr = INADDR_ANY;
    proxy_addr.sin_port = htons(listen_port);

    /* Binding */
    int ret = bind(sockfd, (const struct sockaddr *) &proxy_addr, sizeof(proxy_addr));
    if (ret < 0) {
        printf("%s %d\n", __FUNCTION__, __LINE__);
        close(sockfd);
        return -1;
    }

    /* Listening */
    ret = listen(sockfd, BACKLOG);
    if (ret < 0) {
        printf("%s %d\n", __FUNCTION__, __LINE__);
        close(sockfd);
        return -1;
    }
    if (argc == 6) { // do this if we need to get ip from servers.txt
        int _sockfd;  // listen on sock_fd, new connection on new_fd
        struct addrinfo _hints, *_servinfo, *_p;
        int _rv; 
        memset(&_hints, 0, sizeof _hints);
        _hints.ai_family = AF_UNSPEC;
        _hints.ai_socktype = SOCK_STREAM;
        _hints.ai_flags = AI_PASSIVE; // use my IP
        if ((_rv = getaddrinfo((char *) dns_ip, (char *) dns_port, &_hints, &_servinfo)) != 0) {
            fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(_rv));
            return 1;
        }

        for(_p = _servinfo; _p != nullptr; _p = _p->ai_next) {
            if ((_sockfd = socket(_p->ai_family, _p->ai_socktype,
                    _p->ai_protocol)) == -1) {
                perror("client: socket");
                continue;
            }

            if (connect(_sockfd, _p->ai_addr, _p->ai_addrlen) == -1) {
                close(_sockfd);
                perror("client: connect 1");
                continue;
            }
            break;
        }
        dns_sd = _sockfd;
    } else {
        int _sockfd;  // listen on sock_fd, new connection on new_fd
        struct addrinfo _hints, *_servinfo, *_p;
        int _rv; 
        memset(&_hints, 0, sizeof _hints);
        _hints.ai_family = AF_UNSPEC;
        _hints.ai_socktype = SOCK_STREAM;
        _hints.ai_flags = AI_PASSIVE; // use my IP

        int numbytes;  
        if ((_rv = getaddrinfo((char *) www_ip, (char *) "80", &_hints, &_servinfo)) != 0) {
            fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(_rv));
            return 1;
        }

        for(_p = _servinfo; _p != nullptr; _p = _p->ai_next) {
            if ((_sockfd = socket(_p->ai_family, _p->ai_socktype,
                    _p->ai_protocol)) == -1) {
                perror("client: socket");
                continue;
            }

            if (connect(_sockfd, _p->ai_addr, _p->ai_addrlen) == -1) {
                close(_sockfd);
                perror("client: connect 2");
                continue;
            }
            break;
        }
        new_www_ip = _sockfd;
    }
    // Set of file descriptors to listen to
    fd_set readSet;
    // Keep track of each file descriptor accepted
    vector<int> fds;
    vector<int> corr_dns;
    vector<string> corr_string_ips;
    // Data structure for storing bitrates
    vector<int> bitrate_vector;
    // Data structure for mapping with fd and throughput
    unordered_map<int, double> throughput_map;
    //ofstream to write log info into log_path file
    ofstream log_out(log_path, ofstream::out);

    while (true) {
        // Set up the readSet
        FD_ZERO(&readSet);
        FD_SET(sockfd, &readSet);
        for(int i = 0; i < (int) fds.size(); ++i) {
            FD_SET(fds[i], &readSet);
        }

        int maxfd = 0;
        if(fds.size() > 0) {
            maxfd = *max_element(fds.begin(), fds.end());
        }
        maxfd = max(maxfd, sockfd);
        cout << "new sockfd: " << sockfd << endl;
        // maxfd + 1 is important
        int serv_sd;
        int err = select(maxfd + 1, &readSet, NULL, NULL, NULL);
        assert(err != -1);
        

        if(FD_ISSET(sockfd, &readSet)) {
            int clientsd = accept(sockfd, NULL, NULL);
            if(clientsd == -1) {
                printf("%s %d\n", __FUNCTION__, __LINE__);
                cout << "Error on accept" << endl;
            } else {
                string use_this_ip;
                fds.push_back(clientsd);
                if (argc == 6) {
                    DNSQuery* dns_q = send_dns_query("video.cs.jhu.edu", dns_id);                    
                    int bytes = send(dns_sd, dns_q, sizeof(DNSQuery), 0);
                    if (bytes < 1) {
                        cout << "Bytes not sent" << endl;
                    }
                    Response* res = (Response*) malloc(sizeof(Response));
                    int recvd = recv(dns_sd, res, sizeof(Response), 0);
                    if (recvd < 1) {
                        cout << "check recv() call" << endl;
                        exit(1);
                    }
                    if (res->header.RCODE == '3') {
                        cout << "Hostname not there" << endl;
                        exit(1);
                    } else {
                        use_this_ip = string(res->rec.RDATA);
                    }

                    corr_string_ips.push_back(use_this_ip);
/*                    serv_sd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
                    if (serv_sd == -1) {
                        cout << "check socket() call" << endl;
                        exit(1);
                    }
                    struct sockaddr_in serv_this;
                    memset(&serv_this, 0, sizeof(serv_this));
                    serv_this.sin_family = AF_INET;
                    serv_this.sin_port = htons((u_short) 80);
                    serv_this.sin_addr.s_addr = inet_addr(use_this_ip.c_str());
                    cout << use_this_ip.c_str() << endl;
                    int yo = connect(serv_sd, (sockaddr*) &serv_this, sizeof(serv_this));
 */


        struct addrinfo _hints, *_servinfo, *_p;
        int _rv; 
        memset(&_hints, 0, sizeof _hints);
        _hints.ai_family = AF_UNSPEC;
        _hints.ai_socktype = SOCK_STREAM;
        _hints.ai_flags = AI_PASSIVE; // use my IP
        int yo = -10;
        cout << "first" << endl;
        if ((_rv = getaddrinfo((char *) use_this_ip.c_str(), (char *) "80", &_hints, &_servinfo)) != 0) {
            fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(_rv));
            return 1;
        }
        cout << "second" << endl;
        for(_p = _servinfo; _p != nullptr; _p = _p->ai_next) {
            if ((serv_sd = socket(_p->ai_family, _p->ai_socktype,
                    _p->ai_protocol)) == -1) {
                perror("client: socket");
                continue;
            }

            if (yo = connect(serv_sd, _p->ai_addr, _p->ai_addrlen) == -1) {
                close(serv_sd);
                perror("client: connect 1");
                continue;
            }
            break;
        }
        cout << "third" << endl;
                    if (yo == -1) {
                        cout << "Connect() bad" << endl;
                        exit(1);
                    } else {
                        corr_dns.push_back(serv_sd);
                    }
                }
            }
        }
        for(int i = 0; i < (int) fds.size(); ++i) {
            if(FD_ISSET(fds[i], &readSet)) {
                char buf;
                string request;
                bool closed = false;
                struct timeval t1, t2;
                gettimeofday(&t1, NULL);
                int cur_fd = fds[i];
                //int temp = corr_dns[i];
                /* Complete receiving the request from clients */
                while (1) {
                    int bytesRecvd = recv(cur_fd, &buf, 1, 0);

                    if(bytesRecvd < 0) {
                        printf("%s %d\n", __FUNCTION__, __LINE__);
                        cout << "Error recving bytes" << endl;
                        cout << strerror(errno) << endl;
                        exit(1);
                    } else if(bytesRecvd == 0) {
                        printf("client: %d, fds i = %d\n", cur_fd, i);
                        perror("Connection closed");
                        if (fds.size() != 0)
                            fds.erase(fds.begin() + i);
                        //close(temp);
                        if (corr_dns.size() != 0)
                            corr_dns.erase(corr_dns.begin() + i);
                        if (throughput_map.find(cur_fd) != throughput_map.end())
                            throughput_map.erase(cur_fd);
                        closed = true;
                        break;
                    }

                    request += buf;
                    if (request.size() >= 4) {
                        if (request.substr(request.size() - 4) == "\r\n\r\n")
                            break;
                    }
                }

                /* Client Keep alive */
                if (closed == false) {
                    bool is_video_data = check_video_data(request);
                    int cur_bitrate;
                    string chunk_name;

                    /* Client is requesting video data,
                       modify the request for bitrate adaption */
                    if (is_video_data) {
                        chunk_name = get_chunkname(request);
                        int seg_index = chunk_name.find("Seg");
                        int bitrate = atoi(chunk_name.substr(0, seg_index).c_str());
                        cur_bitrate = bitrate;

                        if (throughput_map.find(cur_fd) != throughput_map.end()) {
                            int chosen_bitrate;
                            int last_bitrate = bitrate_vector.front();

                            for(int b : bitrate_vector) {
                                if (b <= (throughput_map[cur_fd] / 1.5))
                                    chosen_bitrate = b;
                                else {
                                    chosen_bitrate = last_bitrate;
                                    break;
                                }
                                last_bitrate = b;
                            }
                            cur_bitrate = chosen_bitrate;
                            if (cur_bitrate != bitrate) {
                                string new_name = to_string(cur_bitrate) + chunk_name.substr(seg_index);
                                int get_index = request.find("GET");
                                int end_index = request.find(' ', get_index + 4);
                                int vod_index = request.rfind("vod", end_index);
                                request.replace(vod_index + 4, end_index - vod_index - 4, new_name);
                                chunk_name = new_name;
                            }
                        }
                    }
                    int server_sd;
                    if (argc == 7) {
                        server_sd = new_www_ip;
                    } else {
                        server_sd = corr_dns[i];
                    }
                    /* Send */
                    if (send(server_sd, request.c_str(), request.size(), 0) < 0) {
                        printf("%s %d\n", __FUNCTION__, __LINE__);
                        perror("send");
                        close(sockfd);
                        close(server_sd);
                        return -1;
                    }
                
                    /* Receive from server */
                    string response = recv_response(server_sd);
                    string content_type = get_value(response, "Content-Type");
                    int content_len = atoi(get_value(response, "Content-Length").c_str());
                    for (int i = 0; i < content_len; i++) {
                        if (recv(server_sd, &buf, 1, 0) < 0) {
                            printf("%s %d\n", __FUNCTION__, __LINE__);
                            perror("recv");
                            close(sockfd);
                            close(server_sd);
                            return -1;
                        }
                        response += buf;
                    }

                    /* Getting video xml data and parse available bitrates */
                    if (content_type.find("text/xml") != -1) {
                        int index = 0;
                        int cur = response.find("bitrate", index);
                        while (cur != -1) {
                            int fst_index = response.find('\"', cur);
                            int snd_index = response.find('\"', fst_index);
                            int br = atoi(response.substr(fst_index + 1, snd_index - fst_index - 1).c_str());
                            bool new_br = true;
                            for (int b : bitrate_vector) {
                                if (br == b) {
                                    new_br = false;
                                    break;
                                }
                                new_br = true;
                            }

                            if (new_br)
                                bitrate_vector.push_back(br);
                            sort(bitrate_vector.begin(), bitrate_vector.end());
                            index = snd_index;
                            cur = response.find("bitrate", index);
                        }

                        /* Request nolist.f4m*/
                        string request_cp = request;
                        request_cp.replace(request.find(".f4m"), 4, "_nolist.f4m");

                        if(send(server_sd, request_cp.c_str(), request_cp.size(), 0) < 0) {
                            printf("%s %d\n", __FUNCTION__, __LINE__);
                            perror("send");
                            close(sockfd);
                            close(server_sd);
                            return -1;
                        }

                        response = recv_response(server_sd);
                        content_len = atoi(get_value(response, "Content-Length").c_str());
                        for (int i = 0; i < content_len; i++) {
                            if (recv(server_sd, &buf, 1, 0) < 0) {
                                printf("%s %d\n", __FUNCTION__, __LINE__);
                                perror("recv");
                                close(sockfd);
                                close(server_sd);
                                return -1;
                            }
                            response += buf;
                        }
                    }
                    /* Getting video chunk, calculate the throughput and log out info*/
                    else if (content_type.find("video/f4f") != -1) {
                        gettimeofday(&t2, NULL);
                        double duration = t2.tv_sec-t1.tv_sec +(t2.tv_usec-t1.tv_usec)/1000000.0;
                        double t_new = (content_len / 1000) / duration * 8;
                        double t_cur;
                        if (throughput_map.find(cur_fd) == throughput_map.end())
                            t_cur = bitrate_vector.front();
                        else
                            t_cur = throughput_map[cur_fd];

                        t_cur = alpha * t_new + (1 - alpha) * t_cur;
                        throughput_map[cur_fd] = t_cur;

                        log_out << duration << " " << t_new << " " << t_cur << " " << cur_bitrate << " " << string(www_ip) << " " << chunk_name << endl;
                    }

                    /* Send response to client */
                    if (send(cur_fd, response.c_str(), response.size(), 0) < 0) {
                        printf("%s %d\n", __FUNCTION__, __LINE__);
                        perror("send");
                        close(sockfd);
                        close(server_sd);
                        return -1;
                    }
                }
            }
        }
    }
    close(sockfd);
    return 0;
}

/* Extract the video chunk name from request */
string get_chunkname(string request) {
    int get_index = request.find("GET");
    int end_index = request.find(' ', get_index + 4);
    int vod_index = request.rfind("vod", end_index);
    string fn = request.substr(vod_index + 4, end_index - vod_index - 4);
    //printf("%s\n", fn.c_str());
    return fn;
}

/* Check if the str is the video data format */
bool check_video_data(string str) {
    if(str.find("Seg") == -1) return false;
    if(str.find("Frag") == -1) return false;
    if(str.find("Frag") > str.find("Seg")) return true;
    return false;
}

/* Get the value from str based on the key */
string get_value(string str, string key) {
    int key_index = str.find(key);
    int space_index = str.find(' ', key_index);
    int end = str.find('\n', key_index);
    return str.substr(space_index + 1, end - space_index - 1);
}

/* Recerive the response from the server */
string recv_response(int server_sd){
    string data = "";
    char buf;
    while(1) {
        int bytesRecvd = recv(server_sd, &buf, 1, 0);

        if (bytesRecvd < 0)
        {
            printf("%s %d\n", __FUNCTION__, __LINE__);
            perror("recv");
            exit(-1);
        }

        data += buf;
        if (data.size() >= 4) {
            if(data.substr(data.size() - 4) == "\r\n\r\n")
                break;
        }
    }
    return data;
}
