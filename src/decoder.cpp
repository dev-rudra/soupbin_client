#include "decoder.h"

#include <cstdio>

// Read 2 bytes as big-endian (network order)
// unsigned 16-bit value.
static uint16_t read_u16_big_endian(const uint8_t* bytes) {
    uint16_t high = (uint16_t)bytes[0];
    uint16_t low = (uint16_t)bytes[1];
    return (uint16_t)((high << 8) | low);
}

// Read 8 bytes as big-endian (network order)
// unsigned 64-bit value.
static uint64_t read_u64_big_endian(const uint8_t* bytes) {
    uint64_t b0 = (uint64_t)bytes[0];
    uint64_t b1 = (uint64_t)bytes[1];
    uint64_t b2 = (uint64_t)bytes[2];
    uint64_t b3 = (uint64_t)bytes[3];
    uint64_t b4 = (uint64_t)bytes[4];
    uint64_t b5 = (uint64_t)bytes[5];
    uint64_t b6 = (uint64_t)bytes[6];
    uint64_t b7 = (uint64_t)bytes[7];

    return (b0 << 56) |
           (b1 << 48) |
           (b2 << 40) |
           (b3 << 32) |
           (b4 << 24) |
           (b5 << 16) |
           (b6 << 8) |
           (b7 << 0);
}

static uint32_t read_u32_big_endian(const uint8_t* bytes) {
    uint32_t b0 = (uint32_t)bytes[0];
    uint32_t b1 = (uint32_t)bytes[1];
    uint32_t b2 = (uint32_t)bytes[2];
    uint32_t b3 = (uint32_t)bytes[3];

    return (b0 << 24) |
           (b1 << 16) |
           (b2 << 8)  |
           (b3 << 0);
}

static void print_field_value(FieldType type, const uint8_t* field_data, uint32_t size) {
    switch (type) {
        case STRING:
            std::printf("%.*s", (int)size, (const char*)field_data);
            return;
        case CHAR:
            std::printf("%c", (char)field_data[0]);
            return;
        case UINT8:
            std::printf("%u", (unsigned)field_data[0]);
            return;
        case UINT16:
            std::printf("%u", (unsigned)read_u16_big_endian(field_data));
            return;
        case UINT32:
            std::printf("%u", (unsigned)read_u32_big_endian(field_data));
            return;
        case UINT64:
            std::printf("%llu", (unsigned long long)read_u64_big_endian(field_data));
            return;
        case INT16:
            std::printf("%d", (int)(int16_t)read_u16_big_endian(field_data));
            return;
        case INT32:
            std::printf("%d", (int)(int32_t)read_u32_big_endian(field_data));
            return;
        case INT64:
            std::printf("%lld", (long long)(int64_t)read_u64_big_endian(field_data));
            return;
        case BINARY:
        default:
            for (uint32_t i = 0; i < size; i++) {
                std::printf("%02X", (unsigned)field_data[i]);
            }
            return;
    }
}

bool decode_itch_message(const uint8_t* msg, uint16_t msg_len,
                         const AppConfig& cfg, const std::string& session,
                         uint64_t seq, bool verbose) {

    if (!msg || msg_len == 0) {
        return false;
    }

    char msg_type = (char)msg[0];
    const MsgSpec* spec = cfg.spec_by_type[(unsigned char)msg_type];

    std::printf(">> {'%.*s', %llu",
                (int)session.size(), session.c_str(),
                (unsigned long long)seq);

    if (!spec) {
        std::printf(", 'Unknown(type=%c)'}\n", msg_type);
        return false;
    }

    // Validate MsgLength with Spec TotalLength (if length is in the spec)
    if (spec->total_length != 0 && msg_len != (uint16_t)spec->total_length) {
        std::printf(", 'Length Mismatch', 'type=%c', 'exp=%u', 'got=%u'",
                    msg_type,
                    (unsigned)spec->total_length,
                    (unsigned)msg_len);
    }

    for (size_t i = 0; i < spec->fields.size(); i++) {
        const FieldSpec& field = spec->fields[i];

        if (field.offset + field.size > msg_len) {
            std::printf(", 'TRUNC', 'type=%c', 'need=%u', 'got=%u'}\n",
                        msg_type,
                        (unsigned)(field.offset + field.size),
                        (unsigned)msg_len);
            return false;
        }

        const uint8_t* field_data = msg + field.offset;
        std::printf(", '");

        // verbose print
        if (verbose) {
            std::printf("%s=", field.name.c_str());
        }

        print_field_value(field.type, field_data, field.size);
        std::printf("'");
    }
    std::printf("}\n");
    return true;
}
