#include "application.h"
#include "config.h"
#include "tcp_socket.h"
#include "decoder.h"
#include "soupbintcp.h"

#include <cstdio>
#include <cstdint>
#include <string>
#include <cstring>
#include <cerrno>
#include <poll.h>
#include <ctime>

Application::Application()
: max_messages(0),
  verbose(false),
  has_type_filter(false),
  has_start_seq(false),
  start_seq(0) {
    std::memset(type_allowed, 0, sizeof(type_allowed));
}

void Application::set_max_messages(uint64_t value) {
    max_messages = value;
}

void Application::set_verbose(bool value) {
    verbose = value;
}

void Application::set_type_filter(char type) {
    has_type_filter = true;
    type_allowed[(unsigned char)type] = true;
}

void Application::set_start_seq(uint64_t value) {
    has_start_seq = true;
    start_seq = value;
}

// -----------------------------------------------
// SoupBinTCP packet read/write helpers
// -----------------------------------------------

// Read big-endian u16
static uint16_t read_u16_be(const uint8_t* p) {
    return (uint16_t)((uint16_t)p[0] << 8 | (uint16_t)p[1]);
}

// Write big-endian u16
static void write_u16_be(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
}

// Format a uint64 as ASCII, right-justified,
// space-padded into a fixed-width field.
static void format_ascii_u64(char* dst, int width, uint64_t value) {
    // Fill with spaces
    std::memset(dst, ' ', (size_t)width);

    // Write digits right-to-left
    char tmp[32];
    int len = std::snprintf(tmp, sizeof(tmp), "%llu", (unsigned long long)value);

    if (len > width) {
        len = width;
    }

    std::memcpy(dst + (width - len), tmp, (size_t)len);
}

// Parse ASCII uint64 from a fixed-width
// space-padded field.
static uint64_t parse_ascii_u64(const char* src, int width) {
    // Skip leading spaces
    int start = 0;
    while (start < width && src[start] == ' ') {
        start++;
    }

    uint64_t val = 0;
    for (int i = start; i < width; i++) {
        if (src[i] < '0' || src[i] > '9') {
            break;
        }
        val = val * 10 + (uint64_t)(src[i] - '0');
    }

    return val;
}

// Copy string into fixed-width field
// left-justified, space-padded.
static void copy_padded(char* dst, int width, const std::string& src) {
    std::memset(dst, ' ', (size_t)width);
    size_t len = src.size();
    if (len > (size_t)width) {
        len = (size_t)width;
    }
    if (len > 0) {
        std::memcpy(dst, src.data(), len);
    }
}

// -----------------------------------------------
// Send SoupBinTCP packets
// -----------------------------------------------

static bool send_login_request(TcpSocket& sock, const AppConfig& cfg,
                               uint64_t seq_override, bool use_override) {

    // Packet: 2-byte length + 1-byte type + 46-byte payload = 49 total on wire
    const int payload_len = 1 + LOGIN_REQUEST_PAYLOAD_LEN;
    uint8_t packet[2 + 1 + LOGIN_REQUEST_PAYLOAD_LEN];

    write_u16_be(packet, (uint16_t)payload_len);
    packet[2] = (uint8_t)SOUP_PKT_LOGIN_REQUEST;

    LoginRequestPayload* login = (LoginRequestPayload*)(packet + 3);
    copy_padded(login->username, 6, cfg.username);
    copy_padded(login->password, 10, cfg.password);
    copy_padded(login->requested_session, 10, cfg.requested_session);

    uint64_t req_seq = cfg.requested_sequence;
    if (use_override) {
        req_seq = seq_override;
    }

    // Sequence = 0 means "next available"
    // Send as ASCII number for all values
    // (some servers reject space-filled fields)
    format_ascii_u64(login->requested_sequence, 20, req_seq);

    return sock.send_bytes(packet, (int)sizeof(packet));
}

