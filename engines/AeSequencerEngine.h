#ifndef AE_SEQUENCER_ENGINE_H
#define AE_SEQUENCER_ENGINE_H

#include "SequencerEngine.h"

class AeSequencerEngine : public SequencerEngine {
public:
    AeSequencerEngine();

    void init(uint32_t sampleRate) override;
    EngineOutput clockTick(const ScaleQuantizer* scale) override;
    void reset() override;
    void parameterChanged(int localIndex, int16_t value) override;
    int getParameterDefs(_NT_parameter* defs) const override;
    const char* name() const override { return "AE Seq"; }
    void getFocusDetail(FocusDetail& detail) const override;
    void getFocusBarInfo(FocusBarInfo& info) const override;
    int currentStep() const override;
    int sequenceLength() const override;
    int getStatusText(char* buf, int maxLen) const override;
    int cvStepCount() const;
    int gateStepCount() const;
    int currentCvStep() const;
    int currentGateStep() const;
    uint8_t getCvLevel(int stepIndex) const;
    bool getGateOn(int stepIndex) const;

    enum Param {
        kAeCvSeq = 0,
        kAeGateSeq,
        kAeCvSteps,
        kAeMinCv,
        kAeMaxCv,
        kAePolarity,
        kAeBitDepth,
        kAeGateSteps,
        kAeThreshold,
        kAeVelocity,
        kNumAeParams
    };

    enum Polarity {
        kPolarityPositive = 0,
        kPolarityBipolar,
        kPolarityNegative,
        kNumPolarities
    };

private:
    static constexpr int kNumSequences = 20;
    static constexpr int kMaxSteps = 32;

    // Parameters
    int cvSeq_;       // 0-7
    int gateSeq_;     // 0-7
    int cvSteps_;     // 1-32
    int minCv_;       // -100 to 100 (decivolts)
    int maxCv_;       // -100 to 100
    int polarity_;    // 0=Positive, 1=Bipolar, 2=Negative
    int bitDepth_;    // 2-16
    int gateSteps_;   // 1-32
    int threshold_;   // 1-100
    int velocity_;    // 0-100

    struct VoltageSequence {
        uint32_t seed;
        uint8_t currentStep;
    };

    struct GateSequence {
        uint32_t seed;
        uint8_t currentStep;
    };

    VoltageSequence voltSeqs_[kNumSequences];
    GateSequence gateSeqs_[kNumSequences];

    // PRNG
    uint32_t rngState_;
    uint32_t rng();
    int16_t sequenceRaw(uint32_t seed, int stepIndex) const;

    void getEffectiveRange(float& effMin, float& effMax) const;
    float mapRawToVoltage(int16_t raw, float effMin, float effMax) const;
    float quantizeVoltage(float value, float effMin, float effMax) const;
};

#endif // AE_SEQUENCER_ENGINE_H
