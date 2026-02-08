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
    void noteCvGate(float vOct, bool rising) override;
    bool usesTimedGate() const override { return true; }
    int gateLengthPercent() const override { return gateLen_; }
    // Focus-page song editing helpers.
    void uiSetChainLength(int len);
    void uiSetChainPos(int pos);
    int uiChainLength() const;
    int uiChainPos() const;
    int uiChainSlot() const;
    void uiAdjustChainPos(int delta);
    void uiAdjustChainSlot(int delta);
    void uiInsertChainStep();
    void uiDeleteChainStep();

    enum Param {
        kThorpPattern = 0,
        kThorpVelPattern,
        kThorpLength,
        kThorpOffset,
        kThorpReverse,
        kThorpArpSlot,
        kThorpGateProb,
        kThorpOctJump,
        kThorpOctRange,
        kThorpSequenceMode,
        kThorpGlobalVelocity,
        kThorpGateLen,
        kThorpPlayMode,
        kThorpChainLen,
        kNumThorpParams
    };

    enum SequenceMode {
        kModeSeq = 0,
        kModePingPong,
        kModeRndWalk,
        kModeRandom,
        kNumSequenceModes
    };

    enum PlayMode {
        kPlayJam = 0,
        kPlaySong,
        kNumPlayModes
    };

    static constexpr int kNumPatterns = 23;
    static constexpr int kPatternLen = 8;
    static constexpr int kNumVelPatterns = 15;
    static constexpr int kNumSlots = 16;

private:
    struct Slot {
        uint8_t pattern;
        uint8_t velPattern;
        uint8_t length;
        uint8_t offset;
        uint8_t reverse;
        uint8_t numNotes;
        uint8_t notes[16];
        uint8_t velocities[kPatternLen];
    };

    // Parameters (cached)
    int pattern_;
    int velPattern_;
    int length_;
    int offset_;
    int reverse_;
    int arpSlot_;
    int gateProb_;
    int octJump_;
    int octRange_;
    int sequenceMode_;
    int globalVelocity_;
    int gateLen_;
    int playMode_;
    int chainLen_;

    // Playback state
    int currentStep_;
    int pingDir_;
    int velocityStepCount_;
    int stepCount_;
    int localStep_;
    int chainPos_;
    int editChainPos_;
    bool gateActive_;
    int lastMidiNote_;
    float lastPitch_;
    float lastVelocity_;

    Slot slots_[kNumSlots];
    uint8_t chain_[kNumSlots];

    static constexpr int kMaxHeldNotes = 16;
    uint8_t latchedNotes_[kMaxHeldNotes];
    uint8_t activeNotes_[kMaxHeldNotes];
    int numLatchedNotes_;
    int numActiveNotes_;

    // PRNG
    uint32_t rngState_;
    uint32_t rng();
    int rngRange(int min, int max);
    void addUniqueNote(uint8_t* arr, int& count, uint8_t note);
    void removeNote(uint8_t* arr, int& count, uint8_t note);
    void initSlots();
    void refreshSlotVelocity(int slotIndex);
    void loadSelectedSlotParams();
    void saveSelectedSlotParams();
    void copyLatchedToSelectedSlot();
    void advanceSongChain();
};

#endif // THORP_ENGINE_H
