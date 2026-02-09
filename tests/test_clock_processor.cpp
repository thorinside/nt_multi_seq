// Unit tests for ClockProcessor.
// Pure logic, no NT API stubs needed.

#include "../clock/ClockProcessor.cpp"
#include <stdio.h>

static int failures = 0;
static int tests = 0;

#define ASSERT_TRUE(cond, msg) do { \
    tests++; \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s\n", msg); \
        failures++; \
    } \
} while (0)

#define ASSERT_EQ(a, b, msg) do { \
    tests++; \
    if ((a) != (b)) { \
        fprintf(stderr, "FAIL: %s: expected %d, got %d\n", msg, (int)(b), (int)(a)); \
        failures++; \
    } \
} while (0)

// Division by 1: every tick fires
static void test_divider_1()
{
    ClockProcessor cp;
    cp.setDivider(1);
    for (int i = 0; i < 10; i++) {
        ASSERT_TRUE(cp.tick(), "div=1: every tick should fire");
    }
}

// Division by 4: fires every 4th tick
static void test_divider_4()
{
    ClockProcessor cp;
    cp.setDivider(4);
    for (int cycle = 0; cycle < 3; cycle++) {
        for (int i = 0; i < 3; i++) {
            ASSERT_TRUE(!cp.tick(), "div=4: ticks 1-3 should not fire");
        }
        ASSERT_TRUE(cp.tick(), "div=4: tick 4 should fire");
    }
}

// Reset mid-count restores counter
static void test_reset_mid_count()
{
    ClockProcessor cp;
    cp.setDivider(4);

    // Advance 2 ticks
    cp.tick();
    cp.tick();

    // Reset
    cp.reset();

    // Should need 4 ticks again to fire
    ASSERT_TRUE(!cp.tick(), "after reset: tick 1 should not fire");
    ASSERT_TRUE(!cp.tick(), "after reset: tick 2 should not fire");
    ASSERT_TRUE(!cp.tick(), "after reset: tick 3 should not fire");
    ASSERT_TRUE(cp.tick(), "after reset: tick 4 should fire");
}

// setDivider(0) clamps to 1
static void test_divider_0_clamps()
{
    ClockProcessor cp;
    cp.setDivider(0);
    ASSERT_TRUE(cp.tick(), "div=0 clamped to 1: should fire immediately");
    ASSERT_TRUE(cp.tick(), "div=0 clamped to 1: should fire again");
}

// setDivider(-5) clamps to 1
static void test_divider_negative_clamps()
{
    ClockProcessor cp;
    cp.setDivider(-5);
    ASSERT_TRUE(cp.tick(), "div=-5 clamped to 1: should fire immediately");
    ASSERT_TRUE(cp.tick(), "div=-5 clamped to 1: should fire again");
}

// Change divider mid-count
static void test_change_divider_mid_count()
{
    ClockProcessor cp;
    cp.setDivider(8);

    // Advance 2 ticks (count=2, need 8 to fire)
    cp.tick();
    cp.tick();

    // Change to 3: count is already 2, next tick (count=3) should fire
    cp.setDivider(3);
    ASSERT_TRUE(cp.tick(), "after div change 8→3: tick 3 should fire");

    // Now it restarted — need 3 more
    ASSERT_TRUE(!cp.tick(), "after fire: tick 1 should not fire");
    ASSERT_TRUE(!cp.tick(), "after fire: tick 2 should not fire");
    ASSERT_TRUE(cp.tick(), "after fire: tick 3 should fire");
}

// Verify constructor defaults to divider 1
static void test_default_constructor()
{
    ClockProcessor cp;
    ASSERT_TRUE(cp.tick(), "default ctor: should fire immediately (div=1)");
}

// Large divider
static void test_large_divider()
{
    ClockProcessor cp;
    cp.setDivider(100);
    for (int i = 0; i < 99; i++) {
        ASSERT_TRUE(!cp.tick(), "div=100: early ticks should not fire");
    }
    ASSERT_TRUE(cp.tick(), "div=100: tick 100 should fire");
}

int main()
{
    test_divider_1();
    test_divider_4();
    test_reset_mid_count();
    test_divider_0_clamps();
    test_divider_negative_clamps();
    test_change_divider_mid_count();
    test_default_constructor();
    test_large_divider();

    printf("%d tests, %d failures\n", tests, failures);
    return failures ? 1 : 0;
}
