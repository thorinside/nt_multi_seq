# Seq Sift

Seq Sift is a pseudo-random CV/gate sequencer that filters its gate sequence through an adjustable threshold, like flour through a sifter. It uses deterministic sequences seeded from random values, giving you 20 independent CV sequences and 20 independent gate sequences that are stable and repeatable but sound complex and unpredictable.

## How It Works

At initialization, each of the 20 CV and 20 gate sequence banks gets a random seed. From that seed, every step value is computed via a deterministic hash function -- so the same seed and step index always produce the same value. This means sequences are stable (no drift) but don't require storing arrays of step data.

The CV output is shaped by bit depth reduction, voltage range, and polarity. The gate output is determined by thresholding the gate sequence's pseudo-random values.

## Parameters

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| CV Seq | 1-20 | 1 | Which CV sequence bank to use |
| Gate Seq | 1-20 | 1 | Which gate sequence bank to use |
| CV Steps | 1-32 | 8 | Length of the CV sequence |
| Min CV | -10.0V to +10.0V | -1.0V | Minimum output voltage |
| Max CV | -10.0V to +10.0V | +1.0V | Maximum output voltage |
| Polarity | Positive/Bipolar/Negative | Bipolar | How the voltage range is applied |
| CV Bit Depth | 2-16 | 16 | Quantization resolution. Lower values create staircase-like sequences |
| Gate Steps | 1-32 | 16 | Length of the gate sequence (independent of CV length) |
| Gate Threshold | 1-100% | 50% | How much of the gate sequence registers as "on". Lower = fewer gates, higher = more gates |
| Velocity | 0-100% | 100% | Output velocity level (0-5V) |

## Polarity Modes

- **Positive**: Output ranges from 0V to Max CV
- **Bipolar**: Output ranges from Min CV to Max CV
- **Negative**: Output ranges from Min CV to 0V

## Bit Depth

Bit depth controls the number of quantization levels in the output voltage. At 16 bits, the output is essentially smooth. At 2 bits, there are only 3 possible voltage levels, creating very stepped, digital-sounding sequences. Values around 3-5 bits give an interesting lo-fi character.

## Independent CV and Gate Lengths

CV Steps and Gate Steps are independent. If CV Steps is 8 and Gate Steps is 16, the pitch pattern repeats every 8 clocks while the rhythm repeats every 16, creating polymetric patterns. The focus UI step bar shows the longer of the two lengths.

## Focus UI Controls

When focused on a Seq Sift channel:

| Control | Function |
|---------|----------|
| Encoder L | Cycle through CV sequence banks (1-20, wraps) |
| Encoder R | Cycle through gate sequence banks (1-20, wraps) |
| Pot L | Gate threshold (1-100%) |
| Pot C | CV bit depth (2-16) |
| Pot R | Velocity (0-100%) |

The focus display shows:
- **Line 1**: CV Seq number, CV steps, Gate Seq number, Gate steps
- **Line 2**: Voltage range, bit depth, gate threshold

The step bar in focus mode shows each step as a segment: brightness indicates the quantized CV level, and dim steps have the gate off.
