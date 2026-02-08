#include "SeqMarkovEngine.h"
#include "../scale/ScaleQuantizer.h"
#include <math.h>

static const char* const styleStrings[] = {
    "Tonal", "Stepwise", "Vamp", "Leaping",
    "Melodic", "Driving", "Hypnotic", "Chaotic", nullptr
};

// Each style is defined by behavioral parameters rather than a hardcoded matrix.
// This lets any style work with any number of scale degrees (5, 7, 12, 19, 31...).
//
// selfBoost:    how likely to repeat the current degree (0=no boost, 12=very sticky)
// stepBias:     positive favors adjacent degrees, negative favors distant leaps
// homeGravity:  pull toward degree 0 (tonic)
// fifthGravity: pull toward the approximate dominant (~7/12 of the period)

const SeqMarkovEngine::StyleDef SeqMarkovEngine::kStyleDefs[kNumStyles] = {
    //               self   step   home  fifth
    /* Tonal    */ {  1.0f,  0.6f, 0.8f, 0.6f },  // Strong tonal center, moderate steps
    /* Stepwise */ {  0.5f,  0.9f, 0.1f, 0.1f },  // Wandering, strongly prefers neighbors
    /* Vamp     */ { 12.0f,  0.3f, 0.2f, 0.1f },  // Stays on current note, rare moves
    /* Leaping  */ {  0.3f, -0.5f, 0.2f, 0.3f },  // Prefers distant intervals
    /* Melodic  */ {  2.0f,  0.5f, 0.3f, 0.3f },  // Balanced, flowing lines
    /* Driving  */ {  5.0f,  0.4f, 0.6f, 0.5f },  // Moderate vamp, strong tonal pull
    /* Hypnotic */ {  8.0f,  0.5f, 0.3f, 0.2f },  // Sticky, gradual drift
    /* Chaotic  */ {  0.1f,  0.1f, 0.05f, 0.05f }, // Near-uniform, unpredictable
};

SeqMarkovEngine::SeqMarkovEngine()
    : style_(kStyleTonal)
    , emotion_(50)
    , jumpiness_(30)
    , range_(2)
    , mutation_(20)
    , length_(16)
    , density_(60)
    , velocity_(100)
    , currentStep_(0)
    , lastDegree_(0)
    , numDegrees_(7)
    , needsRegenerate_(true)
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
    return (float)(rng() & 0x7FFFFFFF) / (float)0x7FFFFFFF;
}

void SeqMarkovEngine::init(uint32_t sampleRate)
{
    rngState_ = sampleRate ^ 0xFEEDFACE;
    currentStep_ = 0;
    lastDegree_ = 0;
    needsRegenerate_ = true;
}

float SeqMarkovEngine::computeWeight(int fromDeg, int toDeg, int numDegrees) const
{
    const StyleDef& s = kStyleDefs[style_];
    float w = 1.0f;

    // Self-transition boost
    if (fromDeg == toDeg) {
        w += s.selfBoost;
        return w;
    }

    // Circular distance between degrees (wraps around the scale)
    int absDist = fromDeg > toDeg ? fromDeg - toDeg : toDeg - fromDeg;
    int wrapDist = numDegrees - absDist;
    int circDist = absDist < wrapDist ? absDist : wrapDist;
    float maxDist = (float)numDegrees / 2.0f;
    if (maxDist < 1.0f) maxDist = 1.0f;
    float normalizedDist = (float)circDist / maxDist;

    // Step bias: positive = exponential decay with distance (stepwise)
    //            negative = inverted decay (leaping)
    if (s.stepBias >= 0.0f) {
        w *= expf(-normalizedDist * s.stepBias * 4.0f);
    } else {
        w *= expf(-(1.0f - normalizedDist) * (-s.stepBias) * 4.0f);
    }

    // Home (tonic) gravity — degree 0 attracts
    if (toDeg == 0) {
        w *= (1.0f + s.homeGravity * 5.0f);
    }

    // Dominant gravity — approximate perfect fifth at 7/12 of period
    int fifthDeg = (numDegrees * 7 + 6) / 12;
    if (fifthDeg >= numDegrees) fifthDeg = numDegrees - 1;
    if (fifthDeg > 0 && toDeg == fifthDeg) {
        w *= (1.0f + s.fifthGravity * 3.0f);
    }

    return w;
}

