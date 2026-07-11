#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <distingnt/api.h>

static int failures = 0;
static int tests = 0;

#define CHECK(condition, message) do { \
    ++tests; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s\n", message); \
        ++failures; \
    } \
} while (0)

typedef uintptr_t (*PluginEntry)(_NT_selector selector, uint32_t data);

// The plugin's data relocation for NT_globals is resolved when the dylib is
// loaded, even though these tests only call factory metadata callbacks.
const _NT_globals NT_globals = { 48000, 4, nullptr, 0, 0, 0 };

int32_t NT_algorithmIndex(const _NT_algorithm*)
{
    return -1;
}

int NT_intToString(char* buffer, int32_t value)
{
    int length = snprintf(buffer, 16, "%d", static_cast<int>(value));
    return length > 0 ? length : 0;
}

int main()
{
#if defined(__APPLE__)
    const char* pluginPath = "plugins/nt_seq.dylib";
#else
    const char* pluginPath = "plugins/nt_seq.so";
#endif

    void* handle = dlopen(pluginPath, RTLD_LAZY | RTLD_LOCAL);
    if (!handle) {
        fprintf(stderr, "FAIL: dlopen %s: %s\n", pluginPath, dlerror());
        return 1;
    }

    PluginEntry entry = reinterpret_cast<PluginEntry>(dlsym(handle, "pluginEntry"));
    CHECK(entry != nullptr, "pluginEntry is exported");
    if (!entry) {
        dlclose(handle);
        return 1;
    }

    CHECK(entry(kNT_selector_version, 0) == kNT_apiVersionCurrent,
        "plugin reports current API version");
    CHECK(entry(kNT_selector_numFactories, 0) == 7,
        "plugin exposes legacy plus six dedicated factories");

    static const char* const expectedNames[] = {
        "nt_multi_seq", "Thorp", "Soma", "AE Seq", "Markov", "Ferro", "Quantum"
    };
    static const uint32_t expectedGuids[] = {
        NT_MULTICHAR('T', 'h', 'M', 's'),
        NT_MULTICHAR('N', 's', 'T', 'h'),
        NT_MULTICHAR('N', 's', 'S', 'o'),
        NT_MULTICHAR('N', 's', 'A', 'e'),
        NT_MULTICHAR('N', 's', 'M', 'k'),
        NT_MULTICHAR('N', 's', 'F', 'e'),
        NT_MULTICHAR('N', 's', 'Q', 'u'),
    };
    static const int engineParamCounts[] = { 32, 15, 5, 10, 8, 11, 10 };

    for (uint32_t i = 0; i < 7; ++i) {
        const _NT_factory* factory = reinterpret_cast<const _NT_factory*>(
            entry(kNT_selector_factoryInfo, i));
        CHECK(factory != nullptr, "factoryInfo returns a factory");
        if (!factory)
            continue;

        CHECK(strcmp(factory->name, expectedNames[i]) == 0,
            "factory name matches its engine");
        CHECK(factory->guid == expectedGuids[i],
            "factory GUID is stable and expected");
        CHECK(factory->numSpecifications == 1,
            "factory exposes the Channels specification");
        CHECK(factory->specifications != nullptr,
            "factory specification pointer is present");
        CHECK(factory->calculateRequirements != nullptr,
            "factory calculateRequirements callback is present");
        CHECK(factory->construct != nullptr,
            "factory construct callback is present");
        CHECK(factory->step != nullptr,
            "factory step callback is present");

        for (uint32_t other = 0; other < i; ++other) {
            const _NT_factory* prior = reinterpret_cast<const _NT_factory*>(
                entry(kNT_selector_factoryInfo, other));
            CHECK(factory->guid != prior->guid, "factory GUIDs are unique");
        }

        int32_t oneChannel[] = { 1 };
        _NT_algorithmRequirements one = {};
        factory->calculateRequirements(one, oneChannel);
        uint32_t expectedOne = i == 0
            ? 5 + 1 + 47
            : 5 + 15 + engineParamCounts[i];
        CHECK(one.numParameters == expectedOne,
            "one-channel parameter count matches factory layout");
        CHECK(one.sram > 0, "factory reserves SRAM for algorithm state");

        int32_t fourChannels[] = { 4 };
        _NT_algorithmRequirements four = {};
        factory->calculateRequirements(four, fourChannels);
        uint32_t expectedFour = i == 0
            ? 5 + 4 + 4 * 47
            : 5 + 4 * (15 + engineParamCounts[i]);
        CHECK(four.numParameters == expectedFour,
            "four-channel parameter count matches factory layout");

        uint8_t* sram = new uint8_t[one.sram];
        _NT_algorithmMemoryPtrs memory = { sram, nullptr, nullptr, nullptr };
        _NT_algorithm* algorithm = factory->construct(memory, one, oneChannel);
        CHECK(algorithm != nullptr, "factory constructs a one-channel algorithm");
        if (algorithm) {
            CHECK(algorithm->parameters != nullptr,
                "constructed algorithm exposes parameter definitions");
            CHECK(algorithm->parameterPages != nullptr,
                "constructed algorithm exposes parameter pages");
            uint32_t expectedPages = i == 0 ? 4 : 3;
            CHECK(algorithm->parameterPages->numPages == expectedPages,
                "factory page count matches legacy or dedicated layout");
            if (i == 0) {
                CHECK(strcmp(algorithm->parameterPages->pages[1].name, "Engines") == 0,
                    "legacy factory retains the Engines page");
            } else {
                const _NT_parameterPage& enginePage =
                    algorithm->parameterPages->pages[expectedPages - 1];
                CHECK(enginePage.numParams == static_cast<uint32_t>(engineParamCounts[i]),
                    "dedicated engine page has the exact parameter count");
                CHECK(strstr(enginePage.name, expectedNames[i]) != nullptr,
                    "dedicated engine page names its engine");
            }
        }
        delete[] sram;
    }

    CHECK(entry(kNT_selector_factoryInfo, 7) == 0,
        "factoryInfo rejects out-of-range indices");

    dlclose(handle);
    printf("%d tests, %d failures\n", tests, failures);
    return failures > 0 ? 1 : 0;
}
