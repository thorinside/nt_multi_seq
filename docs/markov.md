# Markov Engine

A Markov chain melodic generator that produces evolving sequences using probability matrices shaped by behavioral style parameters. Rather than hardcoding transition tables, Markov computes note-to-note weights on the fly, so it works with any scale size -- from 5-note pentatonic to 31-tone equal temperament.

## How It Works

Markov generates a fixed-length sequence of scale degrees using weighted random walks. The weights between any two scale degrees are computed from the current style's behavioral parameters (self-boost, step bias, home gravity, fifth gravity), then further shaped by the Emotion parameter.

At the end of each sequence cycle, the engine either mutates the existing sequence (replacing individual steps with probability set by Mutation) or fully regenerates it. Style, Emotion, and Jumpiness changes don't take effect instantly -- they drift toward their target values over multiple cycles, creating smooth transitions between moods.

## Parameters

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| Style | 8 styles | Tonal | Behavioral preset that shapes the transition matrix (see below) |
| Emotion | 0-100% | 50% | Below 50% biases motion downward (darker); above 50% biases upward (brighter). Also affects rhythm pattern selection and velocity range |
| Jumpiness | 0-100% | 30% | Probability of octave shifts per note. Direction is biased by Emotion |
| Oct Range | 1-3 | 2 | Maximum octave displacement |
| Mutation | 0-100% | 20% | Per-step probability that a note is replaced at the end of each cycle |
| Length | 1-64 | 8 | Sequence length in steps |
| Density | 1-100% | 100% | Probability that each active step actually fires. Lower values create rests |
| Velocity | 0-100% | 100% | Master velocity scaler |
| Mutate | Off/On | Off | When On, forces mutation at the next cycle boundary |
| Regenerate | Off/On | Off | When On, forces full regeneration at the next cycle boundary |

## Styles

Each style defines four behavioral parameters that shape the transition probability matrix:

| Style | Character | Description |
|-------|-----------|-------------|
| Tonal | Balanced, musical | Moderate step preference with strong pull toward tonic and fifth |
| Stepwise | Scale runs | Strong preference for adjacent scale degrees, minimal tonal gravity |
| Vamp | Repetitive, hypnotic | Very high self-boost (stays on current note), weak step preference |
| Leaping | Wide intervals | Negative step bias favors larger intervals, avoids stepwise motion |
| Melodic | Singable lines | Moderate self-boost with balanced step and tonal preferences |
| Driving | Rhythmic, insistent | High self-boost with moderate tonal pull |
| Hypnotic | Minimal variation | Very high self-boost, gentle stepwise tendency |
| Chaotic | Unpredictable | Minimal self-boost, weak step bias, almost no tonal gravity |

## Emotion

Emotion does three things:

1. **Melodic direction**: Below 50% biases note transitions downward; above 50% biases upward
2. **Rhythm selection**: Low emotion selects sparser rhythm patterns; high emotion selects denser ones
3. **Octave jump direction**: Combined with Jumpiness, low emotion favors downward jumps, high emotion favors upward

## Parameter Drift

When you change Style, Emotion, Jumpiness, Range, or Density, the engine doesn't snap to the new values immediately. Instead, the "applied" values drift toward the targets over multiple sequence cycles:

- **Style and Range** move one step per cycle
- **Emotion, Jumpiness, and Density** glide by up to 5 units per cycle

This creates smooth, musical transitions between settings rather than jarring changes.

## Rhythm

Markov uses one of 8 hardcoded 16-step rhythm patterns (selected based on Emotion at generation time) to determine which steps are active. The Density parameter further thins out active steps probabilistically.

## Focus UI Controls

When focused on a Markov channel:

| Control | Function |
|---------|----------|
| Pot L button | Force mutate (immediately mutates the sequence) |
| Pot R button | Force regenerate (immediately generates a new sequence and resets to step 1) |

The focus display shows:
- **Line 1**: Style name, Emotion %, Density %
- **Line 2**: Length, Jumpiness %, Oct Range, Mutation %
