// Unit tests for spec-to-engine mapping, channel counting,
// page name generation, and dense channel mapping.
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

// --- Test: spec-to-engine mapping ---

static void test_engineTypeFromSpec()
{
    ASSERT_EQ(engineTypeFromSpec(0), -1, "spec 0 -> None");
    ASSERT_EQ(engineTypeFromSpec(1), 0,  "spec 1 -> Thorp (0)");
    ASSERT_EQ(engineTypeFromSpec(2), 1,  "spec 2 -> Soma (1)");
    ASSERT_EQ(engineTypeFromSpec(3), 2,  "spec 3 -> AE Seq (2)");
    ASSERT_EQ(engineTypeFromSpec(4), 3,  "spec 4 -> Markov (3)");
    ASSERT_EQ(engineTypeFromSpec(-1), -1, "spec -1 -> None");
    ASSERT_EQ(engineTypeFromSpec(5), -1,  "spec 5 -> None (out of range)");
    ASSERT_EQ(engineTypeFromSpec(100), -1, "spec 100 -> None (out of range)");
}

// --- Test: active channel counting ---

static void test_countActiveChannels()
{
    {
        int specs[] = {2, 0, 0, 0, 0, 0, 0, 0};
        ASSERT_EQ(countActiveChannels(specs), 1, "[2,0,...] -> 1");
    }
    {
        int specs[] = {2, 1, 0, 0, 0, 0, 0, 0};
        ASSERT_EQ(countActiveChannels(specs), 2, "[2,1,0,...] -> 2");
    }
    {
        int specs[] = {2, 2, 2, 2, 0, 0, 0, 0};
        ASSERT_EQ(countActiveChannels(specs), 4, "[2,2,2,2,0,...] -> 4");
    }
    {
        int specs[] = {0, 0, 0, 0, 0, 0, 0, 0};
        ASSERT_EQ(countActiveChannels(specs), 0, "all zero -> 0");
    }
    {
        int specs[] = {0, 0, 0, 4, 0, 0, 0, 0};
        ASSERT_EQ(countActiveChannels(specs), 1, "[0,0,0,4,0,...] -> 1");
    }
    {
        int specs[] = {1, 2, 3, 4, 0, 0, 0, 0};
        ASSERT_EQ(countActiveChannels(specs), 4, "[1,2,3,4,0,...] -> 4");
    }
    {
        int specs[] = {1, 2, 3, 4, 1, 2, 3, 4};
        ASSERT_EQ(countActiveChannels(specs), 8, "all 8 active -> 8");
    }
    {
        int specs[] = {0, 0, 0, 0, 0, 0, 0, 1};
        ASSERT_EQ(countActiveChannels(specs), 1, "only slot 8 -> 1");
    }
}

// --- Test: page name generation ---

static void test_pageNames_singleSoma()
{
    int specs[] = {2, 0, 0, 0, 0, 0, 0, 0};
    char routing[8][24], engine[8][24];
    int n = buildPageNames(specs, routing, engine);
    ASSERT_EQ(n, 1, "single soma: 1 active");
    ASSERT_STR_EQ(routing[0], "Soma Routing", "single soma: routing name");
    ASSERT_STR_EQ(engine[0], "Soma", "single soma: engine name");
}

static void test_pageNames_twoSomas()
{
    int specs[] = {2, 2, 0, 0, 0, 0, 0, 0};
    char routing[8][24], engine[8][24];
    int n = buildPageNames(specs, routing, engine);
    ASSERT_EQ(n, 2, "two somas: 2 active");
    ASSERT_STR_EQ(routing[0], "Soma 1 Routing", "two somas: routing 1");
    ASSERT_STR_EQ(engine[0], "Soma 1", "two somas: engine 1");
    ASSERT_STR_EQ(routing[1], "Soma 2 Routing", "two somas: routing 2");
    ASSERT_STR_EQ(engine[1], "Soma 2", "two somas: engine 2");
}

static void test_pageNames_mixed()
{
    int specs[] = {2, 1, 0, 0, 0, 0, 0, 0};
    char routing[8][24], engine[8][24];
    int n = buildPageNames(specs, routing, engine);
    ASSERT_EQ(n, 2, "mixed: 2 active");
    ASSERT_STR_EQ(routing[0], "Soma Routing", "mixed: soma routing");
    ASSERT_STR_EQ(engine[0], "Soma", "mixed: soma engine");
    ASSERT_STR_EQ(routing[1], "Thorp Routing", "mixed: thorp routing");
    ASSERT_STR_EQ(engine[1], "Thorp", "mixed: thorp engine");
}

static void test_pageNames_allDifferent()
{
    int specs[] = {1, 2, 3, 4, 0, 0, 0, 0};
    char routing[8][24], engine[8][24];
    int n = buildPageNames(specs, routing, engine);
    ASSERT_EQ(n, 4, "all different: 4 active");
    ASSERT_STR_EQ(engine[0], "Thorp", "all diff: thorp");
    ASSERT_STR_EQ(engine[1], "Soma", "all diff: soma");
    ASSERT_STR_EQ(engine[2], "AE Seq", "all diff: ae seq");
    ASSERT_STR_EQ(engine[3], "Markov", "all diff: markov");
    ASSERT_STR_EQ(routing[0], "Thorp Routing", "all diff: thorp routing");
    ASSERT_STR_EQ(routing[1], "Soma Routing", "all diff: soma routing");
    ASSERT_STR_EQ(routing[2], "AE Seq Routing", "all diff: ae seq routing");
    ASSERT_STR_EQ(routing[3], "Markov Routing", "all diff: markov routing");
}

