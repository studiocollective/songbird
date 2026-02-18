#ifndef SEQUENCING_CC_EVENT
#define SEQUENCING_CC_EVENT

#include <string>
using std::string;

struct CCEvent {
    int cc;       // CC number (0-127)
    int value;    // CC value (0-127)
    int tick;     // When to fire

    CCEvent(int cc, int value, int tick) : cc(cc), value(value), tick(tick) {};

    bool operator<(const CCEvent &other) const { return tick < other.tick; }
    string name() { return "CC: " + std::to_string(cc) + " Val: " + std::to_string(value) + " Tick: " + std::to_string(tick); };
};

#endif // SEQUENCING_CC_EVENT
