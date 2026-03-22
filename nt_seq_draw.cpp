#include "nt_seq.h"
#include "engines/ThorpEngine.h"
#include "engines/AeSequencerEngine.h"
#include "engines/SeqMarkovEngine.h"
#include "engines/SomaEngine.h"
#include "engines/FerromagneticEngine.h"
#include <string.h>

static const char* rootNoteNames[] = {
    "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"
};

// --- Helpers ---

// Format pitch as note name (when scale enabled) or voltage
static int formatPitch(char* buf, float pitch, bool scaleEnabled, int rootNote, int octave)
{
    if (scaleEnabled && rootNote >= 0 && rootNote < 12) {
        // Convert V/oct pitch back to MIDI-ish note number
        // pitch is already offset by root/octave, so reverse that
        float rawPitch = pitch;
        int midiNote = (int)(rawPitch * 12.0f + 60.0f + 0.5f);
        if (midiNote < 0) midiNote = 0;
        if (midiNote > 127) midiNote = 127;
        int note = midiNote % 12;
        int oct = midiNote / 12 - 1;
        const char* nn = rootNoteNames[note];
        int len = 0;
        while (*nn) buf[len++] = *nn++;
        if (oct < 0) {
            buf[len++] = '-';
            len += NT_intToString(buf + len, -oct);
        } else {
            len += NT_intToString(buf + len, oct);
        }
        buf[len] = 0;
        return len;
    } else {
        int len = NT_floatToString(buf, pitch, 1);
        buf[len++] = 'V';
        buf[len] = 0;
        return len;
    }
}

// Generic renderer: draw segmented bars from FocusBarInfo data.
static void drawFocusBars(int x, int y, int w, int h, const FocusBarInfo& info)
{
    if (info.numBars <= 0) return;

    int gap = (info.numBars > 1) ? 1 : 0;
    int barH = (h - gap * (info.numBars - 1)) / info.numBars;
    if (barH < 2) barH = 2;

    for (int b = 0; b < info.numBars && b < kMaxFocusBars; ++b) {
        const FocusBar& bar = info.bars[b];
        if (bar.length <= 0) continue;

        int by = y + b * (barH + gap);

        // Background
        NT_drawShapeI(kNT_rectangle, x, by, x + w - 1, by + barH - 1, 1);

        // Per-step segments
        int segW = w / bar.length;
        if (segW < 1) segW = 1;

        for (int i = 0; i < bar.length && i < kMaxBarSteps && i * segW < w; ++i) {
            int sx0 = x + i * segW;
            int sx1 = sx0 + segW - 2;
            if (sx1 >= x + w) sx1 = x + w - 1;
            if (sx1 < sx0) sx1 = sx0;
            int colour = (int)bar.levels[i];
            if (colour < 1) colour = 1;
            NT_drawShapeI(kNT_rectangle, sx0, by, sx1, by + barH - 1, colour);
        }

        // Playhead: white underline beneath the current step segment
        if (bar.playhead >= 0) {
            int ph = bar.playhead % bar.length;
            int sx0 = x + ph * segW;
            int sx1 = sx0 + segW - 2;
            if (sx1 >= x + w) sx1 = x + w - 1;
            if (sx1 < sx0) sx1 = sx0;
            NT_drawShapeI(kNT_line, sx0, by + barH, sx1, by + barH, 15);
        }
    }
}

// Render FocusDetail lines: groups same-colour character runs into single
// NT_drawText calls. Tiny font = 4px per character.
static void drawFocusDetailLines(int y1, int y2, const FocusDetail& detail)
{
    const int yPos[2] = { y1, y2 };

    for (int lineIdx = 0; lineIdx < 2; ++lineIdx) {
        const FocusDetailLine& line = detail.lines[lineIdx];
        if (line.len <= 0) continue;

        int i = 0;
        while (i < line.len) {
            // Find run of same colour
            uint8_t colour = line.colours[i];
            int start = i;
            while (i < line.len && line.colours[i] == colour)
                ++i;

            // Extract substring for this run
            char buf[65];
            int runLen = i - start;
            for (int j = 0; j < runLen; ++j)
                buf[j] = line.text[start + j];
            buf[runLen] = 0;

            int xPos = start * 4; // tiny font: 4px per char
            NT_drawText(xPos, yPos[lineIdx], buf, colour, kNT_textLeft, kNT_textTiny);
        }
    }
}

