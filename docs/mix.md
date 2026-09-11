# Seq Mix

Seq Mix combines the pitch, gate, and velocity outputs of several sequencers sharing the same busses. Pitch is summed or averaged and quantized to a scale, gates are combined with a boolean operation, and velocity is summed, averaged, or scaled. Use it to build a single voice from two or more sequencers.

## How it works

1. Set each sequencer's Gate Out, Pitch Out, and Velocity Out to the same busses (14, 15, and 16 by default) in Add mode.
2. Turn each sequencer's Scale On to Off so they contribute raw V/oct.
3. Place Seq Mix after the sequencers in the preset.

Seq Mix reads each bus, combines it, and writes the result back in Replace mode. What follows in the chain sees only one gate, one quantized note, and one velocity.

## Pitch page

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| Pitch In | None, 1 to 28 | 15 | Bus to read. None disables the pitch stage. |
| Pitch Out | None, 1 to 28 | 15 | Bus to write. None disables the pitch stage. |
| Pitch Out mode | Add, Replace | Replace | Replace consumes the summed bus in place. Add stacks the result onto another bus. |
| Mix | Sum, Average | Sum | Sum passes the bus voltage through. Average divides it by Sources. |
| Sources | 1 to 8 | 2 | Number of sequencers feeding the busses. Used by Average, velocity Average, and the AND gate op. |
| Scale On | Off, On | On | Off bypasses quantization. |
| Root Note | C to B | C | Semitone the scale is built from. |
| Scale File | .scl on card | 0 | Scale loaded from the MicroSD card. With no card mounted, output is unquantized. |
| S&H | Off, On | Off | On samples pitch and velocity only on a rising edge of the mixed gate and holds them between edges. Bypassed when Gate In is None. |

## Gate page

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| Gate In | None, 1 to 28 | 14 | Bus carrying the summed gates. None disables the gate stage. |
| Gate Out | None, 1 to 28 | 14 | Bus to write the combined gate. None writes nothing, but the gate is still evaluated for S&H. |
| Gate Out mode | Add, Replace | Replace | Replace consumes the summed bus in place. |
| Gate Op | OR, AND, XOR | OR | OR is high when any gate is high. AND needs all Sources gates high. XOR is high when an odd number of gates are high. |

Each sequencer gate is 5 V, so the bus carries 5 V per high gate. Seq Mix rounds the bus voltage to the nearest 5 V step to count how many gates are high, then outputs a single 5 V gate or 0 V.

## Velocity page

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| Velocity In | None, 1 to 28 | 16 | Bus carrying the summed velocities. None disables the velocity stage. |
| Velocity Out | None, 1 to 28 | 16 | Bus to write. None disables the velocity stage. |
| Velocity Out mode | Add, Replace | Replace | Replace consumes the summed bus in place. |
| Vel Mix | Sum, Average, Scale | Sum | Sum passes the bus through. Average divides by Sources. Scale multiplies by Vel Scale. |
| Vel Scale | 0 to 200 % | 100 % | Gain applied in Scale mode only. |

Sequencers emit velocity as 0 to 5 V. The mixed velocity is clamped to 0 to 10 V.

## Behavior notes

- Pitch is clamped to plus or minus 10 V before quantization.
- Quantization recomputes only when the input voltage or a setting changes, so it costs nothing while a step is held.
- With Sum and two sequencers each holding 1 V, the bus carries 2 V and the output is 2 V snapped to the scale. With Average and Sources set to 2, the output is 1 V snapped to the scale.
- With S&H off, pitch and velocity track their busses every sample. With S&H on, they update only when the combined gate goes from low to high, so a note change on one sequencer does not move the pitch until the next gate.
- The three stages are independent. Set any In or Out bus to None to skip that stage.
- Presets saved with version 1.2.0 load with the gate and velocity stages on their default busses 14 and 16. Set Gate In and Velocity In to None to restore the pitch-only behavior.
