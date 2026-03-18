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
    bool usesTimedGate() const override { return true; }
    int gateLengthPercent() const override { return 99; }
    void getFocusDetail(FocusDetail& detail) const override;
    void getFocusBarInfo(FocusBarInfo& info) const override;
    int currentStep() const override;
    int sequenceLength() const override;
    int getStatusText(char* buf, int maxLen) const override;
    void uiForceRandomize();
    void uiForceRegenerate();

    enum Param {
        kMarkovStyle = 0,
        kMarkovEmotion,
        kMarkovJumpiness,
        kMarkovRange,
        kMarkovMutation,
        kMarkovLength,
        kMarkovRandomizeSwitch,
        kMarkovRegenerateSwitch,
        kNumMarkovParams
    };

    enum Style {
        kStylePopRock = 0,
        kStyleClassical,
        kStyleJazz,
        kStyleTechno,
        kStyleMinimalTechno,
        kStyleMelodicTechno,
        kStyleLmdAllGenres,
        kStyleLmdElectronic,
        kNumStyles
    };

private:
    static constexpr int kMaxSteps = 64;
    static constexpr int kMaxDegrees = 128;
    static constexpr int kMatrixDeg = 7;
    static constexpr int kRhythmPatternLen = 16;
    static constexpr int kNumRhythmPatterns = 8;

    // One 7x7 transition matrix per style, mapped to any scale via degreeMap_.
    static const float kStyleMatrices[kNumStyles][kMatrixDeg][kMatrixDeg];
    static const uint8_t kRhythmPatterns[kNumRhythmPatterns][kRhythmPatternLen];

    // Parameters
    int style_;       // 0-7
    int emotion_;     // 0-100
    int jumpiness_;   // 0-100
    int range_;       // 1-3
    int mutation_;    // 0-100
    int length_;      // 1-64
    int randomizeSwitch_;  // 0/1
    int regenerateSwitch_; // 0/1

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
    bool rhythmInitialized_;
    bool regeneratePending_;
    bool randomizePending_;

    // Degree mapping: maps each real scale degree to a base matrix degree (0-6).
    int degreeMap_[kMaxDegrees];

    // PRNG
    uint32_t rngState_;
    uint32_t rng();
    float rngFloat();

    void buildDegreeMap(const ScaleQuantizer* scale);
    float applyEmotion(float weight, int fromDeg, int toDeg) const;
    int pickNextDegree(int currentDeg, int numDegrees);
    void initializeRhythm();
    void ensureAtLeastOneActive(int length);
    void generateSequence(int numDegrees);
    void mutateSequence(int numDegrees);
};

#endif // SEQ_MARKOV_ENGINE_H
