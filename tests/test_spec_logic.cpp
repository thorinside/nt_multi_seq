// Unit tests for spec helpers: engine naming, string utilities,
// engine pool alignment.
// No NT API dependency — standalone compilation.

#include "../spec_helpers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static int failures = 0;
static int tests = 0;

#define ASSERT_EQ(a, b, msg) do { \
    tests++; \
    if ((a) != (b)) { \
        fprintf(stderr, "FAIL: %s: expected %d, got %d\n", msg, (int)(b), (int)(a)); \
        failures++; \
    } \
} while (0)

#define ASSERT_STR_EQ(a, b, msg) do { \
    tests++; \
    if (strcmp((a), (b)) != 0) { \
        fprintf(stderr, "FAIL: %s: expected \"%s\", got \"%s\"\n", msg, (b), (a)); \
        failures++; \
    } \
} while (0)

// --- Test: engine name lookup ---

static void test_specEngineName()
{
    ASSERT_STR_EQ(specEngineName(0), "Thorp", "engine 0 -> Thorp");
    ASSERT_STR_EQ(specEngineName(1), "Soma", "engine 1 -> Soma");
    ASSERT_STR_EQ(specEngineName(2), "AE Seq", "engine 2 -> AE Seq");
    ASSERT_STR_EQ(specEngineName(3), "Markov", "engine 3 -> Markov");
    ASSERT_STR_EQ(specEngineName(4), "Ferro", "engine 4 -> Ferro");
    ASSERT_STR_EQ(specEngineName(5), "Quantum", "engine 5 -> Quantum");
    ASSERT_STR_EQ(specEngineName(-1), "?", "engine -1 -> ?");
    ASSERT_STR_EQ(specEngineName(6), "?", "engine 6 -> ?");
}

// --- Test: specStrCopy ---

static void test_specStrCopy()
{
    char buf[24];
    int len;

    len = specStrCopy(buf, "Hello", 24);
    buf[len] = 0;
    ASSERT_EQ(len, 5, "strCopy Hello len");
    ASSERT_STR_EQ(buf, "Hello", "strCopy Hello");

    len = specStrCopy(buf, "TooLong", 3);
    buf[len] = 0;
    ASSERT_EQ(len, 3, "strCopy truncated len");
    ASSERT_STR_EQ(buf, "Too", "strCopy truncated");

    len = specStrCopy(buf, "", 24);
    ASSERT_EQ(len, 0, "strCopy empty len");
}

// --- Test: specIntToString ---

static void test_specIntToString()
{
    char buf[16];
    int len;

    len = specIntToString(buf, 0);
    buf[len] = 0;
    ASSERT_STR_EQ(buf, "0", "intToString 0");

    len = specIntToString(buf, 1);
    buf[len] = 0;
    ASSERT_STR_EQ(buf, "1", "intToString 1");

    len = specIntToString(buf, 42);
    buf[len] = 0;
    ASSERT_STR_EQ(buf, "42", "intToString 42");

    len = specIntToString(buf, -7);
    buf[len] = 0;
    ASSERT_STR_EQ(buf, "-7", "intToString -7");

    len = specIntToString(buf, 100);
    buf[len] = 0;
    ASSERT_STR_EQ(buf, "100", "intToString 100");
}

// --- Test: page name building (simulates what construct/switchEngine does) ---

