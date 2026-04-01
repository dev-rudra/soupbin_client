#ifndef SOUPBINTCP_H
#define SOUPBINTCP_H

#include <cstdint>

// SoupBinTCP 3.0 Packet Types
//
// Client -> Server
static const char SOUP_PKT_LOGIN_REQUEST     = 'L';
static const char SOUP_PKT_LOGOUT_REQUEST    = 'O';
static const char SOUP_PKT_CLIENT_HEARTBEAT  = 'R';
static const char SOUP_PKT_UNSEQUENCED_DATA  = 'U';

// Server -> Client
static const char SOUP_PKT_LOGIN_ACCEPTED    = 'A';
static const char SOUP_PKT_LOGIN_REJECTED    = 'J';
static const char SOUP_PKT_SEQUENCED_DATA    = 'S';
static const char SOUP_PKT_SERVER_HEARTBEAT  = 'H';
static const char SOUP_PKT_END_OF_SESSION    = 'Z';
static const char SOUP_PKT_DEBUG             = '+';

// SoupBinTCP packet header:
//   2 bytes  Packet Length (big-endian, excludes itself)
//   1 byte   Packet Type
static const int SOUP_HEADER_LEN = 3;

// Login Request payload (after packet header):
//   6 bytes   Username (alpha, left-justified, space-padded)
//  10 bytes   Password (alpha, left-justified, space-padded)
//  10 bytes   Requested Session (space = any)
//  20 bytes   Requested Sequence Number (ASCII, right-justified, space-padded)
//             "                    " = next available (or 1 for start)
#pragma pack(push, 1)
struct LoginRequestPayload {
    char username[6];
    char password[10];
    char requested_session[10];
    char requested_sequence[20];
};
#pragma pack(pop)

static const int LOGIN_REQUEST_PAYLOAD_LEN = 46;

// Login Accepted payload:
//  10 bytes   Session
//  20 bytes   Sequence Number (ASCII, right-justified, space-padded)
#pragma pack(push, 1)
struct LoginAcceptedPayload {
    char session[10];
    char sequence_number[20];
};
#pragma pack(pop)

static const int LOGIN_ACCEPTED_PAYLOAD_LEN = 30;

// Login Rejected payload:
//   1 byte    Reject Reason Code
//             'A' = Not Authorized
//             'S' = Session Not Available
static const int LOGIN_REJECTED_PAYLOAD_LEN = 1;

#endif
