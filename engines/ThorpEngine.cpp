#include "ThorpEngine.h"
#include "../scale/ScaleQuantizer.h"

// --- Static pattern data (shared across all instances, no per-instance cost) ---

// 23 rhythmic patterns, 8 steps each.
// Values 1-8 map to scale degrees 0-7. Value 0 = rest.
static const int8_t kPatterns[ThorpEngine::kNumPatterns][ThorpEngine::kPatternLen] = {
    { 1, 2, 3, 4, 5, 6, 7, 8 },  //  0: Ascending
    { 8, 7, 6, 5, 4, 3, 2, 1 },  //  1: Descending
    { 1, 2, 3, 4, 5, 6, 7, 6 },  //  2: UpDown
    { 8, 7, 6, 5, 4, 3, 2, 3 },  //  3: DownUp
    { 1, 8, 2, 7, 3, 6, 4, 5 },  //  4: Alternate
    { 1, 3, 5, 1, 3, 5, 1, 3 },  //  5: TriadOnRoot
    { 3, 5, 1, 3, 5, 1, 3, 5 },  //  6: TriadOnThird
    { 1, 3, 5, 7, 5, 3, 1, 3 },  //  7: SeventhArp
    { 1, 5, 2, 6, 3, 7, 4, 8 },  //  8: FifthLeap
    { 1, 2, 3, 5, 6, 1, 2, 3 },  //  9: PentatonicAsc
    { 6, 5, 3, 2, 1, 6, 5, 3 },  // 10: PentatonicDesc
    { 1, 2, 3, 5, 6, 5, 3, 2 },  // 11: MajorBlues
    { 1, 3, 4, 5, 7, 5, 4, 3 },  // 12: MinorBlues
    { 1, 5, 2, 6, 3, 7, 4, 1 },  // 13: CircleOfFifths
    { 1, 4, 7, 3, 6, 2, 5, 8 },  // 14: Arpeggio4ths
    { 1, 3, 5, 7, 2, 4, 6, 8 },  // 15: Arpeggio3rds
    { 2, 5, 1, 7, 3, 8, 4, 6 },  // 16: Random1
    { 3, 6, 2, 8, 4, 1, 5, 7 },  // 17: Random2
    { 5, 2, 8, 4, 7, 3, 1, 6 },  // 18: Random3
    { 1, 0, 2, 0, 3, 0, 4, 0 },  // 19: Syncopated
    { 1, 2, 0, 0, 5, 6, 0, 0 },  // 20: Burst2
    { 1, 2, 3, 0, 0, 0, 6, 7 },  // 21: Burst3
    { 1, 2, 3, 4, 0, 4, 3, 2 },  // 22: ClusterStep
};

static const char* const kPatternStrings[] = {
    "Ascending", "Descending", "UpDown", "DownUp", "Alternate",
    "TriadRoot", "TriadThird", "7thArp", "FifthLeap",
    "PentaAsc", "PentaDesc", "MajBlues", "MinBlues",
    "Circle5th", "Arp4ths", "Arp3rds",
    "Random1", "Random2", "Random3",
    "Syncopated", "Burst2", "Burst3", "ClusterStep",
    nullptr
};

