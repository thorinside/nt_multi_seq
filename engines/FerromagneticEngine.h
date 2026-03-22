#ifndef FERROMAGNETIC_ENGINE_H
#define FERROMAGNETIC_ENGINE_H

#include "SequencerEngine.h"
#include "../scale/ScaleQuantizer.h"

class FerromagneticEngine : public SequencerEngine {
public:
    FerromagneticEngine();

    void init(uint32_t sampleRate) override;
    EngineOutput clockTick(const ScaleQuantizer* scale) override;
    void reset() override;
    void parameterChanged(int localIndex, int16_t value) override;
    int getParameterDefs(_NT_parameter* defs) const override;
    int getPageDefs(_NT_parameterPage* page, uint8_t* indices, int baseParamIndex) const override;
    const char* name() const override { return "Ferro"; }
    void getFocusDetail(FocusDetail& detail) const override;
    void getFocusBarInfo(FocusBarInfo& info) const override;
    int currentStep() const override;
    int sequenceLength() const override;
    int getStatusText(char* buf, int maxLen) const override;

    bool usesTimedGate() const override { return role_ == kRoleMelody; }
    int gateLengthPercent() const override { return gateLength_; }

    // Engine-specific parameter indices (local)
    enum Param {
        kFerroRole = 0,
        kFerroLoopSteps,
        kFerroMaxLayers,
        kFerroHarmonyMode,
        kFerroVoicing,
        kFerroCompletion,
        kFerroNoteDensity,
        kFerroGateLength,
        kFerroVelocity,
        kFerroOctSpread,
        kFerroRefreshRate,
        kNumFerroParams
    };

    enum Role { kRoleMelody = 0, kRoleLoopTrig, kRoleRecGate };
    enum HarmonyMode { kHarmonyStructured = 0, kHarmonyGenerative };
    enum Voicing { kVoicingTriads = 0, kVoicing7ths, kVoicingStack5ths, kVoicingOctaves, kVoicingUnison, kNumVoicings };
    enum Completion { kCompletionHold = 0, kCompletionDecayRebuild, kCompletionRefresh, kCompletionNewInversion, kNumCompletions };

private:
    static constexpr int kMaxSteps = 128;
    static constexpr int kMaxLayers = 8;

    void resetLoop();
    void handleLoopWrap();
    void computeProbabilities(const ScaleQuantizer* scale);
    int weightedPick(int numDegrees);
    void generateLayerZeroNote(int tick, const ScaleQuantizer* scale);
    void generateStructuredNote(int layer, int tick);
    void generateGenerativeNote(int layer, int tick, const ScaleQuantizer* scale);
    void outputNote(EngineOutput& out, int layer, int tick, const ScaleQuantizer* scale) const;

    bool getGateBit(int layer, int step) const {
        return (layerGates_[layer][step >> 5] >> (step & 31)) & 1;
    }
    void setGateBit(int layer, int step, bool val) {
        if (val) layerGates_[layer][step >> 5] |= (1u << (step & 31));
        else     layerGates_[layer][step >> 5] &= ~(1u << (step & 31));
    }

    // Parameters (cached)
    int role_;
    int loopSteps_;
    int maxLayers_;
    int harmonyMode_;
    int voicing_;
    int completion_;
    int noteDensity_;
    int gateLength_;
    int velocity_;
    int octaveSpread_;
    int refreshRate_;

    // Sequence state
    int currentTick_;
    int currentLayer_;
    int loopCount_;
    bool allLayersComplete_;
    int silentLoops_;
    int refreshLayerIdx_;
    int inversionOffset_;

    // Layer data
    uint8_t layerNotes_[kMaxLayers][kMaxSteps];   // scale degree, 0xFF = rest
    uint32_t layerGates_[kMaxLayers][4];           // 128-bit bitmask per layer

    // Probability weights for scale degrees
    float probabilities_[128];
    int numDegrees_;
    bool scaleLoaded_;

    // Simple PRNG state (xorshift32)
    uint32_t rngState_;
    uint32_t rng();
    float rngFloat();
};

#endif // FERROMAGNETIC_ENGINE_H
