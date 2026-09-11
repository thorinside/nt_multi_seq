# Seq Mix

Seq Mix combines the gate, pitch, and velocity outputs of several sequencers into one voice. Each sequencer feeds its own channel inputs, so nothing has to be summed on a bus. Pitch is summed or averaged and quantized to a scale, gates are combined with a boolean operation, and velocity is summed, averaged, or scaled.

## Setup

1. When adding Seq Mix, set the **Channels** specification to the number of sequencers you want to combine (1 to 8, default 2). This fixes the number of channel pages.
2. Give each sequencer its own Gate, Pitch, and Velocity Out busses in Replace mode. The channel defaults step by three, so sequencer 1 on 14/15/16, sequencer 2 on 17/18/19, sequencer 3 on 20/21/22, and so on match Seq Mix without any changes.
3. Turn each sequencer's Scale On to Off so they contribute raw V/oct, and let Seq Mix quantize.
4. Place Seq Mix after the sequencers in the preset.

Seq Mix writes its combined gate, pitch, and velocity to busses 14, 15, and 16 in Replace mode by default. What follows in the chain sees one gate, one quantized note, and one velocity.

## Channel pages (Ch 1 to Ch N)

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| Ch N Gate In | None, 1 to 64 | 14, 17, 20, ... | Gate bus for this channel. None leaves the channel out of the gate logic. |
| Ch N Pitch In | None, 1 to 64 | 15, 18, 21, ... | Pitch bus for this channel. None leaves it out of the pitch mix. |
| Ch N Vel In | None, 1 to 64 | 16, 19, 22, ... | Velocity bus for this channel. None leaves it out of the velocity mix. |

Channels whose default bus would exceed 64 start at None.

## Pitch page

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| Pitch Out | None, 1 to 64 | 15 | Bus to write. None disables the pitch stage. |
| Pitch Out mode | Add, Replace | Replace | Replace overwrites the bus. Add stacks the result onto it. |
| Mix | Sum, Average | Sum | Sum adds the connected pitch inputs. Average divides the sum by the number of connected pitch inputs. |
| Scale On | Off, On | On | Off bypasses quantization. |
| Root Note | C to B | C | Semitone the scale is built from. |
| Scale File | .scl on card | 0 | Scale loaded from the MicroSD card. With no card mounted, output is unquantized. |
| S&H | Off, On | Off | On samples pitch and velocity only on a rising edge of the combined gate and holds them between edges. Bypassed when no channel has a gate input. |

## Gate page

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| Gate Out | None, 1 to 64 | 14 | Bus to write the combined gate. None writes nothing, but the gate is still evaluated for S&H. |
| Gate Out mode | Add, Replace | Replace | Replace overwrites the bus. |
| Gate Op | OR, AND, XOR | OR | OR is high when any connected gate is high. AND needs every connected gate high. XOR is high when an odd number are high. |

A channel gate counts as high above 1 V. The combined gate is 5 V or 0 V.

## Velocity page

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| Velocity Out | None, 1 to 64 | 16 | Bus to write. None disables the velocity stage. |
| Velocity Out mode | Add, Replace | Replace | Replace overwrites the bus. |
| Vel Mix | Sum, Average, Scale | Sum | Sum adds the connected velocities. Average divides by the number connected. Scale multiplies the sum by Vel Scale. |
| Vel Scale | 0 to 200 % | 100 % | Gain applied in Scale mode only. |

Sequencers emit velocity as 0 to 5 V. The combined velocity is clamped to 0 to 10 V.

## Behavior notes

- Pitch is clamped to plus or minus 10 V before quantization.
- Quantization recomputes only when the summed pitch or a setting changes, so it costs nothing while a step is held.
- With Sum and two channels each at 1 V, the output is 2 V snapped to the scale. With Average, the output is 1 V snapped to the scale.
- With S&H off, pitch and velocity track their inputs every sample. With S&H on, they update only when the combined gate goes from low to high, so a note change on one sequencer does not move the pitch until the next gate.
- The three stages are independent. Set an Out bus to None to skip that stage, or a channel's In bus to None to drop that channel from the stage.
- Presets saved with version 1.2.0 used a single summed pitch bus with different parameter indices. After upgrading, re-add Seq Mix and set its channels.
