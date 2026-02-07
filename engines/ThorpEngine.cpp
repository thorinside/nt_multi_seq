#include "ThorpEngine.h"
#include "../scale/ScaleQuantizer.h"

// --- Static pattern data (shared across all instances, no per-instance cost) ---

// 23 rhythmic patterns, 8 steps each.
// Values 1-8 map to scale degrees 0-7. Value 0 = rest.
static const int8_t kPatterns[ThorpEngine::kNumPatterns][ThorpEngine::kPatternLen] = {
    { 1, 2, 3, 4, 5, 6, 7, 8 },  //  0: Ascending
    { 8, 7, 6, 5, 4, 3, 2, 1 },  //  1: Descending
    { 1, 2, 3, 4, 5, 6, 7, 6 },  //  2: UpDown
    { 8, 7, 6, 5, 4, 3, 2, 3 },  //  3: DownUp
    { 1, 8, 2, 7, 3, 6, 4, 5 },  //  4: Alternate
    { 1, 3, 5, 1, 3, 5, 1, 3 },  //  5: TriadOnRoot
    { 3, 5, 1, 3, 5, 1, 3, 5 },  //  6: TriadOnThird
    { 1, 3, 5, 7, 5, 3, 1, 3 },  //  7: SeventhArp
    { 1, 5, 2, 6, 3, 7, 4, 8 },  //  8: FifthLeap
    { 1, 2, 3, 5, 6, 1, 2, 3 },  //  9: PentatonicAsc
    { 6, 5, 3, 2, 1, 6, 5, 3 },  // 10: PentatonicDesc
    { 1, 2, 3, 5, 6, 5, 3, 2 },  // 11: MajorBlues
    { 1, 3, 4, 5, 7, 5, 4, 3 },  // 12: MinorBlues
    { 1, 5, 2, 6, 3, 7, 4, 1 },  // 13: CircleOfFifths
    { 1, 4, 7, 3, 6, 2, 5, 8 },  // 14: Arpeggio4ths
    { 1, 3, 5, 7, 2, 4, 6, 8 },  // 15: Arpeggio3rds
    { 2, 5, 1, 7, 3, 8, 4, 6 },  // 16: Random1
    { 3, 6, 2, 8, 4, 1, 5, 7 },  // 17: Random2
    { 5, 2, 8, 4, 7, 3, 1, 6 },  // 18: Random3
    { 1, 0, 2, 0, 3, 0, 4, 0 },  // 19: Syncopated
    { 1, 2, 0, 0, 5, 6, 0, 0 },  // 20: Burst2
    { 1, 2, 3, 0, 0, 0, 6, 7 },  // 21: Burst3
    { 1, 2, 3, 4, 0, 4, 3, 2 },  // 22: ClusterStep
};

static const char* const kPatternStrings[] = {
    "Ascending", "Descending", "UpDown", "DownUp", "Alternate",
    "TriadRoot", "TriadThird", "7thArp", "FifthLeap",
    "PentaAsc", "PentaDesc", "MajBlues", "MinBlues",
    "Circle5th", "Arp4ths", "Arp3rds",
    "Random1", "Random2", "Random3",
    "Syncopated", "Burst2", "Burst3", "ClusterStep",
    nullptr
};

