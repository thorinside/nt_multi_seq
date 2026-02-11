#include "nt_seq.h"
#include "engines/ThorpEngine.h"
#include "engines/SomaEngine.h"
#include "engines/AeSequencerEngine.h"
#include "engines/SeqMarkovEngine.h"
#include <new>
#include <string.h>

// Root note names for parameterString
static const char* rootNoteNames[] = {
    "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"
};

// Placeholder parameter for greyed-out engine slots
static const _NT_parameter placeholderParam = {
    .name = "-", .min = 0, .max = 0, .def = 0, .unit = kNT_unitNone, .scaling = 0, .enumStrings = nullptr
};

// Copy string, return chars written (no null terminator added)
static int strCopy(char* dst, const char* src, int maxLen)
{
    int i = 0;
    while (src[i] && i < maxLen) {
        dst[i] = src[i];
        i++;
    }
    return i;
}

// Engine name lookup by EngineType
static const char* engineName(EngineType type)
{
    switch (type) {
    case kEngineThorp:     return "Thorp";
    case kEngineSoma:      return "Soma";
    case kEngineAeSeq:     return "AE Seq";
    case kEngineSeqMarkov: return "Markov";
    default:               return "";
    }
}

// Helper to create engine instance via placement new
static SequencerEngine* createEngine(EngineType type, uint8_t* mem)
{
    switch (type) {
    case kEngineThorp:     return new (mem) ThorpEngine();
    case kEngineSoma:      return new (mem) SomaEngine();
    case kEngineAeSeq:     return new (mem) AeSequencerEngine();
    case kEngineSeqMarkov: return new (mem) SeqMarkovEngine();
    default:               return nullptr;
    }
}

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

// Switch a channel's engine at runtime. Called from parameterChanged when engine type changes.
static void switchChannelEngine(NtSeq* alg, int algIdx, int ch, int newTypeWithNone)
{
    if (algIdx < 0) return;
    uint32_t paramOffset = NT_parameterOffset();
    int engBase = alg->channels[ch].engineParamBase;

    // Convert param value (0=None,1=Thorp,2=Soma,3=AE,4=Markov) to EngineType
    EngineType newType;
    if (newTypeWithNone <= 0 || newTypeWithNone > kNumEngineTypes)
        newType = kEngineNone;
    else
        newType = (EngineType)(newTypeWithNone - 1);

    // No change?
    if (newType == alg->channels[ch].engineType)
        return;

    // Destroy old engine
    if (alg->channels[ch].engine) {
        alg->channels[ch].engine->~SequencerEngine();
        alg->channels[ch].engine = nullptr;
    }

    alg->channels[ch].engineType = newType;

    // Create new engine (or leave nullptr for None)
    if (newType != kEngineNone) {
        uint8_t* engineMem = alg->enginePool + ch * 2048;
        alg->channels[ch].engine = createEngine(newType, engineMem);
        if (alg->channels[ch].engine)
            alg->channels[ch].engine->init(alg->sampleRate);
    }

    // Get new engine param defs (or empty for None)
    _NT_parameter engineDefs[kMaxEngineParams];
    int numEngineDefs = 0;
    if (alg->channels[ch].engine)
        numEngineDefs = alg->channels[ch].engine->getParameterDefs(engineDefs);
    alg->channels[ch].numEngineParams = numEngineDefs;

    // Overwrite paramDefs for all 32 engine slots
    alg->switchingEngine = true;  // re-entrancy guard
    for (int i = 0; i < kMaxEngineParams; ++i) {
        if (i < numEngineDefs) {
            alg->paramDefs[engBase + i] = engineDefs[i];
        } else {
            alg->paramDefs[engBase + i] = placeholderParam;
        }
        // Update the framework's copy of this param definition
        NT_updateParameterDefinition(algIdx, engBase + i);
    }

    // Reset values to defaults and set greyouts
    for (int i = 0; i < kMaxEngineParams; ++i) {
        bool unused = (i >= numEngineDefs);
        int16_t defVal = unused ? 0 : alg->paramDefs[engBase + i].def;
        NT_setParameterFromAudio((uint32_t)algIdx, (uint32_t)(engBase + i) + paramOffset, defVal);
        NT_setParameterGrayedOut(algIdx, (uint32_t)(engBase + i) + paramOffset, unused);
    }
    alg->switchingEngine = false;

    // Update engine page name: "Ch N <Engine>" or "Ch N" for None
    {
        char* buf = alg->pageNameBufs[ch * 2 + 1];
        int len = strCopy(buf, "Ch ", 3);
        len += NT_intToString(buf + len, ch + 1);
        if (newType != kEngineNone) {
            buf[len++] = ' ';
            len += strCopy(buf + len, engineName(newType), 23 - len);
        }
        buf[len] = 0;
    }

    // Reset channel CV cache
    alg->channels[ch].cachedPitch = 0.0f;
    alg->channels[ch].cachedGate = 0.0f;
    alg->channels[ch].cachedVelocity = 0.0f;

    // Propagate note weight mode to new engine
    if (alg->channels[ch].engine)
        alg->channels[ch].engine->setWeightMode(alg->v[kParamNoteWeight]);
}

