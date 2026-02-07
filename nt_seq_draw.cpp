#include "nt_seq.h"
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

// Draw step position bar (thin horizontal bar with playhead)
static void drawStepBar(int x, int y, int w, int h, int step, int length)
{
    if (length <= 0) return;
    // Background bar
    NT_drawShapeI(kNT_rectangle, x, y, x + w - 1, y + h - 1, 2);
    // Playhead
    if (step >= 0) {
        int px = x + (step * (w - 1)) / (length > 1 ? length - 1 : 1);
        NT_drawShapeI(kNT_rectangle, px, y, px + 1, y + h - 1, 15);
    }
}

// Draw step bar with per-step segments (for focus mode with short sequences)
static void drawStepBarSegmented(int x, int y, int w, int h, int step, int length)
{
    if (length <= 0) return;
    // Background
    NT_drawShapeI(kNT_rectangle, x, y, x + w - 1, y + h - 1, 2);
    // Per-step segments
    int segW = w / length;
    if (segW < 2) segW = 2;
    for (int i = 0; i < length && i * segW < w; ++i) {
        int sx = x + i * segW;
        int colour = (i == step) ? 15 : 4;
        NT_drawShapeI(kNT_rectangle, sx, y, sx + segW - 2, y + h - 1, colour);
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

        // Channel number
        NT_intToString(buf, ch + 1);
        NT_drawText(0, y, buf, 15, kNT_textLeft, kNT_textTiny);

        // Engine name
        if (eng)
            NT_drawText(7, y, eng->name(), 10, kNT_textLeft, kNT_textTiny);

        // Status text from engine
        if (eng) {
            eng->getStatusText(buf, sizeof(buf));
            NT_drawText(40, y, buf, 8, kNT_textLeft, kNT_textTiny);
        }

        // Step position bar
        if (eng) {
            int step = eng->currentStep();
            int length = eng->sequenceLength();
            drawStepBar(95, y - 1, 64, 5, step, length);
        }

        // Pitch display
        formatPitch(buf, alg->channels[ch].cachedPitch, scaleEnabled,
                    alg->v[kParamRootNote], alg->v[kParamOctave]);
        NT_drawText(164, y, buf, 12, kNT_textLeft, kNT_textTiny);

        // Gate indicator
        if (alg->channels[ch].cachedGate > 0.0f) {
            NT_drawShapeI(kNT_rectangle, 208, y - 1, 213, y + 4, 15);
        }

        y += 9;
    }
}

// --- Focus mode ---

