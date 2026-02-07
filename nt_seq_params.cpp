#include "nt_seq.h"
#include "engines/SomaEngine.h"
#include "engines/AeSequencerEngine.h"
#include "engines/SeqMarkovEngine.h"
#include "engines/ThorpEngine.h"
#include <string.h>
#include <new>

// Root note names for parameterString
static const char* rootNoteNames[] = {
    "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"
};

void parameterChanged(_NT_algorithm* self, int p)
{
    NtSeq* alg = static_cast<NtSeq*>(self);
    int algIdx = NT_algorithmIndex(static_cast<const _NT_algorithm*>(self));

    // Scale file changed
    if (p == kParamScaleFile) {
        if (!alg->awaitingCallback) {
            alg->sclRequest.index = alg->v[kParamScaleFile];
            alg->awaitingCallback = true;
            if (!NT_readScl(alg->sclRequest))
                alg->awaitingCallback = false;
        }
        return;
    }

    // Check per-channel parameters
    for (uint32_t ch = 0; ch < alg->numChannels; ++ch) {
        int base = alg->channels[ch].paramBase;
        int engBase = alg->channels[ch].engineParamBase;

        // Is this param in this channel's range?
        if (p < base || p >= engBase + kMaxEngineParams) continue;

        int localOffset = p - base;

        // Output mode changed -> gray/ungray CV/MIDI params
        if (localOffset == kChOutputMode) {
            int outputMode = alg->v[p];
            bool isCv = (outputMode == kOutputCV);
            // Gray out MIDI params in CV mode, CV params in MIDI mode
            NT_setParameterGrayedOut(algIdx, base + kChCvOut, !isCv);
            NT_setParameterGrayedOut(algIdx, base + kChCvOutMode, !isCv);
            NT_setParameterGrayedOut(algIdx, base + kChGateOut, !isCv);
            NT_setParameterGrayedOut(algIdx, base + kChGateOutMode, !isCv);
            NT_setParameterGrayedOut(algIdx, base + kChMidiChannel, isCv);
            NT_setParameterGrayedOut(algIdx, base + kChMidiDest, isCv);
            return;
        }

        // Clock divider changed
        if (localOffset == kChClockDiv) {
            alg->channels[ch].clockProc.setDivider(alg->v[p]);
            return;
        }

        // Engine type changed
        if (localOffset == kChEngineType) {
            EngineType newType = (EngineType)alg->v[p];
            if (newType != alg->channels[ch].engineType) {
                // Destroy old engine (call destructor)
                if (alg->channels[ch].engine) {
                    alg->channels[ch].engine->~SequencerEngine();
                }

                // Create new engine in same memory
                uint8_t* engineMem = alg->enginePool + ch * 2048;
                switch (newType) {
                case kEngineSoma:
                    alg->channels[ch].engine = new (engineMem) SomaEngine();
                    break;
                case kEngineAeSeq:
                    alg->channels[ch].engine = new (engineMem) AeSequencerEngine();
                    break;
                case kEngineSeqMarkov:
                    alg->channels[ch].engine = new (engineMem) SeqMarkovEngine();
                    break;
                case kEngineThorp:
                    alg->channels[ch].engine = new (engineMem) ThorpEngine();
                    break;
                default:
                    alg->channels[ch].engine = new (engineMem) SomaEngine();
                    break;
                }
                alg->channels[ch].engine->init(alg->sampleRate);
                alg->channels[ch].engineType = newType;

                // Update engine parameter definitions
                _NT_parameter engineDefs[kMaxEngineParams];
                int numEngDefs = alg->channels[ch].engine->getParameterDefs(engineDefs);

                for (int i = 0; i < kMaxEngineParams; ++i) {
                    if (i < numEngDefs) {
                        alg->paramDefs[engBase + i] = engineDefs[i];
                        NT_setParameterGrayedOut(algIdx, engBase + i, false);
                    } else {
                        alg->paramDefs[engBase + i] = { .name = "-", .min = 0, .max = 0, .def = 0, .unit = kNT_unitNone, .scaling = 0, .enumStrings = nullptr };
                        NT_setParameterGrayedOut(algIdx, engBase + i, true);
                    }
                    NT_updateParameterDefinition(algIdx, engBase + i);
                }
            }
            return;
        }

        // Engine-specific parameter
        if (p >= engBase && p < engBase + kMaxEngineParams) {
            int localEngIdx = p - engBase;
            if (alg->channels[ch].engine)
                alg->channels[ch].engine->parameterChanged(localEngIdx, alg->v[p]);
            return;
        }

        return; // Handled
    }
}

int parameterUiPrefix(_NT_algorithm* self, int p, char* buff)
{
    NtSeq* alg = static_cast<NtSeq*>(self);

    // Global params: no prefix
    if (p < kNumGlobalParams)
        return 0;

    // Find which channel this param belongs to
    for (uint32_t ch = 0; ch < alg->numChannels; ++ch) {
        int base = alg->channels[ch].paramBase;
        int end = alg->channels[ch].engineParamBase + kMaxEngineParams;
        if (p >= base && p < end) {
            int len = NT_intToString(buff, ch + 1);
            buff[len++] = ':';
            buff[len] = 0;
            return len;
        }
    }

    return 0;
}

int parameterString(_NT_algorithm* self, int p, int v, char* buff)
{
    (void)self;

    // Scale file: show name
    if (p == kParamScaleFile) {
        _NT_sclInfo info;
        NT_getSclInfo(v, info);
        if (info.name) {
            strncpy(buff, info.name, kNT_parameterStringSize - 1);
            buff[kNT_parameterStringSize - 1] = 0;
            return strlen(buff);
        }
        return 0;
    }

    // Root note: show note name
    if (p == kParamRootNote) {
        if (v >= 0 && v < 12) {
            strncpy(buff, rootNoteNames[v], kNT_parameterStringSize - 1);
            buff[kNT_parameterStringSize - 1] = 0;
            return strlen(buff);
        }
    }

    return 0;
}