void parameterChanged(_NT_algorithm* self, int p)
{
    NtSeq* alg = static_cast<NtSeq*>(self);

    // Skip recursive calls during engine switching (value resets)
    if (alg->switchingEngine)
        return;

    int algIdx = NT_algorithmIndex(static_cast<const _NT_algorithm*>(self));

    // Note weight mode changed — propagate to all engines
    if (p == kParamNoteWeight) {
        int mode = alg->v[kParamNoteWeight];
        for (uint32_t ch = 0; ch < alg->numChannels; ++ch) {
            if (alg->channels[ch].engine)
                alg->channels[ch].engine->setWeightMode(mode);
        }
        return;
    }

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

    // Engine Type params: indices kNumGlobalParams .. kNumGlobalParams + numChannels - 1
    int engineTypeFirst = kNumGlobalParams;
    int engineTypeLast = kNumGlobalParams + (int)alg->numChannels;
    if (p >= engineTypeFirst && p < engineTypeLast) {
        int ch = p - engineTypeFirst;
        switchChannelEngine(alg, algIdx, ch, alg->v[p]);
        return;
    }

    // Check per-channel parameters
    for (uint32_t ch = 0; ch < alg->numChannels; ++ch) {
        int base = alg->channels[ch].paramBase;
        int engBase = alg->channels[ch].engineParamBase;
        int engEnd = engBase + kMaxEngineParams;

        // Is this param in this channel's range?
        if (p < base || p >= engEnd) continue;

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
            // Skip if this is an unused slot
            if (localEngIdx >= alg->channels[ch].numEngineParams)
                return;

            if (alg->channels[ch].engine)
                alg->channels[ch].engine->parameterChanged(localEngIdx, alg->v[p]);

            // Thorp Arp Slot change: sync loaded slot params back to v[] + UI
            if (alg->channels[ch].engineType == kEngineThorp &&
                localEngIdx == ThorpEngine::kThorpArpSlot &&
                alg->channels[ch].engine && algIdx >= 0)
            {
                ThorpEngine* th = static_cast<ThorpEngine*>(alg->channels[ch].engine);
                int16_t pat, vel, len, off, rev;
                th->getLoadedSlotParams(pat, vel, len, off, rev);
                uint32_t paramOffset = NT_parameterOffset();
                NT_setParameterFromAudio((uint32_t)algIdx, (uint32_t)(engBase + ThorpEngine::kThorpPattern) + paramOffset, pat);
                NT_setParameterFromAudio((uint32_t)algIdx, (uint32_t)(engBase + ThorpEngine::kThorpVelPattern) + paramOffset, vel);
                NT_setParameterFromAudio((uint32_t)algIdx, (uint32_t)(engBase + ThorpEngine::kThorpLength) + paramOffset, len);
                NT_setParameterFromAudio((uint32_t)algIdx, (uint32_t)(engBase + ThorpEngine::kThorpOffset) + paramOffset, off);
                NT_setParameterFromAudio((uint32_t)algIdx, (uint32_t)(engBase + ThorpEngine::kThorpReverse) + paramOffset, rev);
            }

            // Thorp Chain Len change: sync Arp Slot to show what's at the new position
            // Guarded by initDone to avoid re-entrant cascade during parameter restore.
            if (alg->initDone &&
                alg->channels[ch].engineType == kEngineThorp &&
                localEngIdx == ThorpEngine::kThorpChainLen &&
                alg->channels[ch].engine && algIdx >= 0)
            {
                ThorpEngine* th = static_cast<ThorpEngine*>(alg->channels[ch].engine);
                int newChainLen = alg->v[p];
                int slotAtPos = th->uiChainSlotAt(newChainLen - 1); // 1-based
                uint32_t paramOffset = NT_parameterOffset();
                NT_setParameterFromAudio((uint32_t)algIdx, (uint32_t)(engBase + ThorpEngine::kThorpArpSlot) + paramOffset, (int16_t)slotAtPos);
            }
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

    int numCh = (int)alg->numChannels;

    // Engine Type params: indices kNumGlobalParams..kNumGlobalParams+numChannels-1
    int engineTypeFirst = kNumGlobalParams;
    int engineTypeLast = engineTypeFirst + numCh;
    if (p >= engineTypeFirst && p < engineTypeLast) {
        int ch = p - engineTypeFirst;
        int len = NT_intToString(buff, ch + 1);
        buff[len++] = ':';
        buff[len] = 0;
        return len;
    }

    // Per-channel common + engine params: prefix with "N:"
    int perChannelStart = engineTypeLast;
    if (p >= perChannelStart) {
        int ch = (p - perChannelStart) / kParamsPerChannel;
        if (ch < numCh) {
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
