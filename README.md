# nt_multi_seq

A multi-channel sequencer plugin for the [Expert Sleepers disting NT](https://expert-sleepers.co.uk/distingNT.html) Eurorack module.

Each channel runs an independent sequencer engine, selected at instantiation time via the module's specification system. Channels share a global scale/tuning system using `.scl` microtuning files.

## Engines

| Spec Value | Engine | Description |
|:---:|--------|-------------|
| 0 | None | Channel skipped |
| 1 | Thorp | Pattern arpeggiator with 23 note patterns, 15 velocity patterns, chain sequencing, and song/jam modes |
| 2 | Soma | Mutating step sequencer with note and gate mutation probabilities |
| 3 | AE Seq | Analog-style CV/gate sequencer with independent CV and gate sequence selection, bit depth control |
| 4 | Markov | Markov chain melodic generator with 8 behavioral styles and semantic matrix generation |

## Architecture

- **Spec-loadout model**: Up to 8 specification slots, each selecting an engine type. Only active (non-None) slots consume resources.
- **Dense channel array**: Active specs are packed into a contiguous channel array. Each channel tracks its original spec slot for correct UI labeling.
- **Dynamic page naming**: Pages are named after the engine type (e.g. "Soma Routing", "Soma"). When multiple channels use the same engine, names are disambiguated ("Soma 1", "Soma 2").
- **No runtime engine switching**: Engine type is fixed at construct time. This simplifies parameter layout and avoids the complexity of hot-swapping engines.
- **Per-engine parameter counts**: Each engine defines only the parameters it needs. No placeholder slots.
- **Shared scale system**: All channels share a global root note, octave, and `.scl` scale file. Per-channel "Scale On" toggles whether quantization is applied.

## Per-Channel Routing

Each channel can output via CV or MIDI:

- **CV mode**: Configurable pitch, gate, and velocity output buses with add/replace modes
- **MIDI mode**: Configurable MIDI channel and destination (breakout, select bus, USB, internal)
- **Per-channel clock and reset inputs** with clock divider
- **Note gate/CV inputs** for engines that accept external note data (e.g. Thorp)

## Building

Requires the [distingNT API](https://expert-sleepers.co.uk/distingNTSDK.html) as a git submodule in `distingNT_API/`.

```bash
# Desktop testing (builds .dylib for nt_emu)
make test

# Hardware (builds ARM .o for disting NT)
make hardware

# Unit tests (standalone, no NT API dependency)
make unit-test

# All targets
make all
```

### Deploy to nt_emu

```bash
cp plugins/nt_seq.dylib ~/nosuch/nt_emu/vcv-plugin/plugins/
```

## Project Structure

```
nt_seq.h                  Core declarations, enums, NtSeq struct
nt_seq.cpp                Plugin entry point and factory
nt_seq_construct.cpp      Spec-driven channel and parameter building
nt_seq_step.cpp           Audio-rate processing, clock/gate/CV/MIDI output
nt_seq_draw.cpp           Custom UI: overview and per-channel focus modes
nt_seq_params.cpp         Parameter change handling and grayouts
spec_helpers.h            Testable pure-logic helpers for spec mapping
engines/
  SequencerEngine.h       Abstract base class for all engines
  ThorpEngine.cpp/h       Pattern arpeggiator
  SomaEngine.cpp/h        Mutating step sequencer
  AeSequencerEngine.cpp/h Analog-style CV/gate sequencer
  SeqMarkovEngine.cpp/h   Markov chain melodic generator
scale/
  ScaleQuantizer.cpp/h    .scl microtuning support
clock/
  ClockProcessor.cpp/h    Clock divider
tests/
  test_spec_logic.cpp     Unit tests for spec-to-engine mapping and page naming
```

## Compile-Time Safety

The build enforces several constraints via `static_assert`:

- Each engine fits within its 2048-byte memory pool slot
- Each engine's alignment requirement is <= 8 bytes (ARM `strd` safety)
- Each engine's parameter count is <= 32 (`kMaxEngineParams`)
- The specifications array size matches `kMaxChannels`

## License

This project is provided as-is for use with the Expert Sleepers disting NT module.