// 15 velocity patterns, 8 steps each.
// Values 0-100, where 50 = unity/center in Lua's bipolar system.
// We map these to 0-5V output range: value/100 * 5.0
static const uint8_t kVelPatterns[ThorpEngine::kNumVelPatterns][ThorpEngine::kPatternLen] = {
    { 50, 50, 50, 50, 50, 50, 50, 50 },  //  0: Constant
    { 75, 35, 40, 35, 75, 35, 40, 35 },  //  1: Accent
    { 35, 75, 45, 75, 35, 75, 45, 75 },  //  2: OffBeat
    { 25, 30, 35, 40, 42, 45, 47, 50 },  //  3: Crescendo
    { 50, 47, 45, 42, 40, 35, 30, 25 },  //  4: Diminuendo
    { 65, 30, 65, 30, 65, 30, 65, 30 },  //  5: Strong/Weak
    { 60, 40, 55, 37, 60, 40, 55, 37 },  //  6: SwingFeel
    { 42, 60, 32, 55, 37, 57, 40, 62 },  //  7: RandomWalk
    { 75, 25, 75, 25, 75, 25, 75, 25 },  //  8: Pulse
    { 40, 42, 45, 47, 50, 47, 45, 42 },  //  9: Breathe
    { 80, 20, 80, 20, 80, 20, 80, 20 },  // 10: Hard/Soft
    { 30, 32, 35, 37, 40, 42, 45, 47 },  // 11: BuildUp
    { 47, 45, 42, 40, 37, 35, 32, 30 },  // 12: BreakDown
    { 70, 35, 60, 40, 70, 35, 60, 40 },  // 13: Pump
    { 47, 42, 52, 40, 47, 42, 52, 40 },  // 14: Subtle
};

static const char* const kVelPatternStrings[] = {
    "Constant", "Accent", "OffBeat", "Crescendo", "Diminuendo",
    "Str/Weak", "Swing", "RndWalk", "Pulse", "Breathe",
    "Hard/Soft", "BuildUp", "BrkDown", "Pump", "Subtle",
    nullptr
};

static const char* const kSequenceModeStrings[] = {
    "Seq", "PingPong", "RndWalk", "Random", nullptr
};

static const char* const kReverseStrings[] = {
    "Off", "On", nullptr
};

static const char* const kPlayModeStrings[] = {
    "Jam", "Song", nullptr
};

