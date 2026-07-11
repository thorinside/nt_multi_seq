#include "SomaEngine.h"
#include "../scale/ScaleQuantizer.h"

static inline int clampInt(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

SomaEngine::SomaEngine()
    : octaveSpread_(50)
    , noteMutate_(70)
    , gateMutate_(80)
    , length_(8)
    , velocity_(100)
    , currentStep_(0)
    , numDegrees_(7)
    , scaleLoaded_(false)
    , rngState_(12345)
{
    for (int i = 0; i < kMaxSteps; ++i) {
        notePattern_[i] = 0;
        gatePattern_[i] = true;
    }
    for (int i = 0; i < 128; ++i)
        probabilities_[i] = 0.0f;
}

uint32_t SomaEngine::rng()
{
    rngState_ ^= rngState_ << 13;
    rngState_ ^= rngState_ >> 17;
    rngState_ ^= rngState_ << 5;
    return rngState_;
}

float SomaEngine::rngFloat()
{
    return (float)(rng() & 0x7FFFFFFF) / (float)0x7FFFFFFF;
}

void SomaEngine::init(uint32_t sampleRate)
{
    // Seed RNG with something varying
    rngState_ = sampleRate ^ 0xDEADBEEF;
    currentStep_ = 0;
}

void SomaEngine::computeProbabilities(const ScaleQuantizer* scale)
{
    numDegrees_ = scale && scale->isLoaded() ? (int)scale->numNotes() : 7;
    if (numDegrees_ <= 0) numDegrees_ = 7;

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

int SomaEngine::weightedPick(int numDegrees)
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

void SomaEngine::initializePatterns(const ScaleQuantizer* scale, int length)
{
    computeProbabilities(scale);
    for (int i = 0; i < length; ++i) {
        notePattern_[i] = weightedPick(numDegrees_);
        gatePattern_[i] = rngFloat() > 0.5f;
    }
}

EngineOutput SomaEngine::clockTick(const ScaleQuantizer* scale)
{
    EngineOutput out = { 0.0f, 0.0f, 5.0f, 60 };

    int length = length_;
    if (length < 1) length = 1;
    if (length > kMaxSteps) length = kMaxSteps;

    // Reinitialize patterns when scale first becomes available or note count changes
    if (scale && scale->isLoaded()) {
        if (!scaleLoaded_ || (int)scale->numNotes() != numDegrees_) {
            initializePatterns(scale, length);
            scaleLoaded_ = true;
        }
    }

    // Advance step
    currentStep_ = (currentStep_ + 1) % length;

    // Mutate note at current step based on probability
    float noteMutateF = (float)noteMutate_ / 100.0f;
    if (rngFloat() < noteMutateF) {
        computeProbabilities(scale);
        notePattern_[currentStep_] = weightedPick(numDegrees_);
    }

    // Mutate gate at current step based on probability
    float gateMutateF = (float)gateMutate_ / 100.0f;
    if (rngFloat() < gateMutateF) {
        gatePattern_[currentStep_] = !gatePattern_[currentStep_];
    }

    // Get current note
    int scaleDegree = notePattern_[currentStep_];
    if (scaleDegree < 0) scaleDegree = 0;

    // Apply octave variation
    int octaveOffset = 0;
    int octaveSpreadPct = octaveSpread_;
    if (octaveSpreadPct > 0) {
        int octRange = (octaveSpreadPct * 3) / 100;
        if (octRange > 0)
            octaveOffset = (int)(rng() % (uint32_t)(octRange + 1));
    }

    // Output
    out.gate = gatePattern_[currentStep_] ? 5.0f : 0.0f;

    if (scale && scale->isLoaded()) {
        // Use scale quantizer for pitch
        out.pitch = scale->quantize(scaleDegree, octaveOffset, 0);
        out.midiNote = scale->scaleDegreeToMidi(scaleDegree, octaveOffset + 5, 0);
    } else {
        // Fallback: chromatic
        out.pitch = (float)(scaleDegree + octaveOffset * 12) / 12.0f;
        out.midiNote = (uint8_t)(60 + scaleDegree + octaveOffset * 12);
    }

    out.velocity = (float)velocity_ * 0.05f; // 0-100% -> 0-5V

    return out;
}

void SomaEngine::reset()
{
    currentStep_ = 0;
}

void SomaEngine::parameterChanged(int localIndex, int16_t value)
{
    switch (localIndex) {
    case kSomaOctaveSpread:
        octaveSpread_ = clampInt(value, 0, 100);
        break;
    case kSomaNoteMutate:
        noteMutate_ = clampInt(value, 0, 100);
        break;
    case kSomaGateMutate:
        gateMutate_ = clampInt(value, 0, 100);
        break;
    case kSomaLength:
        length_ = clampInt(value, 1, kMaxSteps);
        break;
    case kSomaVelocity:
        velocity_ = clampInt(value, 0, 100);
        break;
    }
}

int SomaEngine::getParameterDefs(_NT_parameter* defs) const
{
    defs[kSomaOctaveSpread] = { .name = "Oct Spread", .min = 0, .max = 100, .def = 50, .unit = kNT_unitPercent, .scaling = 0, .enumStrings = nullptr };
    defs[kSomaNoteMutate]   = { .name = "Note Mutate", .min = 0, .max = 100, .def = 70, .unit = kNT_unitPercent, .scaling = 0, .enumStrings = nullptr };
    defs[kSomaGateMutate]   = { .name = "Gate Mutate", .min = 0, .max = 100, .def = 80, .unit = kNT_unitPercent, .scaling = 0, .enumStrings = nullptr };
    defs[kSomaLength]       = { .name = "Length", .min = 1, .max = 64, .def = 8, .unit = kNT_unitNone, .scaling = 0, .enumStrings = nullptr };
    defs[kSomaVelocity]     = { .name = "Velocity", .min = 0, .max = 100, .def = 100, .unit = kNT_unitPercent, .scaling = 0, .enumStrings = nullptr };
    return kNumSomaParams;
}

int SomaEngine::currentStep() const { return currentStep_; }
int SomaEngine::sequenceLength() const { return length_; }

int SomaEngine::getStatusText(char* buf, int maxLen) const
{
    // Show "L:8 M:70%"
    int len = 0;
    if (len < maxLen - 1) buf[len++] = 'L';
    if (len < maxLen - 1) buf[len++] = ':';
    len += fmtInt(buf + len, (int32_t)length_);
    if (len < maxLen - 2) { buf[len++] = ' '; buf[len++] = 'M'; }
    if (len < maxLen - 1) buf[len++] = ':';
    len += fmtInt(buf + len, (int32_t)noteMutate_);
    if (len < maxLen - 1) buf[len++] = '%';
    buf[len] = 0;
    return len;
}

void SomaEngine::getFocusDetail(FocusDetail& detail) const
{
    FocusDetailLine& line1 = detail.lines[0];
    FocusDetailLine& line2 = detail.lines[1];
    line1.clear();
    line2.clear();

    // Line 1: Oct Spread:50%  Note Mut:70%
    line1.append("Oct Spread:", 8);
    line1.appendInt(octaveSpread_, 8);
    line1.appendChar('%', 8);
    line1.append("  Note Mut:", 8);
    line1.appendInt(noteMutate_, 8);
    line1.appendChar('%', 8);

    // Line 2: Gate Mut:80%  Length:8
    line2.append("Gate Mut:", 6);
    line2.appendInt(gateMutate_, 6);
    line2.appendChar('%', 6);
    line2.append("  Length:", 6);
    line2.appendInt(length_, 6);
}