// --- Overview mode ---

static void drawOverview(NtSeq* alg)
{
    char buf[48];

    // Title: "Multi Seq" + scale info + "[R]:focus"
    NT_drawText(0, 12, "Multi Seq", 15, kNT_textLeft, kNT_textTiny);

    // Scale info: root note + scale name
    int rootNote = alg->v[kParamRootNote];
    int octave = alg->v[kParamOctave];
    if (rootNote >= 0 && rootNote < 12) {
        int len = 0;
        const char* rn = rootNoteNames[rootNote];
        while (*rn) buf[len++] = *rn++;
        len += NT_intToString(buf + len, octave);
        buf[len] = 0;
        NT_drawText(60, 12, buf, 8, kNT_textLeft, kNT_textTiny);
    }
    if (alg->sclName[0] != 0) {
        int len = 0;
        const char* sn = alg->sclName;
        while (*sn && len < 20) buf[len++] = *sn++;
        if (alg->scaleQuantizer.isLoaded()) {
            buf[len++] = ' ';
            buf[len++] = '(';
            len += NT_intToString(buf + len, (int32_t)alg->scaleQuantizer.numNotes());
            buf[len++] = ')';
        }
        buf[len] = 0;
        NT_drawText(85, 12, buf, 6, kNT_textLeft, kNT_textTiny);
    }

    NT_drawText(255, 12, "[R]:focus", 5, kNT_textRight, kNT_textTiny);

    // Per-channel rows
    int y = 21;
    bool scaleEnabled;

    for (uint32_t ch = 0; ch < alg->numChannels; ++ch) {
        SequencerEngine* eng = alg->channels[ch].engine;
        int base = alg->channels[ch].paramBase;
        scaleEnabled = alg->v[base + kChScaleEnable] != 0;

        // Channel number (1-based)
        NT_intToString(buf, ch + 1);
        NT_drawText(0, y, buf, 15, kNT_textLeft, kNT_textTiny);

        // Engine name (or "--" for None)
        if (eng)
            NT_drawText(7, y, eng->name(), 10, kNT_textLeft, kNT_textTiny);
        else
            NT_drawText(7, y, "--", 5, kNT_textLeft, kNT_textTiny);

        // Status text from engine
        if (eng) {
            eng->getStatusText(buf, sizeof(buf));
            NT_drawText(40, y, buf, 8, kNT_textLeft, kNT_textTiny);
        }

        // Step position bar (simple background + playhead)
        // Tiny font baseline is y; glyphs span ~y-6 to y.
        if (eng) {
            int step = eng->currentStep();
            int length = eng->sequenceLength();
            if (length > 0) {
                NT_drawShapeI(kNT_rectangle, 95, y - 5, 95 + 64 - 1, y - 1, 2);
                if (step >= 0) {
                    int px = 95 + (step * 63) / (length > 1 ? length - 1 : 1);
                    NT_drawShapeI(kNT_rectangle, px, y - 5, px + 1, y - 1, 15);
                }
            }
        }

        // Pitch display
        formatPitch(buf, alg->channels[ch].cachedPitch, scaleEnabled,
                    alg->v[kParamRootNote], alg->v[kParamOctave]);
        NT_drawText(164, y, buf, 12, kNT_textLeft, kNT_textTiny);

        // Gate indicator
        if (alg->channels[ch].cachedGate > 0.0f) {
            NT_drawShapeI(kNT_rectangle, 208, y - 5, 213, y - 1, 15);
        }

        y += 9;
    }
}

// --- Focus mode ---