static inline int clampInt(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// --- Implementation ---

ThorpEngine::ThorpEngine()
    : pattern_(0)
    , velPattern_(0)
    , length_(8)
    , offset_(0)
    , reverse_(0)
    , arpSlot_(1)
    , gateProb_(100)
    , octJump_(0)
    , octRange_(1)
    , sequenceMode_(kModeSeq)
    , globalVelocity_(100)
    , gateLen_(50)
    , playMode_(kPlayJam)
    , chainLen_(1)
    , currentStep_(-1)
    , pingDir_(1)
    , velocityStepCount_(0)
    , stepCount_(0)
    , localStep_(0)
    , chainPos_(0)
    , editChainPos_(0)
    , gateActive_(false)
    , lastMidiNote_(60)
    , lastPitch_(0.0f)
    , lastVelocity_(2.5f)
    , rngState_(12345)
{
    initSlots();
    loadSelectedSlotParams();
}

uint32_t ThorpEngine::rng()
{
    rngState_ ^= rngState_ << 13;
    rngState_ ^= rngState_ >> 17;
    rngState_ ^= rngState_ << 5;
    return rngState_;
}

int ThorpEngine::rngRange(int min, int max)
{
    if (min >= max) return min;
    return min + (int)(rng() % (uint32_t)(max - min + 1));
}

void ThorpEngine::addUniqueNote(uint8_t* arr, int& count, uint8_t note)
{
    for (int i = 0; i < count; ++i) {
        if (arr[i] == note)
            return;
    }
    if (count >= kMaxHeldNotes)
        return;
    arr[count++] = note;
    for (int i = count - 1; i > 0; --i) {
        if (arr[i] < arr[i - 1]) {
            uint8_t t = arr[i];
            arr[i] = arr[i - 1];
            arr[i - 1] = t;
        } else {
            break;
        }
    }
}

void ThorpEngine::removeNote(uint8_t* arr, int& count, uint8_t note)
{
    for (int i = 0; i < count; ++i) {
        if (arr[i] == note) {
            for (int j = i; j < count - 1; ++j)
                arr[j] = arr[j + 1];
            count--;
            return;
        }
    }
}

void ThorpEngine::initSlots()
{
    for (int i = 0; i < kNumSlots; ++i) {
        Slot& s = slots_[i];
        s.pattern = 0;
        s.velPattern = 0;
        s.length = 8;
        s.offset = 0;
        s.reverse = 0;
        s.numNotes = 0;
        for (int n = 0; n < kMaxHeldNotes; ++n)
            s.notes[n] = 0;
        for (int st = 0; st < kPatternLen; ++st)
            s.velocities[st] = 50;

        chain_[i] = (uint8_t)i;
    }
}

void ThorpEngine::refreshSlotVelocity(int slotIndex)
{
    slotIndex = clampInt(slotIndex, 0, kNumSlots - 1);
    Slot& s = slots_[slotIndex];
    int vp = clampInt((int)s.velPattern, 0, kNumVelPatterns - 1);
    for (int i = 0; i < kPatternLen; ++i)
        s.velocities[i] = kVelPatterns[vp][i];
}

void ThorpEngine::loadSelectedSlotParams()
{
    int idx = clampInt(arpSlot_ - 1, 0, kNumSlots - 1);
    const Slot& s = slots_[idx];
    pattern_ = clampInt((int)s.pattern, 0, kNumPatterns - 1);
    velPattern_ = clampInt((int)s.velPattern, 0, kNumVelPatterns - 1);
    length_ = clampInt((int)s.length, 1, 32);
    offset_ = clampInt((int)s.offset, 0, kPatternLen - 1);
    reverse_ = s.reverse ? 1 : 0;
}

void ThorpEngine::saveSelectedSlotParams()
{
    int idx = clampInt(arpSlot_ - 1, 0, kNumSlots - 1);
    Slot& s = slots_[idx];
    s.pattern = (uint8_t)clampInt(pattern_, 0, kNumPatterns - 1);
    s.velPattern = (uint8_t)clampInt(velPattern_, 0, kNumVelPatterns - 1);
    s.length = (uint8_t)clampInt(length_, 1, 32);
    s.offset = (uint8_t)clampInt(offset_, 0, kPatternLen - 1);
    s.reverse = reverse_ ? 1 : 0;
    refreshSlotVelocity(idx);
}

void ThorpEngine::copyLatchedToSelectedSlot()
{
    int idx = clampInt(arpSlot_ - 1, 0, kNumSlots - 1);
    Slot& s = slots_[idx];
    s.numNotes = (uint8_t)clampInt(numLatchedNotes_, 0, kMaxHeldNotes);
    for (int i = 0; i < s.numNotes; ++i)
        s.notes[i] = latchedNotes_[i];
}

void ThorpEngine::advanceSongChain()
{
    int len = clampInt(chainLen_, 1, kNumSlots);
    if (len <= 1) {
        chainPos_ = 0;
        return;
    }

    switch (sequenceMode_) {
    case kModeSeq:
        chainPos_ = (chainPos_ + 1) % len;
        break;

    case kModePingPong:
        if ((chainPos_ == (len - 1) && pingDir_ > 0) || (chainPos_ == 0 && pingDir_ < 0))
            pingDir_ = -pingDir_;
        chainPos_ += pingDir_;
        chainPos_ = clampInt(chainPos_, 0, len - 1);
        break;

    case kModeRndWalk:
        chainPos_ += rngRange(-1, 1);
        chainPos_ = clampInt(chainPos_, 0, len - 1);
        break;

    case kModeRandom:
        chainPos_ = rngRange(0, len - 1);
        break;

    default:
        chainPos_ = (chainPos_ + 1) % len;
        break;
    }
}

void ThorpEngine::init(uint32_t sampleRate)
{
    rngState_ = sampleRate ^ 0xBEEFCAFE;

    currentStep_ = -1;
    pingDir_ = 1;
    velocityStepCount_ = 0;
    stepCount_ = 0;
    localStep_ = 0;
    chainPos_ = 0;
    editChainPos_ = 0;
    gateActive_ = false;

    numLatchedNotes_ = 0;
    numActiveNotes_ = 0;

    lastPitch_ = 0.0f;
    lastVelocity_ = 2.5f;
    lastMidiNote_ = 60;

    initSlots();
    loadSelectedSlotParams();
}

void ThorpEngine::reset()
{
    currentStep_ = -1;
    pingDir_ = 1;
    velocityStepCount_ = 0;
    stepCount_ = 0;
    localStep_ = 0;
    chainPos_ = 0;
    editChainPos_ = 0;
    gateActive_ = false;
}

void ThorpEngine::noteOn(uint8_t midiNote, uint8_t velocity)
{
    (void)velocity;

    // Match Lua phrase behavior: first new press clears latched set.
    if (numActiveNotes_ == 0)
        numLatchedNotes_ = 0;

    addUniqueNote(activeNotes_, numActiveNotes_, midiNote);
    addUniqueNote(latchedNotes_, numLatchedNotes_, midiNote);
    copyLatchedToSelectedSlot();

    // Note input forces Jam mode for live performance.
    playMode_ = kPlayJam;
}

void ThorpEngine::noteOff(uint8_t midiNote)
{
    removeNote(activeNotes_, numActiveNotes_, midiNote);
}

void ThorpEngine::noteCvGate(float vOct, bool rising)
{
    int midiNote = (int)(vOct * 12.0f + 60.0f);
    midiNote = clampInt(midiNote, 0, 127);

    if (rising) {
        addUniqueNote(activeNotes_, numActiveNotes_, (uint8_t)midiNote);
        addUniqueNote(latchedNotes_, numLatchedNotes_, (uint8_t)midiNote);
        copyLatchedToSelectedSlot();
        playMode_ = kPlayJam;
    } else {
        removeNote(activeNotes_, numActiveNotes_, (uint8_t)midiNote);
    }
}

EngineOutput ThorpEngine::clockTick(const ScaleQuantizer* scale)
{
    EngineOutput out = { lastPitch_, 0.0f, lastVelocity_, (uint8_t)lastMidiNote_ };

    // For non-legato gate lengths, previous note is always released before
    // the next clock tick.
    if (gateLen_ < 100)
        gateActive_ = false;

    const int velStepIndex = velocityStepCount_ % kPatternLen;
    velocityStepCount_++;

    const Slot* activeSlot = nullptr;
    int rawNote = -1;

    if (playMode_ == kPlaySong) {
        int chainLen = clampInt(chainLen_, 1, kNumSlots);
        int chainIndex = chainPos_ % chainLen;
        int slotIndex = clampInt((int)chain_[chainIndex], 0, kNumSlots - 1);
        activeSlot = &slots_[slotIndex];

        int len = clampInt((int)activeSlot->length, 1, 32);
        int step = localStep_ % len;
        currentStep_ = step;

        localStep_++;
        if (localStep_ >= len) {
            localStep_ = 0;
            advanceSongChain();
        }

        int pattern = clampInt((int)activeSlot->pattern, 0, kNumPatterns - 1);
        int offset = clampInt((int)activeSlot->offset, 0, kPatternLen - 1);
        int reverse = activeSlot->reverse ? 1 : 0;

        int patIndex = (step + offset) % kPatternLen;
        if (reverse)
            patIndex = (kPatternLen - 1) - patIndex;

        int stepValue = kPatterns[pattern][patIndex];
        if (stepValue != 0 && ((int)(rng() % 100) < gateProb_)) {
            const uint8_t* notes = activeSlot->notes;
            int noteCount = activeSlot->numNotes;
            if (noteCount <= 0 && numLatchedNotes_ > 0) {
                notes = latchedNotes_;
                noteCount = numLatchedNotes_;
            }
            if (noteCount > 0)
                rawNote = notes[(stepValue - 1) % noteCount];
        }
    } else {
        activeSlot = &slots_[clampInt(arpSlot_ - 1, 0, kNumSlots - 1)];

        int len = clampInt(length_, 1, 32);
        int step = stepCount_ % len;
        currentStep_ = step;
        stepCount_++;

        int pattern = clampInt(pattern_, 0, kNumPatterns - 1);
        int offset = clampInt(offset_, 0, kPatternLen - 1);
        int reverse = reverse_ ? 1 : 0;

        int patIndex = (step + offset) % kPatternLen;
        if (reverse)
            patIndex = (kPatternLen - 1) - patIndex;

        int stepValue = kPatterns[pattern][patIndex];
        if (stepValue != 0 && numLatchedNotes_ > 0 && ((int)(rng() % 100) < gateProb_))
            rawNote = latchedNotes_[(stepValue - 1) % numLatchedNotes_];
    }

    if (rawNote < 0)
        return out;

    // Global octave jump performance behavior.
    if (octJump_ > 0 && (int)(rng() % 100) < octJump_) {
        int jump = rngRange(-octRange_, octRange_);
        rawNote += jump * 12;
        rawNote = clampInt(rawNote, 0, 127);
    }

    int stepVelocity = 50;
    if (playMode_ == kPlaySong && activeSlot) {
        stepVelocity = activeSlot->velocities[velStepIndex];
    } else {
        stepVelocity = kVelPatterns[clampInt(velPattern_, 0, kNumVelPatterns - 1)][velStepIndex];
    }

    stepVelocity = (stepVelocity * globalVelocity_) / 100;
    stepVelocity = clampInt(stepVelocity, 0, 100);
    float velocityCV = (float)stepVelocity * 0.05f;

    int quantizedMidi = rawNote;
    float pitch = (float)(rawNote - 60) / 12.0f;

    if (scale && scale->isLoaded()) {
        pitch = scale->midiNoteToVOct((uint8_t)rawNote, 0);
        quantizedMidi = (int)(pitch * 12.0f + 60.0f + 0.5f);
        quantizedMidi = clampInt(quantizedMidi, 0, 127);
    }

    bool isNewNote = (!gateActive_) || (quantizedMidi != lastMidiNote_);
    if (!isNewNote)
        return out;

    out.pitch = pitch;
    out.velocity = velocityCV;
    out.gate = 5.0f;
    out.midiNote = (uint8_t)quantizedMidi;

    lastPitch_ = pitch;
    lastVelocity_ = velocityCV;
    lastMidiNote_ = quantizedMidi;
    gateActive_ = true;

    return out;
}

void ThorpEngine::parameterChanged(int localIndex, int16_t value)
{
    switch (localIndex) {
    case kThorpPattern:
        pattern_ = clampInt(value, 0, kNumPatterns - 1);
        saveSelectedSlotParams();
        break;

    case kThorpVelPattern:
        velPattern_ = clampInt(value, 0, kNumVelPatterns - 1);
        saveSelectedSlotParams();
        break;

    case kThorpLength:
        length_ = clampInt(value, 1, 32);
        saveSelectedSlotParams();
        break;

    case kThorpOffset:
        offset_ = clampInt(value, 0, kPatternLen - 1);
        saveSelectedSlotParams();
        break;

    case kThorpReverse:
        reverse_ = value ? 1 : 0;
        saveSelectedSlotParams();
        break;

    case kThorpArpSlot:
        arpSlot_ = clampInt(value, 1, kNumSlots);
        loadSelectedSlotParams();
        break;

    case kThorpGateProb:
        gateProb_ = clampInt(value, 1, 100);
        break;

    case kThorpOctJump:
        octJump_ = clampInt(value, 0, 100);
        break;

    case kThorpOctRange:
        octRange_ = clampInt(value, 1, 3);
        break;

    case kThorpSequenceMode:
        sequenceMode_ = clampInt(value, 0, kNumSequenceModes - 1);
        break;

    case kThorpGlobalVelocity:
        globalVelocity_ = clampInt(value, 0, 100);
        break;

    case kThorpGateLen:
        gateLen_ = clampInt(value, 1, 100);
        break;

    case kThorpPlayMode:
        playMode_ = clampInt(value, 0, kNumPlayModes - 1);
        if (playMode_ == kPlaySong) {
            chainPos_ = 0;
            localStep_ = 0;
            pingDir_ = 1;
        } else {
            numLatchedNotes_ = 0;
        }
        break;

    case kThorpChainLen:
        chainLen_ = clampInt(value, 1, kNumSlots);
        if (chainPos_ >= chainLen_)
            chainPos_ = 0;
        if (editChainPos_ >= chainLen_)
            editChainPos_ = chainLen_ - 1;
        break;

    default:
        break;
    }
}

void ThorpEngine::uiSetChainLength(int len)
{
    chainLen_ = clampInt(len, 1, kNumSlots);
    if (chainPos_ >= chainLen_)
        chainPos_ = chainLen_ - 1;
    if (editChainPos_ >= chainLen_)
        editChainPos_ = chainLen_ - 1;
}

void ThorpEngine::uiSetChainPos(int pos)
{
    int len = clampInt(chainLen_, 1, kNumSlots);
    editChainPos_ = clampInt(pos, 0, len - 1);
}

int ThorpEngine::uiChainLength() const
{
    return clampInt(chainLen_, 1, kNumSlots);
}

int ThorpEngine::uiChainPos() const
{
    int len = clampInt(chainLen_, 1, kNumSlots);
    return clampInt(editChainPos_, 0, len - 1);
}

int ThorpEngine::uiChainSlot() const
{
    int pos = uiChainPos();
    return clampInt((int)chain_[pos] + 1, 1, kNumSlots);
}

void ThorpEngine::uiAdjustChainPos(int delta)
{
    if (delta == 0) return;
    int len = clampInt(chainLen_, 1, kNumSlots);
    int pos = uiChainPos() + delta;
    while (pos < 0) pos += len;
    while (pos >= len) pos -= len;
    editChainPos_ = pos;
}

void ThorpEngine::uiAdjustChainSlot(int delta)
{
    if (delta == 0) return;
    int pos = uiChainPos();
    int slot = (int)chain_[pos] + delta;
    while (slot < 0) slot += kNumSlots;
    while (slot >= kNumSlots) slot -= kNumSlots;
    chain_[pos] = (uint8_t)slot;
}

void ThorpEngine::uiInsertChainStep()
{
    int len = clampInt(chainLen_, 1, kNumSlots);
    if (len >= kNumSlots)
        return;

    int pos = uiChainPos();
    for (int i = len; i > pos + 1; --i)
        chain_[i] = chain_[i - 1];
    chain_[pos + 1] = chain_[pos];
    chainLen_ = len + 1;
    editChainPos_ = pos + 1;
}

void ThorpEngine::uiDeleteChainStep()
{
    int len = clampInt(chainLen_, 1, kNumSlots);
    if (len <= 1)
        return;

    int pos = uiChainPos();
    for (int i = pos; i < len - 1; ++i)
        chain_[i] = chain_[i + 1];
    chainLen_ = len - 1;
    if (editChainPos_ >= chainLen_)
        editChainPos_ = chainLen_ - 1;
    if (chainPos_ >= chainLen_)
        chainPos_ = chainLen_ - 1;
}

int ThorpEngine::getParameterDefs(_NT_parameter* defs) const
{
    defs[kThorpPattern]        = { .name = "Pattern",    .min = 0, .max = kNumPatterns - 1,    .def = 0,        .unit = kNT_unitEnum,    .scaling = kNT_scalingNone, .enumStrings = kPatternStrings };
    defs[kThorpVelPattern]     = { .name = "Vel Pat",    .min = 0, .max = kNumVelPatterns - 1, .def = 0,        .unit = kNT_unitEnum,    .scaling = kNT_scalingNone, .enumStrings = kVelPatternStrings };
    defs[kThorpLength]         = { .name = "Length",     .min = 1, .max = 32,                  .def = 8,        .unit = kNT_unitNone,    .scaling = kNT_scalingNone, .enumStrings = nullptr };
    defs[kThorpOffset]         = { .name = "Offset",     .min = 0, .max = 7,                   .def = 0,        .unit = kNT_unitNone,    .scaling = kNT_scalingNone, .enumStrings = nullptr };
    defs[kThorpReverse]        = { .name = "Reverse",    .min = 0, .max = 1,                   .def = 0,        .unit = kNT_unitEnum,    .scaling = kNT_scalingNone, .enumStrings = kReverseStrings };
    defs[kThorpArpSlot]        = { .name = "Arp Slot",   .min = 1, .max = 16,                  .def = 1,        .unit = kNT_unitNone,    .scaling = kNT_scalingNone, .enumStrings = nullptr };
    defs[kThorpGateProb]       = { .name = "Gate Prob",  .min = 1, .max = 100,                 .def = 100,      .unit = kNT_unitPercent, .scaling = kNT_scalingNone, .enumStrings = nullptr };
    defs[kThorpOctJump]        = { .name = "Oct Jump",   .min = 0, .max = 100,                 .def = 0,        .unit = kNT_unitPercent, .scaling = kNT_scalingNone, .enumStrings = nullptr };
    defs[kThorpOctRange]       = { .name = "Oct Range",  .min = 1, .max = 3,                   .def = 1,        .unit = kNT_unitNone,    .scaling = kNT_scalingNone, .enumStrings = nullptr };
    defs[kThorpSequenceMode]   = { .name = "Seq Mode",   .min = 0, .max = kNumSequenceModes - 1, .def = kModeSeq, .unit = kNT_unitEnum,  .scaling = kNT_scalingNone, .enumStrings = kSequenceModeStrings };
    defs[kThorpGlobalVelocity] = { .name = "Global Vel", .min = 0, .max = 100,                 .def = 100,      .unit = kNT_unitPercent, .scaling = kNT_scalingNone, .enumStrings = nullptr };
    defs[kThorpGateLen]        = { .name = "Gate Len",   .min = 1, .max = 100,                 .def = 50,       .unit = kNT_unitPercent, .scaling = kNT_scalingNone, .enumStrings = nullptr };
    defs[kThorpPlayMode]       = { .name = "Play Mode",  .min = 0, .max = kNumPlayModes - 1,   .def = kPlayJam, .unit = kNT_unitEnum,    .scaling = kNT_scalingNone, .enumStrings = kPlayModeStrings };
    defs[kThorpChainLen]       = { .name = "Chain Len",  .min = 1, .max = 16,                  .def = 1,        .unit = kNT_unitNone,    .scaling = kNT_scalingNone, .enumStrings = nullptr };
    return kNumThorpParams;
}

int ThorpEngine::currentStep() const
{
    return currentStep_;
}

int ThorpEngine::sequenceLength() const
{
    if (playMode_ == kPlaySong) {
        int chainLen = clampInt(chainLen_, 1, kNumSlots);
        int slotIndex = clampInt((int)chain_[chainPos_ % chainLen], 0, kNumSlots - 1);
        return clampInt((int)slots_[slotIndex].length, 1, 32);
    }
    return clampInt(length_, 1, 32);
}

int ThorpEngine::getStatusText(char* buf, int maxLen) const
{
    if (maxLen <= 0) return 0;

    int len = 0;
    const char* mode = (playMode_ == kPlaySong) ? "Song" : "Jam";
    while (*mode && len < maxLen - 1) buf[len++] = *mode++;
    if (len < maxLen - 2) buf[len++] = ' ';
    if (len < maxLen - 2) buf[len++] = 'S';
    if (len < maxLen - 2) len += NT_intToString(buf + len, arpSlot_);
    buf[len] = 0;
    return len;
}

int ThorpEngine::getPageDefs(_NT_parameterPage* page, uint8_t* indices, int baseParamIndex) const
{
    for (int i = 0; i < kNumThorpParams; ++i)
        indices[i] = (uint8_t)(baseParamIndex + i);
    page->name = "Thorp";
    page->numParams = kNumThorpParams;
    page->group = 0;
    page->unused[0] = 0;
    page->unused[1] = 0;
    page->params = indices;
    return kNumThorpParams;
}
