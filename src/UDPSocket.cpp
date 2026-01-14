#include "UDPSocket.h"
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>
#include <vector>

UDPSocket::UDPSocket(int port) {
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) throw std::runtime_error("Socket creation failed");

    memset(&my_addr, 0, sizeof(my_addr));
    my_addr.sin_family = AF_INET;
    my_addr.sin_addr.s_addr = INADDR_ANY;
    my_addr.sin_port = htons(port);

    if (bind(sockfd, (const struct sockaddr*)&my_addr, sizeof(my_addr)) < 0) {
        throw std::runtime_error("Bind failed on port " + std::to_string(port));
    }

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

UDPSocket::~UDPSocket() {
    close(sockfd);
}

void UDPSocket::sendTo(const std::string& msg, const std::string& ip, int port) {
    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);
    inet_aton(ip.c_str(), &dest_addr.sin_addr);

    sendto(sockfd, msg.c_str(), msg.length(), 0, (const struct sockaddr*)&dest_addr, sizeof(dest_addr));
}

std::string UDPSocket::recvFrom(int max_len) {
    std::vector<char> buffer(max_len);
    struct sockaddr_in client_address;
    socklen_t len = sizeof(client_address);

    int n = recvfrom(sockfd, buffer.data(), max_len, 0, (struct sockaddr*)&client_address, &len);
    
    if (n > 0) {
        return std::string(buffer.data(), n);
    }
    return "";
}