static void drawFocus(NtSeq* alg, int focusCh)
{
    char buf[64];
    SequencerEngine* eng = alg->channels[focusCh].engine;
    int base = alg->channels[focusCh].paramBase;
    bool scaleEnabled = alg->v[base + kChScaleEnable] != 0;

    // --- Line 1 (y=0): "Ch N: <Engine>" ---
    int len = 0;
    const char* s = "Ch "; while (*s) buf[len++] = *s++;
    len += NT_intToString(buf + len, focusCh + 1);
    buf[len++] = ':';
    buf[len++] = ' ';
    if (eng) {
        s = eng->name();
        while (*s) buf[len++] = *s++;
    } else {
        s = "--"; while (*s) buf[len++] = *s++;
    }
    buf[len] = 0;
    NT_drawText(0, 7, buf, 15, kNT_textLeft, kNT_textNormal);

    // Scale info on right side
    int rootNote = alg->v[kParamRootNote];
    int octave = alg->v[kParamOctave];
    if (rootNote >= 0 && rootNote < 12) {
        len = 0;
        const char* rn = rootNoteNames[rootNote];
        while (*rn) buf[len++] = *rn++;
        len += NT_intToString(buf + len, octave);
        if (alg->sclName[0] != 0) {
            buf[len++] = ' ';
            const char* sn = alg->sclName;
            while (*sn && len < 30) buf[len++] = *sn++;
            if (alg->scaleQuantizer.isLoaded()) {
                buf[len++] = ' ';
                buf[len++] = '(';
                len += NT_intToString(buf + len, (int32_t)alg->scaleQuantizer.numNotes());
                buf[len++] = ')';
            }
        }
        buf[len] = 0;
        NT_drawText(255, 7, buf, 8, kNT_textRight, kNT_textTiny);
    }

    // If no engine, just show "No engine selected"
    if (!eng) {
        NT_drawText(0, 30, "No engine selected", 8, kNT_textLeft, kNT_textTiny);
        return;
    }

    // --- Line 2 (y=10): Step bar + step counter ---
    {
        FocusBarInfo barInfo;
        eng->getFocusBarInfo(barInfo);
        drawFocusBars(0, 10, 230, 7, barInfo);

        int step = eng->currentStep();
        int length = eng->sequenceLength();

        // Step counter right-aligned
        len = 0;
        if (step >= 0)
            len += NT_intToString(buf + len, step + 1);
        else
            buf[len++] = '0';
        buf[len++] = '/';
        len += NT_intToString(buf + len, length);
        buf[len] = 0;
        NT_drawText(255, 17, buf, 10, kNT_textRight, kNT_textTiny);
    }

    // --- Line 3 (y=20): Note, Gate, Velocity ---
    len = 0;
    s = "Note:"; while (*s) buf[len++] = *s++;
    len += formatPitch(buf + len, alg->channels[focusCh].cachedPitch, scaleEnabled, rootNote, octave);
    s = "  Gate:"; while (*s) buf[len++] = *s++;
    s = (alg->channels[focusCh].cachedGate > 0.0f) ? "ON" : "OFF";
    while (*s) buf[len++] = *s++;
    s = "  Vel:"; while (*s) buf[len++] = *s++;
    len += NT_floatToString(buf + len, alg->channels[focusCh].cachedVelocity, 1);
    buf[len++] = 'V';
    buf[len] = 0;
    NT_drawText(0, 25, buf, 10, kNT_textLeft, kNT_textTiny);

    // --- Lines 4-5 (y=31, y=39): Engine-specific params ---
    {
        FocusDetail detail;
        eng->getFocusDetail(detail);
        drawFocusDetailLines(33, 41, detail);
    }

    // --- Separator line (y=48) ---
    NT_drawShapeI(kNT_line, 0, 48, 255, 48, 4);

    // --- Other channels summary (y=51) ---
    int x = 0;
    for (uint32_t ch = 0; ch < alg->numChannels; ++ch) {
        if ((int)ch == focusCh) continue;

        int chBase = alg->channels[ch].paramBase;
        bool chScaleEn = alg->v[chBase + kChScaleEnable] != 0;

        len = 0;
        len += NT_intToString(buf + len, ch + 1);
        buf[len++] = ':';
        if (alg->channels[ch].engine) {
            s = alg->channels[ch].engine->name();
            while (*s && len < 12) buf[len++] = *s++;
        } else {
            s = "--"; while (*s && len < 12) buf[len++] = *s++;
        }
        buf[len++] = ' ';
        len += formatPitch(buf + len, alg->channels[ch].cachedPitch, chScaleEn,
                           alg->v[kParamRootNote], alg->v[kParamOctave]);
        buf[len++] = ' ';
        buf[len++] = (alg->channels[ch].cachedGate > 0.0f) ? 'G' : '.';
        buf[len] = 0;
        NT_drawText(x, 56, buf, 7, kNT_textLeft, kNT_textTiny);
        x += 85;
    }
}

