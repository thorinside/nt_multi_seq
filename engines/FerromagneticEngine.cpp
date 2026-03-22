#include "FerromagneticEngine.h"
#include "../scale/ScaleQuantizer.h"
#include <string.h>

static inline int clampInt(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// Enum string arrays
static const char* const roleStrings[] = { "Melody", "Loop Trig", "Rec Gate", nullptr };
static const char* const harmonyModeStrings[] = { "Structured", "Generative", nullptr };
static const char* const voicingStrings[] = { "Triads", "7ths", "Stack 5ths", "Octaves", "Unison", nullptr };
static const char* const completionStrings[] = { "Hold", "Decay+Reb", "Refresh", "New Inv", nullptr };

// Voicing interval table: scale degrees above layer 0's note per layer
static const int8_t kVoicingIntervals[5][8] = {
    { 0, 2, 4, 7, 2, 4, 7, 9 },       // Triads
    { 0, 2, 4, 6, 2, 4, 6, 9 },       // 7ths
    { 0, 4, 8, 12, 4, 8, 12, 16 },    // Stacked 5ths
    { 0, 7, 14, 21, 7, 14, 21, 28 },  // Octaves
    { 0, 0, 0, 0, 0, 0, 0, 0 },       // Unison
};

FerromagneticEngine::FerromagneticEngine()
    : role_(kRoleMelody)
    , loopSteps_(16)
    , maxLayers_(4)
    , harmonyMode_(kHarmonyStructured)
    , voicing_(kVoicingTriads)
    , completion_(kCompletionHold)
    , noteDensity_(80)
    , gateLength_(75)
    , velocity_(100)
    , octaveSpread_(0)
    , refreshRate_(4)
    , currentTick_(0)
    , currentLayer_(0)
    , allLayersComplete_(false)
    , silentLoops_(0)
    , refreshLayerIdx_(0)
    , inversionOffset_(0)
    , numDegrees_(7)
    , scaleLoaded_(false)
    , rngState_(12345)
{
    memset(layerNotes_, 0xFF, sizeof(layerNotes_));
    memset(layerGates_, 0, sizeof(layerGates_));
    for (int i = 0; i < 128; ++i)
        probabilities_[i] = 0.0f;
}

uint32_t FerromagneticEngine::rng()
{
    rngState_ ^= rngState_ << 13;
    rngState_ ^= rngState_ >> 17;
    rngState_ ^= rngState_ << 5;
    return rngState_;
}

float FerromagneticEngine::rngFloat()
{
    return (float)(rng() & 0x7FFFFFFF) / (float)0x7FFFFFFF;
}

void FerromagneticEngine::init(uint32_t sampleRate)
{
    rngState_ = sampleRate ^ 0xFE220CAFu;
    resetLoop();
}

void FerromagneticEngine::reset()
{
    resetLoop();
}

void FerromagneticEngine::resetLoop()
{
    currentTick_ = 0;
    currentLayer_ = 0;
    allLayersComplete_ = false;
    silentLoops_ = 0;
    refreshLayerIdx_ = 0;
    memset(layerNotes_, 0xFF, sizeof(layerNotes_));
    memset(layerGates_, 0, sizeof(layerGates_));
}

void FerromagneticEngine::computeProbabilities(const ScaleQuantizer* scale)
{
    numDegrees_ = scale && scale->isLoaded() ? (int)scale->numNotes() : 7;
    if (numDegrees_ <= 0) numDegrees_ = 7;
    if (numDegrees_ > 128) numDegrees_ = 128;

    if (scale && scale->isLoaded()) {
        scale->computeNoteWeights(probabilities_, numDegrees_,
            (ScaleQuantizer::WeightMode)weightMode_);
    } else {
        for (int i = 0; i < numDegrees_; ++i)
            probabilities_[i] = 1.0f;
    }

    // Normalize to probabilities
    float total = 0.0f;
    for (int i = 0; i < numDegrees_; ++i) total += probabilities_[i];
    if (total > 0.0f) {
        for (int i = 0; i < numDegrees_; ++i) probabilities_[i] /= total;
    }
}

int FerromagneticEngine::weightedPick(int numDegrees)
{
    float r = rngFloat();
    float accum = 0.0f;
    for (int i = 0; i < numDegrees; ++i) {
        accum += probabilities_[i];
        if (r <= accum)
            return i;
    }
    return numDegrees - 1;
}

void FerromagneticEngine::generateLayerZeroNote(int tick, const ScaleQuantizer* scale)
{
    // Density check: 0 always rests, 100 always plays
    if (noteDensity_ <= 0 || rngFloat() * 100.0f > (float)noteDensity_) {
        layerNotes_[0][tick] = 0xFF;
        setGateBit(0, tick, false);
        return;
    }

    computeProbabilities(scale);
    int offset = (numDegrees_ > 0) ? (inversionOffset_ % numDegrees_) : 0;
    int degree = weightedPick(numDegrees_) + offset;
    if (degree > 254) degree = 254;
    layerNotes_[0][tick] = (uint8_t)degree;
    setGateBit(0, tick, true);
}

void FerromagneticEngine::generateStructuredNote(int layer, int tick)
{
    if (layerNotes_[0][tick] == 0xFF) {
        layerNotes_[layer][tick] = 0xFF;
        setGateBit(layer, tick, false);
        return;
    }

    int baseDeg = layerNotes_[0][tick];
    int interval = kVoicingIntervals[voicing_][layer];
    int degree = baseDeg + interval;
    if (degree > 254) degree = 254;

    layerNotes_[layer][tick] = (uint8_t)degree;
    setGateBit(layer, tick, true);
}

void FerromagneticEngine::generateGenerativeNote(int layer, int tick, const ScaleQuantizer* scale)
{
    if (layerNotes_[0][tick] == 0xFF) {
        layerNotes_[layer][tick] = 0xFF;
        setGateBit(layer, tick, false);
        return;
    }

    // Build modified probability table: zero out already-used degrees
    computeProbabilities(scale);
    float modProb[128];
    for (int i = 0; i < numDegrees_; ++i)
        modProb[i] = probabilities_[i];

    for (int L = 0; L < layer; ++L) {
        int usedDeg = layerNotes_[L][tick];
        if (usedDeg != 0xFF) {
            int baseDeg = usedDeg % numDegrees_;
            if (baseDeg >= 0 && baseDeg < numDegrees_)
                modProb[baseDeg] = 0.0f;
        }
    }

    // Boost consonant intervals (3rds and 5ths above layer 0's degree)
    int rootDeg = layerNotes_[0][tick] % numDegrees_;
    int third = (rootDeg + 2) % numDegrees_;
    int fifth = (rootDeg + 4) % numDegrees_;
    modProb[third] *= 2.0f;
    modProb[fifth] *= 2.0f;

    // Normalize
    float total = 0.0f;
    for (int i = 0; i < numDegrees_; ++i) total += modProb[i];

    if (total <= 0.0f) {
        // All degrees used
        layerNotes_[layer][tick] = 0xFF;
        setGateBit(layer, tick, false);
        return;
    }

    // Weighted pick from modified table
    float r = rngFloat();
    float accum = 0.0f;
    int picked = numDegrees_ - 1;
    for (int i = 0; i < numDegrees_; ++i) {
        accum += modProb[i] / total;
        if (r <= accum) {
            picked = i;
            break;
        }
    }

    // Apply octave spread
    int degree = picked;
    if (octaveSpread_ > 0) {
        int octDisplacement = (int)(rng() % (uint32_t)(octaveSpread_ + 1));
        degree += octDisplacement * numDegrees_;
    }
    if (degree > 254) degree = picked;

    layerNotes_[layer][tick] = (uint8_t)degree;
    setGateBit(layer, tick, true);
}

void FerromagneticEngine::outputNote(EngineOutput& out, int layer, int tick, const ScaleQuantizer* scale) const
{
    if (!getGateBit(layer, tick) || layerNotes_[layer][tick] == 0xFF) {
        out.gate = 0.0f;
        return;
    }

    int degree = layerNotes_[layer][tick];

    if (scale && scale->isLoaded()) {
        out.pitch = scale->quantize(degree, 0, 0);
        out.midiNote = scale->scaleDegreeToMidi(degree, 5, 0);
    } else {
        out.pitch = (float)degree / 12.0f;
        int midi = 60 + degree;
        if (midi > 127) midi = 127;
        out.midiNote = (uint8_t)midi;
    }

    out.gate = 5.0f;
    out.velocity = (float)velocity_ * 0.05f;
}

void FerromagneticEngine::handleLoopWrap()
{
    if (!allLayersComplete_) {
        if (currentLayer_ + 1 >= maxLayers_) {
            // All layers built
            if (completion_ == kCompletionNewInversion) {
                inversionOffset_ = (inversionOffset_ + 1) % numDegrees_;
                resetLoop();
                return;
            }
            allLayersComplete_ = true;
            silentLoops_ = 0;
            refreshLayerIdx_ = 0;
        } else {
            currentLayer_++;
        }
    } else {
        switch (completion_) {
        case kCompletionHold:
            break;
        case kCompletionDecayRebuild:
            silentLoops_++;
            if (silentLoops_ >= refreshRate_) {
                resetLoop();
            }
            break;
        case kCompletionRefresh:
            silentLoops_++;
            if (silentLoops_ >= refreshRate_) {
                silentLoops_ = 0;
                refreshLayerIdx_ = (refreshLayerIdx_ + 1) % maxLayers_;
            }
            break;
        case kCompletionNewInversion:
            // User switched to NewInversion while already complete
            inversionOffset_ = (inversionOffset_ + 1) % numDegrees_;
            resetLoop();
            break;
        }
    }
}

EngineOutput FerromagneticEngine::clockTick(const ScaleQuantizer* scale)
{
    EngineOutput out = { 0.0f, 0.0f, 0.0f, 60 };

    // Reinitialize when scale first becomes available or note count changes
    // Only the Melody role uses the scale for note generation.
    if (role_ == kRoleMelody && scale && scale->isLoaded()) {
        if (!scaleLoaded_ || (int)scale->numNotes() != numDegrees_) {
            computeProbabilities(scale);
            scaleLoaded_ = true;
            resetLoop();
        }
    }

    // --- Loop Trigger role ---
    if (role_ == kRoleLoopTrig) {
        currentTick_ = (currentTick_ + 1) % loopSteps_;
        out.gate = (currentTick_ == 0) ? 5.0f : 0.0f;
        out.velocity = 5.0f;
        return out;
    }

    // --- Record Gate role ---
    if (role_ == kRoleRecGate) {
        currentTick_ = (currentTick_ + 1) % loopSteps_;

        if (!allLayersComplete_) {
            out.gate = 5.0f;
        } else {
            switch (completion_) {
            case kCompletionHold:
                out.gate = 0.0f;
                break;
            case kCompletionDecayRebuild:
                out.gate = 0.0f;
                break;
            case kCompletionRefresh:
                out.gate = 5.0f;
                break;
            case kCompletionNewInversion:
                out.gate = 5.0f;
                break;
            }
        }

        if (currentTick_ == 0) handleLoopWrap();

        return out;
    }

    // --- Melody role ---
    currentTick_ = (currentTick_ + 1) % loopSteps_;

    if (allLayersComplete_) {
        switch (completion_) {
        case kCompletionHold:
        case kCompletionDecayRebuild:
            // Silence
            break;
        case kCompletionRefresh:
            outputNote(out, refreshLayerIdx_, currentTick_, scale);
            break;
        case kCompletionNewInversion:
            // Should not reach here (resetLoop called on completion)
            break;
        }
    } else {
        // Generate note for this tick at current layer
        if (currentLayer_ == 0) {
            generateLayerZeroNote(currentTick_, scale);
        } else if (harmonyMode_ == kHarmonyStructured) {
            generateStructuredNote(currentLayer_, currentTick_);
        } else {
            generateGenerativeNote(currentLayer_, currentTick_, scale);
        }
        outputNote(out, currentLayer_, currentTick_, scale);
    }

    if (currentTick_ == 0) {
        handleLoopWrap();
    }

    return out;
}

void FerromagneticEngine::parameterChanged(int localIndex, int16_t value)
{
    switch (localIndex) {
    case kFerroRole:
        role_ = clampInt(value, 0, 2);
        break;
    case kFerroLoopSteps:
        {
            int newSteps = clampInt(value, 2, kMaxSteps);
            if (newSteps != loopSteps_) {
                loopSteps_ = newSteps;
                resetLoop();
            }
        }
        break;
    case kFerroMaxLayers:
        {
            int newMax = clampInt(value, 1, kMaxLayers);
            maxLayers_ = newMax;
            if (currentLayer_ >= maxLayers_) {
                resetLoop();
            } else if (refreshLayerIdx_ >= maxLayers_) {
                refreshLayerIdx_ = 0;
            }
        }
        break;
    case kFerroHarmonyMode:
        harmonyMode_ = clampInt(value, 0, 1);
        break;
    case kFerroVoicing:
        voicing_ = clampInt(value, 0, kNumVoicings - 1);
        break;
    case kFerroCompletion:
        completion_ = clampInt(value, 0, kNumCompletions - 1);
        break;
    case kFerroNoteDensity:
        noteDensity_ = clampInt(value, 0, 100);
        break;
    case kFerroGateLength:
        gateLength_ = clampInt(value, 10, 100);
        break;
    case kFerroVelocity:
        velocity_ = clampInt(value, 0, 100);
        break;
    case kFerroOctSpread:
        octaveSpread_ = clampInt(value, 0, 3);
        break;
    case kFerroRefreshRate:
        refreshRate_ = clampInt(value, 1, 8);
        break;
    }
}

int FerromagneticEngine::getParameterDefs(_NT_parameter* defs) const
{
    defs[kFerroRole]        = { .name = "Role",         .min = 0,  .max = 2,   .def = 0,  .unit = kNT_unitEnum,    .scaling = 0, .enumStrings = roleStrings };
    defs[kFerroLoopSteps]   = { .name = "Loop Steps",   .min = 2,  .max = 128, .def = 16, .unit = kNT_unitNone,    .scaling = 0, .enumStrings = nullptr };
    defs[kFerroMaxLayers]   = { .name = "Max Layers",   .min = 1,  .max = 8,   .def = 4,  .unit = kNT_unitNone,    .scaling = 0, .enumStrings = nullptr };
    defs[kFerroHarmonyMode] = { .name = "Harmony",      .min = 0,  .max = 1,   .def = 0,  .unit = kNT_unitEnum,    .scaling = 0, .enumStrings = harmonyModeStrings };
    defs[kFerroVoicing]     = { .name = "Voicing",      .min = 0,  .max = 4,   .def = 0,  .unit = kNT_unitEnum,    .scaling = 0, .enumStrings = voicingStrings };
    defs[kFerroCompletion]  = { .name = "Completion",   .min = 0,  .max = 3,   .def = 0,  .unit = kNT_unitEnum,    .scaling = 0, .enumStrings = completionStrings };
    defs[kFerroNoteDensity] = { .name = "Note Density", .min = 0,  .max = 100, .def = 80, .unit = kNT_unitPercent, .scaling = 0, .enumStrings = nullptr };
    defs[kFerroGateLength]  = { .name = "Gate Length",  .min = 10, .max = 100, .def = 75, .unit = kNT_unitPercent, .scaling = 0, .enumStrings = nullptr };
    defs[kFerroVelocity]    = { .name = "Velocity",     .min = 0,  .max = 100, .def = 100,.unit = kNT_unitPercent, .scaling = 0, .enumStrings = nullptr };
    defs[kFerroOctSpread]   = { .name = "Oct Spread",   .min = 0,  .max = 3,   .def = 0,  .unit = kNT_unitNone,    .scaling = 0, .enumStrings = nullptr };
    defs[kFerroRefreshRate] = { .name = "Refresh Rate", .min = 1,  .max = 8,   .def = 4,  .unit = kNT_unitNone,    .scaling = 0, .enumStrings = nullptr };
    return kNumFerroParams;
}

int FerromagneticEngine::getPageDefs(_NT_parameterPage* page, uint8_t* indices, int baseParamIndex) const
{
    for (int i = 0; i < kNumFerroParams; ++i)
        indices[i] = (uint8_t)(baseParamIndex + i);
    page->name = "Ferro";
    page->numParams = kNumFerroParams;
    page->group = 0;
    page->unused[0] = 0;
    page->unused[1] = 0;
    page->params = indices;
    return kNumFerroParams;
}

int FerromagneticEngine::currentStep() const { return currentTick_; }
int FerromagneticEngine::sequenceLength() const { return loopSteps_; }

int FerromagneticEngine::getStatusText(char* buf, int maxLen) const
{
    if (maxLen <= 0) return 0;

    int len = 0;
    if (role_ == kRoleLoopTrig) {
        const char* s = "Trig:";
        while (*s && len < maxLen - 1) buf[len++] = *s++;
        if (len < maxLen - 4) len += fmtInt(buf + len, (int32_t)loopSteps_);
    } else if (role_ == kRoleRecGate) {
        const char* s = "RecG";
        while (*s && len < maxLen - 1) buf[len++] = *s++;
    } else {
        // "L2/4 Tri"
        if (len < maxLen - 1) buf[len++] = 'L';
        if (len < maxLen - 2) len += fmtInt(buf + len, (int32_t)(currentLayer_ + 1));
        if (len < maxLen - 1) buf[len++] = '/';
        if (len < maxLen - 2) len += fmtInt(buf + len, (int32_t)maxLayers_);
        if (len < maxLen - 1) buf[len++] = ' ';
        if (harmonyMode_ == kHarmonyStructured) {
            static const char* voicingAbbr[] = {"Tri", "7th", "St5", "Oct", "Uni"};
            const char* s = voicingAbbr[voicing_];
            while (*s && len < maxLen - 1) buf[len++] = *s++;
        } else {
            const char* s = "Gen";
            while (*s && len < maxLen - 1) buf[len++] = *s++;
        }
    }
    buf[len] = 0;
    return len;
}

void FerromagneticEngine::getFocusDetail(FocusDetail& detail) const
{
    FocusDetailLine& line1 = detail.lines[0];
    FocusDetailLine& line2 = detail.lines[1];
    line1.clear();
    line2.clear();

    if (role_ == kRoleLoopTrig) {
        line1.append("Loop Trigger  Steps:", 8);
        line1.appendInt(loopSteps_, 8);
        return;
    }

    if (role_ == kRoleRecGate) {
        line1.append("Record Gate  ", 8);
        line1.append(allLayersComplete_ ? "IDLE" : "REC", allLayersComplete_ ? 6 : 10);
        return;
    }

    // Melody role
    // Line 1: "L:2/4 Dens:80% Gate:75%"
    line1.append("L:", 8);
    line1.appendInt(currentLayer_ + 1, 8);
    line1.appendChar('/', 8);
    line1.appendInt(maxLayers_, 8);
    line1.append("  Dens:", 8);
    line1.appendInt(noteDensity_, 8);
    line1.appendChar('%', 8);
    line1.append("  Gate:", 8);
    line1.appendInt(gateLength_, 8);
    line1.appendChar('%', 8);

    // Line 2: "Struct Triads Hold"
    if (harmonyMode_ == kHarmonyStructured) {
        line2.append("Struct ", 6);
        line2.append(voicingStrings[voicing_], 6);
    } else {
        line2.append("Generative", 6);
    }
    line2.append("  ", 6);
    line2.append(completionStrings[completion_], 6);
    if (allLayersComplete_) {
        line2.append("  [DONE]", 10);
    }
}

void FerromagneticEngine::getFocusBarInfo(FocusBarInfo& info) const
{
    info.numBars = 2;

    // Bar 1: loop position with layer accumulation brightness
    FocusBar& bar1 = info.bars[0];
    int steps = loopSteps_;
    if (steps > kMaxBarSteps) steps = kMaxBarSteps;
    bar1.length = steps;
    bar1.playhead = currentTick_;

    int layerCount = allLayersComplete_ ? maxLayers_ : currentLayer_ + 1;
    for (int i = 0; i < steps; ++i) {
        int count = 0;
        for (int L = 0; L < layerCount; ++L) {
            if (getGateBit(L, i)) count++;
        }
        if (count == 0)
            bar1.levels[i] = 1;
        else
            bar1.levels[i] = (uint8_t)(2 + count * 13 / maxLayers_);
    }

    // Bar 2: layer progress
    FocusBar& bar2 = info.bars[1];
    bar2.length = maxLayers_;
    bar2.playhead = -1;

    for (int i = 0; i < maxLayers_ && i < kMaxBarSteps; ++i) {
        if (allLayersComplete_)
            bar2.levels[i] = 10;
        else if (i < currentLayer_)
            bar2.levels[i] = 10;
        else if (i == currentLayer_)
            bar2.levels[i] = 15;
        else
            bar2.levels[i] = 2;
    }
}