static bool send_client_heartbeat(TcpSocket& sock) {
    // 2-byte length (=1) + 1-byte type
    uint8_t packet[3];
    write_u16_be(packet, 1);
    packet[2] = (uint8_t)SOUP_PKT_CLIENT_HEARTBEAT;
    return sock.send_bytes(packet, 3);
}

static bool send_logout_request(TcpSocket& sock) {
    uint8_t packet[3];
    write_u16_be(packet, 1);
    packet[2] = (uint8_t)SOUP_PKT_LOGOUT_REQUEST;
    return sock.send_bytes(packet, 3);
}

// -----------------------------------------------
// Main run loop
// -----------------------------------------------

int Application::run() {
    const char* config_path = "config/config.ini";
    if (!load_config(config_path)) {
        std::printf("Failed to load config: %s\n", config_path);
        return 1;
    }

    const AppConfig& cfg = config();
    if (verbose) {
        std::printf("verbose on\n");
    }

    // Connect TCP
    TcpSocket sock;
    std::printf("Connecting to %s:%u ...\n", cfg.server_ip.c_str(), (unsigned)cfg.server_port);

    if (!sock.connect_to(cfg.server_ip, cfg.server_port)) {
        std::printf("Failed to connect to %s:%u errno=%d\n",
                    cfg.server_ip.c_str(), (unsigned)cfg.server_port, errno);
        return 1;
    }

    sock.set_receive_buffer(4 * 1024 * 1024);
    sock.set_nodelay(true);

    std::printf("Connected\n");

    // Send Login Request
    // -s overrides requested_sequence from config
    uint64_t login_seq = cfg.requested_sequence;
    if (has_start_seq) {
        login_seq = start_seq;
    }

    if (!send_login_request(sock, cfg, login_seq, has_start_seq)) {
        std::printf("Failed to send Login Request\n");
        sock.close();
        return 1;
    }

    std::printf("Login Request sent (seq=%llu)\n", (unsigned long long)login_seq);

    // Session and sequence tracking
    // (populated by Login Accepted)
    std::string session;
    uint64_t current_seq = 0;
    uint64_t decoded_count = 0;

    // Read Login Response
    // First packet from server must be
    // Login Accepted or Login Rejected
    {
        uint8_t hdr[3];
        if (!sock.recv_exact(hdr, 3)) {
            std::printf("Failed to read Login Response header\n");
            sock.close();
            return 1;
        }

        uint16_t pkt_len = read_u16_be(hdr);
        char pkt_type = (char)hdr[2];
        uint16_t payload_len = pkt_len - 1;

        if (pkt_type == SOUP_PKT_LOGIN_ACCEPTED) {
            if (payload_len < LOGIN_ACCEPTED_PAYLOAD_LEN) {
                std::printf("Login Accepted payload too short: %u\n", (unsigned)payload_len);
                sock.close();
                return 1;
            }

            uint8_t payload[LOGIN_ACCEPTED_PAYLOAD_LEN];
            if (!sock.recv_exact(payload, LOGIN_ACCEPTED_PAYLOAD_LEN)) {
                std::printf("Failed to read Login Accepted payload\n");
                sock.close();
                return 1;
            }

            // Drain any extra bytes
            if (payload_len > LOGIN_ACCEPTED_PAYLOAD_LEN) {
                uint8_t discard[256];
                int extra = payload_len - LOGIN_ACCEPTED_PAYLOAD_LEN;
                while (extra > 0) {
                    int chunk = extra > (int)sizeof(discard) ? (int)sizeof(discard) : extra;
                    if (!sock.recv_exact(discard, chunk)) break;
                    extra -= chunk;
                }
            }

            LoginAcceptedPayload* accepted = (LoginAcceptedPayload*)payload;
            session.assign(accepted->session, 10);
            current_seq = parse_ascii_u64(accepted->sequence_number, 20);

            std::printf(">> LOGIN_ACCEPTED Session='%s' NextSequence=%llu\n",
                        session.c_str(), (unsigned long long)current_seq);
        }
        else if (pkt_type == SOUP_PKT_LOGIN_REJECTED) {
            uint8_t reason = 0;
            if (payload_len >= 1) {
                sock.recv_exact(&reason, 1);
            }

            // Drain any extra
            if (payload_len > 1) {
                uint8_t discard[256];
                int extra = payload_len - 1;
                while (extra > 0) {
                    int chunk = extra > (int)sizeof(discard) ? (int)sizeof(discard) : extra;
                    if (!sock.recv_exact(discard, chunk)) break;
                    extra -= chunk;
                }
            }

            const char* reason_str = "Unknown";
            if ((char)reason == 'A') reason_str = "Not Authorized";
            if ((char)reason == 'S') reason_str = "Session Not Available";

            std::printf(">> LOGIN_REJECTED Reason='%c' (%s)\n", (char)reason, reason_str);
            sock.close();
            return 1;
        }
        else {
            std::printf("Unexpected packet type after login: '%c' (0x%02X)\n",
                        pkt_type, (unsigned)(uint8_t)pkt_type);
            sock.close();
            return 1;
        }
    }

    // Main receive loop
    // Use poll() for heartbeat timing

    // Heartbeat tracking
    int heartbeat_ms = cfg.heartbeat_interval_sec * 1000;
    if (heartbeat_ms <= 0) {
        heartbeat_ms = 15000;
    }

    time_t last_send_time = std::time(0);
    time_t last_recv_time = std::time(0);

    // Timeout: if no server data for
    // 2x heartbeat interval, assume dead
    int server_timeout_ms = heartbeat_ms * 2;

    // Max payload buffer (64KB is generous for ITCH)
    const int max_payload = 64 * 1024;
    uint8_t payload_buf[64 * 1024];

    struct pollfd pfd;
    pfd.fd = sock.get_fd();
    pfd.events = POLLIN;

    std::printf("Listening... (Ctrl+C to stop)\n");

    while (1) {
        // Poll with heartbeat interval
        // to send client heartbeat on idle
        int poll_timeout = heartbeat_ms;
        int ret = ::poll(&pfd, 1, poll_timeout);

        time_t now = std::time(0);

        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::printf("poll() error errno=%d\n", errno);
            break;
        }

        // Timeout: send client heartbeat
        if (ret == 0) {
            if (!send_client_heartbeat(sock)) {
                std::printf("Failed to send Client Heartbeat\n");
                break;
            }
            last_send_time = now;

            // Check server timeout
            if ((now - last_recv_time) > (server_timeout_ms / 1000)) {
                std::printf(">> SERVER TIMEOUT: no data for %d seconds\n",
                            (int)(now - last_recv_time));
                break;
            }

            continue;
        }

        // Data available - read packet header
        // (2-byte length + 1-byte type)
        uint8_t hdr[3];
        if (!sock.recv_exact(hdr, 3)) {
            std::printf(">> DISCONNECTED\n");
            break;
        }

        last_recv_time = now;

        uint16_t pkt_len = read_u16_be(hdr);
        char pkt_type = (char)hdr[2];
        int payload_len = (pkt_len > 1) ? (int)(pkt_len - 1) : 0;

        switch (pkt_type) {

        case SOUP_PKT_SEQUENCED_DATA: {
            // payload = ITCH message bytes
            if (payload_len == 0) {
                // Empty sequenced data (keep-alive with seq increment)
                current_seq++;
                break;
            }

            if (payload_len > max_payload) {
                std::printf(">> ERROR: Sequenced Data too large: %u bytes\n",
                            (unsigned)payload_len);
                // Drain and skip
                int remaining = payload_len;
                while (remaining > 0) {
                    int chunk = remaining > max_payload ? max_payload : remaining;
                    if (!sock.recv_exact(payload_buf, chunk)) goto disconnect;
                    remaining -= chunk;
                }
                current_seq++;
                break;
            }

            if (!sock.recv_exact(payload_buf, payload_len)) {
                goto disconnect;
            }

            // Track sequence
            // SoupBinTCP: server assigns sequential
            // sequence numbers starting from Login Accepted seq.
            current_seq++;

            // Type filter
            if (payload_len > 0) {
                char msg_type = (char)payload_buf[0];
                bool allow_print = true;

                if (has_type_filter) {
                    allow_print = type_allowed[(unsigned char)msg_type];
                }

                if (allow_print) {
                    decode_itch_message(payload_buf, payload_len, cfg,
                                        session, current_seq, verbose);
                }
            }

            decoded_count++;

            // Stop after N messages
            if (max_messages != 0 && decoded_count >= max_messages) {
                std::printf(">> STOP: Total Decoded=%llu\n", (unsigned long long)decoded_count);
                send_logout_request(sock);
                sock.close();
                return 0;
            }

            break;
        }

        case SOUP_PKT_SERVER_HEARTBEAT: {
            // No payload, just ack
            // Drain payload if any (shouldn't be)
            if (payload_len > 0) {
                int remaining = payload_len;
                while (remaining > 0) {
                    int chunk = remaining > max_payload ? max_payload : remaining;
                    if (!sock.recv_exact(payload_buf, chunk)) goto disconnect;
                    remaining -= chunk;
                }
            }

            if (verbose) {
                std::printf(">> SERVER_HEARTBEAT\n");
            }

            // Send client heartbeat in response
            // if we haven't sent recently
            if ((now - last_send_time) >= (heartbeat_ms / 1000)) {
                if (!send_client_heartbeat(sock)) {
                    std::printf("Failed to send Client Heartbeat\n");
                    goto disconnect;
                }
                last_send_time = now;
            }

            break;
        }

        case SOUP_PKT_END_OF_SESSION: {
            // Drain payload
            if (payload_len > 0) {
                int remaining = payload_len;
                while (remaining > 0) {
                    int chunk = remaining > max_payload ? max_payload : remaining;
                    if (!sock.recv_exact(payload_buf, chunk)) goto disconnect;
                    remaining -= chunk;
                }
            }

            std::printf(">> END_OF_SESSION seq=%llu decoded=%llu\n",
                        (unsigned long long)current_seq,
                        (unsigned long long)decoded_count);
            sock.close();
            return 0;
        }

        case SOUP_PKT_DEBUG: {
            // Debug packet - print payload as text
            if (payload_len > 0 && payload_len <= max_payload) {
                if (!sock.recv_exact(payload_buf, payload_len)) {
                    goto disconnect;
                }
                std::printf(">> DEBUG: '%.*s'\n", (int)payload_len, (const char*)payload_buf);
            } else if (payload_len > 0) {
                int remaining = payload_len;
                while (remaining > 0) {
                    int chunk = remaining > max_payload ? max_payload : remaining;
                    if (!sock.recv_exact(payload_buf, chunk)) goto disconnect;
                    remaining -= chunk;
                }
            }
            break;
        }

        default: {
            // Unknown packet type - drain and continue
            std::printf(">> UNKNOWN PACKET type='%c' (0x%02X) len=%u\n",
                        pkt_type, (unsigned)(uint8_t)pkt_type, (unsigned)pkt_len);

            if (payload_len > 0) {
                int remaining = payload_len;
                while (remaining > 0) {
                    int chunk = remaining > max_payload ? max_payload : remaining;
                    if (!sock.recv_exact(payload_buf, chunk)) goto disconnect;
                    remaining -= chunk;
                }
            }
            break;
        }

        } // switch
    } // while

disconnect:
    std::printf(">> DISCONNECTED seq=%llu decoded=%llu\n",
                (unsigned long long)current_seq,
                (unsigned long long)decoded_count);
    sock.close();
    return 0;
}