// --- Public draw function ---

bool draw(_NT_algorithm* self)
{
    NtSeq* alg = static_cast<NtSeq*>(self);

    if (alg->focusChannel >= 0 && alg->focusChannel < (int8_t)alg->numChannels)
        drawFocus(alg, alg->focusChannel);
    else
        drawOverview(alg);

    return true;
}

// --- Custom UI: right encoder button cycles focus ---

uint32_t hasCustomUi(_NT_algorithm* self)
{
    NtSeq* alg = static_cast<NtSeq*>(self);

    uint32_t controls = kNT_encoderButtonR | kNT_potButtonC;
    int focus = alg->focusChannel;
    if (focus >= 0 && focus < (int)alg->numChannels && alg->channels[focus].engineType == kEngineThorp) {
        controls |= kNT_encoderL | kNT_encoderR | kNT_encoderButtonL;
        controls |= kNT_potL | kNT_potC | kNT_potR;
        controls |= kNT_potButtonL | kNT_potButtonR;
    } else if (focus >= 0 && focus < (int)alg->numChannels && alg->channels[focus].engineType == kEngineAeSeq) {
        controls |= kNT_encoderL | kNT_encoderR;
        controls |= kNT_potL | kNT_potC | kNT_potR;
    } else if (focus >= 0 && focus < (int)alg->numChannels && alg->channels[focus].engineType == kEngineSoma) {
        controls |= kNT_encoderL | kNT_encoderR;
        controls |= kNT_potL | kNT_potC | kNT_potR;
    } else if (focus >= 0 && focus < (int)alg->numChannels && alg->channels[focus].engineType == kEngineSeqMarkov) {
        controls |= kNT_encoderL | kNT_encoderR;
        controls |= kNT_potL | kNT_potC | kNT_potR;
        controls |= kNT_potButtonL | kNT_potButtonR;
    } else if (focus >= 0 && focus < (int)alg->numChannels && alg->channels[focus].engineType == kEngineFerro) {
        controls |= kNT_encoderL | kNT_encoderR;
        controls |= kNT_potL | kNT_potC | kNT_potR;
    }
    return controls;
}

