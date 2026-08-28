# Seq Thorp

A pattern arpeggiator that plays held notes through rhythmic and melodic patterns. Feed it notes via MIDI or CV, choose a pattern, and Thorp arpeggates them with velocity shaping, octave jumps, and chain sequencing.

## How It Works

Thorp stores up to **16 slots**, each containing a set of latched notes plus per-slot pattern settings. On each clock tick, the active slot's pattern selects which of the held notes to play. Patterns are 8-step sequences that index into the held notes by position (1st, 2nd, 3rd, etc.), with 0 meaning rest.

Notes are captured from MIDI or CV input. The first key press after all keys are released clears the latch, so you can build new chords naturally.

## Parameters

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| Pattern | 0-22 | Ascending | Note pattern (see list below) |
| Vel Pat | 0-14 | Constant | Velocity pattern (see list below) |
| Length | 1-32 | 8 | Number of steps before the pattern repeats |
| Offset | 0-7 | 0 | Rotates the pattern start point |
| Reverse | Off/On | Off | Plays the pattern backwards |
| Arp Slot | 1-16 | 1 | Which slot to edit/play in Jam mode |
| Gate Prob | 1-100% | 100% | Probability that a note actually fires |
| Oct Jump | 0-100% | 0% | Probability of octave displacement per note |
| Oct Range | 1-3 | 1 | Maximum octave displacement when Oct Jump triggers |
| Seq Mode | Seq/PingPong/RndWalk/Random | Seq | How the pattern steps advance |
| Global Vel | 0-100% | 100% | Master velocity scaler applied after velocity pattern |
| Gate Len | 1-100% | 50% | Gate length as a percentage of clock period |
| Play Mode | Jam/Song | Jam | Jam plays one slot; Song chains multiple slots |
| Chain Len | 1-16 | 1 | Number of slots in the song chain |
| MIDI In Ch | 0-16 | 0 (Omni) | MIDI input channel for note capture (0=Omni, 1-16=channel) |

## Note Patterns (23)

| # | Name | Description |
|---|------|-------------|
| 0 | Ascending | 1-2-3-4-5-6-7-8 |
| 1 | Descending | 8-7-6-5-4-3-2-1 |
| 2 | UpDown | Up then reverses at the top |
| 3 | DownUp | Down then reverses at the bottom |
| 4 | Alternate | Interleaves low and high notes |
| 5 | TriadRoot | Root triad repeated |
| 6 | TriadThird | Triad from the third |
| 7 | 7thArp | Seventh chord arpeggio up and down |
| 8 | FifthLeap | Alternates with fifth intervals |
| 9 | PentaAsc | Pentatonic ascending |
| 10 | PentaDesc | Pentatonic descending |
| 11 | MajBlues | Major blues figure |
| 12 | MinBlues | Minor blues figure |
| 13 | Circle5th | Circle of fifths motion |
| 14 | Arp4ths | Arpeggiated fourths |
| 15 | Arp3rds | Arpeggiated thirds |
| 16-18 | Random 1-3 | Fixed pseudo-random orderings |
| 19 | Syncopated | Notes with rests between them |
| 20 | Burst2 | Two-note bursts with rests |
| 21 | Burst3 | Three-note bursts with rests |
| 22 | ClusterStep | Cluster stepping up and back |

## Velocity Patterns (15)

Constant, Accent, OffBeat, Crescendo, Diminuendo, Strong/Weak, Swing, RandomWalk, Pulse, Breathe, Hard/Soft, BuildUp, BreakDown, Pump, Subtle.

## Step Modes

- **Seq**: Steps advance forward, wrapping at the end
- **PingPong**: Steps bounce between start and end
- **RndWalk**: Steps wander randomly by +/-1
- **Random**: Each step is chosen randomly within the length

## Jam vs Song Mode

In **Jam** mode, you play one slot at a time. Change the Arp Slot parameter to switch which slot is active. Notes you latch go into the current slot.

In **Song** mode, Thorp chains multiple slots together. When the current slot finishes its pattern, it advances to the next slot in the chain. The chain uses the same Seq Mode (Seq, PingPong, RndWalk, Random) to determine how it advances through the chain. Receiving note input automatically switches to Jam mode.

## Focus UI Controls

When focused on a Seq Thorp channel, the hardware controls become a song editor:

| Control | Function |
|---------|----------|
| Pot L | Chain length (1-16) |
| Pot C | Chain cursor position |
| Pot R | Slot assignment at cursor position (1-16) |
| Encoder L | Nudge chain cursor position |
| Encoder R | Nudge slot assignment at cursor |
| Encoder L push | Insert chain step after cursor |
| Button 1 | Chain length -1 |
| Button 2 | Toggle Jam/Song mode |
| Button 3 | Delete chain step (or cycle Seq Mode if chain length = 1) |
| Button 4 | Chain length +1 |

The focus display shows:
- **Line 1**: Pattern name, sequence mode, and play mode
- **Line 2**: Compact status: L(ength), O(ffset), R(everse), S(lot), C(hain len), P(os), CS(chain slot), G(ate prob), V(elocity), GL(gate len)
