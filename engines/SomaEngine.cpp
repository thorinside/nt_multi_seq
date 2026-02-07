#include "SomaEngine.h"
#include "../scale/ScaleQuantizer.h"

SomaEngine::SomaEngine()
    : octaveSpread_(50)
    , noteMutate_(70)
    , gateMutate_(80)
    , length_(8)
    , currentStep_(0)
    , numDegrees_(7)
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
    if (!scale || !scale->isLoaded()) {
        // Default: equal probability across 7 degrees
        numDegrees_ = 7;
        for (int i = 0; i < numDegrees_; ++i)
            probabilities_[i] = 1.0f / (float)numDegrees_;
        return;
    }

    numDegrees_ = (int)scale->numNotes();
    if (numDegrees_ <= 0) {
        numDegrees_ = 7;
        for (int i = 0; i < numDegrees_; ++i)
            probabilities_[i] = 1.0f / (float)numDegrees_;
        return;
    }

    // Weight: give "characteristic" degrees (those not in the major scale)
    // a higher probability. Since we're using .scl files (arbitrary tunings),
    // we just use uniform weighting. The Lua Soma weighted against major scale
    // degrees but that concept doesn't map well to arbitrary .scl tunings.
    // Instead: slight bias toward middle degrees (creates more interesting patterns)
    float total = 0.0f;
    for (int i = 0; i < numDegrees_; ++i) {
        // Gentle bell curve: center degrees slightly more likely
        float center = (float)(numDegrees_ - 1) / 2.0f;
        float dist = (float)i - center;
        float w = 1.0f + 0.5f * (1.0f - (dist * dist) / (center * center + 1.0f));
        probabilities_[i] = w;
        total += w;
    }
    for (int i = 0; i < numDegrees_; ++i)
        probabilities_[i] /= total;
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

    // Recompute probabilities if scale changed
    if (scale && scale->isLoaded() && (int)scale->numNotes() != numDegrees_) {
        initializePatterns(scale, length);
    }

    // Initialize on first tick if needed
    if (numDegrees_ <= 0) {
        initializePatterns(scale, length);
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

    out.velocity = 5.0f; // Full velocity

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
        octaveSpread_ = value;
        break;
    case kSomaNoteMutate:
        noteMutate_ = value;
        break;
    case kSomaGateMutate:
        gateMutate_ = value;
        break;
    case kSomaLength:
        length_ = value;
        break;
    }
}

int SomaEngine::getParameterDefs(_NT_parameter* defs) const
{
    defs[kSomaOctaveSpread] = { .name = "Oct Spread", .min = 0, .max = 100, .def = 50, .unit = kNT_unitPercent, .scaling = 0, .enumStrings = nullptr };
    defs[kSomaNoteMutate]   = { .name = "Note Mutate", .min = 0, .max = 100, .def = 70, .unit = kNT_unitPercent, .scaling = 0, .enumStrings = nullptr };
    defs[kSomaGateMutate]   = { .name = "Gate Mutate", .min = 0, .max = 100, .def = 80, .unit = kNT_unitPercent, .scaling = 0, .enumStrings = nullptr };
    defs[kSomaLength]       = { .name = "Length", .min = 1, .max = 64, .def = 8, .unit = kNT_unitNone, .scaling = 0, .enumStrings = nullptr };
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
    len += NT_intToString(buf + len, (int32_t)length_);
    if (len < maxLen - 2) { buf[len++] = ' '; buf[len++] = 'M'; }
    if (len < maxLen - 1) buf[len++] = ':';
    len += NT_intToString(buf + len, (int32_t)noteMutate_);
    if (len < maxLen - 1) buf[len++] = '%';
    buf[len] = 0;
    return len;
}

int SomaEngine::getPageDefs(_NT_parameterPage* page, uint8_t* indices, int baseParamIndex) const
{
    for (int i = 0; i < kNumSomaParams; ++i)
        indices[i] = (uint8_t)(baseParamIndex + i);
    page->name = "Soma";
    page->numParams = kNumSomaParams;
    page->group = 0;
    page->unused[0] = 0;
    page->unused[1] = 0;
    page->params = indices;
    return kNumSomaParams;
}
