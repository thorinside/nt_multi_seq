#ifndef SPEC_HELPERS_H
#define SPEC_HELPERS_H

// Standalone spec-to-engine mapping helpers.
// Used by nt_seq_construct.cpp and tests/test_spec_logic.cpp.
// No NT API dependency.

#include <string.h>

// Must match kMaxChannels in nt_seq.h
static const int kSpecMaxSlots = 8;

// Number of engine types (must match kNumEngineTypes)
static const int kSpecNumEngines = 4;

// Convert spec value to engine type. Returns -1 for None (spec=0).
static inline int engineTypeFromSpec(int specValue)
{
    if (specValue <= 0 || specValue > kSpecNumEngines) return -1;
    return specValue - 1;
}

// Count active (non-None) channels from spec array
static inline int countActiveChannels(const int* specs)
{
    int count = 0;
    for (int i = 0; i < kSpecMaxSlots; ++i) {
        if (specs[i] > 0)
            count++;
    }
    return count;
}

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

// Build page names for active channels.
// specs: kSpecMaxSlots-element array of spec values (0=None, 1-4=engine)
// routingNames/engineNames: output arrays of char[24], one per active channel
// Returns number of active channels (page name pairs written)
static inline int buildPageNames(const int* specs, char routingNames[][24], char engineNames[][24])
{
    // Count engine type occurrences
    int engineCount[kSpecNumEngines] = {};
    int numActive = 0;
    for (int i = 0; i < kSpecMaxSlots; ++i) {
        int et = engineTypeFromSpec(specs[i]);
        if (et >= 0 && et < kSpecNumEngines) {
            engineCount[et]++;
            numActive++;
        }
    }

    // Build names
    int engineInstance[kSpecNumEngines] = {};
    int idx = 0;
    for (int slot = 0; slot < kSpecMaxSlots; ++slot) {
        int et = engineTypeFromSpec(specs[slot]);
        if (et < 0) continue;

        const char* eName = specEngineName(et);
        int inst = engineInstance[et]++;
        bool needNumber = (engineCount[et] > 1);

        // Routing page name
        {
            char* buf = routingNames[idx];
            int len = specStrCopy(buf, eName, 20);
            if (needNumber) {
                buf[len++] = ' ';
                len += specIntToString(buf + len, inst + 1);
            }
            len += specStrCopy(buf + len, " Routing", 23 - len);
            buf[len] = 0;
        }

        // Engine page name
        {
            char* buf = engineNames[idx];
            int len = specStrCopy(buf, eName, 20);
            if (needNumber) {
                buf[len++] = ' ';
                len += specIntToString(buf + len, inst + 1);
            }
            buf[len] = 0;
        }

        idx++;
    }

    return numActive;
}

// Build dense channel mapping: for each active channel, store its spec slot index.
// Returns number of active channels.
static inline int buildDenseMapping(const int* specs, int* specSlots)
{
    int idx = 0;
    for (int slot = 0; slot < kSpecMaxSlots; ++slot) {
        if (specs[slot] > 0) {
            specSlots[idx++] = slot;
        }
    }
    return idx;
}

#endif // SPEC_HELPERS_H
