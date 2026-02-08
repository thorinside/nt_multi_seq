#include "nt_seq.h"
#include <string.h>

// Root note names for parameterString
static const char* rootNoteNames[] = {
    "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"
};

static void applyRoutingGrayouts(NtSeq* alg, int algIdx, uint32_t ch)
{
    if (algIdx < 0) return;
    int base = alg->channels[ch].paramBase;
    uint32_t paramOffset = NT_parameterOffset();
    bool isCv = (alg->v[base + kChRouting] == kRoutingCV);
    NT_setParameterGrayedOut(algIdx, (uint32_t)(base + kChCvOut) + paramOffset, !isCv);
    NT_setParameterGrayedOut(algIdx, (uint32_t)(base + kChCvOutMode) + paramOffset, !isCv);
    NT_setParameterGrayedOut(algIdx, (uint32_t)(base + kChGateOut) + paramOffset, !isCv);
    NT_setParameterGrayedOut(algIdx, (uint32_t)(base + kChGateOutMode) + paramOffset, !isCv);
    NT_setParameterGrayedOut(algIdx, (uint32_t)(base + kChVelOut) + paramOffset, !isCv);
    NT_setParameterGrayedOut(algIdx, (uint32_t)(base + kChVelOutMode) + paramOffset, !isCv);
    NT_setParameterGrayedOut(algIdx, (uint32_t)(base + kChMidiChannel) + paramOffset, isCv);
    NT_setParameterGrayedOut(algIdx, (uint32_t)(base + kChMidiDest) + paramOffset, isCv);
}

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
        if (p < base || p >= engBase + alg->channels[ch].numEngineParams) continue;

        int localOffset = p - base;

        // Output mode changed -> gray/ungray CV/MIDI params
        if (localOffset == kChRouting) {
            applyRoutingGrayouts(alg, algIdx, ch);
            return;
        }

        // Clock divider changed
        if (localOffset == kChClockDiv) {
            alg->channels[ch].clockProc.setDivider(alg->v[p]);
            return;
        }

        // Engine-specific parameter
        if (p >= engBase) {
            int localEngIdx = p - engBase;
            if (alg->channels[ch].engine)
                alg->channels[ch].engine->parameterChanged(localEngIdx, alg->v[p]);
            return;
        }

        // Keep grayouts synchronized during host init replay.
        applyRoutingGrayouts(alg, algIdx, ch);
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
        int end = alg->channels[ch].engineParamBase + alg->channels[ch].numEngineParams;
        if (p >= base && p < end) {
            int len = NT_intToString(buff, alg->channels[ch].specSlot + 1);
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
