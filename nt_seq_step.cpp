#include "nt_seq.h"
#include <string.h>

void parameterChanged(_NT_algorithm* self, int p);

// Edge detection helpers
static inline bool risingEdge(float sample, bool& state)
{
    bool high = sample > 1.0f;
    bool rising = high && !state;
    state = high;
    return rising;
}

static inline float* busPtr(float* busFrames, int busIndex, int numFrames)
{
    return (busIndex > 0) ? (busFrames + (busIndex - 1) * numFrames) : nullptr;
}

void step(_NT_algorithm* self, float* busFrames, int numFramesBy4)
{
    NtSeq* alg = static_cast<NtSeq*>(self);
    int numFrames = numFramesBy4 * 4;

    // --- SD card mount detection ---
    bool cardMounted = NT_isSdCardMounted();
    if (alg->cardMounted != cardMounted) {
        alg->cardMounted = cardMounted;
        if (cardMounted) {
            int numScl = NT_getNumScl();
            if (numScl > 0) {
                alg->paramDefs[kParamScaleFile].max = numScl - 1;
                int algIdx = NT_algorithmIndex(self);
                if (algIdx >= 0)
                    NT_updateParameterDefinition(algIdx, kParamScaleFile);
            }
            parameterChanged(self, kParamScaleFile);
        }
    }

    // --- Check if scale was loaded (callback completed) ---
    if (alg->scaleDirty && !alg->sclRequest.error && alg->sclRequest.numNotes > 0) {
        alg->scaleQuantizer.loadScale(alg->sclNotes, alg->sclRequest.numNotes);
        alg->scaleDirty = false;
    }

    // --- Get global params ---
    int rootNote = alg->v[kParamRootNote];
    int octave = alg->v[kParamOctave];

    // --- Per-channel bus info (cached outside sample loop) ---
    struct ChannelBusses {
        float* clock;
        float* reset;
        float* pitch;
        float* gate;
        float* velocity;
        bool pitchReplace;
        bool gateReplace;
        bool velocityReplace;
        bool isCvMode;
    };
    ChannelBusses chBus[kMaxChannels] = {};

    for (uint32_t ch = 0; ch < alg->numChannels; ++ch) {
        int base = alg->channels[ch].paramBase;

        chBus[ch].clock = busPtr(busFrames, alg->v[base + kChClockIn], numFrames);
        chBus[ch].reset = busPtr(busFrames, alg->v[base + kChResetIn], numFrames);

        int outputMode = alg->v[base + kChRouting];
        chBus[ch].isCvMode = (outputMode == kRoutingCV);

        if (chBus[ch].isCvMode) {
            chBus[ch].pitch = busPtr(busFrames, alg->v[base + kChCvOut], numFrames);
            chBus[ch].gate = busPtr(busFrames, alg->v[base + kChGateOut], numFrames);
            chBus[ch].velocity = busPtr(busFrames, alg->v[base + kChVelOut], numFrames);
            chBus[ch].pitchReplace = alg->v[base + kChCvOutMode];
            chBus[ch].gateReplace = alg->v[base + kChGateOutMode];
            chBus[ch].velocityReplace = alg->v[base + kChVelOutMode];
        }
    }

    // --- Audio-rate loop ---
    for (int n = 0; n < numFrames; ++n) {
        for (uint32_t ch = 0; ch < alg->numChannels; ++ch) {
            if (alg->channels[ch].samplesSinceClock < 0x3fffffff)
                alg->channels[ch].samplesSinceClock++;

            if (alg->channels[ch].engine && alg->channels[ch].engine->usesTimedGate()) {
                if (alg->channels[ch].gateSamplesRemaining > 0) {
                    alg->channels[ch].gateSamplesRemaining--;
                    if (alg->channels[ch].gateSamplesRemaining == 0)
                        alg->channels[ch].cachedGate = 0.0f;
                }
            }

            // Per-channel reset edge detection
            bool resetRise = false;
            if (chBus[ch].reset)
                resetRise = risingEdge(chBus[ch].reset[n], alg->channels[ch].resetHigh);

            if (resetRise) {
                if (alg->channels[ch].engine)
                    alg->channels[ch].engine->reset();
                alg->channels[ch].clockProc.reset();
                alg->channels[ch].cachedGate = 0.0f;
                alg->channels[ch].gateSamplesRemaining = 0;
                alg->channels[ch].samplesSinceClock = 0;
            }

            // Per-channel clock edge detection
            bool clockRise = false;
            if (chBus[ch].clock)
                clockRise = risingEdge(chBus[ch].clock[n], alg->channels[ch].clockHigh);

            if (clockRise) {
                if (!alg->channels[ch].engine) continue;

                // Per-channel clock divider
                if (!alg->channels[ch].clockProc.tick()) continue;

                if (alg->channels[ch].samplesSinceClock > 0) {
                    alg->channels[ch].clockPeriodSamples = alg->channels[ch].samplesSinceClock;
                    alg->channels[ch].samplesSinceClock = 0;
                }

                int base = alg->channels[ch].paramBase;
                bool scaleEnable = alg->v[base + kChScaleEnable] != 0;

                // Tick the engine
                const ScaleQuantizer* scalePtr = (scaleEnable && alg->scaleQuantizer.isLoaded())
                    ? &alg->scaleQuantizer : nullptr;
                EngineOutput eo = alg->channels[ch].engine->clockTick(scalePtr);

                // Apply root note and octave offsets (only for scale-quantized engines)
                float finalPitch;
                if (scaleEnable) {
                    float pitchOffset = (float)rootNote / 12.0f + (float)(octave - 5);
                    finalPitch = eo.pitch + pitchOffset;
                } else {
                    finalPitch = eo.pitch;
                }

                bool timedGate = alg->channels[ch].engine->usesTimedGate();
                if (timedGate) {
                    if (eo.gate > 0.0f) {
                        // New note: update pitch/velocity and retrigger timed gate.
                        alg->channels[ch].cachedPitch = finalPitch;
                        alg->channels[ch].cachedGate = eo.gate;
                        alg->channels[ch].cachedVelocity = eo.velocity;

                        int gateLenPct = alg->channels[ch].engine->gateLengthPercent();
                        if (gateLenPct >= 100) {
                            // Legato mode: hold gate until explicitly reset.
                            alg->channels[ch].gateSamplesRemaining = -1;
                        } else {
                            int holdSamples = (alg->channels[ch].clockPeriodSamples * gateLenPct) / 100;
                            if (holdSamples < 1) holdSamples = 1;
                            alg->channels[ch].gateSamplesRemaining = holdSamples;
                        }
                    }
                    // Rests/probability misses intentionally do not overwrite
                    // pitch/velocity or force gate low; countdown handles release.
                } else {
                    // Default sample-and-hold behavior for engines without timed gate.
                    alg->channels[ch].cachedPitch = finalPitch;
                    alg->channels[ch].cachedGate = eo.gate;
                    alg->channels[ch].cachedVelocity = eo.velocity;
                }

                // MIDI output (only on clock tick)
                if (!chBus[ch].isCvMode) {
                    int midiCh = alg->v[base + kChMidiChannel] - 1;
                    int midiDestIdx = alg->v[base + kChMidiDest];
                    uint32_t dest = midiDestFlags[midiDestIdx];

                    // Compute MIDI note
                    int midiNoteInt;
                    if (scaleEnable) {
                        midiNoteInt = (int)eo.midiNote + rootNote + (octave - 5) * 12;
                    } else {
                        midiNoteInt = (int)eo.midiNote;
                    }
                    if (midiNoteInt < 0) midiNoteInt = 0;
                    if (midiNoteInt > 127) midiNoteInt = 127;
                    uint8_t finalMidiNote = (uint8_t)midiNoteInt;

                    // Note off for previous
                    if (alg->channels[ch].midiNoteOn) {
                        NT_sendMidi3ByteMessage(dest,
                            0x80 | (uint8_t)midiCh,
                            alg->channels[ch].lastMidiNote, 0);
                        alg->channels[ch].midiNoteOn = false;
                    }

                    // Note on if gate high
                    if (eo.gate > 0.0f) {
                        uint8_t velocity = (uint8_t)(eo.velocity * 25.4f);
                        if (velocity > 127) velocity = 127;
                        if (velocity < 1) velocity = 1;
                        NT_sendMidi3ByteMessage(dest,
                            0x90 | (uint8_t)midiCh,
                            finalMidiNote, velocity);
                        alg->channels[ch].lastMidiNote = finalMidiNote;
                        alg->channels[ch].midiNoteOn = true;
                    }
                }
            }
        }

        // --- Write CV outputs every sample (sample-and-hold) ---
        for (uint32_t ch = 0; ch < alg->numChannels; ++ch) {
            if (!chBus[ch].isCvMode) continue;

            if (chBus[ch].pitch) {
                if (chBus[ch].pitchReplace)
                    chBus[ch].pitch[n] = alg->channels[ch].cachedPitch;
                else
                    chBus[ch].pitch[n] += alg->channels[ch].cachedPitch;
            }
            if (chBus[ch].gate) {
                if (chBus[ch].gateReplace)
                    chBus[ch].gate[n] = alg->channels[ch].cachedGate;
                else
                    chBus[ch].gate[n] += alg->channels[ch].cachedGate;
            }
            if (chBus[ch].velocity) {
                if (chBus[ch].velocityReplace)
                    chBus[ch].velocity[n] = alg->channels[ch].cachedVelocity;
                else
                    chBus[ch].velocity[n] += alg->channels[ch].cachedVelocity;
            }
        }
    }
}

void midiMessage(_NT_algorithm* self, uint8_t byte0, uint8_t byte1, uint8_t byte2)
{
    NtSeq* alg = static_cast<NtSeq*>(self);
    uint8_t status = byte0 & 0xF0;
    uint8_t midiCh = (byte0 & 0x0F) + 1;
    uint8_t note = byte1 & 0x7F;
    uint8_t velocity = byte2 & 0x7F;

    bool isNoteOn = (status == 0x90 && velocity > 0);
    bool isNoteOff = (status == 0x80 || (status == 0x90 && velocity == 0));
    if (!isNoteOn && !isNoteOff)
        return;

    for (uint32_t ch = 0; ch < alg->numChannels; ++ch) {
        if (!alg->channels[ch].engine)
            continue;
        int base = alg->channels[ch].paramBase;
        int configuredCh = alg->v[base + kChMidiChannel];
        if (configuredCh != (int)midiCh)
            continue;

        if (isNoteOn)
            alg->channels[ch].engine->noteOn(note, velocity);
        else
            alg->channels[ch].engine->noteOff(note);
    }
}
