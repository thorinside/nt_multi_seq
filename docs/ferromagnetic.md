# Seq Ferro

A sequencer engine that builds chordal textures over multiple passes of an external tape loop (e.g., Instruo Lubadh). Notes accumulate into chords across layers via sound-on-sound recording.

## Concept

Ferromagnetic tracks loop position and layer number, outputting different notes per layer. When recorded onto a tape loop via sound-on-sound, these layers accumulate into chords. The engine itself does not do any DSP or tape emulation -- it sequences notes that a separate recording device captures.

## Two-Channel Setup

Dedicate 2 channels in Multi Seq:

- **Channel A (Role: Melody)** -- Outputs pitch/gate/velocity to a synth voice. The synth's audio output feeds the tape loop's audio input.
- **Channel B (Role: Loop Trig)** -- Outputs a gate pulse at each loop boundary. Patch this to the tape loop's retrigger/reset input.

Both channels count from the same external clock independently. Set matching Loop Steps values on both to keep them synchronized.

Optional third channel:
- **Channel C (Role: Rec Gate)** -- Outputs gate high while building layers, low when complete (Hold mode). Useful for automating record enable on the tape loop.

## Parameters

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| Role | Melody/Loop Trig/Rec Gate | Melody | Channel function |
| Loop Steps | 2-128 | 16 | Clock ticks per tape loop cycle |
| Max Layers | 1-8 | 4 | Number of chord layers to build |
| Harmony | Structured/Generative | Structured | How upper layers choose notes |
| Voicing | Triads/7ths/Stack 5ths/Octaves/Unison | Triads | Interval pattern (Structured only) |
| Completion | Hold/Decay+Reb/Refresh/New Inv | Hold | Behavior after all layers built |
| Note Density | 0-100% | 80% | Probability of a note per beat (layer 0) |
| Gate Length | 10-100% | 75% | Gate duration as % of clock period |
| Velocity | 0-100% | 100% | Output velocity |
| Oct Spread | 0-3 | 0 | Random octave displacement (Generative only) |
| Refresh Rate | 1-8 | 4 | Loops between refresh/rebuild cycles |

## Harmony Modes

### Structured

Each layer adds a fixed interval above layer 0's notes, defined by the Voicing preset:

- **Triads**: Root, 3rd, 5th, octave (layers 0-3), then reinforcement
- **7ths**: Root, 3rd, 5th, 7th (layers 0-3), then reinforcement
- **Stacked 5ths**: Intervals of a 5th stacked progressively higher
- **Octaves**: Pure octave doublings
- **Unison**: All layers play the same notes (reinforcement only)

### Generative

Each layer independently picks consonant notes from weighted scale degrees, avoiding duplicates already present at that step. 3rds and 5ths relative to the root are boosted. Oct Spread adds random octave displacement to upper layers.

## Completion Modes

After all layers are built:

- **Hold**: Silence. Let the tape loop play back what was recorded.
- **Decay+Rebuild**: Silence for Refresh Rate loops, then clear and rebuild from layer 0.
- **Refresh**: Cyclically replay one earlier layer every Refresh Rate loops to reinforce fading recordings.
- **New Inversion**: Shift the voicing up by one scale degree and immediately rebuild. Creates evolving harmonic progressions.

## Focus UI Controls

When focused:
- **Encoder L**: Loop Steps
- **Encoder R**: Max Layers
- **Pot L**: Note Density
- **Pot C**: Gate Length
- **Pot R**: Velocity

## Example Patch

1. Set Ch 1 to Seq Ferro, Role: Melody, Loop Steps: 16, Max Layers: 4, Voicing: Triads
2. Set Ch 2 to Seq Ferro, Role: Loop Trig, Loop Steps: 16
3. Patch a clock source to both channels' Clock In
4. Patch Ch 1 pitch/gate to a synth voice, synth audio to Lubadh input
5. Patch Ch 2 gate to Lubadh retrigger input
6. Set Lubadh to sound-on-sound mode
7. Start the clock -- layer 0 plays a melody, layer 1 adds thirds, etc.
8. After 4 passes, the tape has a full triadic texture
