// Pre-flight checklist: headless regression tests for every bug fixed.
// Mission Control Status: Stellar
#include "chip8.h"
#include <cstdio>
#include <vector>
#include <cstring>

static int failures = 0;
#define CHECK(cond, name) do{ if(cond) printf("  [ OK ] %s\n", name); else { printf("  [FAIL] %s\n", name); failures++; } }while(0)

static Chip8 boot(std::vector<uint8_t> prog){
    Chip8 c; c.load_program(prog.data(), prog.size()); return c;
}
static void run(Chip8& c, int n){ for(int i=0;i<n;i++) c.emulate_cycle(); }

int main(){
    printf("Running pre-flight checks...\n");

    { // 00EE: call then return must resume right after the CALL
        Chip8 c = boot({0x22,0x06, 0x60,0x07, 0x12,0x08, 0x00,0xEE, 0x00,0x00});
        // 200: CALL 206 | 202: V0=7 | 204: JP 208 | 206: RET
        run(c, 2); // CALL, RET
        CHECK(c.get_pc() == 0x202 && c.get_sp() == 0, "00EE returns to caller (sp decremented before read)");
    }
    { // 8XY5 equal values -> VF=1
        Chip8 c = boot({0x60,0x05, 0x61,0x05, 0x80,0x15});
        run(c,3);
        CHECK(c.get_v(0) == 0 && c.get_v(0xF) == 1, "8XY5 equal operands set VF=1 (>=)");
    }
    { // 8XY5 with X==F: flag must win
        Chip8 c = boot({0x6F,0x09, 0x61,0x03, 0x8F,0x15});
        run(c,3);
        CHECK(c.get_v(0xF) == 1, "8XY5 VF written after result");
    }
    { // 8XY4 carry
        Chip8 c = boot({0x60,0xFF, 0x61,0x02, 0x80,0x14});
        run(c,3);
        CHECK(c.get_v(0) == 1 && c.get_v(0xF) == 1, "8XY4 carry");
    }
    { // FX0A blocks until key
        Chip8 c = boot({0xF3,0x0A, 0x60,0x01});
        run(c,5);
        bool held = (c.get_pc() == 0x200);
        c.key[0xB] = 1; run(c,1);
        CHECK(held && c.get_v(3) == 0xB && c.get_pc() == 0x202, "FX0A waits for key, then stores it");
    }
    { // FX33 BCD of 254 -> 2,5,4
        Chip8 c = boot({0x60,0xFE, 0xA3,0x00, 0xF0,0x33});
        run(c,3);
        CHECK(c.read_memory(0x300)==2 && c.read_memory(0x301)==5 && c.read_memory(0x302)==4, "FX33 BCD tens digit");
    }
    { // FX55 / FX65 inclusive of VX
        Chip8 c = boot({0x60,0x11, 0x61,0x22, 0x62,0x33, 0xA3,0x00, 0xF2,0x55,
                        0x60,0x00, 0x61,0x00, 0x62,0x00, 0xF2,0x65});
        run(c,9);
        CHECK(c.read_memory(0x302)==0x33, "FX55 stores V0..VX inclusive");
        CHECK(c.get_v(0)==0x11 && c.get_v(1)==0x22 && c.get_v(2)==0x33, "FX65 loads V0..VX inclusive");
    }
    { // Timers only move on tick_timers()
        Chip8 c = boot({0x60,0x3C, 0xF0,0x15, 0x12,0x04});
        run(c,50);
        CHECK(c.get_delay_timer()==0x3C, "Timers do NOT tick per CPU cycle");
        c.tick_timers();
        CHECK(c.get_delay_timer()==0x3B, "tick_timers() decrements at 60 Hz");
    }
    { // Sprite draw + collision + no vertical flip in core
        Chip8 c = boot({0xA0,0x00, 0x60,0x00, 0x61,0x00, 0xD0,0x15, 0xD0,0x15});
        run(c,4);
        bool drawn = c.display[0]==1 && c.display[1]==1;
        run(c,1);
        CHECK(drawn && c.get_v(0xF)==1 && c.display[0]==0, "DXYN draws, detects collision, XOR erases");
    }
    { // Savestate round trip
        Chip8 c = boot({0x60,0x42, 0x61,0x99, 0x62,0x07});
        run(c,2);
        c.save_state("/tmp/c8_test.sav");
        run(c,1);
        bool ok = c.load_state("/tmp/c8_test.sav");
        CHECK(ok && c.get_v(2)==0 && c.get_v(0)==0x42 && c.get_pc()==0x204, "Savestate save/load restores registers + PC");
        FILE* f = fopen("/tmp/c8_bad.sav","wb"); fputs("garbage",f); fclose(f);
        CHECK(!c.load_state("/tmp/c8_bad.sav") && c.get_pc()==0x204, "Corrupt savestate rejected, craft unharmed");
    }
    { // Disassembler
        CHECK(Chip8::disassemble(0x00E0) == "CLS" && Chip8::disassemble(0xD125) == "DRW  V1, V2, 5", "Disassembler mnemonics");
    }

    printf(failures ? "\n%d check(s) FAILED\n" : "\nAll systems nominal. Mission Control Status: Stellar\n", failures);
    return failures ? 1 : 0;
}
