# nt_seq

A collection of sequencer algorithms for the [Expert Sleepers disting NT](https://expert-sleepers.co.uk/distingNT.html) Eurorack module.

The single `nt_seq.o` binary exposes six independent algorithms. Each algorithm instance contains exactly one fixed sequencer engine with no specifications or runtime engine selection. Add multiple instances to the disting NT stack to combine engines or run several copies of the same engine.

## Engines

| Algorithm | Description |
|-----------|-------------|
| [Seq Thorp](docs/thorp.md) | Pattern arpeggiator with 23 note patterns, 15 velocity patterns, chain sequencing, and song/jam modes |
| [Seq Soma](docs/soma.md) | Mutating step sequencer with note and gate mutation probabilities |
| [Seq Sift](docs/sift.md) | Pseudo-random CV and threshold-sifted gate sequencer with independent sequence selection and bit-depth control |
| [Seq Markov](docs/markov.md) | Markov-chain melodic generator with eight behavioral styles |
| [Seq Ferro](docs/ferromagnetic.md) | Tape-loop chord builder with layered melody, loop trigger, and record gate roles |
| Seq Quantum | Hierarchical generative sequencer with motif, transformation, and large-form cycles |

## Algorithm Entries

The plugin follows the disting NT multi-factory pattern used by the official SDK and community plugins such as NerdRoger's Directional Sequencer. Loading `nt_seq.o` makes these entries available in the algorithm browser:

| Algorithm | GUID |
|-----------|------|
| `Seq Thorp` | `NsTh` |
| `Seq Soma` | `NsSo` |
| `Seq Sift` | `NsAe` |
| `Seq Markov` | `NsMk` |
| `Seq Ferro` | `NsFe` |
| `Seq Quantum` | `NsQu` |

Every factory has zero specifications. Selecting an algorithm adds one instance immediately.

## Display and Hardware UI

Each algorithm opens directly on its engine display:

- **Line 1**: Engine name and scale info (root note, scale file, note count)
- **Line 2**: Step bar with per-step segments (or Seq Sift's CV-level/gate visualization) plus step counter
- **Line 3**: Current note, gate state (ON/OFF), and velocity voltage
- **Lines 4-5**: Engine-specific parameter readout (varies by engine -- see individual engine docs)

Each engine claims only the hardware controls it uses. See the individual engine docs for details.

## Global Parameters

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| Root Note | C-B | C | Root note for scale quantization |
| Octave | 0-8 | 4 | Base octave |
| Scale File | (file picker) | - | `.scl` microtuning file from SD card |
| Note Weight | Major / Harmonic / Equal | Major | How Soma weights scale degrees when mutating (see [Soma docs](docs/soma.md)) |
| Warp Amount | 0-100% | 0% | Post-quantization bias toward characteristic (non-diatonic) notes. Higher values shift more notes toward spicy scale degrees. Has no effect when Note Weight is Equal. |

## Routing Parameters

Every algorithm has these common routing parameters:

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| Clock In | CV input | - | Clock source |
| Reset In | CV input | - | Reset source |
| Routing | CV / MIDI | CV | Output mode |
| Pitch Out | CV output | - | Pitch CV bus assignment |
| Pitch mode | Add / Replace | - | How pitch CV is written to the bus |
| Gate Out | CV output | - | Gate CV bus assignment |
| Gate mode | Add / Replace | - | How gate CV is written to the bus |
| Velocity Out | CV output | - | Velocity CV bus assignment |
| Velocity mode | Add / Replace | - | How velocity CV is written to the bus |
| MIDI Ch | 1-16 | 1 | MIDI output channel (when routing = MIDI) |
| MIDI Dest | Breakout / Sel.Bus / USB / Internal | Breakout | MIDI output destination |
| Clock Div | 1-16 | 1 | Clock divider |
| Scale On | Off / On | On | Whether scale quantization is applied |
| Note Gate In | CV input | - | Gate input for note capture (Thorp) |
| Note CV In | CV input | - | V/Oct input for note capture (Thorp) |

## Architecture

- **Six factories, one binary**: `pluginEntry()` exposes six independent algorithms while shared routing, scale, clock, MIDI, and UI code remains centralized.
- **No specifications**: Selecting an algorithm constructs it immediately.
- **One engine per instance**: Combine engines by adding multiple algorithms to the disting NT stack.
- **Static pages**: Every instance has exactly three pages: Global, Routing, and its engine page. Page definitions never change at runtime.
- **Exact parameter allocation**: Each instance reserves Global(5) + Routing(15) + only its engine's parameters.
- **Exact engine storage**: SRAM requirements include only the selected engine's concrete size plus alignment, not a maximum-size engine pool.
- **Shared scale system**: Every instance has root note, octave, `.scl` microtuning, note weighting, and optional warp controls.

## Routing

Each algorithm can output via CV or MIDI:

- **CV mode**: Configurable pitch, gate, and velocity output buses with add/replace modes
- **MIDI mode**: Configurable MIDI channel and destination (breakout, select bus, USB, internal)
- **Clock and reset inputs** with clock divider
- **Note gate/CV inputs** for engines that accept external note data (e.g. Thorp)

## Building

Requires the [distingNT API](https://expert-sleepers.co.uk/distingNTSDK.html) as a git submodule in `distingNT_API/`.

```bash
# Desktop testing (builds .dylib for nt_emu)
make test

# Hardware (builds ARM .o for disting NT)
make hardware

# Unit and factory-loading tests
make unit-test

# All targets
make all
```

## Project Structure

```
nt_seq.h                  Core declarations, enums, NtSeq struct
nt_seq.cpp                Plugin entry point and six factories
nt_seq_construct.cpp      Fixed engine, parameter, and page construction
nt_seq_step.cpp           Audio-rate processing, clock/gate/CV/MIDI output
nt_seq_draw.cpp           Engine display and hardware controls
nt_seq_params.cpp         Parameter change handling and grayouts
engines/
  SequencerEngine.h       Abstract base class for all engines
  ThorpEngine.cpp/h       Pattern arpeggiator
  SomaEngine.cpp/h        Mutating step sequencer
  SiftEngine.cpp/h Seq Sift implementation (pseudo-random CV and thresholded gates)
  SeqMarkovEngine.cpp/h   Markov chain melodic generator
  FerromagneticEngine.cpp/h Tape-loop chord sequencer
  QuantumEngine.cpp/h     Hierarchical generative sequencer
scale/
  ScaleQuantizer.cpp/h    .scl microtuning support
clock/
  ClockProcessor.cpp/h    Clock divider
tests/
  nt_stubs.h              NT API stubs for standalone engine testing
  test_scale_quantizer.cpp  Unit tests for scale degree weighting modes
  test_clock_processor.cpp  Unit tests for clock divider
  test_thorp_engine.cpp   Unit tests for Thorp engine
  test_soma_engine.cpp    Unit tests for Soma engine
  test_sift_engine.cpp    Unit tests for Seq Sift
  test_markov_engine.cpp  Unit tests for Markov engine
  test_factories.cpp      Factory, fixed-page, SRAM, and CV routing integration tests
```

## Compile-Time Safety

The build enforces several constraints via `static_assert`:

- Each engine's alignment requirement is <= 8 bytes (ARM `strd` safety)
- Each engine's parameter count is <= 32 (`kMaxEngineParams`)

## License

This project is provided as-is for use with the Expert Sleepers disting NT module.