// Engine-specific focus detail strings
static void drawFocusEngineDetail(NtSeq* alg, int ch, int y1, int y2)
{
    char buf[64];
    int base = alg->channels[ch].engineParamBase;
    EngineType type = alg->channels[ch].engineType;

    switch (type) {
    case kEngineThorp: {
        // Line 1: Pat: <name>  Vel: <name>  Mode: <name>
        int len = 0;
        const char* s;
        s = "Pat:"; while (*s) buf[len++] = *s++;
        // Pattern name from param enum string
        int patIdx = alg->v[base + 0]; // kThorpPattern
        if (patIdx < 0) patIdx = 0;
        if (patIdx > 22) patIdx = 22;
        buf[len++] = ' ';
        // We know the param has enum strings, but we can't access static arrays
        // from ThorpEngine.cpp here. Use the parameter definition's enumStrings.
        const char* const* strings = alg->paramDefs[base + 0].enumStrings;
        if (strings && strings[patIdx]) {
            s = strings[patIdx];
            while (*s && len < 30) buf[len++] = *s++;
        }
        buf[len] = 0;
        NT_drawText(0, y1, buf, 8, kNT_textLeft, kNT_textTiny);

        // Mode
        len = 0;
        s = "Mode:"; while (*s) buf[len++] = *s++;
        buf[len++] = ' ';
        int modeIdx = alg->v[base + 9]; // kThorpStepMode
        const char* const* modeStrings = alg->paramDefs[base + 9].enumStrings;
        if (modeStrings && modeIdx >= 0 && modeStrings[modeIdx]) {
            s = modeStrings[modeIdx];
            while (*s && len < 20) buf[len++] = *s++;
        }
        buf[len] = 0;
        NT_drawText(140, y1, buf, 8, kNT_textLeft, kNT_textTiny);

        // Line 2: Len:8 Off:0 Rev:Off Mut:25% Gate:85%
        len = 0;
        s = "Len:"; while (*s) buf[len++] = *s++;
        len += NT_intToString(buf + len, alg->v[base + 2]); // kThorpLength
        s = " Off:"; while (*s) buf[len++] = *s++;
        len += NT_intToString(buf + len, alg->v[base + 3]); // kThorpOffset
        s = " Rev:"; while (*s) buf[len++] = *s++;
        int rev = alg->v[base + 4]; // kThorpReverse
        s = rev ? "On" : "Off";
        while (*s) buf[len++] = *s++;
        s = " Mut:"; while (*s) buf[len++] = *s++;
        len += NT_intToString(buf + len, alg->v[base + 10]); // kThorpMutation
        buf[len++] = '%';
        s = " Gate:"; while (*s) buf[len++] = *s++;
        len += NT_intToString(buf + len, alg->v[base + 6]); // kThorpGateProb
        buf[len++] = '%';
        buf[len] = 0;
        NT_drawText(0, y2, buf, 6, kNT_textLeft, kNT_textTiny);
        break;
    }

    case kEngineSoma: {
        // Line 1: Oct Spread: 50%  Note Mut: 70%
        int len = 0;
        const char* s;
        s = "Oct Spread:"; while (*s) buf[len++] = *s++;
        len += NT_intToString(buf + len, alg->v[base + 0]); // kSomaOctaveSpread
        buf[len++] = '%';
        s = "  Note Mut:"; while (*s) buf[len++] = *s++;
        len += NT_intToString(buf + len, alg->v[base + 1]); // kSomaNoteMutate
        buf[len++] = '%';
        buf[len] = 0;
        NT_drawText(0, y1, buf, 8, kNT_textLeft, kNT_textTiny);

        // Line 2: Gate Mut: 80%  Length: 8
        len = 0;
        s = "Gate Mut:"; while (*s) buf[len++] = *s++;
        len += NT_intToString(buf + len, alg->v[base + 2]); // kSomaGateMutate
        buf[len++] = '%';
        s = "  Length:"; while (*s) buf[len++] = *s++;
        len += NT_intToString(buf + len, alg->v[base + 3]); // kSomaLength
        buf[len] = 0;
        NT_drawText(0, y2, buf, 6, kNT_textLeft, kNT_textTiny);
        break;
    }

    case kEngineAeSeq: {
        // Line 1: CV Seq:1 Steps:8  Gate Seq:1 Steps:16
        int len = 0;
        const char* s;
        s = "CV Seq:"; while (*s) buf[len++] = *s++;
        len += NT_intToString(buf + len, alg->v[base + 0]); // kAeCvSeq
        s = " Steps:"; while (*s) buf[len++] = *s++;
        len += NT_intToString(buf + len, alg->v[base + 2]); // kAeCvSteps
        s = "  Gate Seq:"; while (*s) buf[len++] = *s++;
        len += NT_intToString(buf + len, alg->v[base + 1]); // kAeGateSeq
        s = " Steps:"; while (*s) buf[len++] = *s++;
        len += NT_intToString(buf + len, alg->v[base + 7]); // kAeGateSteps
        buf[len] = 0;
        NT_drawText(0, y1, buf, 8, kNT_textLeft, kNT_textTiny);

        // Line 2: Range: -1.0..+1.0V  Bits:16  Thresh:50%
        len = 0;
        s = "Range:"; while (*s) buf[len++] = *s++;
        len += NT_floatToString(buf + len, (float)alg->v[base + 3] / 10.0f, 1); // kAeMinCv
        s = ".."; while (*s) buf[len++] = *s++;
        len += NT_floatToString(buf + len, (float)alg->v[base + 4] / 10.0f, 1); // kAeMaxCv
        buf[len++] = 'V';
        s = "  Bits:"; while (*s) buf[len++] = *s++;
        len += NT_intToString(buf + len, alg->v[base + 6]); // kAeBitDepth
        s = "  Thresh:"; while (*s) buf[len++] = *s++;
        len += NT_intToString(buf + len, alg->v[base + 8]); // kAeThreshold
        buf[len++] = '%';
        buf[len] = 0;
        NT_drawText(0, y2, buf, 6, kNT_textLeft, kNT_textTiny);
        break;
    }

    case kEngineSeqMarkov: {
        // Line 1: Style:Tonal  Emotion:50%  Density:60%
        int len = 0;
        const char* s;
        s = "Style:"; while (*s) buf[len++] = *s++;
        int styleIdx = alg->v[base + 0]; // kMarkovStyle
        const char* const* strings = alg->paramDefs[base + 0].enumStrings;
        if (strings && styleIdx >= 0 && strings[styleIdx]) {
            s = strings[styleIdx];
            while (*s && len < 20) buf[len++] = *s++;
        }
        s = "  Emotion:"; while (*s) buf[len++] = *s++;
        len += NT_intToString(buf + len, alg->v[base + 1]); // kMarkovEmotion
        buf[len++] = '%';
        s = "  Density:"; while (*s) buf[len++] = *s++;
        len += NT_intToString(buf + len, alg->v[base + 6]); // kMarkovDensity
        buf[len++] = '%';
        buf[len] = 0;
        NT_drawText(0, y1, buf, 8, kNT_textLeft, kNT_textTiny);

        // Line 2: Len:16  Jump:30%  Range:2  Mut:20%
        len = 0;
        s = "Len:"; while (*s) buf[len++] = *s++;
        len += NT_intToString(buf + len, alg->v[base + 5]); // kMarkovLength
        s = "  Jump:"; while (*s) buf[len++] = *s++;
        len += NT_intToString(buf + len, alg->v[base + 2]); // kMarkovJumpiness
        buf[len++] = '%';
        s = "  Range:"; while (*s) buf[len++] = *s++;
        len += NT_intToString(buf + len, alg->v[base + 3]); // kMarkovRange
        s = "  Mut:"; while (*s) buf[len++] = *s++;
        len += NT_intToString(buf + len, alg->v[base + 4]); // kMarkovMutation
        buf[len++] = '%';
        buf[len] = 0;
        NT_drawText(0, y2, buf, 6, kNT_textLeft, kNT_textTiny);
        break;
    }

    default:
        break;
    }
}

