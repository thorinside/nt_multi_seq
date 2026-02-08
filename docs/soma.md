# Soma Engine

A mutating step sequencer. Soma generates a random note and gate pattern on startup, then continuously mutates it based on probability parameters. The result is a sequence that evolves over time while maintaining a sense of continuity.

## How It Works

Soma initializes a pattern of scale degrees and gate states. On each clock tick, the current step has a chance of its note being replaced by a new weighted random pick, and its gate state being toggled. At low mutation rates, patterns drift slowly. At high rates, every step is essentially random.

Note probability weighting uses a gentle bell curve favoring middle scale degrees, creating more interesting melodic shapes than pure uniform random.

## Parameters

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| Oct Spread | 0-100% | 50% | Probability and range of octave displacement. Higher values mean more octave jumps and wider range (up to 3 octaves) |
| Note Mutate | 0-100% | 70% | Per-step probability that the note is replaced with a new random scale degree |
| Gate Mutate | 0-100% | 80% | Per-step probability that the gate state is toggled |
| Length | 1-64 | 8 | Sequence length in steps |
| Velocity | 0-100% | 100% | Output velocity level (0-5V) |

## Behavior Notes

- When a `.scl` scale file is loaded, Soma reinitializes its pattern to match the new scale size. It works with any number of notes per octave.
- Note Mutate at 0% freezes the note pattern completely. Gate Mutate at 0% freezes the rhythm.
- The combination of moderate Note Mutate (~50-70%) with low Gate Mutate (~20-30%) gives evolving melodies over a stable rhythm.
- Oct Spread adds random upward octave displacement. The range scales with the parameter: low values stay within 1 octave, high values can jump up to 3 octaves.

## Focus UI Display

The focus view shows:
- **Line 1**: Oct Spread and Note Mutate percentages
- **Line 2**: Gate Mutate percentage and Length

Soma has no custom hardware controls in focus mode beyond the standard parameter page.
