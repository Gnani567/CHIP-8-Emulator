#ifndef CHIP8_H
#define CHIP8_H

// ============================================================================
//  CHIP-8 Flight Computer -- core module
//  Mission Control Status: Stellar
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <string>

class Chip8{
    public:
        Chip8();

        // --- Launch sequence ---------------------------------------------------
        bool load_rom(const std::string& filename);               // Load cargo (ROM) from disk
        bool load_program(const uint8_t* data, size_t size);      // Load cargo from RAM (used by tests)

        // --- Flight operations -------------------------------------------------
        void emulate_cycle();   // Fire one instruction thruster
        void tick_timers();     // Advance the 60 Hz mission clocks (call ONCE per frame)

        // --- Mission-state archive (savestates) --------------------------------
        bool save_state(const std::string& filename) const;
        bool load_state(const std::string& filename);

        // --- Ground-control diagnostics ---------------------------------------
        void cosmo_polo_telemetry() const;                        // Dump full telemetry to stdout
        static std::string disassemble(uint16_t op);              // Opcode -> human-readable mnemonic

        // --- Public cockpit instruments ---------------------------------------
        bool draw_flag;
        uint8_t display[64*32];
        uint8_t key[16];                                          // Keypad (16 keys)

        uint8_t  get_sound_timer() const {return sound_timer;}
        uint8_t  get_delay_timer() const {return delay_timer;}
        uint8_t  get_v(int i) const {return v[i & 0xF];}
        uint16_t get_pc() const {return pc;}
        uint16_t get_index() const {return index;}
        uint8_t  get_sp() const {return sp;}
        uint8_t  read_memory(uint16_t addr) const {return memory[addr & 0xFFF];}

    private:
        uint8_t memory[4096];       // 4 KB cargo bay
        uint8_t v[16];              // Thruster registers V0..VF (VF = status flag)
        uint16_t index;             // Navigation index register
        uint16_t pc;                // Program counter (flight path)
        uint16_t stack[16];         // Orbital-return stack
        uint8_t sp;                 // Stack pointer
        uint8_t delay_timer;        // 60 Hz countdown
        uint8_t sound_timer;        // Beeps while > 0, 60 Hz countdown
        uint16_t opcode;            // Current instruction
        void initialise();          // Pre-launch checklist
        void load_fonts();          // Load glyph library (0-9, A-F)
};

#endif
