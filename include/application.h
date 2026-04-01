#ifndef APPLICATION_H
#define APPLICATION_H

#include <cstdint>

class Application {
public:
    Application();

    void set_max_messages(uint64_t value);
    void set_verbose(bool value);
    void set_type_filter(char type);
    void set_start_seq(uint64_t value);

    int run();

private:
    uint64_t max_messages;
    bool verbose;
    bool has_type_filter;
    bool type_allowed[256];

    bool has_start_seq;
    uint64_t start_seq;
};

#endif