void customUi(_NT_algorithm* self, const _NT_uiData& data)
{
    NtSeq* alg = static_cast<NtSeq*>(self);
    int algIdx = NT_algorithmIndex(self);
    if (algIdx < 0)
        return;
    uint32_t paramOffset = NT_parameterOffset();

    int focus = alg->focusChannel;
    bool markovFocus = (focus >= 0 && focus < (int)alg->numChannels &&
        alg->channels[focus].engineType == kEngineSeqMarkov);

    // Rising edge on right encoder button: cycle focus except while using it
    // as Markov's force-regenerate control.
    if (!markovFocus &&
        (data.controls & kNT_encoderButtonR) &&
        !(data.lastButtons & kNT_encoderButtonR)) {
        // Cycle: -1 -> 0 -> 1 -> ... -> (numChannels-1) -> -1
        alg->focusChannel++;
        if (alg->focusChannel >= (int8_t)alg->numChannels) {
            alg->focusChannel = -1;
        }
        return;
    }

    // Pot center button cycles focus (works in all engine focuses).
    if ((data.controls & kNT_potButtonC) && !(data.lastButtons & kNT_potButtonC)) {
        alg->focusChannel++;
        if (alg->focusChannel >= (int8_t)alg->numChannels) {
            alg->focusChannel = -1;
        }
        return;
    }

    focus = alg->focusChannel;
    if (focus < 0 || focus >= (int)alg->numChannels)
        return;

    // AE focus controls
    if (alg->channels[focus].engineType == kEngineAeSeq && alg->channels[focus].engine) {
        int engBase = alg->channels[focus].engineParamBase;

        if (data.encoders[0] != 0) {
            int v = alg->v[engBase + AeSequencerEngine::kAeCvSeq] + data.encoders[0];
            while (v < 1) v += 20;
            while (v > 20) v -= 20;
            NT_setParameterFromUi((uint32_t)algIdx, (uint32_t)(engBase + AeSequencerEngine::kAeCvSeq) + paramOffset, (int16_t)v);
        }
        if (data.encoders[1] != 0) {
            int v = alg->v[engBase + AeSequencerEngine::kAeGateSeq] + data.encoders[1];
            while (v < 1) v += 20;
            while (v > 20) v -= 20;
            NT_setParameterFromUi((uint32_t)algIdx, (uint32_t)(engBase + AeSequencerEngine::kAeGateSeq) + paramOffset, (int16_t)v);
        }

        if (data.controls & kNT_potL) {
            int v = 1 + (int)(data.pots[0] * 99.999f);
            if (v < 1) v = 1;
            if (v > 100) v = 100;
            NT_setParameterFromUi((uint32_t)algIdx, (uint32_t)(engBase + AeSequencerEngine::kAeThreshold) + paramOffset, (int16_t)v);
        }
        if (data.controls & kNT_potC) {
            int v = 2 + (int)(data.pots[1] * 14.999f);
            if (v < 2) v = 2;
            if (v > 16) v = 16;
            NT_setParameterFromUi((uint32_t)algIdx, (uint32_t)(engBase + AeSequencerEngine::kAeBitDepth) + paramOffset, (int16_t)v);
        }
        if (data.controls & kNT_potR) {
            int v = (int)(data.pots[2] * 100.0f + 0.5f);
            if (v < 0) v = 0;
            if (v > 100) v = 100;
            NT_setParameterFromUi((uint32_t)algIdx, (uint32_t)(engBase + AeSequencerEngine::kAeVelocity) + paramOffset, (int16_t)v);
        }
        return;
    }

    // Soma focus controls
    if (alg->channels[focus].engineType == kEngineSoma && alg->channels[focus].engine) {
        int engBase = alg->channels[focus].engineParamBase;

        if (data.controls & kNT_potL) {
            int v = (int)(data.pots[0] * 100.0f + 0.5f);
            if (v < 0) v = 0;
            if (v > 100) v = 100;
            NT_setParameterFromUi((uint32_t)algIdx, (uint32_t)(engBase + SomaEngine::kSomaNoteMutate) + paramOffset, (int16_t)v);
        }
        if (data.controls & kNT_potC) {
            int v = (int)(data.pots[1] * 100.0f + 0.5f);
            if (v < 0) v = 0;
            if (v > 100) v = 100;
            NT_setParameterFromUi((uint32_t)algIdx, (uint32_t)(engBase + SomaEngine::kSomaGateMutate) + paramOffset, (int16_t)v);
        }
        if (data.controls & kNT_potR) {
            int v = (int)(data.pots[2] * 100.0f + 0.5f);
            if (v < 0) v = 0;
            if (v > 100) v = 100;
            NT_setParameterFromUi((uint32_t)algIdx, (uint32_t)(engBase + SomaEngine::kSomaOctaveSpread) + paramOffset, (int16_t)v);
        }
        if (data.encoders[0] != 0) {
            int v = alg->v[engBase + SomaEngine::kSomaLength] + data.encoders[0];
            if (v < 1) v = 1;
            if (v > 64) v = 64;
            NT_setParameterFromUi((uint32_t)algIdx, (uint32_t)(engBase + SomaEngine::kSomaLength) + paramOffset, (int16_t)v);
        }
        if (data.encoders[1] != 0) {
            int v = alg->v[engBase + SomaEngine::kSomaVelocity] + data.encoders[1];
            if (v < 0) v = 0;
            if (v > 100) v = 100;
            NT_setParameterFromUi((uint32_t)algIdx, (uint32_t)(engBase + SomaEngine::kSomaVelocity) + paramOffset, (int16_t)v);
        }
        return;
    }

    // Markov focus controls
    if (alg->channels[focus].engineType == kEngineSeqMarkov && alg->channels[focus].engine) {
        SeqMarkovEngine* mk = static_cast<SeqMarkovEngine*>(alg->channels[focus].engine);
        int engBase = alg->channels[focus].engineParamBase;

        // Encoder L: Style (wrapping 0-7)
        if (data.encoders[0] != 0) {
            int v = alg->v[engBase + SeqMarkovEngine::kMarkovStyle] + data.encoders[0];
            while (v < 0) v += SeqMarkovEngine::kNumStyles;
            while (v >= SeqMarkovEngine::kNumStyles) v -= SeqMarkovEngine::kNumStyles;
            NT_setParameterFromUi((uint32_t)algIdx, (uint32_t)(engBase + SeqMarkovEngine::kMarkovStyle) + paramOffset, (int16_t)v);
        }

        // Encoder R: Length (1-64)
        if (data.encoders[1] != 0) {
            int v = alg->v[engBase + SeqMarkovEngine::kMarkovLength] + data.encoders[1];
            if (v < 1) v = 1;
            if (v > 64) v = 64;
            NT_setParameterFromUi((uint32_t)algIdx, (uint32_t)(engBase + SeqMarkovEngine::kMarkovLength) + paramOffset, (int16_t)v);
        }

        // Pot L: Emotion (0-100)
        if (data.controls & kNT_potL) {
            int v = (int)(data.pots[0] * 100.0f + 0.5f);
            if (v < 0) v = 0;
            if (v > 100) v = 100;
            NT_setParameterFromUi((uint32_t)algIdx, (uint32_t)(engBase + SeqMarkovEngine::kMarkovEmotion) + paramOffset, (int16_t)v);
        }

        // Pot C: Jumpiness (0-100)
        if (data.controls & kNT_potC) {
            int v = (int)(data.pots[1] * 100.0f + 0.5f);
            if (v < 0) v = 0;
            if (v > 100) v = 100;
            NT_setParameterFromUi((uint32_t)algIdx, (uint32_t)(engBase + SeqMarkovEngine::kMarkovJumpiness) + paramOffset, (int16_t)v);
        }

        // Pot R: Mutation (0-100)
        if (data.controls & kNT_potR) {
            int v = (int)(data.pots[2] * 100.0f + 0.5f);
            if (v < 0) v = 0;
            if (v > 100) v = 100;
            NT_setParameterFromUi((uint32_t)algIdx, (uint32_t)(engBase + SeqMarkovEngine::kMarkovMutation) + paramOffset, (int16_t)v);
        }

        // Pot Button L: Randomize
        if ((data.controls & kNT_potButtonL) && !(data.lastButtons & kNT_potButtonL))
            mk->uiForceRandomize();

        // Pot Button R: Regenerate
        if ((data.controls & kNT_potButtonR) && !(data.lastButtons & kNT_potButtonR))
            mk->uiForceRegenerate();
        return;
    }

    // Ferro focus controls
    if (alg->channels[focus].engineType == kEngineFerro && alg->channels[focus].engine) {
        int engBase = alg->channels[focus].engineParamBase;

        // Encoder L: Loop Steps (2-128)
        if (data.encoders[0] != 0) {
            int v = alg->v[engBase + FerromagneticEngine::kFerroLoopSteps] + data.encoders[0];
            if (v < 2) v = 2;
            if (v > 128) v = 128;
            NT_setParameterFromUi((uint32_t)algIdx, (uint32_t)(engBase + FerromagneticEngine::kFerroLoopSteps) + paramOffset, (int16_t)v);
        }
        // Encoder R: Max Layers (1-8)
        if (data.encoders[1] != 0) {
            int v = alg->v[engBase + FerromagneticEngine::kFerroMaxLayers] + data.encoders[1];
            if (v < 1) v = 1;
            if (v > 8) v = 8;
            NT_setParameterFromUi((uint32_t)algIdx, (uint32_t)(engBase + FerromagneticEngine::kFerroMaxLayers) + paramOffset, (int16_t)v);
        }
        // Pot L: Note Density (0-100%)
        if (data.controls & kNT_potL) {
            int v = (int)(data.pots[0] * 100.0f + 0.5f);
            if (v < 0) v = 0;
            if (v > 100) v = 100;
            NT_setParameterFromUi((uint32_t)algIdx, (uint32_t)(engBase + FerromagneticEngine::kFerroNoteDensity) + paramOffset, (int16_t)v);
        }
        // Pot C: Gate Length (10-100%)
        if (data.controls & kNT_potC) {
            int v = 10 + (int)(data.pots[1] * 90.0f + 0.5f);
            if (v < 10) v = 10;
            if (v > 100) v = 100;
            NT_setParameterFromUi((uint32_t)algIdx, (uint32_t)(engBase + FerromagneticEngine::kFerroGateLength) + paramOffset, (int16_t)v);
        }
        // Pot R: Velocity (0-100%)
        if (data.controls & kNT_potR) {
            int v = (int)(data.pots[2] * 100.0f + 0.5f);
            if (v < 0) v = 0;
            if (v > 100) v = 100;
            NT_setParameterFromUi((uint32_t)algIdx, (uint32_t)(engBase + FerromagneticEngine::kFerroVelocity) + paramOffset, (int16_t)v);
        }
        return;
    }

    // Thorp focus controls
    if (alg->channels[focus].engineType != kEngineThorp || !alg->channels[focus].engine)
        return;

    int engBase = alg->channels[focus].engineParamBase;

    // Encoder L turn: select arp slot (wraps 1-16, also writes to chain)
    if (data.encoders[0] != 0) {
        int v = alg->v[engBase + ThorpEngine::kThorpArpSlot] + data.encoders[0];
        while (v < 1) v += 16;
        while (v > 16) v -= 16;
        NT_setParameterFromUi((uint32_t)algIdx, (uint32_t)(engBase + ThorpEngine::kThorpArpSlot) + paramOffset, (int16_t)v);
    }

    // Encoder L push: increment Chain Len (extend chain)
    if ((data.controls & kNT_encoderButtonL) && !(data.lastButtons & kNT_encoderButtonL)) {
        int v = alg->v[engBase + ThorpEngine::kThorpChainLen] + 1;
        if (v <= ThorpEngine::kNumSlots)
            NT_setParameterFromUi((uint32_t)algIdx, (uint32_t)(engBase + ThorpEngine::kThorpChainLen) + paramOffset, (int16_t)v);
    }

    // Encoder R turn: adjust Chain Len
    if (data.encoders[1] != 0) {
        int v = alg->v[engBase + ThorpEngine::kThorpChainLen] + data.encoders[1];
        if (v < 1) v = 1;
        if (v > ThorpEngine::kNumSlots) v = ThorpEngine::kNumSlots;
        NT_setParameterFromUi((uint32_t)algIdx, (uint32_t)(engBase + ThorpEngine::kThorpChainLen) + paramOffset, (int16_t)v);
    }

    // Pot L: Oct Jump %
    if (data.controls & kNT_potL) {
        int v = (int)(data.pots[0] * 100.0f + 0.5f);
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        NT_setParameterFromUi((uint32_t)algIdx, (uint32_t)(engBase + ThorpEngine::kThorpOctJump) + paramOffset, (int16_t)v);
    }

    // Pot C: Gate Prob %
    if (data.controls & kNT_potC) {
        int v = 1 + (int)(data.pots[1] * 99.0f + 0.5f);
        if (v < 1) v = 1;
        if (v > 100) v = 100;
        NT_setParameterFromUi((uint32_t)algIdx, (uint32_t)(engBase + ThorpEngine::kThorpGateProb) + paramOffset, (int16_t)v);
    }

    // Pot R: Sequence Mode
    if (data.controls & kNT_potR) {
        int v = (int)(data.pots[2] * (float)ThorpEngine::kNumSequenceModes);
        if (v >= ThorpEngine::kNumSequenceModes) v = ThorpEngine::kNumSequenceModes - 1;
        if (v < 0) v = 0;
        NT_setParameterFromUi((uint32_t)algIdx, (uint32_t)(engBase + ThorpEngine::kThorpSequenceMode) + paramOffset, (int16_t)v);
    }

    // Pot L push: shrink chain
    if ((data.controls & kNT_potButtonL) && !(data.lastButtons & kNT_potButtonL)) {
        int v = alg->v[engBase + ThorpEngine::kThorpChainLen] - 1;
        if (v >= 1)
            NT_setParameterFromUi((uint32_t)algIdx, (uint32_t)(engBase + ThorpEngine::kThorpChainLen) + paramOffset, (int16_t)v);
    }

    // Pot R push: toggle Play Mode
    if ((data.controls & kNT_potButtonR) && !(data.lastButtons & kNT_potButtonR)) {
        int playMode = alg->v[engBase + ThorpEngine::kThorpPlayMode];
        playMode = (playMode == ThorpEngine::kPlaySong) ? ThorpEngine::kPlayJam : ThorpEngine::kPlaySong;
        NT_setParameterFromUi((uint32_t)algIdx, (uint32_t)(engBase + ThorpEngine::kThorpPlayMode) + paramOffset, (int16_t)playMode);
    }
}
