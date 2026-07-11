#include "nt_seq.h"

void parameterChanged(_NT_algorithm* self, int p);

static inline bool risingEdge(float sample, bool& state)
{
    bool high = sample > 1.0f;
    bool rising = high && !state;
    state = high;
    return rising;
}

static inline float* busPtr(float* busFrames, int busIndex, int numFrames)
{
    return busIndex > 0 ? busFrames + (busIndex - 1) * numFrames : nullptr;
}

void step(_NT_algorithm* self, float* busFrames, int numFramesBy4)
{
    NtSeq* alg = static_cast<NtSeq*>(self);
    SequencerState& seq = alg->seq;
    alg->initDone = true;
    int numFrames = numFramesBy4 * 4;

    bool cardMounted = NT_isSdCardMounted();
    if (alg->cardMounted != cardMounted) {
        alg->cardMounted = cardMounted;
        if (cardMounted) {
            int numScales = NT_getNumScl();
            if (numScales > 0) {
                alg->paramDefs[kParamScaleFile].max = numScales - 1;
                int algIdx = NT_algorithmIndex(self);
                if (algIdx >= 0)
                    NT_updateParameterDefinition(algIdx, kParamScaleFile);
            }
            parameterChanged(self, kParamScaleFile);
        } else {
            alg->awaitingCallback = false;
        }
    }

    if (alg->scaleDirty) {
        if (!alg->sclRequest.error && alg->sclRequest.numNotes > 0) {
            alg->scaleQuantizer.loadScale(alg->sclNotes, alg->sclRequest.numNotes);
            alg->warpDirty = true;
        }
        alg->scaleDirty = false;
    }

    if (alg->warpDirty) {
        alg->warpDirty = false;
        int warpAmount = alg->v[kParamWarpAmount];
        int numDegrees = static_cast<int>(alg->scaleQuantizer.numNotes());
        if (warpAmount > 0 && numDegrees > 0 && alg->scaleQuantizer.isLoaded()) {
            int weightMode = alg->v[kParamNoteWeight];
            float characteristicWeight = 1.0f + static_cast<float>(warpAmount) / 100.0f * 4.0f;
            float weights[128];
            alg->scaleQuantizer.computeNoteWeights(
                weights,
                numDegrees,
                static_cast<ScaleQuantizer::WeightMode>(weightMode),
                characteristicWeight);

            float cumulative[128];
            cumulative[0] = weights[0];
            for (int i = 1; i < numDegrees; ++i)
                cumulative[i] = cumulative[i - 1] + weights[i];
            float totalWeight = cumulative[numDegrees - 1];

            for (int degree = 0; degree < numDegrees; ++degree) {
                float position = (static_cast<float>(degree) + 0.5f)
                    / static_cast<float>(numDegrees) * totalWeight;
                int warped = numDegrees - 1;
                for (int i = 0; i < numDegrees; ++i) {
                    if (position <= cumulative[i]) {
                        warped = i;
                        break;
                    }
                }
                alg->warpLut[degree] = static_cast<int8_t>(warped);
            }
            alg->cachedWarpNumNotes = numDegrees;
        } else {
            alg->cachedWarpNumNotes = 0;
        }
    }

    int base = seq.paramBase;
    int rootNote = alg->v[kParamRootNote];
    int octave = alg->v[kParamOctave];

    struct Busses {
        float* clock;
        float* reset;
        float* noteGate;
        float* noteCv;
        float* pitch;
        float* gate;
        float* velocity;
        bool pitchReplace;
        bool gateReplace;
        bool velocityReplace;
        bool isCvMode;
    } busses = {};

    busses.clock = busPtr(busFrames, alg->v[base + kRouteClockIn], numFrames);
    busses.reset = busPtr(busFrames, alg->v[base + kRouteResetIn], numFrames);
    busses.noteGate = busPtr(busFrames, alg->v[base + kRouteNoteGateIn], numFrames);
    busses.noteCv = busPtr(busFrames, alg->v[base + kRouteNoteCvIn], numFrames);
    busses.isCvMode = alg->v[base + kRouteMode] == kRoutingCV;

    if (busses.isCvMode) {
        busses.pitch = busPtr(busFrames, alg->v[base + kRoutePitchOut], numFrames);
        busses.gate = busPtr(busFrames, alg->v[base + kRouteGateOut], numFrames);
        busses.velocity = busPtr(busFrames, alg->v[base + kRouteVelocityOut], numFrames);
        busses.pitchReplace = alg->v[base + kRoutePitchOutMode];
        busses.gateReplace = alg->v[base + kRouteGateOutMode];
        busses.velocityReplace = alg->v[base + kRouteVelocityOutMode];
    }

    for (int frame = 0; frame < numFrames; ++frame) {
        if (seq.samplesSinceClock < 0x3fffffff)
            seq.samplesSinceClock++;

        if (seq.engine && seq.engine->usesTimedGate() && seq.gateSamplesRemaining > 0) {
            seq.gateSamplesRemaining--;
            if (seq.gateSamplesRemaining == 0)
                seq.cachedGate = 0.0f;
        }

        bool resetRise = busses.reset
            ? risingEdge(busses.reset[frame], seq.resetHigh)
            : false;
        if (resetRise) {
            if (seq.engine)
                seq.engine->reset();
            seq.clockProc.reset();
            seq.cachedGate = 0.0f;
            seq.gateSamplesRemaining = 0;
            seq.samplesSinceClock = 0;
        }

        if (busses.noteGate && seq.engine) {
            bool wasHigh = seq.noteGateHigh;
            bool high = busses.noteGate[frame] > 1.0f;
            bool noteRise = high && !wasHigh;
            bool noteFall = !high && wasHigh;
            seq.noteGateHigh = high;
            if (noteRise || noteFall) {
                float noteCv = busses.noteCv ? busses.noteCv[frame] : 0.0f;
                seq.engine->noteCvGate(noteCv, noteRise);
            }
        }

        bool clockRise = busses.clock
            ? risingEdge(busses.clock[frame], seq.clockHigh)
            : false;
        if (clockRise && seq.engine && seq.clockProc.tick()) {
            if (seq.samplesSinceClock > 0) {
                seq.clockPeriodSamples = seq.samplesSinceClock;
                seq.samplesSinceClock = 0;
            }

            bool scaleEnabled = alg->v[base + kRouteScaleEnable] != 0;
            const ScaleQuantizer* scale = scaleEnabled && alg->scaleQuantizer.isLoaded()
                ? &alg->scaleQuantizer
                : nullptr;
            EngineOutput output = seq.engine->clockTick(scale);

            if (alg->cachedWarpNumNotes > 0 && scale) {
                int degreeOctave;
                int degree = scale->findNearestDegree(output.pitch, degreeOctave);
                int warped = alg->warpLut[degree];
                if (warped != degree) {
                    output.pitch = scale->quantize(warped, degreeOctave, 0);
                    output.midiNote = scale->scaleDegreeToMidi(warped, degreeOctave + 5, 0);
                }
            }

            float finalPitch = output.pitch;
            if (scaleEnabled) {
                float pitchOffset = static_cast<float>(rootNote) / 12.0f
                    + static_cast<float>(octave - 5);
                finalPitch += pitchOffset;
            }

            if (seq.engine->usesTimedGate()) {
                if (output.gate > 0.0f) {
                    seq.cachedPitch = finalPitch;
                    seq.cachedGate = output.gate;
                    seq.cachedVelocity = output.velocity;

                    int gateLength = seq.engine->gateLengthPercent();
                    if (gateLength >= 100) {
                        seq.gateSamplesRemaining = -1;
                    } else {
                        int holdSamples = seq.clockPeriodSamples * gateLength / 100;
                        seq.gateSamplesRemaining = holdSamples > 0 ? holdSamples : 1;
                    }
                }
            } else {
                seq.cachedPitch = finalPitch;
                seq.cachedGate = output.gate;
                seq.cachedVelocity = output.velocity;
            }

            if (!busses.isCvMode) {
                int midiChannel = alg->v[base + kRouteMidiChannel] - 1;
                int destinationIndex = alg->v[base + kRouteMidiDest];
                uint32_t destination = midiDestFlags[destinationIndex];
                int midiNote = scaleEnabled
                    ? static_cast<int>(output.midiNote) + rootNote + (octave - 5) * 12
                    : static_cast<int>(output.midiNote);
                if (midiNote < 0)
                    midiNote = 0;
                if (midiNote > 127)
                    midiNote = 127;

                if (seq.midiNoteOn) {
                    NT_sendMidi3ByteMessage(
                        destination,
                        0x80 | static_cast<uint8_t>(midiChannel),
                        seq.lastMidiNote,
                        0);
                    seq.midiNoteOn = false;
                }

                if (output.gate > 0.0f) {
                    uint8_t velocity = static_cast<uint8_t>(output.velocity * 25.4f);
                    if (velocity > 127)
                        velocity = 127;
                    if (velocity < 1)
                        velocity = 1;
                    NT_sendMidi3ByteMessage(
                        destination,
                        0x90 | static_cast<uint8_t>(midiChannel),
                        static_cast<uint8_t>(midiNote),
                        velocity);
                    seq.lastMidiNote = static_cast<uint8_t>(midiNote);
                    seq.midiNoteOn = true;
                }
            }
        }

        if (!busses.isCvMode)
            continue;

        if (busses.pitch) {
            if (busses.pitchReplace)
                busses.pitch[frame] = seq.cachedPitch;
            else
                busses.pitch[frame] += seq.cachedPitch;
        }
        if (busses.gate) {
            if (busses.gateReplace)
                busses.gate[frame] = seq.cachedGate;
            else
                busses.gate[frame] += seq.cachedGate;
        }
        if (busses.velocity) {
            if (busses.velocityReplace)
                busses.velocity[frame] = seq.cachedVelocity;
            else
                busses.velocity[frame] += seq.cachedVelocity;
        }
    }
}

void midiMessage(_NT_algorithm* self, uint8_t byte0, uint8_t byte1, uint8_t byte2)
{
    NtSeq* alg = static_cast<NtSeq*>(self);
    SequencerEngine* engine = alg->seq.engine;
    if (!engine)
        return;

    uint8_t status = byte0 & 0xF0;
    uint8_t midiChannel = (byte0 & 0x0F) + 1;
    uint8_t note = byte1 & 0x7F;
    uint8_t velocity = byte2 & 0x7F;
    bool isNoteOn = status == 0x90 && velocity > 0;
    bool isNoteOff = status == 0x80 || (status == 0x90 && velocity == 0);
    if (!isNoteOn && !isNoteOff)
        return;

    int engineMidiChannel = engine->midiInputChannel();
    if (engineMidiChannel < 0)
        return;
    if (engineMidiChannel > 0 && engineMidiChannel != static_cast<int>(midiChannel))
        return;

    if (isNoteOn)
        engine->noteOn(note, velocity);
    else
        engine->noteOff(note);
}