static void drawFocus(NtSeq* alg, int focusCh)
{
    char buf[64];
    SequencerEngine* eng = alg->channels[focusCh].engine;
    int base = alg->channels[focusCh].paramBase;
    bool scaleEnabled = alg->v[base + kChScaleEnable] != 0;

    // --- Line 1 (y=0): "Ch 2: Soma" + scale info ---
    int len = 0;
    const char* s = "Ch "; while (*s) buf[len++] = *s++;
    len += NT_intToString(buf + len, focusCh + 1);
    buf[len++] = ':';
    buf[len++] = ' ';
    if (eng) {
        s = eng->name();
        while (*s) buf[len++] = *s++;
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

    // --- Line 2 (y=10): Step bar + step counter ---
    if (eng) {
        int step = eng->currentStep();
        int length = eng->sequenceLength();

        if (length > 0 && length <= 64) {
            drawStepBarSegmented(0, 10, 230, 7, step, length);
        } else if (length > 0) {
            drawStepBar(0, 10, 230, 7, step, length);
        }

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
    drawFocusEngineDetail(alg, focusCh, 33, 41);

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

    if (alg->focusChannel >= 0 && alg->focusChannel < (int8_t)alg->numChannels) {
        drawFocus(alg, alg->focusChannel);
        return true;  // Suppress parameter line, claim full display
    } else {
        drawOverview(alg);
        return false;  // Keep parameter line visible
    }
}

// --- Custom UI: right encoder button cycles focus ---

uint32_t hasCustomUi(_NT_algorithm* self)
{
    return kNT_encoderButtonR;
}

void customUi(_NT_algorithm* self, const _NT_uiData& data)
{
    NtSeq* alg = static_cast<NtSeq*>(self);

    // Rising edge on right encoder button
    if ((data.controls & kNT_encoderButtonR) && !(data.lastButtons & kNT_encoderButtonR)) {
        // Cycle: -1 -> 0 -> 1 -> ... -> (numChannels-1) -> -1
        alg->focusChannel++;
        if (alg->focusChannel >= (int8_t)alg->numChannels) {
            alg->focusChannel = -1;
        }
    }
}
