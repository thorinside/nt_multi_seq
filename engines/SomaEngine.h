#ifndef SOMA_ENGINE_H
#define SOMA_ENGINE_H

#include "SequencerEngine.h"
#include "../scale/ScaleQuantizer.h"

class SomaEngine : public SequencerEngine {
public:
    SomaEngine();

    void init(uint32_t sampleRate) override;
    EngineOutput clockTick(const ScaleQuantizer* scale) override;
    void reset() override;
    void parameterChanged(int localIndex, int16_t value) override;
    int getParameterDefs(_NT_parameter* defs) const override;
    int getPageDefs(_NT_parameterPage* page, uint8_t* indices, int baseParamIndex) const override;
    const char* name() const override { return "Soma"; }
    void getFocusDetail(FocusDetail& detail) const override;
    int currentStep() const override;
    int sequenceLength() const override;
    int getStatusText(char* buf, int maxLen) const override;

    // Engine-specific parameter indices (local)
    enum Param {
        kSomaOctaveSpread = 0,
        kSomaNoteMutate,
        kSomaGateMutate,
        kSomaLength,
        kSomaVelocity,
        kNumSomaParams
    };

private:
    static constexpr int kMaxSteps = 64;

    void computeProbabilities(const ScaleQuantizer* scale);
    int weightedPick(int numDegrees);
    void initializePatterns(const ScaleQuantizer* scale, int length);

    // Parameters (cached from v[])
    int octaveSpread_;   // 0-100
    int noteMutate_;     // 0-100
    int gateMutate_;     // 0-100
    int length_;         // 1-64
    int velocity_;       // 0-100

    // Sequence state
    int currentStep_;
    int notePattern_[kMaxSteps];   // Scale degrees
    bool gatePattern_[kMaxSteps];

    // Probability weights for scale degrees (max 128 degrees)
    float probabilities_[128];
    int numDegrees_;
    bool scaleLoaded_; // true once we've initialized patterns with a real scale

    // Simple PRNG state (xorshift32)
    uint32_t rngState_;
    uint32_t rng();
    float rngFloat(); // 0.0 to 1.0
};

#endif // SOMA_ENGINE_H