static void test_pageNames_threeMarkovs()
{
    int specs[] = {4, 4, 4, 0, 0, 0, 0, 0};
    char routing[8][24], engine[8][24];
    int n = buildPageNames(specs, routing, engine);
    ASSERT_EQ(n, 3, "three markovs: 3 active");
    ASSERT_STR_EQ(engine[0], "Markov 1", "3 markovs: engine 1");
    ASSERT_STR_EQ(engine[1], "Markov 2", "3 markovs: engine 2");
    ASSERT_STR_EQ(engine[2], "Markov 3", "3 markovs: engine 3");
    ASSERT_STR_EQ(routing[0], "Markov 1 Routing", "3 markovs: routing 1");
    ASSERT_STR_EQ(routing[1], "Markov 2 Routing", "3 markovs: routing 2");
    ASSERT_STR_EQ(routing[2], "Markov 3 Routing", "3 markovs: routing 3");
}

static void test_pageNames_eightSomas()
{
    int specs[] = {2, 2, 2, 2, 2, 2, 2, 2};
    char routing[8][24], engine[8][24];
    int n = buildPageNames(specs, routing, engine);
    ASSERT_EQ(n, 8, "eight somas: 8 active");
    ASSERT_STR_EQ(engine[0], "Soma 1", "8 somas: engine 1");
    ASSERT_STR_EQ(engine[7], "Soma 8", "8 somas: engine 8");
    ASSERT_STR_EQ(routing[0], "Soma 1 Routing", "8 somas: routing 1");
    ASSERT_STR_EQ(routing[7], "Soma 8 Routing", "8 somas: routing 8");
}

// --- Test: dense channel mapping ---

static void test_denseMapping()
{
    {
        int specs[] = {2, 0, 1, 0, 0, 0, 0, 0};
        int slots[8] = {};
        int n = buildDenseMapping(specs, slots);
        ASSERT_EQ(n, 2, "[2,0,1,0,...] -> 2 active");
        ASSERT_EQ(slots[0], 0, "[2,0,1,0,...] ch0 -> slot 0");
        ASSERT_EQ(slots[1], 2, "[2,0,1,0,...] ch1 -> slot 2");
    }
    {
        int specs[] = {0, 0, 0, 4, 0, 0, 0, 0};
        int slots[8] = {};
        int n = buildDenseMapping(specs, slots);
        ASSERT_EQ(n, 1, "[0,0,0,4,...] -> 1 active");
        ASSERT_EQ(slots[0], 3, "[0,0,0,4,...] ch0 -> slot 3");
    }
    {
        int specs[] = {1, 2, 3, 4, 0, 0, 0, 0};
        int slots[8] = {};
        int n = buildDenseMapping(specs, slots);
        ASSERT_EQ(n, 4, "[1,2,3,4,...] -> 4 active");
        ASSERT_EQ(slots[0], 0, "ch0 -> slot 0");
        ASSERT_EQ(slots[1], 1, "ch1 -> slot 1");
        ASSERT_EQ(slots[2], 2, "ch2 -> slot 2");
        ASSERT_EQ(slots[3], 3, "ch3 -> slot 3");
    }
    {
        int specs[] = {0, 0, 0, 0, 0, 0, 0, 3};
        int slots[8] = {};
        int n = buildDenseMapping(specs, slots);
        ASSERT_EQ(n, 1, "only slot 8 -> 1 active");
        ASSERT_EQ(slots[0], 7, "ch0 -> slot 7");
    }
}

// --- Test: engine pool alignment ---

static const int kPoolSlotSize = 2048;
static const int kPoolAlignment = 8;

// Verify pool slot stride preserves alignment
static_assert(kPoolSlotSize % kPoolAlignment == 0,
    "Pool slot size must be a multiple of alignment");

static void test_poolSlotAlignment()
{
    // Simulate the pool as it exists in NtSeq: alignas(8) uint8_t[8*2048]
    alignas(8) uint8_t pool[kSpecMaxSlots * kPoolSlotSize];

    for (int ch = 0; ch < kSpecMaxSlots; ++ch) {
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

// Verify that dense channel indexing into pool maintains alignment
static void test_densePoolAlignment()
{
    alignas(8) uint8_t pool[kSpecMaxSlots * kPoolSlotSize];

    // Simulate sparse specs with gaps
    int specs[] = {2, 0, 4, 0, 0, 1, 0, 3};
    int slots[8] = {};
    int n = buildDenseMapping(specs, slots);

    for (int ch = 0; ch < n; ++ch) {
        uint8_t* slot = pool + ch * kPoolSlotSize;
        uintptr_t addr = (uintptr_t)slot;
        tests++;
        if (addr % kPoolAlignment != 0) {
            fprintf(stderr, "FAIL: dense slot %d (spec %d) misaligned: 0x%lx\n",
                ch, slots[ch], (unsigned long)addr);
            failures++;
        }
    }
}

int main()
{
    test_engineTypeFromSpec();
    test_countActiveChannels();
    test_pageNames_singleSoma();
    test_pageNames_twoSomas();
    test_pageNames_mixed();
    test_pageNames_allDifferent();
    test_pageNames_threeMarkovs();
    test_pageNames_eightSomas();
    test_denseMapping();
    test_poolSlotAlignment();
    test_densePoolAlignment();

    printf("%d tests, %d failures\n", tests, failures);
    return failures > 0 ? 1 : 0;
}
