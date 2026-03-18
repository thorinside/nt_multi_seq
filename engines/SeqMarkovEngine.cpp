#include "SeqMarkovEngine.h"
#include "../scale/ScaleQuantizer.h"
#include <math.h>

static const char* const styleStrings[] = {
    "Pop/Rock", "Classical", "Jazz", "Techno",
    "Min Techno", "Mel Techno", "LMD All", "LMD Elec", nullptr
};
static const char* const offOnStrings[] = { "Off", "On", nullptr };

static inline int clampInt(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// -----------------------------------------------------------------------
// One 7x7 transition matrix per style (static const for SDRAM).
// All matrices operate in abstract 7-degree harmonic space; any scale
// is mapped into this space via buildDegreeMap().
// -----------------------------------------------------------------------

const float SeqMarkovEngine::kStyleMatrices[kNumStyles][kMatrixDeg][kMatrixDeg] = {
    // Pop/Rock — from Lua major matrix: strong I-IV-V-vi patterns
    {
        {0.05f, 0.08f, 0.02f, 0.25f, 0.31f, 0.25f, 0.04f},
        {0.15f, 0.05f, 0.05f, 0.10f, 0.45f, 0.15f, 0.05f},
        {0.10f, 0.05f, 0.05f, 0.30f, 0.10f, 0.35f, 0.05f},
        {0.32f, 0.05f, 0.03f, 0.05f, 0.29f, 0.22f, 0.04f},
        {0.45f, 0.05f, 0.03f, 0.20f, 0.05f, 0.18f, 0.04f},
        {0.15f, 0.10f, 0.05f, 0.35f, 0.25f, 0.05f, 0.05f},
        {0.50f, 0.05f, 0.05f, 0.10f, 0.20f, 0.05f, 0.05f},
    },
    // Classical — from Lua harmonic_minor: strong V-I resolution, vii-I
    {
        {0.08f, 0.10f, 0.05f, 0.22f, 0.35f, 0.12f, 0.08f},
        {0.12f, 0.05f, 0.05f, 0.08f, 0.50f, 0.12f, 0.08f},
        {0.10f, 0.08f, 0.05f, 0.30f, 0.15f, 0.25f, 0.07f},
        {0.25f, 0.08f, 0.05f, 0.05f, 0.40f, 0.10f, 0.07f},
        {0.55f, 0.05f, 0.05f, 0.08f, 0.05f, 0.15f, 0.07f},
        {0.15f, 0.15f, 0.10f, 0.20f, 0.25f, 0.05f, 0.10f},
        {0.60f, 0.05f, 0.05f, 0.08f, 0.12f, 0.05f, 0.05f},
    },
    // Jazz — from Lua natural_minor: weaker resolution, more modal
    {
        {0.05f, 0.05f, 0.20f, 0.25f, 0.20f, 0.15f, 0.10f},
        {0.20f, 0.05f, 0.10f, 0.10f, 0.40f, 0.10f, 0.05f},
        {0.15f, 0.05f, 0.05f, 0.30f, 0.10f, 0.25f, 0.10f},
        {0.30f, 0.05f, 0.10f, 0.05f, 0.25f, 0.15f, 0.10f},
        {0.40f, 0.05f, 0.10f, 0.15f, 0.05f, 0.15f, 0.10f},
        {0.15f, 0.05f, 0.20f, 0.20f, 0.20f, 0.05f, 0.15f},
        {0.35f, 0.05f, 0.15f, 0.15f, 0.10f, 0.15f, 0.05f},
    },
    // Techno — from Lua techno_minor: vamp + VI-VII-i climb
    {
        {0.40f, 0.02f, 0.05f, 0.15f, 0.15f, 0.13f, 0.10f},
        {0.25f, 0.20f, 0.15f, 0.10f, 0.15f, 0.10f, 0.05f},
        {0.15f, 0.05f, 0.30f, 0.20f, 0.05f, 0.15f, 0.10f},
        {0.30f, 0.03f, 0.10f, 0.25f, 0.20f, 0.07f, 0.05f},
        {0.35f, 0.02f, 0.08f, 0.20f, 0.25f, 0.05f, 0.05f},
        {0.10f, 0.02f, 0.05f, 0.05f, 0.08f, 0.30f, 0.40f},
        {0.45f, 0.02f, 0.05f, 0.05f, 0.08f, 0.15f, 0.20f},
    },
    // Minimal Techno — from Lua minimal_techno: extreme vamp bias
    {
        {0.60f, 0.02f, 0.05f, 0.10f, 0.10f, 0.08f, 0.05f},
        {0.30f, 0.40f, 0.10f, 0.05f, 0.10f, 0.03f, 0.02f},
        {0.20f, 0.05f, 0.50f, 0.10f, 0.05f, 0.05f, 0.05f},
        {0.35f, 0.02f, 0.08f, 0.40f, 0.10f, 0.03f, 0.02f},
        {0.40f, 0.02f, 0.05f, 0.10f, 0.35f, 0.05f, 0.03f},
        {0.15f, 0.02f, 0.05f, 0.05f, 0.08f, 0.45f, 0.20f},
        {0.50f, 0.02f, 0.03f, 0.05f, 0.05f, 0.10f, 0.25f},
    },
    // Melodic Techno — from Lua melodic_techno: Dorian feel
    {
        {0.30f, 0.08f, 0.20f, 0.12f, 0.15f, 0.05f, 0.10f},
        {0.20f, 0.20f, 0.15f, 0.15f, 0.20f, 0.05f, 0.05f},
        {0.15f, 0.05f, 0.25f, 0.15f, 0.25f, 0.05f, 0.10f},
        {0.30f, 0.08f, 0.15f, 0.20f, 0.15f, 0.05f, 0.07f},
        {0.25f, 0.05f, 0.15f, 0.20f, 0.25f, 0.03f, 0.07f},
        {0.20f, 0.10f, 0.15f, 0.15f, 0.20f, 0.10f, 0.10f},
        {0.35f, 0.05f, 0.15f, 0.10f, 0.10f, 0.05f, 0.20f},
    },
    // LMD All Genres — from 91,842 transitions across 491 MIDI files
    {
        {0.45f, 0.12f, 0.05f, 0.14f, 0.11f, 0.12f, 0.02f},
        {0.17f, 0.35f, 0.09f, 0.05f, 0.12f, 0.16f, 0.07f},
        {0.16f, 0.14f, 0.34f, 0.02f, 0.07f, 0.17f, 0.10f},
        {0.46f, 0.11f, 0.01f, 0.25f, 0.05f, 0.10f, 0.01f},
        {0.32f, 0.26f, 0.08f, 0.05f, 0.18f, 0.06f, 0.05f},
        {0.24f, 0.24f, 0.11f, 0.07f, 0.04f, 0.24f, 0.06f},
        {0.13f, 0.26f, 0.25f, 0.02f, 0.08f, 0.09f, 0.18f},
    },
    // LMD Electronic — from 88,421 transitions across 483 electronic MIDI files
    {
        {0.49f, 0.12f, 0.02f, 0.17f, 0.09f, 0.10f, 0.01f},
        {0.22f, 0.32f, 0.08f, 0.05f, 0.14f, 0.15f, 0.04f},
        {0.14f, 0.15f, 0.30f, 0.03f, 0.07f, 0.19f, 0.12f},
        {0.48f, 0.08f, 0.02f, 0.28f, 0.05f, 0.09f, 0.00f},
        {0.33f, 0.25f, 0.06f, 0.04f, 0.18f, 0.08f, 0.05f},
        {0.26f, 0.21f, 0.08f, 0.10f, 0.04f, 0.26f, 0.06f},
        {0.10f, 0.20f, 0.18f, 0.01f, 0.19f, 0.12f, 0.22f},
    },
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

// -----------------------------------------------------------------------
// Construction / init
// -----------------------------------------------------------------------

SeqMarkovEngine::SeqMarkovEngine()
    : style_(kStylePopRock)
    , emotion_(50)
    , jumpiness_(30)
    , range_(2)
    , mutation_(20)
    , length_(8)
    , randomizeSwitch_(0)
    , regenerateSwitch_(0)
    , currentStep_(0)
    , lastDegree_(0)
    , numDegrees_(7)
    , needsRegenerate_(true)
    , rhythmInitialized_(false)
    , regeneratePending_(false)
    , randomizePending_(false)
    , rngState_(98765)
{
    for (int i = 0; i < kMaxSteps; ++i)
        sequence_[i] = { 0, 0, 100, true };
    for (int i = 0; i < kMaxDegrees; ++i)
        degreeMap_[i] = i < kMatrixDeg ? i : i % kMatrixDeg;
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
    randomizePending_ = false;
    for (int i = 0; i < kMaxDegrees; ++i)
        degreeMap_[i] = i < kMatrixDeg ? i : i % kMatrixDeg;
}

// -----------------------------------------------------------------------
// Degree mapping: maps any N-note scale into the 7-degree matrix space
// by finding the nearest diatonic position for each scale degree.
// -----------------------------------------------------------------------

void SeqMarkovEngine::buildDegreeMap(const ScaleQuantizer* scale)
{
    // Reference: major scale positions within a 1200-cent octave.
    static const float kRefCents[kMatrixDeg] = {
        0.0f, 200.0f, 400.0f, 500.0f, 700.0f, 900.0f, 1100.0f
    };

    if (!scale || !scale->isLoaded()) {
        for (int i = 0; i < kMaxDegrees; ++i)
            degreeMap_[i] = i < kMatrixDeg ? i : i % kMatrixDeg;
        return;
    }

    float period = scale->periodCents();
    if (period <= 0.0f) period = 1200.0f;

    int n = (int)scale->numNotes();
    if (n > kMaxDegrees) n = kMaxDegrees;

    for (int d = 0; d < n; ++d) {
        float cents = scale->degreeCents(d);
        // Normalize to 1200-cent space for comparison with reference.
        float normalized = (cents / period) * 1200.0f;

        int bestBase = 0;
        float bestDist = 99999.0f;
        for (int b = 0; b < kMatrixDeg; ++b) {
            float dist = normalized - kRefCents[b];
            if (dist < 0.0f) dist = -dist;
            if (dist < bestDist) {
                bestDist = dist;
                bestBase = b;
            }
        }
        degreeMap_[d] = bestBase;
    }

    // Fill remainder with modular wrap.
    for (int d = n; d < kMaxDegrees; ++d)
        degreeMap_[d] = degreeMap_[d % n];
}

// -----------------------------------------------------------------------
// Rhythm
// -----------------------------------------------------------------------

void SeqMarkovEngine::initializeRhythm()
{
    int patternIndex;
    if (emotion_ > 70) {
        patternIndex = 4 + (int)(rng() % 4u);
    } else if (emotion_ < 30) {
        patternIndex = (int)(rng() % 4u);
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

// -----------------------------------------------------------------------
// Emotion modifier (matches Lua get_transition_matrix emotion logic)
// -----------------------------------------------------------------------

float SeqMarkovEngine::applyEmotion(float weight, int fromDeg, int toDeg) const
{
    float emotionF = (float)emotion_ / 100.0f;
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

// -----------------------------------------------------------------------
// Markov chain: degree selection via matrix + degree mapping
// -----------------------------------------------------------------------

int SeqMarkovEngine::pickNextDegree(int currentDeg, int numDegrees)
{
    const float (*matrix)[kMatrixDeg] = kStyleMatrices[clampInt(style_, 0, kNumStyles - 1)];
    int fromBase = degreeMap_[currentDeg];

    float weights[kMaxDegrees];
    float total = 0.0f;

    for (int j = 0; j < numDegrees; ++j) {
        int toBase = degreeMap_[j];
        float w = matrix[fromBase][toBase];
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

// -----------------------------------------------------------------------
// Sequence generation
// -----------------------------------------------------------------------

void SeqMarkovEngine::generateSequence(int numDegrees)
{
    int length = clampInt(length_, 1, kMaxSteps);
    int deg = lastDegree_;
    if (deg >= numDegrees) deg = 0;

    for (int i = 0; i < length; ++i) {
        deg = pickNextDegree(deg, numDegrees);

        int8_t octShift = 0;
        if (range_ > 1) {
            float emotionF = (float)emotion_ / 100.0f;
            float jumpF = (float)jumpiness_ / 100.0f;
            float jumpUp = jumpF * emotionF;
            float jumpDown = jumpF * (1.0f - emotionF);

            if (rngFloat() < jumpUp)
                octShift = 1;
            else if (rngFloat() < jumpDown)
                octShift = -1;
        }

        int emotionDist = emotion_ > 50 ? emotion_ - 50 : 50 - emotion_;
        int vMin = 80 + emotionDist;
        int vMax = 127;
        if (vMin > vMax) vMin = vMax - 10;
        uint8_t vel = (uint8_t)(vMin + (int)(rng() % (uint32_t)(vMax - vMin + 1)));

        sequence_[i] = { (int8_t)deg, octShift, vel, sequence_[i].active };
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
        if (range_ > 1) {
            float emotionF = (float)emotion_ / 100.0f;
            float jumpF = (float)jumpiness_ / 100.0f;
            float jumpUp = jumpF * emotionF;
            float jumpDown = jumpF * (1.0f - emotionF);

            if (rngFloat() < jumpUp)
                octShift = 1;
            else if (rngFloat() < jumpDown)
                octShift = -1;
        }

        int emotionDist = emotion_ > 50 ? emotion_ - 50 : 50 - emotion_;
        int vMin = 80 + emotionDist;
        int vMax = 127;
        if (vMin > vMax) vMin = vMax - 10;
        uint8_t vel = (uint8_t)(vMin + (int)(rng() % (uint32_t)(vMax - vMin + 1)));

        candidate[i] = { (int8_t)deg, octShift, vel, sequence_[i].active };
    }

    float mutF = (float)mutation_ / 100.0f;
    for (int i = 0; i < length; ++i) {
        if (rngFloat() < mutF)
            sequence_[i] = candidate[i];
    }

    ensureAtLeastOneActive(length);
    lastDegree_ = deg;
}

// -----------------------------------------------------------------------
// Clock tick (main entry point per clock rising edge)
// -----------------------------------------------------------------------

EngineOutput SeqMarkovEngine::clockTick(const ScaleQuantizer* scale)
{
    EngineOutput out = { 0.0f, 0.0f, 5.0f, 60 };

    int numDeg = (scale && scale->isLoaded()) ? (int)scale->numNotes() : 7;
    if (numDeg < 1) numDeg = 1;
    if (numDeg > kMaxDegrees) numDeg = kMaxDegrees;

    if (numDeg != numDegrees_) {
        numDegrees_ = numDeg;
        buildDegreeMap(scale);
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
        randomizePending_ = false;
    } else if (wrapped) {
        if (regeneratePending_ || regenerateSwitch_ != 0) {
            lastDegree_ = 0;
            initializeRhythm();
            generateSequence(numDeg);
            currentStep_ = 0;
            regeneratePending_ = false;
        } else if (randomizePending_ || randomizeSwitch_ != 0) {
            generateSequence(numDeg);
            randomizePending_ = false;
        } else if (mutation_ > 0 && (int)(rng() % 100u) < mutation_) {
            mutateSequence(numDeg);
        }
    }

    const Step& step = sequence_[currentStep_];

    out.gate = step.active ? 5.0f : 0.0f;
    out.velocity = step.active ? (float)step.velocity / 25.4f : 0.0f;

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

// -----------------------------------------------------------------------
// Reset / UI actions
// -----------------------------------------------------------------------

void SeqMarkovEngine::reset()
{
    currentStep_ = 0;
}

void SeqMarkovEngine::uiForceRandomize()
{
    int numDeg = numDegrees_;
    if (numDeg < 1) numDeg = 7;
    if (numDeg > kMaxDegrees) numDeg = kMaxDegrees;
    generateSequence(numDeg);
}

void SeqMarkovEngine::uiForceRegenerate()
{
    int numDeg = numDegrees_;
    if (numDeg < 1) numDeg = 7;
    if (numDeg > kMaxDegrees) numDeg = kMaxDegrees;
    lastDegree_ = 0;
    initializeRhythm();
    generateSequence(numDeg);
    currentStep_ = 0;
}

// -----------------------------------------------------------------------
// Parameters
// -----------------------------------------------------------------------

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
    case kMarkovRandomizeSwitch:
        randomizeSwitch_ = clampInt(value, 0, 1);
        if (randomizeSwitch_) randomizePending_ = true;
        break;
    case kMarkovRegenerateSwitch:
        regenerateSwitch_ = clampInt(value, 0, 1);
        if (regenerateSwitch_) regeneratePending_ = true;
        break;
    }
}

int SeqMarkovEngine::getParameterDefs(_NT_parameter* defs) const
{
    defs[kMarkovStyle]     = { .name = "Style",     .min = 0, .max = kNumStyles - 1, .def = kStylePopRock, .unit = kNT_unitEnum,    .scaling = kNT_scalingNone, .enumStrings = styleStrings };
    defs[kMarkovEmotion]   = { .name = "Emotion",   .min = 0, .max = 100,            .def = 50,            .unit = kNT_unitPercent, .scaling = kNT_scalingNone, .enumStrings = nullptr };
    defs[kMarkovJumpiness] = { .name = "Jumpiness", .min = 0, .max = 100,            .def = 30,            .unit = kNT_unitPercent, .scaling = kNT_scalingNone, .enumStrings = nullptr };
    defs[kMarkovRange]     = { .name = "Oct Range", .min = 1, .max = 3,              .def = 2,             .unit = kNT_unitNone,    .scaling = kNT_scalingNone, .enumStrings = nullptr };
    defs[kMarkovMutation]  = { .name = "Mutation",  .min = 0, .max = 100,            .def = 20,            .unit = kNT_unitPercent, .scaling = kNT_scalingNone, .enumStrings = nullptr };
    defs[kMarkovLength]    = { .name = "Length",    .min = 1, .max = kMaxSteps,      .def = 8,             .unit = kNT_unitNone,    .scaling = kNT_scalingNone, .enumStrings = nullptr };
    defs[kMarkovRandomizeSwitch] = { .name = "Randomize", .min = 0, .max = 1,        .def = 0,             .unit = kNT_unitEnum,    .scaling = kNT_scalingNone, .enumStrings = offOnStrings };
    defs[kMarkovRegenerateSwitch] = { .name = "Regenerate", .min = 0, .max = 1,      .def = 0,             .unit = kNT_unitEnum,    .scaling = kNT_scalingNone, .enumStrings = offOnStrings };
    return kNumMarkovParams;
}

// -----------------------------------------------------------------------
// Display / status
// -----------------------------------------------------------------------

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
    const char* s = styleStrings[clampInt(style_, 0, kNumStyles - 1)];
    int len = 0;
    while (*s && len < maxLen - 1) buf[len++] = *s++;
    buf[len] = 0;
    return len;
}

void SeqMarkovEngine::getFocusDetail(FocusDetail& detail) const
{
    FocusDetailLine& line1 = detail.lines[0];
    FocusDetailLine& line2 = detail.lines[1];
    line1.clear();
    line2.clear();

    line1.append("Style:", 8);
    const char* s = styleStrings[clampInt(style_, 0, kNumStyles - 1)];
    if (s) line1.append(s, 8);
    line1.append("  Emotion:", 8);
    line1.appendInt(emotion_, 8);
    line1.appendChar('%', 8);

    line2.append("Len:", 6);
    line2.appendInt(length_, 6);
    line2.append("  Jump:", 6);
    line2.appendInt(jumpiness_, 6);
    line2.appendChar('%', 6);
    line2.append("  Range:", 6);
    line2.appendInt(range_, 6);
    line2.append("  Mut:", 6);
    line2.appendInt(mutation_, 6);
    line2.appendChar('%', 6);
}

void SeqMarkovEngine::getFocusBarInfo(FocusBarInfo& info) const
{
    info.numBars = 1;
    FocusBar& bar = info.bars[0];
    bar.length = length_;
    bar.playhead = currentStep_;
    for (int i = 0; i < length_ && i < kMaxBarSteps; ++i) {
        if (!sequence_[i].active)
            bar.levels[i] = 1;  // black for rests
        else
            bar.levels[i] = (uint8_t)((sequence_[i].scaleDegree % 15) + 1);
    }
    if (currentStep_ >= 0 && currentStep_ < kMaxBarSteps)
        bar.levels[currentStep_] = 15;  // bright playhead
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
