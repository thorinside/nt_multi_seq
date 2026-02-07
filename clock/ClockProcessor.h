#ifndef CLOCK_PROCESSOR_H
#define CLOCK_PROCESSOR_H

#include <stdint.h>

class ClockProcessor {
public:
    ClockProcessor();

    // Set clock divider (1 = every tick, 2 = every other, etc.)
    void setDivider(int div);

    // Process a clock tick. Returns true if this channel should advance.
    bool tick();

    // Reset the divider counter
    void reset();

private:
    int divider_;
    int count_;
};

#endif // CLOCK_PROCESSOR_H
