#ifndef DECODER_H
#define DECODER_H

#include <cstdint>
#include <string>
#include "config.h"

// Decode a single ITCH message payload
// (the data inside a SoupBinTCP Sequenced Data packet).
// Returns true on success.
bool decode_itch_message(const uint8_t* msg,
                         uint16_t msg_len,
                         const AppConfig& cfg,
                         const std::string& session,
                         uint64_t seq,
                         bool verbose);

#endif
