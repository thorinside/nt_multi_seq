#include "SequencerEngine.h"

void FocusDetailLine::append(const char* s, uint8_t colour)
{
    while (*s && len < 63) {
        text[len] = *s++;
        colours[len] = colour;
        len++;
    }
    text[len] = 0;
}

void FocusDetailLine::appendChar(char c, uint8_t colour)
{
    if (len < 63) {
        text[len] = c;
        colours[len] = colour;
        len++;
    }
    text[len] = 0;
}

void FocusDetailLine::appendInt(int32_t val, uint8_t colour)
{
    char tmp[16];
    int i = 0;
    if (val < 0) {
        tmp[i++] = '-';
        val = -val;
    }
    if (val == 0) {
        tmp[i++] = '0';
    } else {
        char digits[12];
        int d = 0;
        while (val > 0) {
            digits[d++] = '0' + (char)(val % 10);
            val /= 10;
        }
        while (d > 0) tmp[i++] = digits[--d];
    }
    tmp[i] = 0;
    append(tmp, colour);
}

void FocusDetailLine::appendFloat(float val, int decimals, uint8_t colour)
{
    if (val < 0.0f) {
        appendChar('-', colour);
        val = -val;
    }
    int intPart = (int)val;
    appendInt(intPart, colour);
    if (decimals > 0) {
        appendChar('.', colour);
        float frac = val - (float)intPart;
        for (int d = 0; d < decimals; ++d) {
            frac *= 10.0f;
            int digit = (int)frac;
            if (digit > 9) digit = 9;
            appendChar('0' + (char)digit, colour);
            frac -= (float)digit;
        }
    }
}

int fmtInt(char* buf, int32_t val)
{
    char tmp[16];
    int i = 0;
    if (val < 0) {
        tmp[i++] = '-';
        val = -val;
    }
    if (val == 0) {
        tmp[i++] = '0';
    } else {
        char digits[12];
        int d = 0;
        while (val > 0) {
            digits[d++] = '0' + (char)(val % 10);
            val /= 10;
        }
        while (d > 0) tmp[i++] = digits[--d];
    }
    tmp[i] = 0;
    for (int j = 0; j < i; ++j) buf[j] = tmp[j];
    return i;
}
