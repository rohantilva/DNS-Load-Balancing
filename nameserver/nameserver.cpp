#include <cstring>
#include <limits>
#include <map>
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
#include <arpa/inet.h>
#include "DNSHeader.h"
#include "DNSQuestion.h"
#include "DNSRecord.h"
#include "Structs.h"
#include <queue>
#include <list>
#define BACKLOG 10

using namespace std;

Response* send_dns_response(char * host, ushort id) {
    Response* ret = new Response();
    memset(ret, 0, sizeof(Response));
    ret->header.ID = id;
    ret->header.AA = 1;
    ret->rec.CLASS = 1;
    ret->rec.TYPE = 1;
    if (host != nullptr) {
        ret->header.RCODE = '0';
        strcpy(ret->rec.RDATA, host);
    } else {
        ret->header.RCODE = '3';
    }
    return ret;
}

string read_file_dijktras(string connected_ip, string textfile) {
    ifstream file;
    file.open(textfile);
    string line;
    int number_lines = 0;
    int nodes = 0;
    int links = 0;
    int found = 0;
    vector<pair<int, string>> all_servers;
    vector<vector<int>> all_links;
    if (file.is_open()) {
        while (getline(file, line)) {
            int line_len = line.length();
            number_lines++;
            if (number_lines == 1) {
                nodes = atoi(line.substr(11).c_str());
            } else if (line.find("NUM_LINKS: ") != string::npos) {
                links = atoi(line.substr(11).c_str());
            }
            if (number_lines >= nodes + 3) {
                vector<int> push_this;
                size_t fir = line.find(' ');
                push_this.push_back(atoi(line.substr(0, (int) fir).c_str()));
                size_t sec = line.substr((int) fir + 1, line_len - (int) fir + 1).find(' ');
                push_this.push_back(atoi(line.substr((int) fir + 1, (int) sec).c_str()));
                push_this.push_back(atoi(line.substr((int) sec + 2 + (int) fir, line_len - ((int) sec + (int) fir + 2)).c_str()));
                all_links.push_back(push_this);
            } else if (number_lines >= 2 && number_lines <= nodes + 1) {
                int spaces = 0;
                int host_id = 0;
                string push_ip = "";
                for (int i = 0; i < line_len; ++i) {
                    if (line[i] == ' ') {
                        spaces++;
                        if (spaces == 1) {
                            host_id = atoi(line.substr(0, i).c_str());
                        } else if(spaces == 2) {
                            push_ip = line.substr(i + 1, line_len - i);
                            if (line[i - 1] == 'T') {
                                if (push_ip == connected_ip) {
                                    found = host_id;
                                }
                            } else if (line[i -1] == 'R') {
                                all_servers.push_back(make_pair(host_id, push_ip));
                            }
                            break;
                        }
                    }
                }
            }
        }
    }
    string ret = "";
    list<pair<int, int>> *edges = new list<pair<int, int>>[nodes];
    priority_queue<pair<int, int>, vector<pair<int, int>>, greater<pair<int, int>>> q;
    q.push(make_pair(0, found));
    vector<int> lengths(nodes);
    for (int i = 0; i < lengths.size(); ++i) {
        if (i == found) {
            lengths[i] = 0;
        } else {
            lengths[i] = numeric_limits<int>::max();
        }
    }
    for (int i = 0; i != all_links.size(); ++i) {
        cout << all_links[i][0] << " " << all_links[i][1] << " " << all_links[i][2] << endl;
        edges[all_links[i][0]].push_back(make_pair(all_links[i][1], all_links[i][2]));
        edges[all_links[i][1]].push_back(make_pair(all_links[i][0], all_links[i][2]));
    }    
    while (!q.empty()) {
        int first_vertex = q.top().second;
        q.pop();
        for (list<pair<int, int>>::iterator iter = edges[first_vertex].begin(); iter != edges[first_vertex].end(); ++iter) {
            int second_vertex = (*iter).first;
            int dist = (*iter).second;
            if (dist + lengths[first_vertex] < lengths[second_vertex]) {
                lengths[second_vertex] = dist + lengths[first_vertex]; // relax the edge
                pair<int, int> put = make_pair(lengths[second_vertex], second_vertex);
                q.push(put);
            }
        }
    }
    priority_queue<pair<int, int>, vector<pair<int, int>>, greater<pair<int, int>>> new_guy;
    for (int i = 0; i < all_servers.size(); ++i) {
        new_guy.push(make_pair(lengths[all_servers[i].first], all_servers[i].first));
    }
    int look_for = new_guy.top().second;
    for (int k = 0; k < all_servers.size(); ++k) {
        if (all_servers[k].first == look_for) {
            ret = all_servers[k].second;
        }
    }
    return ret;
}

