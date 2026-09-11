# Seq Mix

Seq Mix combines the pitch outputs of several sequencers on one bus, then quantizes the result to a scale. Use it to build a single melody line from two or more sequencers.

## How it works

1. Set each sequencer's Pitch Out to the same bus (bus 15 by default) in Add mode.
2. Turn each sequencer's Scale On to Off so they contribute raw V/oct.
3. Place Seq Mix after the sequencers in the preset.

Seq Mix reads the bus, sums or averages it, snaps the result to the nearest note of the loaded scale, and writes it back to the bus in Replace mode. What follows in the chain sees only the quantized note.

## Parameters

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| Pitch In | None, 1 to 28 | 15 | Bus to read. None disables processing. |
| Pitch Out | None, 1 to 28 | 15 | Bus to write. None disables processing. |
| Pitch Out mode | Add, Replace | Replace | Replace consumes the summed bus in place. Add stacks the result onto another bus. |
| Mix | Sum, Average | Sum | Sum passes the bus voltage through. Average divides it by Sources. |
| Sources | 1 to 8 | 2 | Number of sequencers feeding the bus. Only used by Average. |
| Scale On | Off, On | On | Off bypasses quantization. |
| Root Note | C to B | C | Semitone the scale is built from. |
| Scale File | .scl on card | 0 | Scale loaded from the MicroSD card. With no card mounted, output is unquantized. |

## Behavior notes

- Pitch is clamped to plus or minus 10 V before quantization.
- Quantization recomputes only when the input voltage or a setting changes, so it costs nothing while a step is held.
- With Sum and two sequencers each holding 1 V, the bus carries 2 V and the output is 2 V snapped to the scale. With Average and Sources set to 2, the output is 1 V snapped to the scale.
- Seq Mix has no gate output. Use the sequencers' own gates, or a downstream gate combiner.
