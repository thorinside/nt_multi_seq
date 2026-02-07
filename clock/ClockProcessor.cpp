#include "ClockProcessor.h"

ClockProcessor::ClockProcessor()
    : divider_(1)
    , count_(0)
{
}

void ClockProcessor::setDivider(int div)
{
    if (div < 1) div = 1;
    divider_ = div;
}

bool ClockProcessor::tick()
{
    count_++;
    if (count_ >= divider_) {
        count_ = 0;
        return true;
    }
    return false;
}

void ClockProcessor::reset()
{
    count_ = 0;
}
