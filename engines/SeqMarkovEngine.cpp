#include "SeqMarkovEngine.h"
#include "../scale/ScaleQuantizer.h"
#include <math.h>

static const char* const styleStrings[] = {
    "Tonal", "Stepwise", "Vamp", "Leaping",
    "Melodic", "Driving", "Hypnotic", "Chaotic", nullptr
};
static const char* const offOnStrings[] = { "Off", "On", nullptr };

static inline int clampInt(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// Style definitions are intentionally behavioral (not fixed matrices)
// so they still work for arbitrary scale sizes from Scala files.
const SeqMarkovEngine::StyleDef SeqMarkovEngine::kStyleDefs[kNumStyles] = {
    //               self   step   home  fifth
    /* Tonal    */ {  1.0f,  0.6f, 0.8f, 0.6f },
    /* Stepwise */ {  0.5f,  0.9f, 0.1f, 0.1f },
    /* Vamp     */ { 12.0f,  0.3f, 0.2f, 0.1f },
    /* Leaping  */ {  0.3f, -0.5f, 0.2f, 0.3f },
    /* Melodic  */ {  2.0f,  0.5f, 0.3f, 0.3f },
    /* Driving  */ {  5.0f,  0.4f, 0.6f, 0.5f },
    /* Hypnotic */ {  8.0f,  0.5f, 0.3f, 0.2f },
    /* Chaotic  */ {  0.1f,  0.1f, 0.05f, 0.05f },
};

// 16-step rhythm presets from seq_markov.lua.
const uint8_t SeqMarkovEngine::kRhythmPatterns[kNumRhythmPatterns][kRhythmPatternLen] = {
    {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0},
    {1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0},
    {1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1},
    {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0},
    {1, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0},
    {1, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0},
    {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1},
    {1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1},
};

SeqMarkovEngine::SeqMarkovEngine()
    : style_(kStyleTonal)
    , emotion_(50)
    , jumpiness_(30)
    , range_(2)
    , mutation_(20)
    , length_(8)
    , density_(100)
    , velocity_(100)
    , mutateSwitch_(0)
    , regenerateSwitch_(0)
    , appliedStyle_(kStyleTonal)
    , appliedEmotion_(50)
    , appliedJumpiness_(30)
    , appliedRange_(2)
    , appliedDensity_(100)
    , currentStep_(0)
    , lastDegree_(0)
    , numDegrees_(7)
    , needsRegenerate_(true)
    , rhythmInitialized_(false)
    , regeneratePending_(false)
    , rngState_(98765)
{
    for (int i = 0; i < kMaxSteps; ++i)
        sequence_[i] = { 0, 0, 100, true };
}

uint32_t SeqMarkovEngine::rng()
{
    rngState_ ^= rngState_ << 13;
    rngState_ ^= rngState_ >> 17;
    rngState_ ^= rngState_ << 5;
    return rngState_;
}

float SeqMarkovEngine::rngFloat()
{
    return (float)(rng() & 0x7FFFFFFFu) / (float)0x7FFFFFFFu;
}

void SeqMarkovEngine::init(uint32_t sampleRate)
{
    rngState_ = sampleRate ^ 0xFEEDFACEu;
    currentStep_ = 0;
    lastDegree_ = 0;
    numDegrees_ = 7;
    needsRegenerate_ = true;
    rhythmInitialized_ = false;
    regeneratePending_ = false;

    appliedStyle_ = style_;
    appliedEmotion_ = emotion_;
    appliedJumpiness_ = jumpiness_;
    appliedRange_ = range_;
    appliedDensity_ = density_;
}

void SeqMarkovEngine::initializeRhythm()
{
    int patternIndex;
    if (appliedEmotion_ > 70) {
        patternIndex = 4 + (int)(rng() % 4u); // 4..7
    } else if (appliedEmotion_ < 30) {
        patternIndex = (int)(rng() % 4u); // 0..3
    } else {
        patternIndex = (int)(rng() % (uint32_t)kNumRhythmPatterns);
    }

    for (int i = 0; i < kMaxSteps; ++i)
        sequence_[i].active = (kRhythmPatterns[patternIndex][i % kRhythmPatternLen] != 0);

    rhythmInitialized_ = true;
}

void SeqMarkovEngine::ensureAtLeastOneActive(int length)
{
    bool anyActive = false;
    for (int i = 0; i < length; ++i) {
        if (sequence_[i].active) {
            anyActive = true;
            break;
        }
    }
    if (!anyActive && length > 0)
        sequence_[0].active = true;
}

bool SeqMarkovEngine::driftTowardTargets()
{
    bool moved = false;

    auto drift = [&moved](int& cur, int target, int maxStep) {
        if (cur < target) {
            int d = target - cur;
            if (d > maxStep) d = maxStep;
            cur += d;
            moved = true;
        } else if (cur > target) {
            int d = cur - target;
            if (d > maxStep) d = maxStep;
            cur -= d;
            moved = true;
        }
    };

    // Discrete parameters move one step per cycle.
    drift(appliedStyle_, style_, 1);
    drift(appliedRange_, range_, 1);

    // Continuous parameters glide over multiple cycles.
    drift(appliedEmotion_, emotion_, 5);
    drift(appliedJumpiness_, jumpiness_, 5);
    drift(appliedDensity_, density_, 5);

    return moved;
}

float SeqMarkovEngine::computeWeight(int fromDeg, int toDeg, int numDegrees) const
{
    const StyleDef& s = kStyleDefs[clampInt(appliedStyle_, 0, kNumStyles - 1)];
    float w = 1.0f;

    if (fromDeg == toDeg) {
        w += s.selfBoost;
        return w;
    }

    int absDist = fromDeg > toDeg ? fromDeg - toDeg : toDeg - fromDeg;
    int wrapDist = numDegrees - absDist;
    int circDist = absDist < wrapDist ? absDist : wrapDist;

    float maxDist = (float)numDegrees * 0.5f;
    if (maxDist < 1.0f) maxDist = 1.0f;
    float normalizedDist = (float)circDist / maxDist;

    if (s.stepBias >= 0.0f) {
        w *= expf(-normalizedDist * s.stepBias * 4.0f);
    } else {
        w *= expf(-(1.0f - normalizedDist) * (-s.stepBias) * 4.0f);
    }

    if (toDeg == 0)
        w *= (1.0f + s.homeGravity * 5.0f);

    int fifthDeg = (numDegrees * 7 + 6) / 12;
    if (fifthDeg >= numDegrees) fifthDeg = numDegrees - 1;
    if (fifthDeg > 0 && toDeg == fifthDeg)
        w *= (1.0f + s.fifthGravity * 3.0f);

    return w;
}

float SeqMarkovEngine::applyEmotion(float weight, int fromDeg, int toDeg) const
{
    float emotionF = (float)appliedEmotion_ / 100.0f;
    float influence;

    if (emotionF < 0.5f) {
        float strength = (0.5f - emotionF) * 1.5f;
        if (toDeg < fromDeg) influence = 1.0f + strength;
        else if (toDeg > fromDeg) influence = 1.0f - strength;
        else influence = 1.0f;
    } else {
        float strength = (emotionF - 0.5f) * 1.5f;
        if (toDeg > fromDeg) influence = 1.0f + strength;
        else if (toDeg < fromDeg) influence = 1.0f - strength;
        else influence = 1.0f;
    }

    if (influence < 0.01f) influence = 0.01f;
    return weight * influence;
}

int SeqMarkovEngine::pickNextDegree(int currentDeg, int numDegrees)
{
    float weights[kMaxDegrees];
    float total = 0.0f;

    for (int j = 0; j < numDegrees; ++j) {
        float w = computeWeight(currentDeg, j, numDegrees);
        w = applyEmotion(w, currentDeg, j);
        weights[j] = w;
        total += w;
    }

    float r = rngFloat() * total;
    float accum = 0.0f;
    for (int j = 0; j < numDegrees; ++j) {
        accum += weights[j];
        if (r <= accum)
            return j;
    }
    return numDegrees - 1;
}

void SeqMarkovEngine::generateSequence(int numDegrees)
{
    int length = clampInt(length_, 1, kMaxSteps);
    int deg = lastDegree_;
    if (deg >= numDegrees) deg = 0;

    for (int i = 0; i < length; ++i) {
        deg = pickNextDegree(deg, numDegrees);

        int8_t octShift = 0;
        if (appliedRange_ > 1) {
            float emotionF = (float)appliedEmotion_ / 100.0f;
            float jumpF = (float)appliedJumpiness_ / 100.0f;
            float jumpUp = jumpF * emotionF;
            float jumpDown = jumpF * (1.0f - emotionF);

            if (rngFloat() < jumpUp) {
                octShift = 1;
                if (appliedRange_ > 2 && rngFloat() < 0.3f) octShift = 2;
            } else if (rngFloat() < jumpDown) {
                octShift = -1;
                if (appliedRange_ > 2 && rngFloat() < 0.3f) octShift = -2;
            }
        }

        int emotionDist = appliedEmotion_ > 50 ? appliedEmotion_ - 50 : 50 - appliedEmotion_;
        int vMin = 80 + emotionDist;
        int vMax = 127;
        if (vMin > vMax - 10) vMin = vMax - 10;
        uint8_t vel = (uint8_t)(vMin + (int)(rng() % (uint32_t)(vMax - vMin + 1)));

        bool active = sequence_[i].active;
        if (active && appliedDensity_ < 100) {
            if ((int)(rng() % 100u) >= appliedDensity_)
                active = false;
        }

        sequence_[i] = { (int8_t)deg, octShift, vel, active };
    }

    for (int i = length; i < kMaxSteps; ++i)
        sequence_[i].active = false;

    ensureAtLeastOneActive(length);
    lastDegree_ = deg;
}

void SeqMarkovEngine::mutateSequence(int numDegrees)
{
    int length = clampInt(length_, 1, kMaxSteps);
    Step candidate[kMaxSteps];

    int deg = lastDegree_;
    if (deg >= numDegrees) deg = 0;

    for (int i = 0; i < length; ++i) {
        deg = pickNextDegree(deg, numDegrees);

        int8_t octShift = 0;
        if (appliedRange_ > 1) {
            float emotionF = (float)appliedEmotion_ / 100.0f;
            float jumpF = (float)appliedJumpiness_ / 100.0f;
            if (rngFloat() < jumpF * emotionF) octShift = 1;
            else if (rngFloat() < jumpF * (1.0f - emotionF)) octShift = -1;
        }

        int emotionDist = appliedEmotion_ > 50 ? appliedEmotion_ - 50 : 50 - appliedEmotion_;
        int vMin = 80 + emotionDist;
        int vMax = 127;
        if (vMin > vMax - 10) vMin = vMax - 10;
        uint8_t vel = (uint8_t)(vMin + (int)(rng() % (uint32_t)(vMax - vMin + 1)));

        bool active = sequence_[i].active;
        if (active && appliedDensity_ < 100) {
            if ((int)(rng() % 100u) >= appliedDensity_)
                active = false;
        }

        candidate[i] = { (int8_t)deg, octShift, vel, active };
    }

    float mutF = (float)mutation_ / 100.0f;
    for (int i = 0; i < length; ++i) {
        if (rngFloat() < mutF)
            sequence_[i] = candidate[i];
    }

    ensureAtLeastOneActive(length);
    lastDegree_ = deg;
}

EngineOutput SeqMarkovEngine::clockTick(const ScaleQuantizer* scale)
{
    EngineOutput out = { 0.0f, 0.0f, 5.0f, 60 };

    int numDeg = (scale && scale->isLoaded()) ? (int)scale->numNotes() : 7;
    if (numDeg < 1) numDeg = 1;
    if (numDeg > kMaxDegrees) numDeg = kMaxDegrees;

    if (numDeg != numDegrees_) {
        numDegrees_ = numDeg;
        regeneratePending_ = true;
    }

    int length = clampInt(length_, 1, kMaxSteps);

    // Match Lua clocking feel: step first on each rising clock.
    currentStep_++;
    bool wrapped = false;
    if (currentStep_ >= length) {
        currentStep_ = 0;
        wrapped = true;
    }

    if (needsRegenerate_) {
        if (!rhythmInitialized_)
            initializeRhythm();
        generateSequence(numDeg);
        needsRegenerate_ = false;
        regeneratePending_ = false;
    } else if (wrapped) {
        bool moved = driftTowardTargets();

        if (regeneratePending_ || regenerateSwitch_ != 0) {
            if (!rhythmInitialized_)
                initializeRhythm();
            generateSequence(numDeg);
            regeneratePending_ = false;
        } else {
            int mutChance = mutation_;
            if (moved) {
                mutChance += 15;
                if (mutChance > 100) mutChance = 100;
            }
            if (mutateSwitch_ != 0 || (mutChance > 0 && (int)(rng() % 100u) < mutChance))
                mutateSequence(numDeg);
        }
    }

    const Step& step = sequence_[currentStep_];

    out.gate = step.active ? 5.0f : 0.0f;
    out.velocity = ((float)step.velocity / 25.4f) * ((float)velocity_ / 100.0f);

    int deg = step.scaleDegree;
    int octShift = step.octaveShift;

    if (scale && scale->isLoaded()) {
        out.pitch = scale->quantize(deg, octShift, 0);
        out.midiNote = scale->scaleDegreeToMidi(deg, octShift + 5, 0);
    } else {
        out.pitch = (float)(deg + octShift * 12) / 12.0f;
        int midi = 60 + deg + octShift * 12;
        if (midi < 0) midi = 0;
        if (midi > 127) midi = 127;
        out.midiNote = (uint8_t)midi;
    }

    return out;
}

void SeqMarkovEngine::reset()
{
    // Lua reset returns to step 1 but does not regenerate a new sequence.
    currentStep_ = 0;
}

void SeqMarkovEngine::uiForceMutate()
{
    int numDeg = numDegrees_;
    if (numDeg < 1) numDeg = 7;
    if (numDeg > kMaxDegrees) numDeg = kMaxDegrees;
    mutateSequence(numDeg);
}

void SeqMarkovEngine::uiForceRegenerate()
{
    int numDeg = numDegrees_;
    if (numDeg < 1) numDeg = 7;
    if (numDeg > kMaxDegrees) numDeg = kMaxDegrees;
    initializeRhythm();
    generateSequence(numDeg);
    currentStep_ = 0;
}

void SeqMarkovEngine::parameterChanged(int localIndex, int16_t value)
{
    switch (localIndex) {
    case kMarkovStyle:
        style_ = clampInt(value, 0, kNumStyles - 1);
        break;
    case kMarkovEmotion:
        emotion_ = clampInt(value, 0, 100);
        break;
    case kMarkovJumpiness:
        jumpiness_ = clampInt(value, 0, 100);
        break;
    case kMarkovRange:
        range_ = clampInt(value, 1, 3);
        break;
    case kMarkovMutation:
        mutation_ = clampInt(value, 0, 100);
        break;
    case kMarkovLength:
        length_ = clampInt(value, 1, kMaxSteps);
        break;
    case kMarkovDensity:
        density_ = clampInt(value, 1, 100);
        break;
    case kMarkovVelocity:
        velocity_ = clampInt(value, 0, 100);
        break;
    case kMarkovMutateSwitch:
        mutateSwitch_ = clampInt(value, 0, 1);
        break;
    case kMarkovRegenerateSwitch:
        regenerateSwitch_ = clampInt(value, 0, 1);
        break;
    }
}

int SeqMarkovEngine::getParameterDefs(_NT_parameter* defs) const
{
    defs[kMarkovStyle]     = { .name = "Style",     .min = 0, .max = kNumStyles - 1, .def = kStyleTonal, .unit = kNT_unitEnum,    .scaling = kNT_scalingNone, .enumStrings = styleStrings };
    defs[kMarkovEmotion]   = { .name = "Emotion",   .min = 0, .max = 100,            .def = 50,          .unit = kNT_unitPercent, .scaling = kNT_scalingNone, .enumStrings = nullptr };
    defs[kMarkovJumpiness] = { .name = "Jumpiness", .min = 0, .max = 100,            .def = 30,          .unit = kNT_unitPercent, .scaling = kNT_scalingNone, .enumStrings = nullptr };
    defs[kMarkovRange]     = { .name = "Oct Range", .min = 1, .max = 3,              .def = 2,           .unit = kNT_unitNone,    .scaling = kNT_scalingNone, .enumStrings = nullptr };
    defs[kMarkovMutation]  = { .name = "Mutation",  .min = 0, .max = 100,            .def = 20,          .unit = kNT_unitPercent, .scaling = kNT_scalingNone, .enumStrings = nullptr };
    defs[kMarkovLength]    = { .name = "Length",    .min = 1, .max = kMaxSteps,      .def = 8,           .unit = kNT_unitNone,    .scaling = kNT_scalingNone, .enumStrings = nullptr };
    defs[kMarkovDensity]   = { .name = "Density",   .min = 1, .max = 100,            .def = 100,         .unit = kNT_unitPercent, .scaling = kNT_scalingNone, .enumStrings = nullptr };
    defs[kMarkovVelocity]  = { .name = "Velocity",  .min = 0, .max = 100,            .def = 100,         .unit = kNT_unitPercent, .scaling = kNT_scalingNone, .enumStrings = nullptr };
    defs[kMarkovMutateSwitch] = { .name = "Mutate", .min = 0, .max = 1,              .def = 0,           .unit = kNT_unitEnum,    .scaling = kNT_scalingNone, .enumStrings = offOnStrings };
    defs[kMarkovRegenerateSwitch] = { .name = "Regenerate", .min = 0, .max = 1,      .def = 0,           .unit = kNT_unitEnum,    .scaling = kNT_scalingNone, .enumStrings = offOnStrings };
    return kNumMarkovParams;
}

int SeqMarkovEngine::currentStep() const
{
    return currentStep_;
}

int SeqMarkovEngine::sequenceLength() const
{
    return clampInt(length_, 1, kMaxSteps);
}

int SeqMarkovEngine::getStatusText(char* buf, int maxLen) const
{
    const char* s = styleStrings[clampInt(appliedStyle_, 0, kNumStyles - 1)];
    int len = 0;
    while (*s && len < maxLen - 1) buf[len++] = *s++;
    buf[len] = 0;
    return len;
}

void SeqMarkovEngine::drawFocusDetail(int y1, int y2) const
{
    char buf[64];
    int len = 0;
    const char* s;

    // Line 1: Style:Tonal  Emotion:50%  Density:60%
    s = "Style:"; while (*s) buf[len++] = *s++;
    s = styleStrings[clampInt(style_, 0, kNumStyles - 1)];
    if (s) while (*s && len < 20) buf[len++] = *s++;
    s = "  Emotion:"; while (*s) buf[len++] = *s++;
    len += NT_intToString(buf + len, emotion_);
    buf[len++] = '%';
    s = "  Density:"; while (*s) buf[len++] = *s++;
    len += NT_intToString(buf + len, density_);
    buf[len++] = '%';
    buf[len] = 0;
    NT_drawText(0, y1, buf, 8, kNT_textLeft, kNT_textTiny);

    // Line 2: Len:16  Jump:30%  Range:2  Mut:20%
    len = 0;
    s = "Len:"; while (*s) buf[len++] = *s++;
    len += NT_intToString(buf + len, length_);
    s = "  Jump:"; while (*s) buf[len++] = *s++;
    len += NT_intToString(buf + len, jumpiness_);
    buf[len++] = '%';
    s = "  Range:"; while (*s) buf[len++] = *s++;
    len += NT_intToString(buf + len, range_);
    s = "  Mut:"; while (*s) buf[len++] = *s++;
    len += NT_intToString(buf + len, mutation_);
    buf[len++] = '%';
    buf[len] = 0;
    NT_drawText(0, y2, buf, 6, kNT_textLeft, kNT_textTiny);
}

int SeqMarkovEngine::getPageDefs(_NT_parameterPage* page, uint8_t* indices, int baseParamIndex) const
{
    for (int i = 0; i < kNumMarkovParams; ++i)
        indices[i] = (uint8_t)(baseParamIndex + i);
    page->name = "Markov";
    page->numParams = kNumMarkovParams;
    page->group = 0;
    page->unused[0] = 0;
    page->unused[1] = 0;
    page->params = indices;
    return kNumMarkovParams;
}
