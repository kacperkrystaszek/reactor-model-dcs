#pragma once
#include <string>
#include <arpa/inet.h>

class UDPSocket {
    private:
        int sockfd;
        struct sockaddr_in my_addr;

    public:
        UDPSocket(int port);
        ~UDPSocket();

        void sendTo(const std::string& msg, const std::string& ip, int port);
        void sendTo(const char* msg, size_t len, const std::string& ip, int port);
        std::string recvFrom(int max_len = 1024);
        int recvFrom(char* buffer, int max_len);
};