static void test_pageNameBuild()
{
    // Simulate building "Ch 1 Thorp" page name
    char buf[24];
    int len = specStrCopy(buf, "Ch ", 3);
    len += specIntToString(buf + len, 1);
    buf[len++] = ' ';
    len += specStrCopy(buf + len, specEngineName(0), 23 - len);  // Thorp
    buf[len] = 0;
    ASSERT_STR_EQ(buf, "Ch 1 Thorp", "page name Ch 1 Thorp");

    // "Ch 3 AE Seq"
    len = specStrCopy(buf, "Ch ", 3);
    len += specIntToString(buf + len, 3);
    buf[len++] = ' ';
    len += specStrCopy(buf + len, specEngineName(2), 23 - len);  // AE Seq
    buf[len] = 0;
    ASSERT_STR_EQ(buf, "Ch 3 AE Seq", "page name Ch 3 AE Seq");

    // "Ch 8" (None — no engine suffix)
    len = specStrCopy(buf, "Ch ", 3);
    len += specIntToString(buf + len, 8);
    buf[len] = 0;
    ASSERT_STR_EQ(buf, "Ch 8", "page name Ch 8 (None)");

    // "Ch 2 Routing"
    len = specStrCopy(buf, "Ch ", 3);
    len += specIntToString(buf + len, 2);
    len += specStrCopy(buf + len, " Routing", 23 - len);
    buf[len] = 0;
    ASSERT_STR_EQ(buf, "Ch 2 Routing", "page name Ch 2 Routing");
}

// --- Test: engine pool alignment ---

static const int kMaxChannels = 4;
static const int kPoolSlotSize = 2048;
static const int kPoolAlignment = 8;

// Verify pool slot stride preserves alignment
static_assert(kPoolSlotSize % kPoolAlignment == 0,
    "Pool slot size must be a multiple of alignment");

static void test_poolSlotAlignment()
{
    alignas(8) uint8_t pool[kMaxChannels * kPoolSlotSize];

    for (int ch = 0; ch < kMaxChannels; ++ch) {
        uint8_t* slot = pool + ch * kPoolSlotSize;
        uintptr_t addr = (uintptr_t)slot;
        tests++;
        if (addr % kPoolAlignment != 0) {
            fprintf(stderr, "FAIL: pool slot %d misaligned: addr 0x%lx %% %d = %lu\n",
                ch, (unsigned long)addr, kPoolAlignment,
                (unsigned long)(addr % kPoolAlignment));
            failures++;
        }
    }
}

// --- Test: parameter layout math ---

static void test_paramLayout()
{
    // Verify the parameter layout formula
    int numGlobal = 5;
    int numCommon = 15;
    int numEngSlots = 32;
    int paramsPerCh = numCommon + numEngSlots;
    ASSERT_EQ(paramsPerCh, 47, "params per channel = 47");

    // 1 channel: 5 + 1 + 47 = 53
    int total1 = numGlobal + 1 + 1 * paramsPerCh;
    ASSERT_EQ(total1, 53, "1 channel total = 53");

    // 4 channels: 5 + 4 + 4*47 = 197
    int total4 = numGlobal + 4 + 4 * paramsPerCh;
    ASSERT_EQ(total4, 197, "4 channel total = 197");

    // Engine type param indices for 4 channels: 5, 6, 7, 8
    for (int ch = 0; ch < 4; ++ch) {
        int etIdx = numGlobal + ch;
        ASSERT_EQ(etIdx, 5 + ch, "engine type param index");
    }

    // First channel's common base (4 channels): 5 + 4 = 9
    int firstCommon = numGlobal + 4;
    ASSERT_EQ(firstCommon, 9, "ch0 common base (4ch) = 9");

    // Second channel's common base: 9 + 47 = 56
    int secondCommon = firstCommon + paramsPerCh;
    ASSERT_EQ(secondCommon, 56, "ch1 common base (4ch) = 56");

    // Dedicated factories omit the engine selector and unused engine slots.
    int thorpParams = 15;
    int dedicated1 = numGlobal + numCommon + thorpParams;
    ASSERT_EQ(dedicated1, 35, "1 channel dedicated Thorp total = 35");

    int dedicated4 = numGlobal + 4 * (numCommon + thorpParams);
    ASSERT_EQ(dedicated4, 125, "4 channel dedicated Thorp total = 125");
}

int main()
{
    test_specEngineName();
    test_specStrCopy();
    test_specIntToString();
    test_pageNameBuild();
    test_poolSlotAlignment();
    test_paramLayout();

    printf("%d tests, %d failures\n", tests, failures);
    return failures > 0 ? 1 : 0;
}
