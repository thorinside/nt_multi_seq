# Seq Soma

A mutating step sequencer. Soma generates a random note and gate pattern on startup, then continuously mutates it based on probability parameters. The result is a sequence that evolves over time while maintaining a sense of continuity.

## How It Works

Soma initializes a pattern of scale degrees and gate states. On each clock tick, the current step has a chance of its note being replaced by a new weighted random pick, and its gate state being toggled. At low mutation rates, patterns drift slowly. At high rates, every step is essentially random.

Note probability weighting is controlled by the global **Note Weight** parameter (on the Global page), which determines how scale degrees are weighted when picking new notes.

## Parameters

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| Oct Spread | 0-100% | 50% | Probability and range of octave displacement. Higher values mean more octave jumps and wider range (up to 3 octaves) |
| Note Mutate | 0-100% | 70% | Per-step probability that the note is replaced with a new random scale degree |
| Gate Mutate | 0-100% | 80% | Per-step probability that the gate state is toggled |
| Length | 1-64 | 8 | Sequence length in steps |
| Velocity | 0-100% | 100% | Output velocity level (0-5V) |

## Characteristic Note Weighting

The global **Note Weight** parameter controls how Soma picks new notes when mutation occurs. It affects which scale degrees are more likely to be chosen:

| Mode | Description |
|------|-------------|
| **Major** (default) | Compares each scale degree against the major scale (C D E F G A B). Degrees that don't match a major scale tone get 3x probability, creating melodies that favor "characteristic" non-diatonic notes. Only works with octave-based scales; non-octave scales fall back to Equal. |
| **Harmonic** | Uses physics-based harmonic consonance. Each degree's frequency ratio is compared against simple fractions (p/q where p,q in 1-12). Degrees near a simple ratio (within 20 cents, p*q <= 18) are consonant (weight 1); others are characteristic (weight 3). Works with any scale period. |
| **Equal** | All degrees have equal probability. Pure uniform random. |

The Note Weight mode is set globally on the Global page and applies to all Soma engines.

The global **Warp Amount** parameter is complementary to Note Weight. While Note Weight affects Soma's internal mutation probability (which notes are more likely to be *chosen*), Warp Amount deterministically shifts *all* engine output toward characteristic notes after quantization. The two effects can stack: Soma's probability weighting favors characteristic notes when mutating, and Warp Amount then pushes the final output further toward them.

## Behavior Notes

- When a `.scl` scale file is loaded, Soma reinitializes its pattern to match the new scale size. It works with any number of notes per octave.
- Note Mutate at 0% freezes the note pattern completely. Gate Mutate at 0% freezes the rhythm.
- The combination of moderate Note Mutate (~50-70%) with low Gate Mutate (~20-30%) gives evolving melodies over a stable rhythm.
- Oct Spread adds random upward octave displacement. The range scales with the parameter: low values stay within 1 octave, high values can jump up to 3 octaves.

## Focus UI Controls

In focus mode, Soma's parameters are mapped to hardware controls:

| Control | Parameter | Range | Notes |
|---------|-----------|-------|-------|
| Pot L | Note Mutate | 0-100% | Continuous pot sweep |
| Pot C | Gate Mutate | 0-100% | Continuous pot sweep |
| Pot R | Oct Spread | 0-100% | Continuous pot sweep |
| Encoder L | Length | 1-64 | Click to increment/decrement |
| Encoder R | Velocity | 0-100% | Click to increment/decrement |

## Focus UI Display

The focus view shows:
- **Line 1**: Oct Spread and Note Mutate percentages
- **Line 2**: Gate Mutate percentage and Length