// 15 velocity patterns, 8 steps each.
// Values 0-100, where 50 = unity/center in Lua's bipolar system.
// We map these to 0-5V output range: value/100 * 5.0
static const uint8_t kVelPatterns[ThorpEngine::kNumVelPatterns][ThorpEngine::kPatternLen] = {
    { 50, 50, 50, 50, 50, 50, 50, 50 },  //  0: Constant
    { 75, 35, 40, 35, 75, 35, 40, 35 },  //  1: Accent
    { 35, 75, 45, 75, 35, 75, 45, 75 },  //  2: OffBeat
    { 25, 30, 35, 40, 42, 45, 47, 50 },  //  3: Crescendo
    { 50, 47, 45, 42, 40, 35, 30, 25 },  //  4: Diminuendo
    { 65, 30, 65, 30, 65, 30, 65, 30 },  //  5: Strong/Weak
    { 60, 40, 55, 37, 60, 40, 55, 37 },  //  6: SwingFeel
    { 42, 60, 32, 55, 37, 57, 40, 62 },  //  7: RandomWalk
    { 75, 25, 75, 25, 75, 25, 75, 25 },  //  8: Pulse
    { 40, 42, 45, 47, 50, 47, 45, 42 },  //  9: Breathe
    { 80, 20, 80, 20, 80, 20, 80, 20 },  // 10: Hard/Soft
    { 30, 32, 35, 37, 40, 42, 45, 47 },  // 11: BuildUp
    { 47, 45, 42, 40, 37, 35, 32, 30 },  // 12: BreakDown
    { 70, 35, 60, 40, 70, 35, 60, 40 },  // 13: Pump
    { 47, 42, 52, 40, 47, 42, 52, 40 },  // 14: Subtle
};

static const char* const kVelPatternStrings[] = {
    "Constant", "Accent", "OffBeat", "Crescendo", "Diminuendo",
    "Str/Weak", "Swing", "RndWalk", "Pulse", "Breathe",
    "Hard/Soft", "BuildUp", "BrkDown", "Pump", "Subtle",
    nullptr
};

static const char* const kStepModeStrings[] = {
    "Seq", "PingPong", "RndWalk", "Random", nullptr
};

static const char* const kReverseStrings[] = {
    "Off", "On", nullptr
};

// --- Implementation ---

ThorpEngine::ThorpEngine()
    : pattern_(0)
    , velPattern_(0)
    , length_(8)
    , offset_(0)
    , reverse_(0)
    , numNotes_(4)
    , gateProb_(100)
    , octJump_(0)
    , octRange_(1)
    , stepMode_(kStepSeq)
    , mutation_(0)
    , currentStep_(-1)
    , pingDir_(1)
    , velStep_(0)
    , patternDirty_(true)
    , rngState_(12345)
{
    loadPattern();
}

uint32_t ThorpEngine::rng()
{
    rngState_ ^= rngState_ << 13;
    rngState_ ^= rngState_ >> 17;
    rngState_ ^= rngState_ << 5;
    return rngState_;
}

int ThorpEngine::rngRange(int min, int max)
{
    if (min >= max) return min;
    return min + (int)(rng() % (uint32_t)(max - min + 1));
}

void ThorpEngine::loadPattern()
{
    for (int i = 0; i < kPatternLen; ++i)
        workingPattern_[i] = kPatterns[pattern_][i];
    patternDirty_ = false;
}

void ThorpEngine::init(uint32_t sampleRate)
{
    rngState_ = sampleRate ^ 0xBEEFCAFE;
    currentStep_ = -1;
    pingDir_ = 1;
    velStep_ = 0;
    loadPattern();
}

void ThorpEngine::reset()
{
    currentStep_ = -1;
    pingDir_ = 1;
    velStep_ = 0;
    loadPattern();
}

