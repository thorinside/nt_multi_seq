#ifndef SPEC_HELPERS_H
#define SPEC_HELPERS_H

// Standalone helpers for page naming and string formatting.
// Used by nt_seq_construct.cpp, nt_seq_params.cpp, and tests.
// No NT API dependency.

// Number of engine types (must match kNumEngineTypes)
static const int kSpecNumEngines = 4;

// Engine name lookup by engine type index (0-3)
static inline const char* specEngineName(int engineType)
{
    switch (engineType) {
    case 0: return "Thorp";
    case 1: return "Soma";
    case 2: return "AE Seq";
    case 3: return "Markov";
    default: return "?";
    }
}

// Copy string, return chars written (no null terminator)
static inline int specStrCopy(char* dst, const char* src, int maxLen)
{
    int i = 0;
    while (src[i] && i < maxLen) {
        dst[i] = src[i];
        i++;
    }
    return i;
}

// Simple int-to-string for tests (doesn't depend on NT API)
static inline int specIntToString(char* buf, int val)
{
    if (val < 0) {
        buf[0] = '-';
        return 1 + specIntToString(buf + 1, -val);
    }
    if (val < 10) {
        buf[0] = '0' + val;
        return 1;
    }
    int len = specIntToString(buf, val / 10);
    buf[len] = '0' + (val % 10);
    return len + 1;
}

#endif // SPEC_HELPERS_H
