#ifndef TCP_SOCKET_H
#define TCP_SOCKET_H

#include <cstdint>
#include <string>

class TcpSocket {
public:
    TcpSocket();
    ~TcpSocket();

    // Connect to server.
    // Returns true on success.
    bool connect_to(const std::string& ip, uint16_t port);

    // Send exactly 'len' bytes.
    // Returns true on success.
    bool send_bytes(const uint8_t* data, int len);

    // Receive exactly 'len' bytes (blocking).
    // Returns true on success, false on disconnect/error.
    bool recv_exact(uint8_t* buffer, int len);

    // Set SO_RCVBUF size.
    bool set_receive_buffer(int bytes);

    // Set TCP_NODELAY (disable Nagle).
    bool set_nodelay(bool enable);

    // Get underlying fd for poll/select.
    int get_fd() const;

    void close();

private:
    int fd;
};

#endif
