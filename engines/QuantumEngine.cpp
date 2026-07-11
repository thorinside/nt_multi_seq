#include "QuantumEngine.h"
#include "../scale/ScaleQuantizer.h"
#include <math.h>

static inline int clampInt(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

QuantumEngine::QuantumEngine()
    : motifLen_(8)
    , gateDensity_(75)
    , range_(5)
    , contour_(kContourArch)
    , mEvery_(4)
    , mTransform_(kMTranspose)
    , lEvery_(4)
    , lAction_(kLNewMotif)
    , gateLen_(75)
    , velocity_(80)
    , stepInMotif_(0)
    , motifCycles_(0)
    , mCycleCount_(0)
    , transpose_(0)
    , octaveShift_(0)
    , numDegrees_(7)
    , scaleLoaded_(false)
    , rngState_(12345)
    , lastNote_(0)
{
    for (int i = 0; i < kMaxMotifLen; ++i) {
        motif_[i] = 0;
        gates_[i] = true;
    }
    for (int i = 0; i < kMaxWeights; ++i)
        noteWeights_[i] = 1.0f;
}

// --- PRNG ---

uint32_t QuantumEngine::rng()
{
    rngState_ ^= rngState_ << 13;
    rngState_ ^= rngState_ >> 17;
    rngState_ ^= rngState_ << 5;
    return rngState_;
}

float QuantumEngine::rngFloat()
{
    return (float)(rng() & 0x7FFFFFFF) / (float)0x7FFFFFFF;
}

int QuantumEngine::rngInt(int max)
{
    if (max <= 0) return 0;
    return (int)(rng() % (uint32_t)max);
}

// --- Lifecycle ---

void QuantumEngine::init(uint32_t sampleRate)
{
    rngState_ = sampleRate ^ 0xCAFEBEEF;
    stepInMotif_ = 0;
    motifCycles_ = 0;
    mCycleCount_ = 0;
    transpose_ = 0;
    octaveShift_ = 0;
    scaleLoaded_ = false;
    lastNote_ = 0;
    // Generate initial motif (chromatic fallback until scale loads)
    generateMotif(nullptr);
}

void QuantumEngine::reset()
{
    stepInMotif_ = 0;
    motifCycles_ = 0;
    mCycleCount_ = 0;
    transpose_ = 0;
    octaveShift_ = 0;
}

// --- Motif Generation ---

int QuantumEngine::pickWeightedDegree(int range)
{
    // Pick a note from 0..range-1, weighted by noteWeights_
    float total = 0.0f;
    int maxDeg = range < numDegrees_ ? range : numDegrees_;
    if (maxDeg <= 0) maxDeg = 1;
    for (int i = 0; i < maxDeg; ++i)
        total += noteWeights_[i];

    float pick = rngFloat() * total;
    float accum = 0.0f;
    for (int i = 0; i < maxDeg; ++i) {
        accum += noteWeights_[i];
        if (pick <= accum)
            return i;
    }
    return maxDeg - 1;
}

void QuantumEngine::generateMotif(const ScaleQuantizer* scale)
{
    int len = motifLen_;
    int rng_ = range_;

    // Generate contour-shaped motif
    for (int i = 0; i < len; ++i) {
        float pos = (float)i / (float)(len > 1 ? len - 1 : 1);
        float contourValue = 0.0f; // 0.0 to 1.0, shape of the melody

        switch (contour_) {
        case kContourArch:
            // Rise to middle, fall back: inverted parabola
            contourValue = 1.0f - (2.0f * pos - 1.0f) * (2.0f * pos - 1.0f);
            break;
        case kContourDescend:
            // Start high, descend
            contourValue = 1.0f - pos;
            break;
        case kContourAscend:
            // Start low, ascend
            contourValue = pos;
            break;
        case kContourWave:
            // Sinusoidal
            contourValue = 0.5f + 0.5f * sinf(pos * 3.14159f * 2.0f);
            break;
        }

        // Map contour to scale degree within range, with some randomness
        int baseDegree = (int)(contourValue * (float)(rng_ - 1) + 0.5f);
        // Add small random deviation (±1 degree) for interest
        int deviation = rngInt(3) - 1; // -1, 0, or +1
        motif_[i] = clampInt(baseDegree + deviation, 0, rng_ - 1);
    }

    regenerateGates();
}

void QuantumEngine::regenerateGates()
{
    int len = motifLen_;
    int numGates = (len * gateDensity_ + 50) / 100;
    if (numGates < 1) numGates = 1;
    if (numGates > len) numGates = len;

    // Start with all gates on, then remove some
    for (int i = 0; i < len; ++i)
        gates_[i] = true;

    int toRemove = len - numGates;
    // Remove gates from less important positions (avoid removing beat 1)
    for (int r = 0; r < toRemove; ++r) {
        int attempts = 0;
        int pos;
        do {
            pos = 1 + rngInt(len - 1);
            attempts++;
        } while (!gates_[pos] && attempts < 20);
        if (gates_[pos])
            gates_[pos] = false;
    }
}

// --- M Layer: Transformations ---

void QuantumEngine::applyMTransform()
{
    int len = motifLen_;

    switch (mTransform_) {
    case kMTranspose: {
        // Transpose by +2 or +3 degrees (musically: a 3rd), cycling back
        int interval = (mCycleCount_ % 2 == 0) ? 2 : 3;
        transpose_ += interval;
        // Wrap transpose to stay within a reasonable range (±1 octave)
        if (transpose_ > numDegrees_)
            transpose_ -= numDegrees_;
        break;
    }
    case kMInvert: {
        // Invert intervals: mirror around the range
        for (int i = 0; i < len; ++i)
            motif_[i] = range_ - 1 - motif_[i];
        break;
    }
    case kMRotate: {
        // Rotate pattern by 1-2 positions
        int shift = 1 + rngInt(2);
        int temp[kMaxMotifLen];
        for (int i = 0; i < len; ++i)
            temp[i] = motif_[(i + shift) % len];
        for (int i = 0; i < len; ++i)
            motif_[i] = temp[i];
        // Also rotate gates
        bool gTemp[kMaxMotifLen];
        for (int i = 0; i < len; ++i)
            gTemp[i] = gates_[(i + shift) % len];
        for (int i = 0; i < len; ++i)
            gates_[i] = gTemp[i];
        break;
    }
    case kMPermute: {
        // Swap 2 adjacent notes (subtle variation)
        int pos = rngInt(len - 1);
        int tmp = motif_[pos];
        motif_[pos] = motif_[pos + 1];
        motif_[pos + 1] = tmp;
        break;
    }
    }

    mCycleCount_++;
}

// --- L Layer: Structural Actions ---

void QuantumEngine::applyLAction(const ScaleQuantizer* scale)
{
    switch (lAction_) {
    case kLNewMotif:
        // Fresh motif, reset transposition
        generateMotif(scale);
        transpose_ = 0;
        break;
    case kLOctaveShift:
        // Shift up or down one octave
        if (octaveShift_ >= 1)
            octaveShift_ = -1;
        else
            octaveShift_++;
        break;
    case kLExpand: {
        // Expand: stretch intervals by doubling each degree offset from center
        int mid = range_ / 2;
        for (int i = 0; i < motifLen_; ++i) {
            int diff = motif_[i] - mid;
            motif_[i] = clampInt(mid + diff * 2, 0, range_ - 1);
        }
        break;
    }
    case kLReverse: {
        // Reverse the motif
        for (int i = 0; i < motifLen_ / 2; ++i) {
            int j = motifLen_ - 1 - i;
            int tmp = motif_[i];
            motif_[i] = motif_[j];
            motif_[j] = tmp;
            bool gtmp = gates_[i];
            gates_[i] = gates_[j];
            gates_[j] = gtmp;
        }
        break;
    }
    }
}

// --- Clock Tick ---

EngineOutput QuantumEngine::clockTick(const ScaleQuantizer* scale)
{
    EngineOutput out = {0.0f, 0.0f, 0.0f, 60};

    // Scale detection
    if (scale && scale->isLoaded()) {
        int nd = (int)scale->numNotes();
        if (nd > kMaxWeights) nd = kMaxWeights;
        if (!scaleLoaded_ || nd != numDegrees_) {
            numDegrees_ = nd;
            scale->computeNoteWeights(noteWeights_, numDegrees_,
                (ScaleQuantizer::WeightMode)weightMode_);
            scaleLoaded_ = true;
            // Regenerate motif with proper scale weights
            generateMotif(scale);
        }
    }

    // --- Read current step from motif ---
    int degree = motif_[stepInMotif_] + transpose_;
    bool gate = gates_[stepInMotif_];

    // Apply octave shift from L layer
    int octave = octaveShift_;

    // Wrap degree into scale
    if (numDegrees_ > 0) {
        while (degree < 0) {
            degree += numDegrees_;
            octave--;
        }
        while (degree >= numDegrees_) {
            degree -= numDegrees_;
            octave++;
        }
    }

    // Quantize to pitch
    if (scale && scale->isLoaded() && numDegrees_ > 0) {
        out.pitch = scale->quantize(degree, octave, 0);
        out.midiNote = scale->scaleDegreeToMidi(degree, octave + 5, 0);
    } else {
        // Chromatic fallback
        int semitone = degree + octave * 12;
        out.pitch = (float)semitone / 12.0f;
        out.midiNote = (uint8_t)clampInt(60 + semitone, 0, 127);
    }

    // Gate and velocity
    if (gate) {
        out.gate = 5.0f;
        out.velocity = (float)velocity_ / 100.0f * 5.0f;
    } else {
        out.gate = 0.0f;
        out.velocity = 0.0f;
    }

    lastNote_ = degree + octave * numDegrees_;

    // --- Advance step ---
    stepInMotif_++;
    if (stepInMotif_ >= motifLen_) {
        stepInMotif_ = 0;
        motifCycles_++;

        // Check M layer: transform after mEvery_ repeats
        if (motifCycles_ % mEvery_ == 0) {
            applyMTransform();

            // Check L layer: act after lEvery_ M cycles
            if (mCycleCount_ % lEvery_ == 0) {
                applyLAction(scale);
            }
        }
    }

    return out;
}

// --- Parameters ---

void QuantumEngine::parameterChanged(int localIndex, int16_t value)
{
    switch (localIndex) {
    case kQMotifLen:
        motifLen_ = clampInt(value, 4, kMaxMotifLen);
        break;
    case kQGateDensity:
        gateDensity_ = clampInt(value, 25, 100);
        regenerateGates();
        break;
    case kQRange:
        range_ = clampInt(value, 2, 12);
        break;
    case kQContour:
        contour_ = clampInt(value, 0, kNumContours - 1);
        break;
    case kQMEvery:
        mEvery_ = clampInt(value, 1, 8);
        break;
    case kQMTransform:
        mTransform_ = clampInt(value, 0, kNumMTransforms - 1);
        break;
    case kQLEvery:
        lEvery_ = clampInt(value, 2, 16);
        break;
    case kQLAction:
        lAction_ = clampInt(value, 0, kNumLActions - 1);
        break;
    case kQGateLen:
        gateLen_ = clampInt(value, 10, 100);
        break;
    case kQVelocity:
        velocity_ = clampInt(value, 0, 100);
        break;
    }
}

static const char* const contourStrings[] = {
    "Arch", "Descend", "Ascend", "Wave", nullptr
};

static const char* const mTransformStrings[] = {
    "Transpose", "Invert", "Rotate", "Permute", nullptr
};

static const char* const lActionStrings[] = {
    "New Motif", "Oct Shift", "Expand", "Reverse", nullptr
};

int QuantumEngine::getParameterDefs(_NT_parameter* defs) const
{
    defs[kQMotifLen]     = { .name = "Motif Len",  .min = 4,  .max = 16,  .def = 8,  .unit = kNT_unitNone,    .scaling = 0, .enumStrings = nullptr };
    defs[kQGateDensity]  = { .name = "Gate Dens",  .min = 25, .max = 100, .def = 75, .unit = kNT_unitPercent, .scaling = 0, .enumStrings = nullptr };
    defs[kQRange]        = { .name = "Range",      .min = 2,  .max = 12,  .def = 5,  .unit = kNT_unitNone,    .scaling = 0, .enumStrings = nullptr };
    defs[kQContour]      = { .name = "Contour",    .min = 0,  .max = 3,   .def = 0,  .unit = kNT_unitEnum,    .scaling = 0, .enumStrings = contourStrings };
    defs[kQMEvery]       = { .name = "M Every",    .min = 1,  .max = 8,   .def = 4,  .unit = kNT_unitNone,    .scaling = 0, .enumStrings = nullptr };
    defs[kQMTransform]   = { .name = "M Action",   .min = 0,  .max = 3,   .def = 0,  .unit = kNT_unitEnum,    .scaling = 0, .enumStrings = mTransformStrings };
    defs[kQLEvery]       = { .name = "L Every",    .min = 2,  .max = 16,  .def = 4,  .unit = kNT_unitNone,    .scaling = 0, .enumStrings = nullptr };
    defs[kQLAction]      = { .name = "L Action",   .min = 0,  .max = 3,   .def = 0,  .unit = kNT_unitEnum,    .scaling = 0, .enumStrings = lActionStrings };
    defs[kQGateLen]      = { .name = "Gate Len",   .min = 10, .max = 100, .def = 75, .unit = kNT_unitPercent, .scaling = 0, .enumStrings = nullptr };
    defs[kQVelocity]     = { .name = "Velocity",   .min = 0,  .max = 100, .def = 80, .unit = kNT_unitPercent, .scaling = 0, .enumStrings = nullptr };
    return kNumQuantumParams;
}

// --- UI ---

int QuantumEngine::currentStep() const { return stepInMotif_; }
int QuantumEngine::sequenceLength() const { return motifLen_; }

int QuantumEngine::getStatusText(char* buf, int maxLen) const
{
    int len = 0;
    if (len < maxLen - 1) buf[len++] = 'L';
    if (len < maxLen - 1) buf[len++] = ':';
    len += fmtInt(buf + len, (int32_t)motifLen_);
    if (len < maxLen - 1) buf[len++] = ' ';
    if (len < maxLen - 1) buf[len++] = 'C';
    if (len < maxLen - 1) buf[len++] = ':';
    len += fmtInt(buf + len, (int32_t)motifCycles_);
    buf[len] = 0;
    return len;
}

void QuantumEngine::getFocusDetail(FocusDetail& detail) const
{
    FocusDetailLine& line1 = detail.lines[0];
    FocusDetailLine& line2 = detail.lines[1];
    line1.clear();
    line2.clear();

    // Line 1: contour shape, step position, transpose offset
    line1.append(contourStrings[contour_ < kNumContours ? contour_ : 0], 15);
    line1.append(" ", 8);
    line1.appendInt(stepInMotif_ + 1, 8);
    line1.appendChar('/', 8);
    line1.appendInt(motifLen_, 8);

    if (transpose_ != 0 || octaveShift_ != 0) {
        line1.append(" T:", 8);
        line1.appendInt(transpose_, 8);
        if (octaveShift_ != 0) {
            line1.append(" O:", 8);
            line1.appendInt(octaveShift_, 8);
        }
    }

    // Line 2: M countdown -> action, L countdown -> action
    int mCountdown = mEvery_ - (motifCycles_ % mEvery_);
    int mCyclesIntoL = mCycleCount_ % lEvery_;
    int lCountdown = lEvery_ - mCyclesIntoL;

    line2.append("M:", 11);
    line2.appendInt(mCountdown, 11);
    line2.appendChar('>', 8);
    line2.append(mTransformStrings[mTransform_ < kNumMTransforms ? mTransform_ : 0], 11);
    line2.append(" L:", 13);
    line2.appendInt(lCountdown, 13);
    line2.appendChar('>', 8);
    line2.append(lActionStrings[lAction_ < kNumLActions ? lAction_ : 0], 13);
}

void QuantumEngine::getFocusBarInfo(FocusBarInfo& info) const
{
    info.numBars = 2;

    // Bar 1: motif pitch contour with gate/rest distinction
    FocusBar& bar1 = info.bars[0];
    bar1.length = motifLen_;
    bar1.playhead = stepInMotif_;

    for (int i = 0; i < motifLen_ && i < kMaxBarSteps; ++i) {
        if (!gates_[i]) {
            bar1.levels[i] = 1;  // Rest = dim
        } else {
            int deg = motif_[i];
            int maxDeg = range_ > 1 ? range_ - 1 : 1;
            int level = 3 + (deg * 12) / maxDeg;
            if (level > 15) level = 15;
            bar1.levels[i] = (uint8_t)level;
        }
    }

    // Bar 2: M/L cycle progress
    // Total length = mEvery_ slots. Filled slots = repeats done this M cycle.
    // Brightness ramps up as L trigger approaches.
    FocusBar& bar2 = info.bars[1];
    bar2.length = mEvery_;
    bar2.playhead = -1;  // No moving playhead

    int repeatsThisCycle = motifCycles_ % mEvery_;
    int mCyclesIntoL = mCycleCount_ % lEvery_;

    // Base brightness scales with how close L trigger is
    int lProgress = (mCyclesIntoL * 10) / (lEvery_ > 1 ? lEvery_ : 1);
    int baseBright = 3 + lProgress;  // 3-13

    for (int i = 0; i < mEvery_ && i < kMaxBarSteps; ++i) {
        if (i < repeatsThisCycle)
            bar2.levels[i] = (uint8_t)baseBright;
        else
            bar2.levels[i] = 1;
    }
}
