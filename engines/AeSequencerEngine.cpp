#include "AeSequencerEngine.h"

static const char* const polarityStrings[] = {
    "Positive", "Bipolar", "Negative", nullptr
};

AeSequencerEngine::AeSequencerEngine()
    : cvSeq_(0)
    , gateSeq_(0)
    , cvSteps_(8)
    , minCv_(-10)
    , maxCv_(10)
    , polarity_(kPolarityBipolar)
    , bitDepth_(16)
    , gateSteps_(16)
    , threshold_(50)
    , rngState_(54321)
{
}

uint32_t AeSequencerEngine::rng()
{
    rngState_ ^= rngState_ << 13;
    rngState_ ^= rngState_ >> 17;
    rngState_ ^= rngState_ << 5;
    return rngState_;
}

void AeSequencerEngine::init(uint32_t sampleRate)
{
    rngState_ = sampleRate ^ 0xCAFEBABE;

    for (int s = 0; s < kNumSequences; ++s) {
        voltSeqs_[s].currentStep = 0;
        for (int i = 0; i < kMaxSteps; ++i)
            voltSeqs_[s].steps[i] = (int16_t)(rng() & 0xFFFF);

        gateSeqs_[s].currentStep = 0;
        for (int i = 0; i < kMaxSteps; ++i)
            gateSeqs_[s].steps[i] = (uint8_t)(rng() % 100 + 1);
    }
}

void AeSequencerEngine::getEffectiveRange(float& effMin, float& effMax) const
{
    float minV = (float)minCv_ / 10.0f;
    float maxV = (float)maxCv_ / 10.0f;

    switch (polarity_) {
    case kPolarityPositive:
        effMin = 0.0f;
        effMax = maxV;
        break;
    case kPolarityNegative:
        effMin = minV;
        effMax = 0.0f;
        break;
    default: // Bipolar
        effMin = minV;
        effMax = maxV;
        break;
    }
}

float AeSequencerEngine::mapRawToVoltage(int16_t raw, float effMin, float effMax) const
{
    float fraction;
    switch (polarity_) {
    case kPolarityPositive: {
        int clamped = raw < 0 ? 0 : raw;
        fraction = (float)clamped / 32767.0f;
        break;
    }
    case kPolarityNegative: {
        int clamped = raw > 0 ? 0 : raw;
        fraction = (float)(clamped + 32768) / 32768.0f;
        break;
    }
    default: // Bipolar
        fraction = (float)(raw + 32768) / 65535.0f;
        break;
    }
    return fraction * (effMax - effMin) + effMin;
}

float AeSequencerEngine::quantizeVoltage(float value, float effMin, float effMax) const
{
    int levels = (1 << bitDepth_) - 1;
    if (levels < 1) levels = 1;
    float range = effMax - effMin;
    if (range <= 0.0f) return effMin;
    float stepSize = range / (float)levels;
    int index = (int)((value - effMin) / stepSize + 0.5f);
    float quantized = (float)index * stepSize + effMin;
    if (quantized < effMin) quantized = effMin;
    if (quantized > effMax) quantized = effMax;
    return quantized;
}

EngineOutput AeSequencerEngine::clockTick(const ScaleQuantizer* /*scale*/)
{
    EngineOutput out = { 0.0f, 0.0f, 5.0f, 60 };

    // Advance voltage sequence
    VoltageSequence& vs = voltSeqs_[cvSeq_];
    int cvSteps = cvSteps_;
    if (cvSteps < 1) cvSteps = 1;
    if (cvSteps > kMaxSteps) cvSteps = kMaxSteps;
    vs.currentStep = (vs.currentStep + 1) % cvSteps;

    // Compute voltage
    float effMin, effMax;
    getEffectiveRange(effMin, effMax);
    float voltage = mapRawToVoltage(vs.steps[vs.currentStep], effMin, effMax);
    out.pitch = quantizeVoltage(voltage, effMin, effMax);

    // Advance gate sequence
    GateSequence& gs = gateSeqs_[gateSeq_];
    int gateSteps = gateSteps_;
    if (gateSteps < 1) gateSteps = 1;
    if (gateSteps > kMaxSteps) gateSteps = kMaxSteps;
    gs.currentStep = (gs.currentStep + 1) % gateSteps;

    // Gate based on threshold
    out.gate = (gs.steps[gs.currentStep] >= threshold_) ? 5.0f : 0.0f;

    // MIDI note approximation from voltage (1V/oct)
    int midiNote = (int)(out.pitch * 12.0f + 60.0f);
    if (midiNote < 0) midiNote = 0;
    if (midiNote > 127) midiNote = 127;
    out.midiNote = (uint8_t)midiNote;

    return out;
}

