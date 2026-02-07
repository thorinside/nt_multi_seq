#ifndef SEQ_MARKOV_ENGINE_H
#define SEQ_MARKOV_ENGINE_H

#include "SequencerEngine.h"

class SeqMarkovEngine : public SequencerEngine {
public:
    SeqMarkovEngine();

    void init(uint32_t sampleRate) override;
    EngineOutput clockTick(const ScaleQuantizer* scale) override;
    void reset() override;
    void parameterChanged(int localIndex, int16_t value) override;
    int getParameterDefs(_NT_parameter* defs) const override;
    int getPageDefs(_NT_parameterPage* page, uint8_t* indices, int baseParamIndex) const override;
    const char* name() const override { return "Markov"; }
    int currentStep() const override;
    int sequenceLength() const override;
    int getStatusText(char* buf, int maxLen) const override;

    enum Param {
        kMarkovStyle = 0,
        kMarkovEmotion,
        kMarkovJumpiness,
        kMarkovRange,
        kMarkovMutation,
        kMarkovLength,
        kMarkovDensity,
        kNumMarkovParams
    };

    enum Style {
        kStyleTonal = 0,
        kStyleStepwise,
        kStyleVamp,
        kStyleLeaping,
        kStyleMelodic,
        kStyleDriving,
        kStyleHypnotic,
        kStyleChaotic,
        kNumStyles
    };

private:
    static constexpr int kMaxSteps = 32;
    static constexpr int kMaxDegrees = 128;

    // Style definitions: behavioral parameters that generate NxN matrices
    // for any number of scale degrees
    struct StyleDef {
        float selfBoost;      // How sticky the current degree is (0-20)
        float stepBias;       // Positive=stepwise, negative=leaping (-1 to 1)
        float homeGravity;    // Pull toward degree 0 / tonic (0-1)
        float fifthGravity;   // Pull toward approximate dominant (0-1)
    };

    static const StyleDef kStyleDefs[kNumStyles];

    // Parameters
    int style_;       // 0-7
    int emotion_;     // 0-100
    int jumpiness_;   // 0-100
    int range_;       // 1-3
    int mutation_;    // 0-100
    int length_;      // 1-32
    int density_;     // 1-100

    // Sequence state
    struct Step {
        int8_t scaleDegree;
        int8_t octaveShift;
        uint8_t velocity;
        bool active;
    };

    Step sequence_[kMaxSteps];
    int currentStep_;
    int lastDegree_;
    int numDegrees_;
    bool needsRegenerate_;

    // PRNG
    uint32_t rngState_;
    uint32_t rng();
    float rngFloat();

    float computeWeight(int fromDeg, int toDeg, int numDegrees) const;
    float applyEmotion(float weight, int fromDeg, int toDeg) const;
    int pickNextDegree(int currentDeg, int numDegrees);
    void generateSequence(int numDegrees);
    void mutateSequence(int numDegrees);
};

#endif // SEQ_MARKOV_ENGINE_H