EngineOutput ThorpEngine::clockTick(const ScaleQuantizer* scale)
{
    EngineOutput out = { 0.0f, 0.0f, 5.0f, 60 };

    int length = length_;
    if (length < 1) length = 1;
    if (length > 32) length = 32;

    // Track whether we wrapped (for mutation)
    bool wrapped = false;

    // Advance step based on step mode
    switch (stepMode_) {
    case kStepSeq:
        currentStep_++;
        if (currentStep_ >= length) {
            currentStep_ = 0;
            wrapped = true;
        }
        break;

    case kStepPingPong:
        currentStep_ += pingDir_;
        if (currentStep_ >= length) {
            currentStep_ = length - 1;
            pingDir_ = -1;
            if (length == 1) wrapped = true;
        } else if (currentStep_ < 0) {
            currentStep_ = 0;
            pingDir_ = 1;
            wrapped = true;
        }
        break;

    case kStepRndWalk: {
        int delta = rngRange(-1, 1);
        currentStep_ += delta;
        if (currentStep_ >= length) {
            currentStep_ = length - 1;
            wrapped = true;
        } else if (currentStep_ < 0) {
            currentStep_ = 0;
            wrapped = true;
        }
        break;
    }

    case kStepRandom:
        currentStep_ = rngRange(0, length - 1);
        // No clear loop boundary for random; use probability instead
        if ((int)(rng() % (uint32_t)length) == 0)
            wrapped = true;
        break;

    default:
        currentStep_ = (currentStep_ + 1) % length;
        break;
    }

    // At loop boundary: maybe mutate the working pattern
    if (wrapped && mutation_ > 0) {
        if ((int)(rng() % 100) < mutation_) {
            // Randomize 1-2 steps in the working pattern
            int numMutations = rngRange(1, 2);
            for (int m = 0; m < numMutations; ++m) {
                int idx = rngRange(0, kPatternLen - 1);
                // Mutate to a random note value (1 to numNotes_) or rest (0)
                // 85% chance of a note, 15% chance of rest
                if ((int)(rng() % 100) < 85) {
                    workingPattern_[idx] = (int8_t)rngRange(1, numNotes_);
                } else {
                    workingPattern_[idx] = 0;
                }
            }
        }
    }

    // Get pattern value at current step with offset and optional reversal
    int patIdx = currentStep_ % kPatternLen;
    patIdx = (patIdx + offset_) % kPatternLen;
    if (reverse_) {
        patIdx = (kPatternLen - 1) - patIdx;
    }
    int8_t patVal = workingPattern_[patIdx];

    // Rest: pattern value 0
    if (patVal == 0) {
        out.gate = 0.0f;
        // Velocity from vel pattern even on rest (some users want vel CV to update)
        int velIdx = velStep_ % kPatternLen;
        out.velocity = (float)kVelPatterns[velPattern_][velIdx] / 100.0f * 5.0f;
        velStep_++;
        return out;
    }

    // Gate probability check
    if (gateProb_ < 100 && (int)(rng() % 100) >= gateProb_) {
        out.gate = 0.0f;
        int velIdx = velStep_ % kPatternLen;
        out.velocity = (float)kVelPatterns[velPattern_][velIdx] / 100.0f * 5.0f;
        velStep_++;
        return out;
    }

    // Map pattern value to scale degree
    // patVal is 1-8, numNotes_ is 2-8 (how many scale degrees are available)
    int degree = (patVal - 1) % numNotes_;

    // Octave shift
    int octShift = 0;
    if (octJump_ > 0 && (int)(rng() % 100) < octJump_) {
        octShift = rngRange(-octRange_, octRange_);
        // Avoid 0 shift (that's not really a "jump")
        if (octShift == 0) octShift = rngRange(0, 1) ? 1 : -1;
    }

    // Velocity from velocity pattern
    int velIdx = velStep_ % kPatternLen;
    out.velocity = (float)kVelPatterns[velPattern_][velIdx] / 100.0f * 5.0f;
    velStep_++;

    // Gate on
    out.gate = 5.0f;

    // Pitch via scale quantizer
    if (scale && scale->isLoaded()) {
        out.pitch = scale->quantize(degree, octShift, 0);
        out.midiNote = scale->scaleDegreeToMidi(degree, octShift + 5, 0);
    } else {
        // Fallback: chromatic, each degree = 1 semitone
        out.pitch = (float)(degree + octShift * 12) / 12.0f;
        int midi = 60 + degree + octShift * 12;
        if (midi < 0) midi = 0;
        if (midi > 127) midi = 127;
        out.midiNote = (uint8_t)midi;
    }

    return out;
}

void ThorpEngine::parameterChanged(int localIndex, int16_t value)
{
    switch (localIndex) {
    case kThorpPattern:
        pattern_ = value;
        if (pattern_ < 0) pattern_ = 0;
        if (pattern_ >= kNumPatterns) pattern_ = kNumPatterns - 1;
        loadPattern();
        break;
    case kThorpVelPattern:
        velPattern_ = value;
        if (velPattern_ < 0) velPattern_ = 0;
        if (velPattern_ >= kNumVelPatterns) velPattern_ = kNumVelPatterns - 1;
        break;
    case kThorpLength:     length_ = value; break;
    case kThorpOffset:     offset_ = value; break;
    case kThorpReverse:    reverse_ = value; break;
    case kThorpNumNotes:   numNotes_ = value; break;
    case kThorpGateProb:   gateProb_ = value; break;
    case kThorpOctJump:    octJump_ = value; break;
    case kThorpOctRange:   octRange_ = value; break;
    case kThorpStepMode:   stepMode_ = value; break;
    case kThorpMutation:   mutation_ = value; break;
    }
}

