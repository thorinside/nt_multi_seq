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

    // --- Seq Mix: per-channel pitch/gate/velocity inputs, combined and quantized ---
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

            CHECK(factory->numSpecifications == 1 && factory->specifications != nullptr,
                "Seq Mix has one specification");
            if (factory->specifications) {
                CHECK(strcmp(factory->specifications[0].name, "Channels") == 0, "specification is Channels");
                CHECK(factory->specifications[0].min == 1, "Channels minimum is 1");
                CHECK(factory->specifications[0].max == kMaxMixChannels, "Channels maximum is the compiled ceiling");
                CHECK(factory->specifications[0].def == 2, "Channels defaults to 2");
            }

            // Parameter count follows the specification.
            for (int32_t channels = 1; channels <= kMaxMixChannels; ++channels) {
                _NT_algorithmRequirements r = {};
                int32_t spec[1] = { channels };
                factory->calculateRequirements(r, spec);
                CHECK(r.numParameters == static_cast<uint32_t>(kNumMixSharedParams + kNumMixChannelParams * channels),
                    "Seq Mix parameter count = shared + 3 per channel");
                CHECK(r.sram == sizeof(NtSeqMix), "Seq Mix reserves exactly its state");
            }

            const int32_t spec[1] = { 3 };
            _NT_algorithmRequirements req = {};
            factory->calculateRequirements(req, spec);
            uint8_t* sram = new uint8_t[req.sram];
            _NT_algorithmMemoryPtrs memory = { sram, nullptr, nullptr, nullptr };
            _NT_algorithm* algorithm = factory->construct(memory, req, spec);
            CHECK(algorithm != nullptr, "Seq Mix constructs with three channels");
            if (algorithm) {
                const int numParams = kNumMixSharedParams + kNumMixChannelParams * 3;
                int16_t values[kNumMixSharedParams + kNumMixChannelParams * kMaxMixChannels] = {};
                for (int param = 0; param < numParams; ++param)
                    values[param] = algorithm->parameters[param].def;
                algorithm->v = values;
                algorithm->vIncludingCommon = values;

                // Per-channel inputs default to the sequencer default busses, stepping by three.
                CHECK(values[mixChannelParam(0, kMixChanGateIn)] == 14, "channel 1 gate defaults to bus 14");
                CHECK(values[mixChannelParam(0, kMixChanPitchIn)] == 15, "channel 1 pitch defaults to bus 15");
                CHECK(values[mixChannelParam(0, kMixChanVelIn)] == 16, "channel 1 velocity defaults to bus 16");
                CHECK(values[mixChannelParam(1, kMixChanGateIn)] == 17, "channel 2 gate defaults to bus 17");
                CHECK(values[mixChannelParam(2, kMixChanVelIn)] == 22, "channel 3 velocity defaults to bus 22");
                CHECK(strcmp(algorithm->parameters[mixChannelParam(1, kMixChanPitchIn)].name, "Ch 2 Pitch In") == 0,
                    "channel parameters carry their channel number");
                CHECK(values[kMixParamPitchOut] == 15, "pitch writes to bus 15 by default");
                CHECK(values[kMixParamGateOut] == 14, "gate writes to bus 14 by default");
                CHECK(values[kMixParamVelOut] == 16, "velocity writes to bus 16 by default");
                CHECK(values[kMixParamPitchOutMode] == 1 && values[kMixParamGateOutMode] == 1
                    && values[kMixParamVelOutMode] == 1, "outputs default to Replace");
                CHECK(values[kMixParamSampleHold] == 0, "S&H defaults to Off");

                // Pages: three shared pages plus one per channel, every parameter exactly once.
                {
                    int seen[kNumMixSharedParams + kNumMixChannelParams * kMaxMixChannels] = {};
                    const _NT_parameterPages* pages = algorithm->parameterPages;
                    CHECK(pages != nullptr && pages->numPages == 3 + 3, "Seq Mix has shared pages plus one per channel");
                    for (uint32_t pg = 0; pages && pg < pages->numPages; ++pg)
                        for (uint32_t i = 0; i < pages->pages[pg].numParams; ++i)
                            seen[pages->pages[pg].params[i]]++;
                    bool once = true;
                    for (int i = 0; i < numParams; ++i)
                        once = once && seen[i] == 1;
                    CHECK(once, "every Seq Mix parameter appears on exactly one page");
                    if (pages && pages->numPages >= 6)
                        CHECK(strcmp(pages->pages[5].name, "Ch 3") == 0, "channel pages are named by channel");
                }

                float busFrames[kNT_lastBus * 4] = {};
                auto setBus = [&](int bus, float value) {
                    for (int frame = 0; frame < 4; ++frame)
                        busFrames[(bus - 1) * 4 + frame] = value;
                };
                auto busIs = [&](int bus, float value) {
                    bool ok = true;
                    for (int frame = 0; frame < 4; ++frame)
                        ok = ok && busFrames[(bus - 1) * 4 + frame] == value;
                    return ok;
                };

                // Pitch: three channels at 0.6, 0.6, 1.2 V average to 0.8 V (no scale loaded).
                values[kMixParamMode] = kMixAverage;
                values[kMixParamScaleOn] = 0;
                setBus(15, 0.6f); setBus(18, 0.6f); setBus(21, 1.2f);
                factory->step(algorithm, busFrames, 1);
                CHECK(busIs(15, 0.8f), "average pitch of three channels is written in place");
                CHECK(busIs(18, 0.6f) && busIs(21, 1.2f), "other channel inputs are untouched");

                values[kMixParamMode] = kMixSum;
                values[kMixParamPitchOut] = 30;
                values[kMixParamPitchOutMode] = 0;
                setBus(15, 0.6f); setBus(30, 1.0f);
                factory->step(algorithm, busFrames, 1);
                CHECK(busIs(15, 0.6f), "channel 1 pitch input is untouched when writing elsewhere");
                CHECK(busIs(30, 3.4f), "sum mode adds the pitch sum onto the output bus");
                values[kMixParamPitchOut] = 15;
                values[kMixParamPitchOutMode] = 1;

                // A channel with Pitch In set to None contributes nothing.
                values[mixChannelParam(2, kMixChanPitchIn)] = 0;
                setBus(15, 0.6f);
                factory->step(algorithm, busFrames, 1);
                CHECK(busIs(15, 1.2f), "None pitch input drops out of the sum");
                values[mixChannelParam(2, kMixChanPitchIn)] = 21;

                // Gate: OR/AND/XOR over the three gate busses 14, 17, 20.
                setBus(14, 5.0f); setBus(17, 0.0f); setBus(20, 0.0f);
                factory->step(algorithm, busFrames, 1);
                CHECK(busIs(14, 5.0f), "OR with one gate high is high");
                values[kMixParamGateOp] = kMixGateAnd;
                setBus(14, 5.0f);
                factory->step(algorithm, busFrames, 1);
                CHECK(busIs(14, 0.0f), "AND with one of three high is low");
                setBus(14, 5.0f); setBus(17, 5.0f); setBus(20, 5.0f);
                factory->step(algorithm, busFrames, 1);
                CHECK(busIs(14, 5.0f), "AND with all three high is high");
                values[kMixParamGateOp] = kMixGateXor;
                values[kMixParamGateOut] = 31;
                values[kMixParamGateOutMode] = 0;
                setBus(14, 5.0f); setBus(17, 5.0f); setBus(20, 0.0f); setBus(31, 1.0f);
                factory->step(algorithm, busFrames, 1);
                CHECK(busIs(14, 5.0f), "gate inputs are untouched in Add mode");
                CHECK(busIs(31, 1.0f), "XOR with two high adds nothing");
                setBus(20, 5.0f); setBus(31, 1.0f);
                factory->step(algorithm, busFrames, 1);
                CHECK(busIs(31, 6.0f), "XOR with three high adds 5 V onto the output bus");
                values[kMixParamGateOut] = 14;
                values[kMixParamGateOutMode] = 1;
                values[kMixParamGateOp] = kMixGateOr;

                // Velocity: busses 16, 19, 22.
                CHECK(values[kMixParamVelMode] == kMixVelSum, "velocity defaults to Sum");
                CHECK(values[kMixParamVelScale] == 100, "velocity scale defaults to 100%");
                setBus(16, 1.0f); setBus(19, 2.0f); setBus(22, 3.0f);
                factory->step(algorithm, busFrames, 1);
                CHECK(busIs(16, 6.0f), "sum velocity adds three channels in place");
                values[kMixParamVelMode] = kMixVelAverage;
                setBus(16, 1.0f);
                factory->step(algorithm, busFrames, 1);
                CHECK(busIs(16, 2.0f), "average velocity divides by channels");
                values[kMixParamVelMode] = kMixVelScale;
                values[kMixParamVelScale] = 50;
                setBus(16, 1.0f);
                factory->step(algorithm, busFrames, 1);
                CHECK(busIs(16, 3.0f), "scale mode applies the percent to the velocity sum");
                values[kMixParamVelMode] = kMixVelSum;
                values[kMixParamVelScale] = 100;

                // S&H: pitch and velocity update only on the combined gate's rising edge.
                values[kMixParamMode] = kMixSum;
                values[kMixParamSampleHold] = 0;
                setBus(14, 0.0f); setBus(17, 0.0f); setBus(20, 0.0f);
                setBus(15, 1.0f); setBus(18, 0.0f); setBus(21, 0.0f);
                setBus(16, 2.0f); setBus(19, 0.0f); setBus(22, 0.0f);
                factory->step(algorithm, busFrames, 1);   // prime held values: 1 V / 2 V
                values[kMixParamSampleHold] = 1;
                setBus(15, 2.0f); setBus(16, 4.0f);
                factory->step(algorithm, busFrames, 1);
                CHECK(busIs(15, 1.0f), "S&H holds pitch until a gate arrives");
                CHECK(busIs(16, 2.0f), "S&H holds velocity until a gate arrives");
                setBus(15, 2.0f); setBus(16, 4.0f);
                for (int frame = 0; frame < 4; ++frame)
                    busFrames[(17 - 1) * 4 + frame] = frame >= 2 ? 5.0f : 0.0f;  // channel 2 gate rises on frame 2
                factory->step(algorithm, busFrames, 1);
                CHECK(busFrames[(15 - 1) * 4 + 1] == 1.0f, "S&H still holds before the edge");
                CHECK(busFrames[(15 - 1) * 4 + 2] == 2.0f, "S&H samples pitch on the rising edge");
                CHECK(busFrames[(16 - 1) * 4 + 2] == 4.0f, "S&H samples velocity on the rising edge");
                CHECK(busFrames[(15 - 1) * 4 + 3] == 2.0f, "S&H keeps the sample while the gate is high");
                setBus(15, 3.0f); setBus(16, 1.0f); setBus(17, 5.0f);
                factory->step(algorithm, busFrames, 1);
                CHECK(busIs(15, 2.0f) && busIs(16, 4.0f), "S&H ignores input changes without a new edge");
                // All gate inputs None: S&H is bypassed and outputs track again.
                values[mixChannelParam(0, kMixChanGateIn)] = 0;
                values[mixChannelParam(1, kMixChanGateIn)] = 0;
                values[mixChannelParam(2, kMixChanGateIn)] = 0;
                setBus(15, 3.0f); setBus(16, 1.0f);
                factory->step(algorithm, busFrames, 1);
                CHECK(busIs(15, 3.0f) && busIs(16, 1.0f), "S&H is bypassed when no channel has a gate input");
            }
            delete[] sram;

            // With no specifications supplied the default channel count is used.
            _NT_algorithmRequirements dflt = {};
            factory->calculateRequirements(dflt, nullptr);
            CHECK(dflt.numParameters == static_cast<uint32_t>(kNumMixSharedParams + kNumMixChannelParams * 2),
                "null specifications fall back to two channels");
        }
    }

    CHECK(entry(kNT_selector_factoryInfo, 7) == 0,
        "factoryInfo rejects out-of-range indices");

    dlclose(handle);
    printf("%d tests, %d failures\n", tests, failures);
    return failures > 0 ? 1 : 0;
}
