// UDP pseudo-accept server, Windows version using select()
// Compile with: cl /EHsc udp_accept_windows.c ws2_32.lib

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdint.h>

#pragma comment(lib, "Ws2_32.lib")

#define MAXBUF        10240
#define MAX_CLIENTS   FD_SETSIZE
#define SERVER_PORT   "1234"

// Initialize Winsock
void init_winsock() {
    WSADATA wsaData;
    int ret = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (ret != 0) {
        fprintf(stderr, "WSAStartup failed: %d\n", ret);
        exit(1);
    }
}

// Clean up Winsock
void cleanup_winsock() {
    WSACleanup();
}

// Read data and print
void read_data(SOCKET s) {
    char buf[MAXBUF + 1];
    struct sockaddr_in peer;
    int peer_len = sizeof(peer);
    int ret = recvfrom(s, buf, MAXBUF, 0, (struct sockaddr*)&peer, &peer_len);
    if (ret > 0) {
        buf[ret] = '\0';
        // Convert peer address to string
        char ipstr[INET_ADDRSTRLEN];
        InetNtopA(AF_INET, &peer.sin_addr, ipstr, INET_ADDRSTRLEN);

        printf("recv[%d] from %s:%d -> %s\n",
            ret,
            ipstr,
            ntohs(peer.sin_port),
            buf);
    }
    else {
        printf("recv error: %d\n", WSAGetLastError());
    }
}

int main() {
    init_winsock();

    struct addrinfo hints, * res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, SERVER_PORT, &hints, &res) != 0) {
        fprintf(stderr, "getaddrinfo failed: %d\n", WSAGetLastError());
        cleanup_winsock();
        return 1;
    }

    SOCKET listener = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (listener == INVALID_SOCKET) {
        fprintf(stderr, "socket failed: %d\n", WSAGetLastError());
        freeaddrinfo(res);
        cleanup_winsock();
        return 1;
    }

    // Allow address reuse
    BOOL opt = TRUE;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    if (bind(listener, res->ai_addr, (int)res->ai_addrlen) == SOCKET_ERROR) {
        fprintf(stderr, "bind failed: %d\n", WSAGetLastError());
        closesocket(listener);
        freeaddrinfo(res);
        cleanup_winsock();
        return 1;
    }
    freeaddrinfo(res);

    printf("UDP server listening on port %s...\n", SERVER_PORT);

    // Use select() to multiplex
    fd_set master_set, read_set;
    SOCKET clients[MAX_CLIENTS];
    int client_count = 0;

    FD_ZERO(&master_set);
    FD_SET(listener, &master_set);

    while (1) {
        read_set = master_set;
        int nfds = select(0, &read_set, NULL, NULL, NULL);
        if (nfds == SOCKET_ERROR) {
            fprintf(stderr, "select failed: %d\n", WSAGetLastError());
            break;
        }

        // Listener ready: accept pseudo-connection
        if (FD_ISSET(listener, &read_set)) {
            struct sockaddr_in peer;
            int peer_len = sizeof(peer);
            char tmp[1];
            int ret = recvfrom(listener, tmp, 1, 0,
                (struct sockaddr*)&peer, &peer_len);
            if (ret > 0) {
                // Create new "connected" UDP socket for this client
                SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
                if (s == INVALID_SOCKET) {
                    fprintf(stderr, "client socket failed: %d\n", WSAGetLastError());
                }
                else {
                    // Bind to same local address/port
                    struct sockaddr_in local;
                    int local_len = sizeof(local);
                    getsockname(listener, (struct sockaddr*)&local, &local_len);
                    bind(s, (struct sockaddr*)&local, local_len);

                    // Connect to client
                    connect(s, (struct sockaddr*)&peer, peer_len);

                    // Add to read set
                    FD_SET(s, &master_set);
                    clients[client_count++] = s;

                    // Convert peer address to string
                    char ipstr[INET_ADDRSTRLEN];
                    InetNtopA(AF_INET, &peer.sin_addr, ipstr, INET_ADDRSTRLEN);

                    printf("New pseudo-connection: sock=%llu client=%s:%d\n",
                        (unsigned long long)s,
                        ipstr,
                        ntohs(peer.sin_port));
                }
            }
        }

        // Check client sockets
        for (int i = 0; i < client_count; ++i) {
            SOCKET s = clients[i];
            if (FD_ISSET(s, &read_set)) {
                read_data(s);
            }
        }
    }

    // Cleanup
    for (int i = 0; i < client_count; ++i) {
        closesocket(clients[i]);
    }
    closesocket(listener);
    cleanup_winsock();
    return 0;
}
