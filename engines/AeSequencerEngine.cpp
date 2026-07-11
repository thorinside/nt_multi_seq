#include "AeSequencerEngine.h"
#include "../scale/ScaleQuantizer.h"

static const char* const polarityStrings[] = {
    "Positive", "Bipolar", "Negative", nullptr
};

static inline int clampInt(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

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
    , velocity_(100)
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

int16_t AeSequencerEngine::sequenceRaw(uint32_t seed, int stepIndex) const
{
    // Deterministic hash from (seed, step) gives stable pseudo-random
    // 16-bit values without storing 32-step tables for all 20x2 banks.
    uint32_t x = seed ^ ((uint32_t)stepIndex * 0x9E3779B9u);
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return (int16_t)(x & 0xFFFFu);
}

void AeSequencerEngine::init(uint32_t sampleRate)
{
    rngState_ = sampleRate ^ 0xCAFEBABE;

    for (int s = 0; s < kNumSequences; ++s) {
        voltSeqs_[s].seed = rng();
        voltSeqs_[s].currentStep = 0;
        gateSeqs_[s].seed = rng();
        gateSeqs_[s].currentStep = 0;
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

EngineOutput AeSequencerEngine::clockTick(const ScaleQuantizer* scale)
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
    int16_t cvRaw = sequenceRaw(vs.seed, (int)vs.currentStep);
    float voltage = mapRawToVoltage(cvRaw, effMin, effMax);
    out.pitch = quantizeVoltage(voltage, effMin, effMax);

    // Advance gate sequence
    GateSequence& gs = gateSeqs_[gateSeq_];
    int gateSteps = gateSteps_;
    if (gateSteps < 1) gateSteps = 1;
    if (gateSteps > kMaxSteps) gateSteps = kMaxSteps;
    gs.currentStep = (gs.currentStep + 1) % gateSteps;

    // Gate based on threshold
    // AE essence: threshold 16-bit raw values for gate state.
    int gateRaw = (int)sequenceRaw(gs.seed, (int)gs.currentStep);
    int gateNorm = ((gateRaw + 32768) * 100) / 65535; // 0..100
    out.gate = (gateNorm >= threshold_) ? 5.0f : 0.0f;

    // MIDI note approximation from voltage (1V/oct)
    int midiNote = (int)(out.pitch * 12.0f + 60.0f + 0.5f);
    if (midiNote < 0) midiNote = 0;
    if (midiNote > 127) midiNote = 127;

    // Optional scale quantization, controlled by host "Scale On".
    if (scale && scale->isLoaded()) {
        out.pitch = scale->midiNoteToVOct((uint8_t)midiNote, 0);
        int qMidi = (int)(out.pitch * 12.0f + 60.0f + 0.5f);
        if (qMidi < 0) qMidi = 0;
        if (qMidi > 127) qMidi = 127;
        out.midiNote = (uint8_t)qMidi;
    } else {
        out.midiNote = (uint8_t)midiNote;
    }
    out.velocity = (float)velocity_ * 0.05f; // 0-100% -> 0-5V

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
        cvSeq_ = clampInt((int)value - 1, 0, kNumSequences - 1);
        break;
    case kAeGateSeq:
        gateSeq_ = clampInt((int)value - 1, 0, kNumSequences - 1);
        break;
    case kAeCvSteps:    cvSteps_ = clampInt(value, 1, kMaxSteps); break;
    case kAeMinCv:      minCv_ = clampInt(value, -100, 100); break;
    case kAeMaxCv:      maxCv_ = clampInt(value, -100, 100); break;
    case kAePolarity:   polarity_ = clampInt(value, 0, kNumPolarities - 1); break;
    case kAeBitDepth:   bitDepth_ = clampInt(value, 2, 16); break;
    case kAeGateSteps:  gateSteps_ = clampInt(value, 1, kMaxSteps); break;
    case kAeThreshold:  threshold_ = clampInt(value, 1, 100); break;
    case kAeVelocity:   velocity_ = clampInt(value, 0, 100); break;
    }
}

int AeSequencerEngine::getParameterDefs(_NT_parameter* defs) const
{
    defs[kAeCvSeq]     = { .name = "CV Seq",    .min = 1,    .max = kNumSequences, .def = 1,  .unit = kNT_unitNone,    .scaling = kNT_scalingNone, .enumStrings = nullptr };
    defs[kAeGateSeq]   = { .name = "Gate Seq",  .min = 1,    .max = kNumSequences, .def = 1,  .unit = kNT_unitNone,    .scaling = kNT_scalingNone, .enumStrings = nullptr };
    defs[kAeCvSteps]   = { .name = "CV Steps",  .min = 1,    .max = kMaxSteps,     .def = 8,  .unit = kNT_unitNone,    .scaling = kNT_scalingNone, .enumStrings = nullptr };
    defs[kAeMinCv]     = { .name = "Min CV",    .min = -100, .max = 100,           .def = -10,.unit = kNT_unitVolts,   .scaling = kNT_scaling10,   .enumStrings = nullptr };
    defs[kAeMaxCv]     = { .name = "Max CV",    .min = -100, .max = 100,           .def = 10, .unit = kNT_unitVolts,   .scaling = kNT_scaling10,   .enumStrings = nullptr };
    defs[kAePolarity]  = { .name = "Polarity",  .min = 0,    .max = kNumPolarities - 1, .def = kPolarityBipolar, .unit = kNT_unitEnum, .scaling = kNT_scalingNone, .enumStrings = polarityStrings };
    defs[kAeBitDepth]  = { .name = "CV Bit Depth", .min = 2, .max = 16,            .def = 16, .unit = kNT_unitNone,    .scaling = kNT_scalingNone, .enumStrings = nullptr };
    defs[kAeGateSteps] = { .name = "Gate Steps", .min = 1,   .max = kMaxSteps,     .def = 16, .unit = kNT_unitNone,    .scaling = kNT_scalingNone, .enumStrings = nullptr };
    defs[kAeThreshold] = { .name = "Gate Threshold", .min = 1, .max = 100,         .def = 50, .unit = kNT_unitPercent, .scaling = kNT_scalingNone, .enumStrings = nullptr };
    defs[kAeVelocity]  = { .name = "Velocity",  .min = 0,    .max = 100,           .def = 100,.unit = kNT_unitPercent, .scaling = kNT_scalingNone, .enumStrings = nullptr };
    return kNumAeParams;
}

int AeSequencerEngine::currentStep() const { return voltSeqs_[cvSeq_].currentStep; }
int AeSequencerEngine::sequenceLength() const { return cvSteps_; }

int AeSequencerEngine::cvStepCount() const
{
    int n = cvSteps_;
    if (n < 1) n = 1;
    if (n > kMaxSteps) n = kMaxSteps;
    return n;
}

int AeSequencerEngine::gateStepCount() const
{
    int n = gateSteps_;
    if (n < 1) n = 1;
    if (n > kMaxSteps) n = kMaxSteps;
    return n;
}

int AeSequencerEngine::currentCvStep() const { return voltSeqs_[cvSeq_].currentStep; }
int AeSequencerEngine::currentGateStep() const { return gateSeqs_[gateSeq_].currentStep; }

uint8_t AeSequencerEngine::getCvLevel(int stepIndex) const
{
    int cvLen = cvStepCount();
    int idx = stepIndex % cvLen;

    float effMin, effMax;
    getEffectiveRange(effMin, effMax);

    int16_t cvRaw = sequenceRaw(voltSeqs_[cvSeq_].seed, idx);
    float voltage = mapRawToVoltage(cvRaw, effMin, effMax);
    float quantized = quantizeVoltage(voltage, effMin, effMax);

    float range = effMax - effMin;
    int levels = (1 << bitDepth_) - 1;
    if (levels < 1) levels = 1;
    int quantIdx = 0;
    if (range > 0.0f) {
        float stepSize = range / (float)levels;
        quantIdx = (int)((quantized - effMin) / stepSize + 0.5f);
        if (quantIdx < 0) quantIdx = 0;
        if (quantIdx > levels) quantIdx = levels;
    }
    return (uint8_t)((quantIdx * 15 + (levels / 2)) / levels);
}

bool AeSequencerEngine::getGateOn(int stepIndex) const
{
    int gateLen = gateStepCount();
    int idx = stepIndex % gateLen;

    int gateRaw = (int)sequenceRaw(gateSeqs_[gateSeq_].seed, idx);
    int gateNorm = ((gateRaw + 32768) * 100) / 65535; // 0..100
    return (gateNorm >= threshold_);
}

int AeSequencerEngine::getStatusText(char* buf, int maxLen) const
{
    // Show "V1 G1 8/16"
    int len = 0;
    if (len < maxLen - 1) buf[len++] = 'V';
    len += fmtInt(buf + len, (int32_t)(cvSeq_ + 1));
    if (len < maxLen - 2) { buf[len++] = ' '; buf[len++] = 'G'; }
    len += fmtInt(buf + len, (int32_t)(gateSeq_ + 1));
    if (len < maxLen - 1) buf[len++] = ' ';
    len += fmtInt(buf + len, (int32_t)cvSteps_);
    if (len < maxLen - 1) buf[len++] = '/';
    len += fmtInt(buf + len, (int32_t)gateSteps_);
    buf[len] = 0;
    return len;
}

void AeSequencerEngine::getFocusBarInfo(FocusBarInfo& info) const
{
    int cvLen = cvStepCount();
    int gateLen = gateStepCount();

    info.numBars = 2;

    // CV bar: brightness from CV level, minimum 2 so steps are visible
    FocusBar& cvBar = info.bars[0];
    cvBar.length = cvLen;
    cvBar.playhead = currentCvStep();
    for (int i = 0; i < cvLen && i < kMaxBarSteps; ++i) {
        int lv = getCvLevel(i);
        cvBar.levels[i] = (uint8_t)(lv < 2 ? 2 : lv);
    }

    // Gate bar: on = bright (12), off = dim (2)
    FocusBar& gateBar = info.bars[1];
    gateBar.length = gateLen;
    gateBar.playhead = currentGateStep();
    for (int i = 0; i < gateLen && i < kMaxBarSteps; ++i)
        gateBar.levels[i] = getGateOn(i) ? 12 : 2;
}

void AeSequencerEngine::getFocusDetail(FocusDetail& detail) const
{
    FocusDetailLine& line1 = detail.lines[0];
    FocusDetailLine& line2 = detail.lines[1];
    line1.clear();
    line2.clear();

    // Line 1: CV Seq:1 Steps:8  Gate Seq:1 Steps:16
    line1.append("CV Seq:", 8);
    line1.appendInt(cvSeq_, 8);
    line1.append(" Steps:", 8);
    line1.appendInt(cvSteps_, 8);
    line1.append("  Gate Seq:", 8);
    line1.appendInt(gateSeq_, 8);
    line1.append(" Steps:", 8);
    line1.appendInt(gateSteps_, 8);

    // Line 2: Range: -1.0..+1.0V  Bits:16  Thresh:50%
    line2.append("Range:", 6);
    line2.appendFloat((float)minCv_ / 10.0f, 1, 6);
    line2.append("..", 6);
    line2.appendFloat((float)maxCv_ / 10.0f, 1, 6);
    line2.appendChar('V', 6);
    line2.append("  Bits:", 6);
    line2.appendInt(bitDepth_, 6);
    line2.append("  Thresh:", 6);
    line2.appendInt(threshold_, 6);
    line2.appendChar('%', 6);
}
