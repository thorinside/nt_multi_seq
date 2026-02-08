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

class SequencerEngine {
public:
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

    // UI support: step position and status text
    virtual int currentStep() const { return -1; }
    virtual int sequenceLength() const { return 0; }
    virtual int getStatusText(char* buf, int maxLen) const { buf[0] = 0; return 0; }

    // Optional hooks for engines that react to MIDI note input directly.
    virtual void noteOn(uint8_t /*midiNote*/, uint8_t /*velocity*/) {}
    virtual void noteOff(uint8_t /*midiNote*/) {}
    virtual void noteCvGate(float /*vOct*/, bool /*rising*/) {}

    // Optional timed-gate behavior. Engines returning true can request that
    // the host hold gate high for a percentage of measured clock period.
    virtual bool usesTimedGate() const { return false; }
    virtual int gateLengthPercent() const { return 100; }
};

#endif // SEQUENCER_ENGINE_H
