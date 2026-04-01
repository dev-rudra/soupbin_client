#include "config.h"

#include <fstream>
#include <string>
#include <cctype>
#include <cstdlib>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

AppConfig::AppConfig()
    : server_port(0),
      requested_sequence(1),
      heartbeat_interval_sec(15) {
    std::memset(spec_by_type, 0, sizeof(spec_by_type));
}

static AppConfig app_config;

static std::string trim(const std::string& s) {
    const char* whitespace = " \t\r\n";

    size_t first_non_whitespace = s.find_first_not_of(whitespace);
    if (first_non_whitespace == std::string::npos) {
        return "";
    }

    size_t last_non_whitespace = s.find_last_not_of(whitespace);
    return s.substr(first_non_whitespace, last_non_whitespace - first_non_whitespace + 1);
}

static std::string to_upper_case(const std::string& s) {
    std::string out = s;
    for (size_t i = 0; i < out.size(); i++) {
        out[i] = (char)std::toupper((unsigned char)out[i]);
    }
    return out;
}

static std::string config_absolute_path(const char* config_path, const std::string& path_from_ini) {
    if (path_from_ini.empty()) {
        return "";
    }

    if (path_from_ini[0] == '/') {
        return path_from_ini;
    }

    std::string config_file = config_path;
    size_t last_slash = config_file.find_last_of('/');
    if (last_slash == std::string::npos) {
        return path_from_ini;
    }

    std::string config_dir = config_file.substr(0, last_slash);
    return config_dir + "/" + path_from_ini;
}

static FieldType parse_field_type(const std::string& s) {
    if (s == "char")   return CHAR;
    if (s == "uint8")  return UINT8;
    if (s == "uint16") return UINT16;
    if (s == "uint32") return UINT32;
    if (s == "uint64") return UINT64;
    if (s == "int16")  return INT16;
    if (s == "int32")  return INT32;
    if (s == "int64")  return INT64;
    if (s == "string") return STRING;
    if (s == "binary") return BINARY;
    return STRING;
}

static bool load_spec(const std::string& spec_path, AppConfig* cfg) {
    std::ifstream file(spec_path.c_str());
    if (!file) {
        return false;
    }

    json root;
    file >> root;

    cfg->msg_specs.clear();
    std::memset(cfg->spec_by_type, 0, sizeof(cfg->spec_by_type));

    for (json::iterator it = root.begin(); it != root.end(); ++it) {
        std::string msg_key = it.key();

        if (msg_key.size() != 1) {
            continue;
        }

        const json& obj = it.value();

        MsgSpec msg;
        msg.msg_type = msg_key[0];
        msg.name = obj.value("name", "");

        uint32_t offset = 0;
        const json& fields = obj["fields"];
        for (size_t i = 0; i < fields.size(); i++) {
            const json& json_file = fields[i];

            FieldSpec field_spec;
            field_spec.name = json_file.value("name", "");
            field_spec.type = parse_field_type(json_file.value("type", "string"));
            field_spec.size = (uint32_t)json_file.value("size", 0);
            field_spec.offset = offset;

            offset += field_spec.size;
            msg.fields.push_back(field_spec);
        }

        msg.total_length = offset;
        cfg->msg_specs[msg.msg_type] = msg;
        cfg->spec_by_type[(unsigned char)msg.msg_type] = &cfg->msg_specs[msg.msg_type];
    }

    return true;
}


bool load_config(const char* config_path) {
    AppConfig cfg;

    std::ifstream file(config_path);
    if (!file) {
        return false;
    }

    std::string section;
    std::string line;

    while (std::getline(file, line)) {
        if (!line.empty() && line[line.size() - 1] == '\r') {
            line.resize(line.size() - 1);
        }

        line = trim(line);

        if (line.empty()) {
            continue;
        }

        if (line[0] == '#' || line[0] == ';') {
            continue;
        }

        if (line.front() == '[' && line.back() == ']') {
            section = to_upper_case(trim(line.substr(1, line.size() - 2)));
            continue;
        }

        size_t pos = line.find(':');
        if (pos == std::string::npos) {
            continue;
        }

        std::string key = trim(line.substr(0, pos));
        std::string val = trim(line.substr(pos + 1));

        if (section == "SOUPBINTCP") {
            if      (key == "server_ip")   cfg.server_ip = val;
            else if (key == "server_port") cfg.server_port = (uint16_t)std::atoi(val.c_str());
            else if (key == "username")    cfg.username = val;
            else if (key == "password")    cfg.password = val;
            else if (key == "requested_session")  cfg.requested_session = val;
            else if (key == "requested_sequence") cfg.requested_sequence = (uint64_t)std::strtoull(val.c_str(), 0, 10);
            else if (key == "heartbeat_interval_sec") cfg.heartbeat_interval_sec = std::atoi(val.c_str());
            else if (key == "protocol_spec") cfg.protocol_spec = val;
        }
    }

    if (cfg.server_ip.empty()) return false;
    if (cfg.server_port == 0) return false;
    if (cfg.protocol_spec.empty()) return false;

    cfg.protocol_spec = config_absolute_path(config_path, cfg.protocol_spec);

    app_config = cfg;
    if (!load_spec(app_config.protocol_spec, &app_config)) {
        return false;
    }

    return true;
}

const AppConfig& config() {
    return app_config;
}