int ThorpEngine::getParameterDefs(_NT_parameter* defs) const
{
    defs[kThorpPattern]    = { .name = "Pattern",    .min = 0, .max = kNumPatterns - 1,    .def = 0,        .unit = kNT_unitEnum,    .scaling = kNT_scalingNone, .enumStrings = kPatternStrings };
    defs[kThorpVelPattern] = { .name = "Vel Pat",    .min = 0, .max = kNumVelPatterns - 1, .def = 0,        .unit = kNT_unitEnum,    .scaling = kNT_scalingNone, .enumStrings = kVelPatternStrings };
    defs[kThorpLength]     = { .name = "Length",     .min = 1, .max = 32,                  .def = 8,        .unit = kNT_unitNone,    .scaling = kNT_scalingNone, .enumStrings = nullptr };
    defs[kThorpOffset]     = { .name = "Offset",     .min = 0, .max = 7,                   .def = 0,        .unit = kNT_unitNone,    .scaling = kNT_scalingNone, .enumStrings = nullptr };
    defs[kThorpReverse]    = { .name = "Reverse",    .min = 0, .max = 1,                   .def = 0,        .unit = kNT_unitEnum,    .scaling = kNT_scalingNone, .enumStrings = kReverseStrings };
    defs[kThorpNumNotes]   = { .name = "Num Notes",  .min = 2, .max = 8,                   .def = 4,        .unit = kNT_unitNone,    .scaling = kNT_scalingNone, .enumStrings = nullptr };
    defs[kThorpGateProb]   = { .name = "Gate Prob",  .min = 1, .max = 100,                 .def = 100,      .unit = kNT_unitPercent, .scaling = kNT_scalingNone, .enumStrings = nullptr };
    defs[kThorpOctJump]    = { .name = "Oct Jump",   .min = 0, .max = 100,                 .def = 0,        .unit = kNT_unitPercent, .scaling = kNT_scalingNone, .enumStrings = nullptr };
    defs[kThorpOctRange]   = { .name = "Oct Range",  .min = 1, .max = 3,                   .def = 1,        .unit = kNT_unitNone,    .scaling = kNT_scalingNone, .enumStrings = nullptr };
    defs[kThorpStepMode]   = { .name = "Step Mode",  .min = 0, .max = kNumStepModes - 1,   .def = kStepSeq, .unit = kNT_unitEnum,    .scaling = kNT_scalingNone, .enumStrings = kStepModeStrings };
    defs[kThorpMutation]   = { .name = "Mutation",   .min = 0, .max = 100,                 .def = 0,        .unit = kNT_unitPercent, .scaling = kNT_scalingNone, .enumStrings = nullptr };
    return kNumThorpParams;
}

int ThorpEngine::currentStep() const { return currentStep_; }
int ThorpEngine::sequenceLength() const { return length_; }

int ThorpEngine::getStatusText(char* buf, int maxLen) const
{
    // Show pattern name abbreviated
    const char* pat = kPatternStrings[pattern_];
    int len = 0;
    while (*pat && len < maxLen - 1) buf[len++] = *pat++;
    buf[len] = 0;
    return len;
}

int ThorpEngine::getPageDefs(_NT_parameterPage* page, uint8_t* indices, int baseParamIndex) const
{
    for (int i = 0; i < kNumThorpParams; ++i)
        indices[i] = (uint8_t)(baseParamIndex + i);
    page->name = "Thorp";
    page->numParams = kNumThorpParams;
    page->group = 0;
    page->unused[0] = 0;
    page->unused[1] = 0;
    page->params = indices;
    return kNumThorpParams;
}