void AeSequencerEngine::reset()
{
    for (int s = 0; s < kNumSequences; ++s) {
        voltSeqs_[s].currentStep = 0;
        gateSeqs_[s].currentStep = 0;
    }
}

void AeSequencerEngine::parameterChanged(int localIndex, int16_t value)
{
    switch (localIndex) {
    case kAeCvSeq:
        cvSeq_ = value - 1;
        if (cvSeq_ < 0) cvSeq_ = 0;
        if (cvSeq_ >= kNumSequences) cvSeq_ = kNumSequences - 1;
        break;
    case kAeGateSeq:
        gateSeq_ = value - 1;
        if (gateSeq_ < 0) gateSeq_ = 0;
        if (gateSeq_ >= kNumSequences) gateSeq_ = kNumSequences - 1;
        break;
    case kAeCvSteps:    cvSteps_ = value;    break;
    case kAeMinCv:      minCv_ = value;      break;
    case kAeMaxCv:      maxCv_ = value;      break;
    case kAePolarity:   polarity_ = value;   break;
    case kAeBitDepth:   bitDepth_ = value;   break;
    case kAeGateSteps:  gateSteps_ = value;  break;
    case kAeThreshold:  threshold_ = value;  break;
    }
}

int AeSequencerEngine::getParameterDefs(_NT_parameter* defs) const
{
    defs[kAeCvSeq]     = { .name = "CV Seq",    .min = 1,    .max = kNumSequences, .def = 1,  .unit = kNT_unitNone,    .scaling = kNT_scalingNone, .enumStrings = nullptr };
    defs[kAeGateSeq]   = { .name = "Gate Seq",  .min = 1,    .max = kNumSequences, .def = 1,  .unit = kNT_unitNone,    .scaling = kNT_scalingNone, .enumStrings = nullptr };
    defs[kAeCvSteps]   = { .name = "CV Steps",  .min = 1,    .max = kMaxSteps,     .def = 8,  .unit = kNT_unitNone,    .scaling = kNT_scalingNone, .enumStrings = nullptr };
    defs[kAeMinCv]     = { .name = "Min CV",    .min = -100, .max = 100,           .def = -10,.unit = kNT_unitNone,    .scaling = kNT_scaling10,   .enumStrings = nullptr };
    defs[kAeMaxCv]     = { .name = "Max CV",    .min = -100, .max = 100,           .def = 10, .unit = kNT_unitNone,    .scaling = kNT_scaling10,   .enumStrings = nullptr };
    defs[kAePolarity]  = { .name = "Polarity",  .min = 0,    .max = kNumPolarities - 1, .def = kPolarityBipolar, .unit = kNT_unitEnum, .scaling = kNT_scalingNone, .enumStrings = polarityStrings };
    defs[kAeBitDepth]  = { .name = "Bit Depth", .min = 2,    .max = 16,            .def = 16, .unit = kNT_unitNone,    .scaling = kNT_scalingNone, .enumStrings = nullptr };
    defs[kAeGateSteps] = { .name = "Gate Steps", .min = 1,   .max = kMaxSteps,     .def = 16, .unit = kNT_unitNone,    .scaling = kNT_scalingNone, .enumStrings = nullptr };
    defs[kAeThreshold] = { .name = "Threshold", .min = 1,    .max = 100,           .def = 50, .unit = kNT_unitPercent, .scaling = kNT_scalingNone, .enumStrings = nullptr };
    return kNumAeParams;
}

int AeSequencerEngine::currentStep() const { return voltSeqs_[cvSeq_].currentStep; }
int AeSequencerEngine::sequenceLength() const { return cvSteps_; }

int AeSequencerEngine::getStatusText(char* buf, int maxLen) const
{
    // Show "S1 8/32"
    int len = 0;
    if (len < maxLen - 1) buf[len++] = 'S';
    len += NT_intToString(buf + len, (int32_t)(cvSeq_ + 1));
    if (len < maxLen - 1) buf[len++] = ' ';
    len += NT_intToString(buf + len, (int32_t)cvSteps_);
    if (len < maxLen - 1) buf[len++] = '/';
    len += NT_intToString(buf + len, (int32_t)32);
    buf[len] = 0;
    return len;
}

int AeSequencerEngine::getPageDefs(_NT_parameterPage* page, uint8_t* indices, int baseParamIndex) const
{
    for (int i = 0; i < kNumAeParams; ++i)
        indices[i] = (uint8_t)(baseParamIndex + i);
    page->name = "AE Seq";
    page->numParams = kNumAeParams;
    page->group = 0;
    page->unused[0] = 0;
    page->unused[1] = 0;
    page->params = indices;
    return kNumAeParams;
}