int main(int argc, char** argv) {
    if (argc != 5) {
        cerr << "Command line arguments are not correct." << endl;
        return 1;
    }
    string log(argv[1]);
    ofstream logfile;
    logfile.open(log);
    logfile << "";
    logfile.close();

    int port(atoi(argv[2]));
    int geo(atoi(argv[3]));
    string servers(argv[4]);
    int index = 0;
    ifstream file(servers);
    string curr_server;
    vector<string> all_ips;
    if (geo == 0) { // round robin
        while (getline(file, curr_server)) {
            all_ips.push_back(curr_server);
        }
    }

    int sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd < 0) {
        perror("socket error");
        return -1;
    }

    /* Create sockaddr */
    struct sockaddr_in proxy_addr;
    proxy_addr.sin_family = AF_INET;
    proxy_addr.sin_addr.s_addr = INADDR_ANY;
    proxy_addr.sin_port = htons((u_short) port);

   /* Binding */
    int ret = bind(sockfd, (const struct sockaddr *) &proxy_addr, sizeof(proxy_addr));
    if (ret < 0) {
        perror("bind error");
        close(sockfd);
        return -1;
    }

    /* Listening */
    ret = listen(sockfd, BACKLOG);
    if (ret < 0) {
        perror("listen error");
        close(sockfd);
        return -1;
    }

    ushort count;
    fd_set readSet;
    vector<string> connected_ips;
    vector<int> fds;

    while (true) {
        FD_ZERO(&readSet);
        FD_SET(sockfd, &readSet);
        for(int i = 0; i < (int) fds.size(); i++) {
            FD_SET(fds[i], &readSet);
        }

        int maxfd = 0;
        if(fds.size() > 0) {
            maxfd = *max_element(fds.begin(), fds.end());
        }
        maxfd = max(maxfd, sockfd);
        int err = select(maxfd + 1, &readSet, NULL, NULL, NULL);

        if(FD_ISSET(sockfd, &readSet)) {
            struct sockaddr_in connected_address;
            socklen_t sze = sizeof(connected_address);
            int clientsd = accept(sockfd, (struct sockaddr *) &connected_address, &sze);
            if(clientsd == -1) {
                printf("%s %d\n", __FUNCTION__, __LINE__);
                cout << "Error on accept" << endl;
            } else {
                char temp[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, (struct sockaddr *) &connected_address.sin_addr.s_addr, temp, sizeof temp);
                connected_ips.push_back(string(temp));
                fds.push_back(clientsd);
                printf("client: %d\n", clientsd);
            }
        }
    
        for(int i = 0; i < (int) fds.size(); ++i) {
            if(FD_ISSET(fds[i], &readSet)) {
                DNSQuery query_dns;
                int bytes_recv = recv(fds[i], &query_dns, sizeof(DNSQuery), 0);
                count = query_dns.header.ID;
                if (bytes_recv == 0) {
                    cout << "ending here" << endl;
                    fds.erase(fds.begin() + i);
                    connected_ips.erase(connected_ips.begin() + i);           
                    break;
                } else if (bytes_recv < 0) {
                    cout << "Error receiving bytes" << endl;
                    exit(1);
                }
                int yes = string(query_dns.question.QNAME).compare("video.cs.jhu.edu");                                
                char* temp = nullptr;
                if (yes == 0) {    // if got video.cs.jhu.edu                       
                    if (geo == 0) { //if round robin
                        temp = (char *) all_ips[index].c_str();
                        index++;
                        index = index % all_ips.size();
                        logfile.open(log, ios_base::app);
                        logfile << connected_ips[i] << " " << query_dns.question.QNAME << " " << temp << "\n";
                        logfile.close();
                    } else { // if dijstras
                        //read text file, call Dijktra's
                        string te = read_file_dijktras(connected_ips[i], servers).c_str();
                        logfile.open(log, ios_base::app);
                        temp = (char *) te.c_str();
                        cout << temp << endl;
                        logfile << connected_ips[i] << " " << query_dns.question.QNAME << " " << temp << "\n";
                        logfile.close();
                    }
                }
                Response* use = send_dns_response(temp, count); 
                int sent = send(fds[i], use, sizeof(Response), 0);
                if (sent <= 0) {
                    cout << "DNS response not sent properly" << endl;
                    exit(1);   
                }                                    
            }
        }
    }
    return 0;
}
