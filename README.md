# Chip-8 Emulator

A Chip-8 emulator built in C++ with SDL2 graphics and audio support.

> **Note to Participants (original):** 
> This codebase is intentionally incomplete and contains implementation defects across opcode handling, memory management, timing control, and rendering pipeline. Please consult the **Problem Statement** document for your exact submission guidelines and evaluation criteria.

## Features

- All 35 Chip-8 opcodes implemented
- 64x32 pixel display with SDL2 rendering
- Keyboard input support
- Sound effects (beep tone)
- 60 FPS rendering

## Architecture

Chip-8 is a virtual machine from the 1970s designed to make programming video games easier on early microcomputers.

### System Specifications

- **Memory**: 4 KB (4096 bytes)
  - `0x000-0x1FF`: Reserved for interpreter and fonts
  - `0x200-0xFFF`: Program/ROM space
- **Registers**:
  - 16 8-bit general-purpose registers (V0-VF)
  - VF doubles as a flag register for arithmetic operations
  - 16-bit index register (I)
  - 16-bit program counter (PC)
  - 8-bit stack pointer (SP)
- **Display**: 64x32 pixels, monochrome
- **Timers**: 
  - Delay timer (counts down at 60 Hz)
  - Sound timer (beeps when > 0, counts down at 60 Hz)
- **Stack**: 16 levels for subroutine calls
- **Keypad**: 16-key hexadecimal input

## Dependencies

- SDL2 library

### Installation

**Arch Linux:**
```bash
sudo pacman -S sdl2
```

**Ubuntu/Debian:**
```bash
sudo apt-get install libsdl2-dev
```

**macOS:**
```bash
brew install sdl2
```

## Building

1. Clone the repo
```bash
git clone https://github.com/TatHack-Tathva/chip8-emulator.git
```

2. Make it
```bash
make
```

## Usage
```bash
./chip8 <path-to-rom-file>
```

**Example:**
```bash
./chip8 roms/PONG.ch8
```

## Keyboard Mapping

The original Chip-8 keypad is mapped to keyboard keys:
```
Chip-8 Keypad:          QWERTY Keyboard:
┌─┬─┬─┬─┐               ┌─┬─┬─┬─┐
│1│2│3│C│               │1│2│3│4│
├─┼─┼─┼─┤               ├─┼─┼─┼─┤
│4│5│6│D│               │Q│W│E│R│
├─┼─┼─┼─┤      =        ├─┼─┼─┼─┤
│7│8│9│E│               │A│S│D│F│
├─┼─┼─┼─┤               ├─┼─┼─┼─┤
│A│0│B│F│               │Z│X│C│V│
└─┴─┴─┴─┘               └─┴─┴─┴─┘
```

**Controls:**
- `ESC` - Quit emulator
- Keyboard keys as mapped above

### Game-Specific Controls

**PONG:**
- Left paddle: `1` (up), `Q` (down)
- Right paddle: `4` (up), `R` (down)

**TETRIS:**
- `Q` - Rotate
- `W` - Drop
- `E` - Move right
- `A` - Move left

## Implementation Details

### Instruction Set

The emulator implements all 35 Chip-8 instructions, including:
- **Arithmetic**: ADD, SUB, AND, OR, XOR, shift operations
- **Graphics**: Draw sprites with XOR mode, collision detection
- **Flow control**: Jump, call/return subroutines, conditional skips
- **Memory**: Load/store registers, BCD conversion
- **Timers**: Delay and sound timer operations
- **Input**: Key press detection (blocking and non-blocking)

### Display

Graphics are rendered using SDL2:
- Each Chip-8 pixel is scaled 10× for visibility (640×320 window)
- XOR-based sprite drawing for collision detection
- 60 FPS rendering

### Audio

Simple square wave generation at 440 Hz (musical note A) plays when `sound_timer > 0`.


## Mission Log: Fixes & New Features
Mission Control Status: Stellar

### Bugs fixed
| Area | Bug | Fix |
|---|---|---|
| Main loop | `SDL_Delay(16)` inside the per-instruction loop (~6 FPS) | N cycles per frame, one delay per frame, compensated for work time |
| Timers | Decremented on every CPU cycle | `tick_timers()` called once per 60 Hz frame; stray `BEEP!` print removed |
| Render | Rows drawn at `31 - y` (upside-down screen) | Draw at `y` |
| Audio | Wave flipped every 100 samples = 220 Hz | Phase accumulator at true 440 Hz; atomic flag shared with audio thread |
| Input | Keymap stored in `uint8_t` | `SDL_Keycode` |
| `00EE` | Read stack before decrementing SP | Decrement, then read (+ underflow guard) |
| `8XY5`/`8XY7` | `>` instead of `>=` for VF; VF written before result | `>=`, flag written last |
| `FX0A` | PC advanced with no key pressed | PC holds until a key is pressed |
| `FX33` | Tens digit was `v/10` | `(v/10) % 10` |
| `FX55`/`FX65` | Loop `< x` skipped VX | Loop `<= x` |
| Memory | Fetch/sprite/BCD/store could overrun 4 KB | Addresses masked to 12 bits; call-stack overflow guard |

### New controls
| Key | Action |
|---|---|
| `UP` / `DOWN` | Increase / decrease CPU cycles per frame (1-200, default 10) |
| `BACKSPACE` | Reset speed |
| `TAB` | Cycle palette: Classic Green, Amber CRT, Neon High-Contrast, Deep-Space Mono, Mars Dust |
| `F5` / `F9` | Save / load state to `<rom>.sav` (memory, registers, I, PC, SP, stack, timers, display) |
| `P` | Pause |
| `F1` | Print `cosmo_polo_telemetry()` (registers, stack, next instruction disassembled) |

Speed, palette and status appear in the window title.

### Testing
`make test` runs headless opcode/timer/savestate regression tests (no SDL or display needed).

## Resources

- [Chip-8 ROMs Archive](https://github.com/kripod/chip8-roms)
# CHIP-8-Emulator
