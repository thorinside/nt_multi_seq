#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../nt_seq.h"
#include "../engines/ThorpEngine.h"
#include "../engines/SomaEngine.h"
#include "../engines/SiftEngine.h"
#include "../engines/SeqMarkovEngine.h"
#include "../engines/FerromagneticEngine.h"
#include "../engines/QuantumEngine.h"
#include "../mix/nt_seq_mix.h"

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

// Minimal runtime symbols needed to load the plugin and construct algorithms.
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

bool NT_isSdCardMounted()
{
    return false;
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
        "plugin exposes six fixed-engine factories plus Seq Mix");

    static const char* const expectedNames[] = {
        "Seq Thorp", "Seq Soma", "Seq Sift", "Seq Markov", "Seq Ferro", "Seq Quantum"
    };
    static const uint32_t expectedGuids[] = {
        NT_MULTICHAR('N', 's', 'T', 'h'),
        NT_MULTICHAR('N', 's', 'S', 'o'),
        NT_MULTICHAR('N', 's', 'A', 'e'),
        NT_MULTICHAR('N', 's', 'M', 'k'),
        NT_MULTICHAR('N', 's', 'F', 'e'),
        NT_MULTICHAR('N', 's', 'Q', 'u'),
    };
    static const int engineParamCounts[] = { 15, 5, 10, 8, 11, 10 };
    static const size_t engineSizes[] = {
        sizeof(ThorpEngine),
        sizeof(SomaEngine),
        sizeof(SiftEngine),
        sizeof(SeqMarkovEngine),
        sizeof(FerromagneticEngine),
        sizeof(QuantumEngine),
    };

    for (uint32_t i = 0; i < 6; ++i) {
        const _NT_factory* factory = reinterpret_cast<const _NT_factory*>(
            entry(kNT_selector_factoryInfo, i));
        CHECK(factory != nullptr, "factoryInfo returns a factory");
        if (!factory)
            continue;

        CHECK(strcmp(factory->name, expectedNames[i]) == 0,
            "factory name matches its engine");
        CHECK(factory->guid == expectedGuids[i],
            "factory GUID is stable and expected");
        CHECK(factory->numSpecifications == 0,
            "factory has no specifications");
        CHECK(factory->specifications == nullptr,
            "factory has no specification array");
        CHECK(factory->calculateRequirements != nullptr,
            "factory calculateRequirements callback is present");
        CHECK(factory->construct != nullptr,
            "factory construct callback is present");
        CHECK(factory->step != nullptr,
            "factory step callback is present");
        CHECK(factory->hasCustomUi != nullptr,
            "factory custom UI declaration is present");

        for (uint32_t other = 0; other < i; ++other) {
            const _NT_factory* prior = reinterpret_cast<const _NT_factory*>(
                entry(kNT_selector_factoryInfo, other));
            CHECK(factory->guid != prior->guid, "factory GUIDs are unique");
        }

        _NT_algorithmRequirements one = {};
        factory->calculateRequirements(one, nullptr);
        uint32_t expectedOne = 5 + 15 + engineParamCounts[i];
        CHECK(one.numParameters == expectedOne,
            "parameter count matches fixed engine layout");
        CHECK(one.sram > 0, "factory reserves SRAM for algorithm state");
        CHECK(one.sram == sizeof(NtSeq) + 7 + engineSizes[i],
            "factory reserves exactly its concrete engine storage");

        uint8_t* sram = new uint8_t[one.sram];
        _NT_algorithmMemoryPtrs memory = { sram, nullptr, nullptr, nullptr };
        _NT_algorithm* algorithm = factory->construct(memory, one, nullptr);
        CHECK(algorithm != nullptr, "factory constructs a fixed engine algorithm");
        if (algorithm) {
            CHECK(algorithm->parameters != nullptr,
                "constructed algorithm exposes parameter definitions");
            CHECK(algorithm->parameterPages != nullptr,
                "constructed algorithm exposes parameter pages");
            CHECK(factory->hasCustomUi(algorithm) != 0,
                "engine exposes its direct hardware controls");
            CHECK(algorithm->parameterPages->numPages == 3,
                "factory exposes exactly three static pages");
            CHECK(strcmp(algorithm->parameterPages->pages[0].name, "Global") == 0,
                "first page is Global");
            CHECK(strcmp(algorithm->parameterPages->pages[1].name, "Routing") == 0,
                "second page is Routing");
            const _NT_parameterPage& enginePage = algorithm->parameterPages->pages[2];
            CHECK(enginePage.numParams == static_cast<uint32_t>(engineParamCounts[i]),
                "engine page has the exact parameter count");
            CHECK(strcmp(enginePage.name, expectedNames[i]) == 0,
                "engine page names its engine");

            uint32_t expectedIndex = 0;
            for (uint32_t page = 0; page < 3; ++page) {
                const _NT_parameterPage& definition = algorithm->parameterPages->pages[page];
                for (uint32_t param = 0; param < definition.numParams; ++param) {
                    CHECK(definition.params[param] == expectedIndex,
                        "static pages use contiguous parameter order");
                    ++expectedIndex;
                }
            }
            CHECK(expectedIndex == one.numParameters,
                "static pages cover every parameter exactly once");

            if (i == 0) {
                int16_t values[kMaxTotalParams] = {};
                for (uint32_t param = 0; param < one.numParameters; ++param)
                    values[param] = algorithm->parameters[param].def;
                algorithm->v = values;
                algorithm->vIncludingCommon = values;

                NtSeq* ntSeq = static_cast<NtSeq*>(algorithm);
                ntSeq->seq.cachedPitch = 1.25f;
                ntSeq->seq.cachedGate = 5.0f;
                ntSeq->seq.cachedVelocity = 3.0f;

                int routingBase = ntSeq->seq.paramBase;
                values[routingBase + kRoutePitchOutMode] = 1;
                values[routingBase + kRouteGateOutMode] = 1;
                values[routingBase + kRouteVelocityOutMode] = 1;

                float busFrames[kNT_lastBus * 4] = {};
                factory->step(algorithm, busFrames, 1);
                for (int frame = 0; frame < 4; ++frame) {
                    CHECK(busFrames[(15 - 1) * 4 + frame] == 1.25f,
                        "replace mode writes pitch across all four frames");
                    CHECK(busFrames[(14 - 1) * 4 + frame] == 5.0f,
                        "replace mode writes gate across all four frames");
                    CHECK(busFrames[(16 - 1) * 4 + frame] == 3.0f,
                        "replace mode writes velocity across all four frames");
                }

                values[routingBase + kRoutePitchOutMode] = 0;
                values[routingBase + kRouteGateOutMode] = 0;
                values[routingBase + kRouteVelocityOutMode] = 0;
                for (int frame = 0; frame < 4; ++frame) {
                    busFrames[(15 - 1) * 4 + frame] = 2.0f;
                    busFrames[(14 - 1) * 4 + frame] = 2.0f;
                    busFrames[(16 - 1) * 4 + frame] = 2.0f;
                }
                factory->step(algorithm, busFrames, 1);
                for (int frame = 0; frame < 4; ++frame) {
                    CHECK(busFrames[(15 - 1) * 4 + frame] == 3.25f,
                        "add mode sums pitch across all four frames");
                    CHECK(busFrames[(14 - 1) * 4 + frame] == 7.0f,
                        "add mode sums gate across all four frames");
                    CHECK(busFrames[(16 - 1) * 4 + frame] == 5.0f,
                        "add mode sums velocity across all four frames");
                }
            }
        }
        delete[] sram;
    }

    // --- Seq Mix: sums or averages a shared pitch bus and quantizes it ---
    {
        const _NT_factory* factory = reinterpret_cast<const _NT_factory*>(
            entry(kNT_selector_factoryInfo, 6));
        CHECK(factory != nullptr, "factoryInfo returns the Seq Mix factory");
        if (factory) {
            CHECK(strcmp(factory->name, "Seq Mix") == 0, "seventh factory is Seq Mix");
            CHECK(factory->guid == NT_MULTICHAR('N', 's', 'M', 'x'), "Seq Mix GUID is NsMx");
            for (uint32_t other = 0; other < 6; ++other) {
                const _NT_factory* prior = reinterpret_cast<const _NT_factory*>(
                    entry(kNT_selector_factoryInfo, other));
                CHECK(factory->guid != prior->guid, "Seq Mix GUID is unique");
            }

            _NT_algorithmRequirements req = {};
            factory->calculateRequirements(req, nullptr);
            CHECK(req.numParameters == kNumMixParams, "Seq Mix parameter count");
            CHECK(req.sram == sizeof(NtSeqMix), "Seq Mix reserves exactly its state");

            uint8_t* sram = new uint8_t[req.sram];
            _NT_algorithmMemoryPtrs memory = { sram, nullptr, nullptr, nullptr };
            _NT_algorithm* algorithm = factory->construct(memory, req, nullptr);
            CHECK(algorithm != nullptr, "Seq Mix constructs");
            if (algorithm) {
                int16_t values[kNumMixParams] = {};
                for (uint32_t param = 0; param < req.numParameters; ++param)
                    values[param] = algorithm->parameters[param].def;
                algorithm->v = values;
                algorithm->vIncludingCommon = values;

                CHECK(values[kMixParamPitchIn] == 15, "Seq Mix reads the default sequencer pitch bus");
                CHECK(values[kMixParamPitchOut] == 15, "Seq Mix writes back to the same bus by default");
                CHECK(values[kMixParamPitchOutMode] == 1, "Seq Mix defaults to Replace so the sum is consumed");

                // Two sequencers added 0.6 V each onto bus 15; average and quantize
                // without a scale loaded (no SD card) leaves 0.6 V untouched.
                values[kMixParamMode] = kMixAverage;
                values[kMixParamSources] = 2;
                values[kMixParamScaleOn] = 0;
                float busFrames[kNT_lastBus * 4] = {};
                for (int frame = 0; frame < 4; ++frame)
                    busFrames[(15 - 1) * 4 + frame] = 1.2f;
                factory->step(algorithm, busFrames, 1);
                for (int frame = 0; frame < 4; ++frame)
                    CHECK(busFrames[(15 - 1) * 4 + frame] == 0.6f,
                        "average mode halves a two-source sum in place");

                // Sum mode to a different bus in Add mode leaves the input bus alone.
                values[kMixParamMode] = kMixSum;
                values[kMixParamPitchOut] = 16;
                values[kMixParamPitchOutMode] = 0;
                for (int frame = 0; frame < 4; ++frame) {
                    busFrames[(15 - 1) * 4 + frame] = 1.2f;
                    busFrames[(16 - 1) * 4 + frame] = 1.0f;
                }
                factory->step(algorithm, busFrames, 1);
                for (int frame = 0; frame < 4; ++frame) {
                    CHECK(busFrames[(15 - 1) * 4 + frame] == 1.2f, "input bus is untouched");
                    CHECK(busFrames[(16 - 1) * 4 + frame] == 2.2f, "add mode sums onto the output bus");
                }

                // --- Gate stage: boolean op over the summed gate bus ---
                CHECK(values[kMixParamGateIn] == 14, "Seq Mix reads the default sequencer gate bus");
                CHECK(values[kMixParamGateOut] == 14, "Seq Mix writes the gate back to the same bus");
                CHECK(values[kMixParamGateOutMode] == 1, "gate defaults to Replace");
                CHECK(values[kMixParamGateOp] == kMixGateOr, "gate op defaults to OR");
                values[kMixParamPitchOut] = 0;  // pitch stage off; gate must still run
                for (int frame = 0; frame < 4; ++frame)
                    busFrames[(14 - 1) * 4 + frame] = 10.0f;  // two gates high
                factory->step(algorithm, busFrames, 1);
                for (int frame = 0; frame < 4; ++frame)
                    CHECK(busFrames[(14 - 1) * 4 + frame] == 5.0f, "OR of two high gates is one 5 V gate");

                values[kMixParamGateOp] = kMixGateAnd;
                for (int frame = 0; frame < 4; ++frame)
                    busFrames[(14 - 1) * 4 + frame] = 5.0f;  // one of two high
                factory->step(algorithm, busFrames, 1);
                for (int frame = 0; frame < 4; ++frame)
                    CHECK(busFrames[(14 - 1) * 4 + frame] == 0.0f, "AND with one of two gates high is low");

                values[kMixParamGateOp] = kMixGateXor;
                values[kMixParamGateOut] = 17;
                values[kMixParamGateOutMode] = 0;
                for (int frame = 0; frame < 4; ++frame) {
                    busFrames[(14 - 1) * 4 + frame] = 5.0f;
                    busFrames[(17 - 1) * 4 + frame] = 1.0f;
                }
                factory->step(algorithm, busFrames, 1);
                for (int frame = 0; frame < 4; ++frame) {
                    CHECK(busFrames[(14 - 1) * 4 + frame] == 5.0f, "gate input bus is untouched in Add mode");
                    CHECK(busFrames[(17 - 1) * 4 + frame] == 6.0f, "XOR gate adds onto the output bus");
                }

                // --- Velocity stage ---
                CHECK(values[kMixParamVelIn] == 16, "Seq Mix reads the default sequencer velocity bus");
                CHECK(values[kMixParamVelOut] == 16, "Seq Mix writes velocity back to the same bus");
                CHECK(values[kMixParamVelOutMode] == 1, "velocity defaults to Replace");
                CHECK(values[kMixParamVelMode] == kMixVelSum, "velocity defaults to Sum");
                CHECK(values[kMixParamVelScale] == 100, "velocity scale defaults to 100%");
                values[kMixParamGateOut] = 0;  // gate stage off; velocity must still run
                values[kMixParamVelMode] = kMixVelAverage;
                for (int frame = 0; frame < 4; ++frame)
                    busFrames[(16 - 1) * 4 + frame] = 6.0f;
                factory->step(algorithm, busFrames, 1);
                for (int frame = 0; frame < 4; ++frame)
                    CHECK(busFrames[(16 - 1) * 4 + frame] == 3.0f, "average velocity halves a two-source sum");

                values[kMixParamVelMode] = kMixVelScale;
                values[kMixParamVelScale] = 50;
                for (int frame = 0; frame < 4; ++frame)
                    busFrames[(16 - 1) * 4 + frame] = 6.0f;
                factory->step(algorithm, busFrames, 1);
                for (int frame = 0; frame < 4; ++frame)
                    CHECK(busFrames[(16 - 1) * 4 + frame] == 3.0f, "scale mode applies the percent to the velocity sum");

                // --- Pitch and velocity sample and hold, triggered by the mixed gate ---
                CHECK(values[kMixParamSampleHold] == 0, "S&H defaults to Off");
                values[kMixParamSampleHold] = 1;
                values[kMixParamPitchOut] = 15;
                values[kMixParamPitchOutMode] = 1;
                values[kMixParamMode] = kMixSum;
                values[kMixParamGateOut] = 14;
                values[kMixParamGateOutMode] = 1;
                values[kMixParamGateOp] = kMixGateOr;
                values[kMixParamVelOut] = 16;
                values[kMixParamVelOutMode] = 1;
                values[kMixParamVelMode] = kMixVelSum;
                // Prime known outputs with S&H off and the gate low.
                values[kMixParamSampleHold] = 0;
                for (int frame = 0; frame < 4; ++frame) {
                    busFrames[(15 - 1) * 4 + frame] = 1.0f;
                    busFrames[(16 - 1) * 4 + frame] = 2.0f;
                    busFrames[(14 - 1) * 4 + frame] = 0.0f;
                }
                factory->step(algorithm, busFrames, 1);
                values[kMixParamSampleHold] = 1;
                // Gate low: pitch and velocity busses change but the held outputs stay put.
                for (int frame = 0; frame < 4; ++frame) {
                    busFrames[(15 - 1) * 4 + frame] = 2.0f;
                    busFrames[(16 - 1) * 4 + frame] = 4.0f;
                    busFrames[(14 - 1) * 4 + frame] = 0.0f;
                }
                factory->step(algorithm, busFrames, 1);
                for (int frame = 0; frame < 4; ++frame) {
                    CHECK(busFrames[(15 - 1) * 4 + frame] == 1.0f, "S&H holds pitch until a gate arrives");
                    CHECK(busFrames[(16 - 1) * 4 + frame] == 2.0f, "S&H holds velocity until a gate arrives");
                }
                // Gate rises on frame 2: frames 0-1 hold, frames 2-3 carry the sample.
                for (int frame = 0; frame < 4; ++frame) {
                    busFrames[(15 - 1) * 4 + frame] = 2.0f;
                    busFrames[(16 - 1) * 4 + frame] = 4.0f;
                    busFrames[(14 - 1) * 4 + frame] = frame >= 2 ? 5.0f : 0.0f;
                }
                factory->step(algorithm, busFrames, 1);
                CHECK(busFrames[(15 - 1) * 4 + 1] == 1.0f, "S&H still holds before the edge");
                CHECK(busFrames[(15 - 1) * 4 + 2] == 2.0f, "S&H samples pitch on the gate's rising edge");
                CHECK(busFrames[(16 - 1) * 4 + 2] == 4.0f, "S&H samples velocity on the gate's rising edge");
                CHECK(busFrames[(15 - 1) * 4 + 3] == 2.0f, "S&H keeps the sample while the gate is high");
                // Gate stays high and the inputs move: no new sample.
                for (int frame = 0; frame < 4; ++frame) {
                    busFrames[(15 - 1) * 4 + frame] = 3.0f;
                    busFrames[(16 - 1) * 4 + frame] = 1.0f;
                    busFrames[(14 - 1) * 4 + frame] = 5.0f;
                }
                factory->step(algorithm, busFrames, 1);
                for (int frame = 0; frame < 4; ++frame) {
                    CHECK(busFrames[(15 - 1) * 4 + frame] == 2.0f, "S&H ignores pitch changes without a new edge");
                    CHECK(busFrames[(16 - 1) * 4 + frame] == 4.0f, "S&H ignores velocity changes without a new edge");
                }
                // With no gate input, S&H is bypassed and pitch tracks again.
                values[kMixParamGateIn] = 0;
                for (int frame = 0; frame < 4; ++frame)
                    busFrames[(15 - 1) * 4 + frame] = 3.0f;
                factory->step(algorithm, busFrames, 1);
                for (int frame = 0; frame < 4; ++frame)
                    CHECK(busFrames[(15 - 1) * 4 + frame] == 3.0f, "S&H is bypassed when Gate In is None");
                values[kMixParamGateIn] = 14;
                values[kMixParamSampleHold] = 0;

                // Pages cover every parameter exactly once.
                {
                    int seen[kNumMixParams] = {};
                    const _NT_parameterPages* pages = algorithm->parameterPages;
                    CHECK(pages != nullptr && pages->numPages == 3, "Seq Mix has Pitch, Gate, and Velocity pages");
                    for (uint32_t pg = 0; pages && pg < pages->numPages; ++pg)
                        for (uint32_t i = 0; i < pages->pages[pg].numParams; ++i)
                            seen[pages->pages[pg].params[i]]++;
                    bool once = true;
                    for (int i = 0; i < kNumMixParams; ++i)
                        once = once && seen[i] == 1;
                    CHECK(once, "every Seq Mix parameter appears on exactly one page");
                }
            }
            delete[] sram;
        }
    }

    CHECK(entry(kNT_selector_factoryInfo, 7) == 0,
        "factoryInfo rejects out-of-range indices");

    dlclose(handle);
    printf("%d tests, %d failures\n", tests, failures);
    return failures > 0 ? 1 : 0;
}
