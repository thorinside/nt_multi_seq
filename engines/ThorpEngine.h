#ifndef THORP_ENGINE_H
#define THORP_ENGINE_H

#include "SequencerEngine.h"

class ThorpEngine : public SequencerEngine {
public:
    ThorpEngine();

    void init(uint32_t sampleRate) override;
    EngineOutput clockTick(const ScaleQuantizer* scale) override;
    void reset() override;
    void parameterChanged(int localIndex, int16_t value) override;
    int getParameterDefs(_NT_parameter* defs) const override;
    int getPageDefs(_NT_parameterPage* page, uint8_t* indices, int baseParamIndex) const override;
    const char* name() const override { return "Thorp"; }
    int currentStep() const override;
    int sequenceLength() const override;
    int getStatusText(char* buf, int maxLen) const override;
    void noteOn(uint8_t midiNote, uint8_t velocity) override;
    void noteOff(uint8_t midiNote) override;
    bool usesTimedGate() const override { return true; }
    int gateLengthPercent() const override { return gateLen_; }

    enum Param {
        kThorpPattern = 0,
        kThorpVelPattern,
        kThorpLength,
        kThorpOffset,
        kThorpReverse,
        kThorpNumNotes,
        kThorpGateProb,
        kThorpOctJump,
        kThorpOctRange,
        kThorpStepMode,
        kThorpMutation,
        kThorpGateLen,
        kNumThorpParams
    };

    enum StepMode {
        kStepSeq = 0,
        kStepPingPong,
        kStepRndWalk,
        kStepRandom,
        kNumStepModes
    };

    static constexpr int kNumPatterns = 23;
    static constexpr int kPatternLen = 8;
    static constexpr int kNumVelPatterns = 15;

private:
    // Parameters (cached)
    int pattern_;
    int velPattern_;
    int length_;
    int offset_;
    int reverse_;
    int numNotes_;
    int gateProb_;
    int octJump_;
    int octRange_;
    int stepMode_;
    int mutation_;
    int gateLen_;

    // Playback state
    int currentStep_;
    int pingDir_;
    int velStep_;
    static constexpr int kMaxHeldNotes = 16;
    uint8_t latchedNotes_[kMaxHeldNotes];
    uint8_t activeNotes_[kMaxHeldNotes];
    int numLatchedNotes_;
    int numActiveNotes_;

    // Working copy of pattern for mutation
    int8_t workingPattern_[kPatternLen];
    bool patternDirty_;

    // PRNG
    uint32_t rngState_;
    uint32_t rng();
    int rngRange(int min, int max);

    void loadPattern();
    void addUniqueNote(uint8_t* arr, int& count, uint8_t note);
    void removeNote(uint8_t* arr, int& count, uint8_t note);
};

#endif // THORP_ENGINE_H
