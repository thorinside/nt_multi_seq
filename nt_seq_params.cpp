#include "nt_seq.h"
#include "engines/ThorpEngine.h"
#include <string.h>

static const char* const rootNoteNames[] = {
    "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"
};

static void applyRoutingGrayouts(NtSeq* alg, int algIdx)
{
    if (algIdx < 0)
        return;

    int base = alg->seq.paramBase;
    uint32_t paramOffset = NT_parameterOffset();
    bool isCv = alg->v[base + kRouteMode] == kRoutingCV;
    NT_setParameterGrayedOut(algIdx, static_cast<uint32_t>(base + kRoutePitchOut) + paramOffset, !isCv);
    NT_setParameterGrayedOut(algIdx, static_cast<uint32_t>(base + kRoutePitchOutMode) + paramOffset, !isCv);
    NT_setParameterGrayedOut(algIdx, static_cast<uint32_t>(base + kRouteGateOut) + paramOffset, !isCv);
    NT_setParameterGrayedOut(algIdx, static_cast<uint32_t>(base + kRouteGateOutMode) + paramOffset, !isCv);
    NT_setParameterGrayedOut(algIdx, static_cast<uint32_t>(base + kRouteVelocityOut) + paramOffset, !isCv);
    NT_setParameterGrayedOut(algIdx, static_cast<uint32_t>(base + kRouteVelocityOutMode) + paramOffset, !isCv);
    NT_setParameterGrayedOut(algIdx, static_cast<uint32_t>(base + kRouteMidiChannel) + paramOffset, isCv);
    NT_setParameterGrayedOut(algIdx, static_cast<uint32_t>(base + kRouteMidiDest) + paramOffset, isCv);
}

void parameterChanged(_NT_algorithm* self, int p)
{
    NtSeq* alg = static_cast<NtSeq*>(self);
    SequencerState& seq = alg->seq;
    int algIdx = NT_algorithmIndex(self);

    if (p == kParamNoteWeight) {
        if (seq.engine)
            seq.engine->setWeightMode(alg->v[kParamNoteWeight]);
        alg->warpDirty = true;
        return;
    }

    if (p == kParamWarpAmount) {
        alg->warpDirty = true;
        return;
    }

    if (p == kParamScaleFile) {
        if (!alg->awaitingCallback) {
            alg->sclRequest.index = alg->v[kParamScaleFile];
            alg->awaitingCallback = true;
            if (!NT_readScl(alg->sclRequest))
                alg->awaitingCallback = false;
        }
        return;
    }

    int base = seq.paramBase;
    int engineBase = seq.engineParamBase;
    int engineEnd = engineBase + seq.numEngineParams;
    if (p < base || p >= engineEnd)
        return;

    int localOffset = p - base;
    if (localOffset == kRouteMode) {
        applyRoutingGrayouts(alg, algIdx);
        return;
    }

    if (localOffset == kRouteClockDiv) {
        seq.clockProc.setDivider(alg->v[p]);
        return;
    }

    if (p >= engineBase) {
        int localEngineIndex = p - engineBase;
        if (seq.engine)
            seq.engine->parameterChanged(localEngineIndex, alg->v[p]);

        if (seq.engineType == kEngineThorp
            && localEngineIndex == ThorpEngine::kThorpArpSlot
            && seq.engine
            && algIdx >= 0) {
            ThorpEngine* thorp = static_cast<ThorpEngine*>(seq.engine);
            int16_t pattern;
            int16_t velocityPattern;
            int16_t length;
            int16_t offset;
            int16_t reverse;
            thorp->getLoadedSlotParams(pattern, velocityPattern, length, offset, reverse);
            uint32_t paramOffset = NT_parameterOffset();
            NT_setParameterFromAudio(static_cast<uint32_t>(algIdx), static_cast<uint32_t>(engineBase + ThorpEngine::kThorpPattern) + paramOffset, pattern);
            NT_setParameterFromAudio(static_cast<uint32_t>(algIdx), static_cast<uint32_t>(engineBase + ThorpEngine::kThorpVelPattern) + paramOffset, velocityPattern);
            NT_setParameterFromAudio(static_cast<uint32_t>(algIdx), static_cast<uint32_t>(engineBase + ThorpEngine::kThorpLength) + paramOffset, length);
            NT_setParameterFromAudio(static_cast<uint32_t>(algIdx), static_cast<uint32_t>(engineBase + ThorpEngine::kThorpOffset) + paramOffset, offset);
            NT_setParameterFromAudio(static_cast<uint32_t>(algIdx), static_cast<uint32_t>(engineBase + ThorpEngine::kThorpReverse) + paramOffset, reverse);
        }

        if (alg->initDone
            && seq.engineType == kEngineThorp
            && localEngineIndex == ThorpEngine::kThorpChainLen
            && seq.engine
            && algIdx >= 0) {
            ThorpEngine* thorp = static_cast<ThorpEngine*>(seq.engine);
            int newChainLength = alg->v[p];
            int slotAtPosition = thorp->uiChainSlotAt(newChainLength - 1);
            uint32_t paramOffset = NT_parameterOffset();
            NT_setParameterFromAudio(
                static_cast<uint32_t>(algIdx),
                static_cast<uint32_t>(engineBase + ThorpEngine::kThorpArpSlot) + paramOffset,
                static_cast<int16_t>(slotAtPosition));
        }
        return;
    }

    applyRoutingGrayouts(alg, algIdx);
}

int parameterString(_NT_algorithm* self, int p, int v, char* buff)
{
    (void)self;

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

    if (p == kParamRootNote && v >= 0 && v < 12) {
        strncpy(buff, rootNoteNames[v], kNT_parameterStringSize - 1);
        buff[kNT_parameterStringSize - 1] = 0;
        return strlen(buff);
    }

    return 0;
}
