#ifndef SEQUENCER_ENGINE_H
#define SEQUENCER_ENGINE_H

#include <stdint.h>
#include <distingnt/api.h>

class ScaleQuantizer;

struct EngineOutput {
    float pitch;      // V/oct (or raw voltage for unscaled engines)
    float gate;       // 0.0 or 5.0
    float velocity;   // 0.0-5.0 (maps to MIDI velocity 0-127)
    uint8_t midiNote; // MIDI note number (for MIDI output mode)
};

// --- Focus view data structs (engine -> renderer) ---

static constexpr int kMaxBarSteps = 32;
static constexpr int kMaxFocusBars = 2;

struct FocusBar {
    int length;                     // 0 = no bar
    int playhead;                   // -1 = no playhead
    uint8_t levels[kMaxBarSteps];   // per-step brightness (1-15)
};

struct FocusBarInfo {
    int numBars;
    FocusBar bars[kMaxFocusBars];
};

struct FocusDetailLine {
    char text[64];
    uint8_t colours[64];  // per-character colour
    int len;

    void clear() { len = 0; text[0] = 0; }
    void append(const char* s, uint8_t colour);
    void appendChar(char c, uint8_t colour);
    void appendInt(int32_t val, uint8_t colour);
    void appendFloat(float val, int decimals, uint8_t colour);
};

struct FocusDetail {
    FocusDetailLine lines[2];
};

// Standalone int-to-string for getStatusText (no NT dependency).
int fmtInt(char* buf, int32_t val);

class SequencerEngine {
public:
    SequencerEngine() : weightMode_(0) {}
    virtual ~SequencerEngine() {}
    virtual void init(uint32_t sampleRate) = 0;
    virtual EngineOutput clockTick(const ScaleQuantizer* scale) = 0;
    virtual void reset() = 0;
    virtual void parameterChanged(int localIndex, int16_t value) = 0;

    // Return the number of engine-specific parameters, filling defs[]
    virtual int getParameterDefs(_NT_parameter* defs) const = 0;

    // Return the number of engine page parameters, filling pages/indices
    virtual int getPageDefs(_NT_parameterPage* page, uint8_t* indices, int baseParamIndex) const = 0;

    virtual const char* name() const = 0;

    // Focus view: fill data structs for rendering by the host.
    virtual void getFocusDetail(FocusDetail& detail) const {
        detail.lines[0].clear();
        detail.lines[1].clear();
    }

    // Focus bar info: default is a single bar with uniform colour 4 and
    // the current step highlighted at 15.
    virtual void getFocusBarInfo(FocusBarInfo& info) const {
        int len = sequenceLength();
        int step = currentStep();
        info.numBars = 1;
        FocusBar& bar = info.bars[0];
        bar.length = len;
        bar.playhead = step;
        for (int i = 0; i < len && i < kMaxBarSteps; ++i)
            bar.levels[i] = 4;
        if (step >= 0 && step < kMaxBarSteps)
            bar.levels[step] = 15;
    }

    // UI support: step position and status text
    virtual int currentStep() const { return -1; }
    virtual int sequenceLength() const { return 0; }
    virtual int getStatusText(char* buf, int maxLen) const { buf[0] = 0; return 0; }

    // Optional hooks for engines that react to MIDI note input directly.
    virtual void noteOn(uint8_t /*midiNote*/, uint8_t /*velocity*/) {}
    virtual void noteOff(uint8_t /*midiNote*/) {}
    virtual void noteCvGate(float /*vOct*/, bool /*rising*/) {}

    // MIDI input channel. -1=no MIDI input (default), 0=omni, 1-16=specific channel.
    virtual int midiInputChannel() const { return -1; }

    // Optional timed-gate behavior. Engines returning true can request that
    // the host hold gate high for a percentage of measured clock period.
    virtual bool usesTimedGate() const { return false; }
    virtual int gateLengthPercent() const { return 100; }

    // Note weight mode (0=Major, 1=Harmonic, 2=Equal). Set by host.
    void setWeightMode(int m) { weightMode_ = m; }
    int weightMode() const { return weightMode_; }

protected:
    int weightMode_;
};

#endif // SEQUENCER_ENGINE_H