float SeqMarkovEngine::applyEmotion(float weight, int fromDeg, int toDeg) const
{
    float emotionF = (float)emotion_ / 100.0f;
    float influence;

    if (emotionF < 0.5f) {
        // Low emotion: melancholic — favor descending motion
        float strength = (0.5f - emotionF) * 1.5f;
        if (toDeg < fromDeg)
            influence = 1.0f + strength;
        else if (toDeg > fromDeg)
            influence = 1.0f - strength;
        else
            influence = 1.0f;
    } else {
        // High emotion: uplifting — favor ascending motion
        float strength = (emotionF - 0.5f) * 1.5f;
        if (toDeg > fromDeg)
            influence = 1.0f + strength;
        else if (toDeg < fromDeg)
            influence = 1.0f - strength;
        else
            influence = 1.0f;
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

    // Weighted random pick
    float r = rngFloat() * total;
    float accum = 0.0f;
    for (int j = 0; j < numDegrees; ++j) {
        accum += weights[j];
        if (r <= accum) return j;
    }
    return numDegrees - 1;
}

void SeqMarkovEngine::generateSequence(int numDegrees)
{
    int deg = lastDegree_;
    if (deg >= numDegrees) deg = 0;

    for (int i = 0; i < length_; ++i) {
        deg = pickNextDegree(deg, numDegrees);

        // Octave shift based on jumpiness and emotion
        int8_t octShift = 0;
        if (range_ > 1) {
            float emotionF = (float)emotion_ / 100.0f;
            float jumpF = (float)jumpiness_ / 100.0f;
            float jumpUp = jumpF * emotionF;
            float jumpDown = jumpF * (1.0f - emotionF);

            if (rngFloat() < jumpUp) {
                octShift = 1;
                if (range_ > 2 && rngFloat() < 0.3f) octShift = 2;
            } else if (rngFloat() < jumpDown) {
                octShift = -1;
                if (range_ > 2 && rngFloat() < 0.3f) octShift = -2;
            }
        }

        // Velocity influenced by emotion (more extreme = higher minimum)
        int emotionDist = emotion_ > 50 ? emotion_ - 50 : 50 - emotion_;
        int vMin = 80 + emotionDist;
        int vMax = 127;
        if (vMin > vMax - 10) vMin = vMax - 10;
        uint8_t vel = (uint8_t)(vMin + (int)(rng() % (uint32_t)(vMax - vMin + 1)));

        // Gate based on density
        bool active = ((int)(rng() % 100) < density_);

        sequence_[i] = { (int8_t)deg, octShift, vel, active };
    }

    lastDegree_ = deg;
}

void SeqMarkovEngine::mutateSequence(int numDegrees)
{
    // Generate a candidate sequence
    Step candidate[kMaxSteps];
    int deg = lastDegree_;
    if (deg >= numDegrees) deg = 0;

    for (int i = 0; i < length_; ++i) {
        deg = pickNextDegree(deg, numDegrees);

        int8_t octShift = 0;
        if (range_ > 1) {
            float emotionF = (float)emotion_ / 100.0f;
            float jumpF = (float)jumpiness_ / 100.0f;
            if (rngFloat() < jumpF * emotionF) octShift = 1;
            else if (rngFloat() < jumpF * (1.0f - emotionF)) octShift = -1;
        }

        int emotionDist = emotion_ > 50 ? emotion_ - 50 : 50 - emotion_;
        int vMin = 80 + emotionDist;
        int vMax = 127;
        if (vMin > vMax - 10) vMin = vMax - 10;
        uint8_t vel = (uint8_t)(vMin + (int)(rng() % (uint32_t)(vMax - vMin + 1)));
        bool active = ((int)(rng() % 100) < density_);

        candidate[i] = { (int8_t)deg, octShift, vel, active };
    }

    // Selectively replace steps based on mutation rate
    float mutF = (float)mutation_ / 100.0f;
    for (int i = 0; i < length_; ++i) {
        if (rngFloat() < mutF)
            sequence_[i] = candidate[i];
    }

    lastDegree_ = deg;
}

EngineOutput SeqMarkovEngine::clockTick(const ScaleQuantizer* scale)
{
    EngineOutput out = { 0.0f, 0.0f, 5.0f, 60 };

    int numDeg;
    if (scale && scale->isLoaded()) {
        numDeg = (int)scale->numNotes();
    } else {
        numDeg = 7;
    }

    // Regenerate if scale changed or first tick
    if (needsRegenerate_ || numDeg != numDegrees_) {
        numDegrees_ = numDeg;
        generateSequence(numDeg);
        needsRegenerate_ = false;
    }

    int length = length_;
    if (length < 1) length = 1;
    if (length > kMaxSteps) length = kMaxSteps;

    // Advance step
    currentStep_++;
    if (currentStep_ >= length) {
        currentStep_ = 0;
        // At sequence boundary: maybe mutate
        if (mutation_ > 0 && (int)(rng() % 100) < mutation_)
            mutateSequence(numDeg);
    }

    const Step& step = sequence_[currentStep_];

    // Gate
    out.gate = step.active ? 5.0f : 0.0f;

    // Velocity (step.velocity is 0-127, map to 0-5V)
    out.velocity = ((float)step.velocity / 25.4f) * ((float)velocity_ / 100.0f);

    // Pitch
    int deg = step.scaleDegree;
    int octShift = step.octaveShift;

    if (scale && scale->isLoaded()) {
        out.pitch = scale->quantize(deg, octShift, 0);
        out.midiNote = scale->scaleDegreeToMidi(deg, octShift + 5, 0);
    } else {
        out.pitch = (float)(deg + octShift * 12) / 12.0f;
        out.midiNote = (uint8_t)(60 + deg + octShift * 12);
    }

    return out;
}

void SeqMarkovEngine::reset()
{
    currentStep_ = 0;
    lastDegree_ = 0;
    needsRegenerate_ = true;
}

void SeqMarkovEngine::parameterChanged(int localIndex, int16_t value)
{
    switch (localIndex) {
    case kMarkovStyle:     style_ = value; needsRegenerate_ = true; break;
    case kMarkovEmotion:   emotion_ = value; break;
    case kMarkovJumpiness: jumpiness_ = value; break;
    case kMarkovRange:     range_ = value; break;
    case kMarkovMutation:  mutation_ = value; break;
    case kMarkovLength:    length_ = value; break;
    case kMarkovDensity:   density_ = value; break;
    case kMarkovVelocity:  velocity_ = value; break;
    }
}

int SeqMarkovEngine::getParameterDefs(_NT_parameter* defs) const
{
    defs[kMarkovStyle]     = { .name = "Style",     .min = 0, .max = kNumStyles - 1, .def = kStyleTonal, .unit = kNT_unitEnum,    .scaling = kNT_scalingNone, .enumStrings = styleStrings };
    defs[kMarkovEmotion]   = { .name = "Emotion",   .min = 0, .max = 100,            .def = 50,          .unit = kNT_unitPercent, .scaling = kNT_scalingNone, .enumStrings = nullptr };
    defs[kMarkovJumpiness] = { .name = "Jumpiness", .min = 0, .max = 100,            .def = 30,          .unit = kNT_unitPercent, .scaling = kNT_scalingNone, .enumStrings = nullptr };
    defs[kMarkovRange]     = { .name = "Oct Range", .min = 1, .max = 3,              .def = 2,           .unit = kNT_unitNone,    .scaling = kNT_scalingNone, .enumStrings = nullptr };
    defs[kMarkovMutation]  = { .name = "Mutation",  .min = 0, .max = 100,            .def = 20,          .unit = kNT_unitPercent, .scaling = kNT_scalingNone, .enumStrings = nullptr };
    defs[kMarkovLength]    = { .name = "Length",    .min = 1, .max = kMaxSteps,       .def = 16,          .unit = kNT_unitNone,    .scaling = kNT_scalingNone, .enumStrings = nullptr };
    defs[kMarkovDensity]   = { .name = "Density",   .min = 1, .max = 100,            .def = 60,          .unit = kNT_unitPercent, .scaling = kNT_scalingNone, .enumStrings = nullptr };
    defs[kMarkovVelocity]  = { .name = "Velocity",  .min = 0, .max = 100,            .def = 100,         .unit = kNT_unitPercent, .scaling = kNT_scalingNone, .enumStrings = nullptr };
    return kNumMarkovParams;
}

int SeqMarkovEngine::currentStep() const { return currentStep_; }
int SeqMarkovEngine::sequenceLength() const { return length_; }

int SeqMarkovEngine::getStatusText(char* buf, int maxLen) const
{
    // Show style name
    const char* s = styleStrings[style_];
    int len = 0;
    while (*s && len < maxLen - 1) buf[len++] = *s++;
    buf[len] = 0;
    return len;
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
