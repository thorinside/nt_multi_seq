#include "nt_seq.h"
#include "engines/ThorpEngine.h"
#include "engines/AeSequencerEngine.h"
#include "engines/SeqMarkovEngine.h"
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

// AE focus bar: gate-on steps are visible; fill brightness follows
// quantized CV level (0..15).
static void drawAeStepBar(int x, int y, int w, int h, AeSequencerEngine* ae, int playhead)
{
    if (!ae) return;
    int length = ae->previewLength();
    if (length <= 0) return;

    NT_drawShapeI(kNT_rectangle, x, y, x + w - 1, y + h - 1, 1);

    int segW = w / length;
    if (segW < 2) segW = 2;

    for (int i = 0; i < length && i * segW < w; ++i) {
        uint8_t cvLevel = 0;
        bool gateOn = false;
        ae->getPreviewStep(i, cvLevel, gateOn);

        int sx0 = x + i * segW;
        int sx1 = sx0 + segW - 2;
        if (sx1 >= x + w) sx1 = x + w - 1;
        if (sx1 < sx0) sx1 = sx0;

        int colour = gateOn ? (int)cvLevel : 1;
        if (gateOn && colour < 2) colour = 2;
        NT_drawShapeI(kNT_rectangle, sx0, y, sx1, y + h - 1, colour);
    }

    if (playhead >= 0) {
        int ph = playhead % length;
        int px = x + (ph * w) / length;
        NT_drawShapeI(kNT_line, px, y, px, y + h - 1, 15);
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

        // Channel number (spec slot, not dense index)
        NT_intToString(buf, alg->channels[ch].specSlot + 1);
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
        ThorpEngine* th = static_cast<ThorpEngine*>(alg->channels[ch].engine);

        // Line 1: Pat:<name> Mode:<name> Play:<mode>
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

        // Sequence mode
        len = 0;
        s = "Mode:"; while (*s) buf[len++] = *s++;
        buf[len++] = ' ';
        int modeIdx = alg->v[base + 9]; // kThorpSequenceMode
        const char* const* modeStrings = alg->paramDefs[base + 9].enumStrings;
        if (modeStrings && modeIdx >= 0 && modeStrings[modeIdx]) {
            s = modeStrings[modeIdx];
            while (*s && len < 20) buf[len++] = *s++;
        }
        buf[len] = 0;
        NT_drawText(108, y1, buf, 8, kNT_textLeft, kNT_textTiny);

        // Play mode
        len = 0;
        s = "Play:"; while (*s) buf[len++] = *s++;
        buf[len++] = ' ';
        int playModeIdx = alg->v[base + 12]; // kThorpPlayMode
        const char* const* playModeStrings = alg->paramDefs[base + 12].enumStrings;
        if (playModeStrings && playModeIdx >= 0 && playModeStrings[playModeIdx]) {
            s = playModeStrings[playModeIdx];
            while (*s && len < 16) buf[len++] = *s++;
        }
        buf[len] = 0;
        NT_drawText(190, y1, buf, 8, kNT_textLeft, kNT_textTiny);

        // Line 2: compact performance values
        len = 0;
        s = "L:"; while (*s) buf[len++] = *s++;
        len += NT_intToString(buf + len, alg->v[base + 2]); // kThorpLength
        s = " O:"; while (*s) buf[len++] = *s++;
        len += NT_intToString(buf + len, alg->v[base + 3]); // kThorpOffset
        s = " R:"; while (*s) buf[len++] = *s++;
        buf[len++] = alg->v[base + 4] ? '1' : '0'; // kThorpReverse
        s = " S:"; while (*s) buf[len++] = *s++;
        len += NT_intToString(buf + len, alg->v[base + 5]); // kThorpArpSlot
        s = " C:"; while (*s) buf[len++] = *s++;
        len += NT_intToString(buf + len, alg->v[base + 13]); // kThorpChainLen
        s = " P:"; while (*s) buf[len++] = *s++;
        int chainPos = th ? (th->uiChainPos() + 1) : 1;
        len += NT_intToString(buf + len, chainPos);
        s = " CS:"; while (*s) buf[len++] = *s++;
        int chainSlot = th ? th->uiChainSlot() : 1;
        len += NT_intToString(buf + len, chainSlot);
        s = " G:"; while (*s) buf[len++] = *s++;
        len += NT_intToString(buf + len, alg->v[base + 6]); // kThorpGateProb
        buf[len++] = '%';
        s = " V:"; while (*s) buf[len++] = *s++;
        len += NT_intToString(buf + len, alg->v[base + 10]); // kThorpGlobalVelocity
        buf[len++] = '%';
        s = " GL:"; while (*s) buf[len++] = *s++;
        len += NT_intToString(buf + len, alg->v[base + 11]); // kThorpGateLen
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
    len += NT_intToString(buf + len, alg->channels[focusCh].specSlot + 1);
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

        if (alg->channels[focusCh].engineType == kEngineAeSeq) {
            AeSequencerEngine* ae = static_cast<AeSequencerEngine*>(eng);
            drawAeStepBar(0, 10, 230, 7, ae, step);
            length = ae->previewLength();
        } else if (length > 0 && length <= 64) {
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
        len += NT_intToString(buf + len, alg->channels[ch].specSlot + 1);
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
    NtSeq* alg = static_cast<NtSeq*>(self);

    uint32_t controls = kNT_encoderButtonR | kNT_potButtonC;
    int focus = alg->focusChannel;
    if (focus >= 0 && focus < (int)alg->numChannels && alg->channels[focus].engineType == kEngineThorp) {
        controls |= kNT_button1 | kNT_button2 | kNT_button3 | kNT_button4;
        controls |= kNT_encoderL | kNT_encoderR | kNT_encoderButtonL;
        controls |= kNT_potL | kNT_potC | kNT_potR;
    } else if (focus >= 0 && focus < (int)alg->numChannels && alg->channels[focus].engineType == kEngineAeSeq) {
        controls |= kNT_encoderL | kNT_encoderR;
        controls |= kNT_potL | kNT_potC | kNT_potR;
    } else if (focus >= 0 && focus < (int)alg->numChannels && alg->channels[focus].engineType == kEngineSeqMarkov) {
        controls |= kNT_potButtonL | kNT_potButtonR;
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

    // AE focus controls:
    // Encoder L/R: CV Seq / Gate Seq
    // Pot L/C/R: Gate Threshold / CV Bit Depth / Velocity
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
            int v = 1 + (int)(data.pots[0] * 99.999f); // 1..100
            if (v < 1) v = 1;
            if (v > 100) v = 100;
            NT_setParameterFromUi((uint32_t)algIdx, (uint32_t)(engBase + AeSequencerEngine::kAeThreshold) + paramOffset, (int16_t)v);
        }
        if (data.controls & kNT_potC) {
            int v = 2 + (int)(data.pots[1] * 14.999f); // 2..16
            if (v < 2) v = 2;
            if (v > 16) v = 16;
            NT_setParameterFromUi((uint32_t)algIdx, (uint32_t)(engBase + AeSequencerEngine::kAeBitDepth) + paramOffset, (int16_t)v);
        }
        if (data.controls & kNT_potR) {
            int v = (int)(data.pots[2] * 100.0f + 0.5f); // 0..100
            if (v < 0) v = 0;
            if (v > 100) v = 100;
            NT_setParameterFromUi((uint32_t)algIdx, (uint32_t)(engBase + AeSequencerEngine::kAeVelocity) + paramOffset, (int16_t)v);
        }
        return;
    }

    // Markov focus controls:
    // Pot 1 button: force mutate
    // Pot 3 button: force regenerate
    if (alg->channels[focus].engineType == kEngineSeqMarkov && alg->channels[focus].engine) {
        SeqMarkovEngine* mk = static_cast<SeqMarkovEngine*>(alg->channels[focus].engine);
        if ((data.controls & kNT_potButtonL) && !(data.lastButtons & kNT_potButtonL))
            mk->uiForceMutate();
        if ((data.controls & kNT_potButtonR) && !(data.lastButtons & kNT_potButtonR))
            mk->uiForceRegenerate();
        return;
    }

    if (alg->channels[focus].engineType != kEngineThorp || !alg->channels[focus].engine)
        return;

    ThorpEngine* th = static_cast<ThorpEngine*>(alg->channels[focus].engine);
    int engBase = alg->channels[focus].engineParamBase;

    // Pot L: chain length (1-16)
    if (data.controls & kNT_potL) {
        int len = 1 + (int)(data.pots[0] * 15.999f);
        if (len < 1) len = 1;
        if (len > 16) len = 16;
        NT_setParameterFromUi((uint32_t)algIdx, (uint32_t)(engBase + ThorpEngine::kThorpChainLen) + paramOffset, (int16_t)len);
        th->uiSetChainLength(len);
    }

    // Pot C: chain cursor position (1..chainLen)
    if (data.controls & kNT_potC) {
        int len = th->uiChainLength();
        int pos = (int)(data.pots[1] * (float)len);
        if (pos >= len) pos = len - 1;
        if (pos < 0) pos = 0;
        th->uiSetChainPos(pos);
    }

    // Pot R: slot at current chain position (1-16)
    if (data.controls & kNT_potR) {
        int target = 1 + (int)(data.pots[2] * 15.999f);
        if (target < 1) target = 1;
        if (target > 16) target = 16;
        int delta = target - th->uiChainSlot();
        th->uiAdjustChainSlot(delta);
    }

    // Encoders: quick nudge position/slot.
    if (data.encoders[0] != 0)
        th->uiAdjustChainPos(data.encoders[0]);
    if (data.encoders[1] != 0)
        th->uiAdjustChainSlot(data.encoders[1]);

    // Encoder L push: insert chain step after cursor.
    if ((data.controls & kNT_encoderButtonL) && !(data.lastButtons & kNT_encoderButtonL)) {
        th->uiInsertChainStep();
        int len = th->uiChainLength();
        NT_setParameterFromUi((uint32_t)algIdx, (uint32_t)(engBase + ThorpEngine::kThorpChainLen) + paramOffset, (int16_t)len);
    }

    // Button 1/4: chain length -/+.
    if ((data.controls & kNT_button1) && !(data.lastButtons & kNT_button1)) {
        int len = th->uiChainLength() - 1;
        if (len < 1) len = 1;
        NT_setParameterFromUi((uint32_t)algIdx, (uint32_t)(engBase + ThorpEngine::kThorpChainLen) + paramOffset, (int16_t)len);
        th->uiSetChainLength(len);
    }
    if ((data.controls & kNT_button4) && !(data.lastButtons & kNT_button4)) {
        int len = th->uiChainLength() + 1;
        if (len > 16) len = 16;
        NT_setParameterFromUi((uint32_t)algIdx, (uint32_t)(engBase + ThorpEngine::kThorpChainLen) + paramOffset, (int16_t)len);
        th->uiSetChainLength(len);
    }

    // Button 2: toggle Play Mode Jam/Song.
    if ((data.controls & kNT_button2) && !(data.lastButtons & kNT_button2)) {
        int playMode = alg->v[engBase + ThorpEngine::kThorpPlayMode];
        playMode = (playMode == ThorpEngine::kPlaySong) ? ThorpEngine::kPlayJam : ThorpEngine::kPlaySong;
        NT_setParameterFromUi((uint32_t)algIdx, (uint32_t)(engBase + ThorpEngine::kThorpPlayMode) + paramOffset, (int16_t)playMode);
    }

    // Button 3: delete chain step at cursor, or cycle sequence mode if len=1.
    if ((data.controls & kNT_button3) && !(data.lastButtons & kNT_button3)) {
        if (th->uiChainLength() > 1) {
            th->uiDeleteChainStep();
            int len = th->uiChainLength();
            NT_setParameterFromUi((uint32_t)algIdx, (uint32_t)(engBase + ThorpEngine::kThorpChainLen) + paramOffset, (int16_t)len);
        } else {
            int seqMode = alg->v[engBase + ThorpEngine::kThorpSequenceMode];
            seqMode = (seqMode + 1) % ThorpEngine::kNumSequenceModes;
            NT_setParameterFromUi((uint32_t)algIdx, (uint32_t)(engBase + ThorpEngine::kThorpSequenceMode) + paramOffset, (int16_t)seqMode);
        }
    }
}
