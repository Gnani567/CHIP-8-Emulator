#include "chip8.h"
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>

// ============================================================================
//  CHIP-8 Flight Computer -- core module
//  Mission Control Status: Stellar
//
//  Bug log (anomalies found during pre-flight inspection):
//    [00EE] Stack pointer was read BEFORE being decremented -> return went to a
//           stale orbit (wrong slot). Fixed ordering.
//    [8XY5/8XY7] VF was computed with '>' instead of '>=' (equal values must set
//           "no borrow"), and VF was written BEFORE the subtraction so VX==VF
//           runs corrupted the telemetry flag. Fixed.
//    [8XY4/6/E] VF is now written AFTER the result so VF as a target works.
//    [FX0A] Program counter advanced even with no key pressed, so the craft
//           never actually waited. Now it holds position until a key is pressed.
//    [FX33] Tens digit was value/10 instead of (value/10)%10.
//    [FX55/FX65] Loops were `< x` instead of `<= x` (missed register VX).
//    [timers] Timers decremented every CPU cycle instead of at 60 Hz. Moved to
//           tick_timers(), called once per frame.
//    [memory] Fetch/sprite/BCD/store accesses could run past 4 KB. Masked.
//    [stack] Call/return now guards against overflow/underflow.
// ============================================================================

static const uint8_t chip8_fontset[80] = {
    0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
    0x20, 0x60, 0x20, 0x20, 0x70, // 1
    0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
    0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
    0x90, 0x90, 0xF0, 0x10, 0x10, // 4
    0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
    0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
    0xF0, 0x10, 0x20, 0x40, 0x40, // 7
    0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
    0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
    0xF0, 0x90, 0xF0, 0x90, 0x90, // A
    0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
    0xF0, 0x80, 0x80, 0x80, 0xF0, // C
    0xE0, 0x90, 0x90, 0x90, 0xE0, // D
    0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
    0xF0, 0x80, 0xF0, 0x80, 0x80  // F
};

Chip8::Chip8(){
    initialise();
}

void Chip8::initialise(){
    pc = 0x200;
    opcode = 0;
    index = 0;
    sp = 0;

    memset(display, 0, sizeof(display));
    memset(stack, 0, sizeof(stack));
    memset(v, 0, sizeof(v));
    memset(memory, 0, sizeof(memory));
    memset(key, 0, sizeof(key));

    load_fonts();
    delay_timer = 0;
    sound_timer = 0;
    draw_flag = false;
}

void Chip8::load_fonts(){
    for(int i=0; i<80; i++) memory[i] = chip8_fontset[i];
}

bool Chip8::load_program(const uint8_t* data, size_t size){
    if(size > (4096-512)){ // 512 bytes reserved for the launch pad (interpreter/fonts)
        std::cerr << "ROM too large to fit in memory" << std::endl;
        return false;
    }
    initialise();
    memcpy(memory+512, data, size);
    return true;
}

bool Chip8::load_rom(const std::string& filename){
    std::ifstream file(filename, std::ios::binary | std::ios::ate);

    if(!file.is_open()){
        std::cerr << "Failed to open ROM: " << filename << std::endl;
        return false;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    if(size < 0 || size > (4096-512)){
        std::cerr << "ROM too large to fit in memory" << std::endl;
        return false;
    }

    uint8_t buffer[4096];
    file.read((char*)buffer, size);
    if(!file) { std::cerr << "Failed to read ROM payload" << std::endl; return false; }

    if(!load_program(buffer, (size_t)size)) return false;
    std::cout << "Loaded ROM: " << filename << " (" << size << " bytes)" << std::endl;
    return true;
}

void Chip8::tick_timers(){
    // Mission clocks tick at 60 Hz, independent of CPU throttle.
    if(delay_timer > 0) delay_timer--;
    if(sound_timer > 0) sound_timer--;
}

void Chip8::emulate_cycle(){
    opcode = (uint16_t)(memory[pc & 0xFFF] << 8 | memory[(pc+1) & 0xFFF]); // 16-bit instruction

    const int x  = (opcode & 0x0F00) >> 8;
    const int y  = (opcode & 0x00F0) >> 4;
    const uint8_t nn = opcode & 0x00FF;
    const uint16_t nnn = opcode & 0x0FFF;

    switch(opcode & 0xF000){ // Gets only the first 4 bits
        case 0x0000:
            switch(opcode & 0x00FF){
                case 0x00E0: // Clears the display
                    std::memset(display, 0, sizeof(display));
                    draw_flag = true;
                    pc += 2;
                    break;
                case 0x00EE: // Returns from subroutine
                    if(sp == 0){
                        std::cerr << "Stack underflow: return with empty orbit stack" << std::endl;
                        pc += 2;
                        break;
                    }
                    sp--;               // FIX: decrement first, then read
                    pc = stack[sp];
                    pc += 2;
                    break;
                default:
                    std::cerr << "Unknown opcode: 0x" << std::hex << opcode << std::dec << std::endl;
                    pc += 2;
            }
            break;
        case 0x1000: // 1NNN = Jump to address NNN
            pc = nnn;
            break;
        case 0x2000: // 2NNN = Call subroutine at NNN
            if(sp >= 16){
                std::cerr << "Stack overflow: orbit stack is full" << std::endl;
                pc += 2;
                break;
            }
            stack[sp] = pc;
            sp++;
            pc = nnn;
            break;
        case 0x3000: // 3XNN = Skip next instruction if v[x] == NN
            pc += (v[x] == nn) ? 4 : 2;
            break;
        case 0x4000: // 4XNN = Skip next instruction if v[x] != NN
            pc += (v[x] != nn) ? 4 : 2;
            break;
        case 0x5000: // 5XY0 = Skip next instruction if v[x] == v[y]
            pc += (v[x] == v[y]) ? 4 : 2;
            break;
        case 0x6000: // 6XNN = set v[x] = NN
            v[x] = nn;
            pc += 2;
            break;
        case 0x7000: // 7XNN = add NN to v[x] (no flag)
            v[x] += nn;
            pc += 2;
            break;
        case 0x8000: // Arithmetic operations: 8XYN
            switch(opcode & 0x000F){
                case 0x0: v[x] = v[y]; pc += 2; break;
                case 0x1: v[x] |= v[y]; pc += 2; break;
                case 0x2: v[x] &= v[y]; pc += 2; break;
                case 0x3: v[x] ^= v[y]; pc += 2; break;
                case 0x4:{ // v[x] += v[y], VF = carry
                    uint16_t sum = v[x] + v[y];
                    v[x] = sum & 0xFF;
                    v[0xF] = (sum > 0xFF) ? 1 : 0; // FIX: flag written last
                    pc += 2;
                    break;
                }
                case 0x5:{ // v[x] -= v[y], VF = NOT borrow
                    uint8_t flag = (v[x] >= v[y]) ? 1 : 0; // FIX: '>=' not '>'
                    v[x] = v[x] - v[y];
                    v[0xF] = flag;
                    pc += 2;
                    break;
                }
                case 0x6:{ // v[x] >>= 1, VF = LSB
                    uint8_t flag = v[x] & 0x1;
                    v[x] >>= 1;
                    v[0xF] = flag;
                    pc += 2;
                    break;
                }
                case 0x7:{ // v[x] = v[y] - v[x], VF = NOT borrow
                    uint8_t flag = (v[y] >= v[x]) ? 1 : 0;
                    v[x] = v[y] - v[x];
                    v[0xF] = flag;
                    pc += 2;
                    break;
                }
                case 0xE:{ // v[x] <<= 1, VF = MSB
                    uint8_t flag = v[x] >> 7;
                    v[x] <<= 1;
                    v[0xF] = flag;
                    pc += 2;
                    break;
                }
                default:
                    std::cerr << "Unknown opcode: 0x" << std::hex << opcode << std::dec << std::endl;
                    pc += 2;
                    break;
            }
            break;
        case 0x9000: // 9XY0 = Skip next instr. if v[x] != v[y]
            pc += (v[x] != v[y]) ? 4 : 2;
            break;
        case 0xA000: // ANNN = set index to NNN
            index = nnn;
            pc += 2;
            break;
        case 0xB000: // BNNN = jump to NNN + v[0]
            pc = (nnn + v[0]) & 0xFFF;
            break;
        case 0xC000:{ // CXNN = v[x] = random_byte & NN
            static std::random_device rd;
            static std::mt19937 gen(rd());
            static std::uniform_int_distribution<> dist(0, 255);
            v[x] = dist(gen) & nn;
            pc += 2;
            break;
        }
        case 0xD000:{ // DXYN = draw sprite at (v[x], v[y]) with height N
            uint8_t px = v[x], py = v[y];
            uint8_t height = opcode & 0x000F;

            v[0xF] = 0; // Reset collision flag
            for(int row=0; row<height; row++){
                uint8_t sprite_row = memory[(index + row) & 0xFFF];
                for(int col=0; col<8; col++){
                    if((sprite_row & (0x80 >> col)) != 0){
                        // Origin wraps; pixels past the screen edge are clipped
                        int screen_x = (px & 63) + col, screen_y = (py & 31) + row;
                        if(screen_x >= 64 || screen_y >= 32) continue;
                        int idx = screen_x + (screen_y*64);
                        if(display[idx] == 1) v[0xF] = 1; // Collision detected
                        display[idx] ^= 1;
                    }
                }
            }
            draw_flag = true;
            pc += 2;
            break;
        }
        case 0xE000:
            switch(opcode & 0x00FF){
                case 0x9E: // EX9E = skip if key[v[x]] pressed
                    pc += (key[v[x] & 0xF] != 0) ? 4 : 2;
                    break;
                case 0xA1: // EXA1 = skip if key[v[x]] NOT pressed
                    pc += (key[v[x] & 0xF] == 0) ? 4 : 2;
                    break;
                default:
                    std::cerr << "Unknown opcode: 0x" << std::hex << opcode << std::dec << std::endl;
                    pc += 2;
            }
            break;
        case 0xF000:
            switch(opcode & 0x00FF){
                case 0x07: v[x] = delay_timer; pc += 2; break;
                case 0x0A:{ // FX0A = wait for key press, store in v[x]
                    for(int i=0; i<16; i++){
                        if(key[i] != 0){
                            v[x] = i;
                            pc += 2; // FIX: only advance once a key is pressed
                            break;
                        }
                    }
                    // No key: hold orbit (pc unchanged) and re-run next cycle
                    break;
                }
                case 0x15: delay_timer = v[x]; pc += 2; break;
                case 0x18: sound_timer = v[x]; pc += 2; break;
                case 0x1E: index += v[x]; pc += 2; break;
                case 0x29: index = (v[x] & 0xF) * 5; pc += 2; break;
                case 0x33:{ // FX33 = BCD of v[x] at index, index+1, index+2
                    uint8_t value = v[x];
                    memory[index & 0xFFF] = value/100;
                    memory[(index+1) & 0xFFF] = (value/10) % 10; // FIX: tens digit
                    memory[(index+2) & 0xFFF] = value%10;
                    pc += 2;
                    break;
                }
                case 0x55: // FX55 = store v[0]..v[x] (inclusive) at index
                    for(int i=0; i<=x; i++) memory[(index+i) & 0xFFF] = v[i]; // FIX: <=
                    pc += 2;
                    break;
                case 0x65: // FX65 = load v[0]..v[x] (inclusive) from index
                    for(int i=0; i<=x; i++) v[i] = memory[(index+i) & 0xFFF]; // FIX: <=
                    pc += 2;
                    break;
                default:
                    std::cerr << "Unknown opcode: 0x" << std::hex << opcode << std::dec << std::endl;
                    pc += 2;
            }
            break;
        default:
            std::cerr << "Unknown opcode: 0x" << std::hex << opcode << std::dec << std::endl;
            pc += 2;
            break;
    }
    pc &= 0xFFF;
}

// ---------------------------------------------------------------------------
//  Mission-state archive: binary format "C8SV" + version + raw registers
// ---------------------------------------------------------------------------
static const char     SAVE_MAGIC[4] = {'C','8','S','V'};
static const uint32_t SAVE_VERSION  = 1;

template<typename T> static void put(std::ofstream& f, const T& val){ f.write((const char*)&val, sizeof(T)); }
template<typename T> static bool get(std::ifstream& f, T& val){ f.read((char*)&val, sizeof(T)); return (bool)f; }

bool Chip8::save_state(const std::string& filename) const{
    std::ofstream f(filename, std::ios::binary | std::ios::trunc);
    if(!f.is_open()) return false;
    f.write(SAVE_MAGIC, 4);
    put(f, SAVE_VERSION);
    f.write((const char*)memory, sizeof(memory));
    f.write((const char*)v, sizeof(v));
    put(f, index); put(f, pc); put(f, sp);
    f.write((const char*)stack, sizeof(stack));
    put(f, delay_timer); put(f, sound_timer);
    f.write((const char*)display, sizeof(display));
    return (bool)f;
}

bool Chip8::load_state(const std::string& filename){
    std::ifstream f(filename, std::ios::binary);
    if(!f.is_open()) return false;

    char magic[4]; uint32_t version = 0;
    f.read(magic, 4);
    if(!f || memcmp(magic, SAVE_MAGIC, 4) != 0) return false;
    if(!get(f, version) || version != SAVE_VERSION) return false;

    // Stage into temporaries so a corrupt file never damages the live craft
    Chip8 staged;
    f.read((char*)staged.memory, sizeof(staged.memory));
    f.read((char*)staged.v, sizeof(staged.v));
    if(!get(f, staged.index) || !get(f, staged.pc) || !get(f, staged.sp)) return false;
    f.read((char*)staged.stack, sizeof(staged.stack));
    if(!get(f, staged.delay_timer) || !get(f, staged.sound_timer)) return false;
    f.read((char*)staged.display, sizeof(staged.display));
    if(!f) return false;
    if(staged.sp > 16 || staged.pc > 0xFFF) return false; // sanity check the telemetry

    memcpy(memory, staged.memory, sizeof(memory));
    memcpy(v, staged.v, sizeof(v));
    memcpy(stack, staged.stack, sizeof(stack));
    memcpy(display, staged.display, sizeof(display));
    index = staged.index; pc = staged.pc; sp = staged.sp;
    delay_timer = staged.delay_timer; sound_timer = staged.sound_timer;
    memset(key, 0, sizeof(key));
    draw_flag = true;
    return true;
}

// ---------------------------------------------------------------------------
//  Disassembler: opcode -> mnemonic
// ---------------------------------------------------------------------------
std::string Chip8::disassemble(uint16_t op){
    std::ostringstream s;
    s << std::hex << std::uppercase << std::setfill('0');
    const int x = (op>>8)&0xF, y = (op>>4)&0xF;
    const int n = op&0xF, nn = op&0xFF, nnn = op&0xFFF;
    switch(op & 0xF000){
        case 0x0000:
            if(op == 0x00E0) s << "CLS";
            else if(op == 0x00EE) s << "RET";
            else s << "SYS " << std::setw(3) << nnn;
            break;
        case 0x1000: s << "JP   " << std::setw(3) << nnn; break;
        case 0x2000: s << "CALL " << std::setw(3) << nnn; break;
        case 0x3000: s << "SE   V" << x << ", " << std::setw(2) << nn; break;
        case 0x4000: s << "SNE  V" << x << ", " << std::setw(2) << nn; break;
        case 0x5000: s << "SE   V" << x << ", V" << y; break;
        case 0x6000: s << "LD   V" << x << ", " << std::setw(2) << nn; break;
        case 0x7000: s << "ADD  V" << x << ", " << std::setw(2) << nn; break;
        case 0x8000:
            switch(n){
                case 0x0: s << "LD   V" << x << ", V" << y; break;
                case 0x1: s << "OR   V" << x << ", V" << y; break;
                case 0x2: s << "AND  V" << x << ", V" << y; break;
                case 0x3: s << "XOR  V" << x << ", V" << y; break;
                case 0x4: s << "ADD  V" << x << ", V" << y; break;
                case 0x5: s << "SUB  V" << x << ", V" << y; break;
                case 0x6: s << "SHR  V" << x; break;
                case 0x7: s << "SUBN V" << x << ", V" << y; break;
                case 0xE: s << "SHL  V" << x; break;
                default:  s << "DW   " << std::setw(4) << op;
            }
            break;
        case 0x9000: s << "SNE  V" << x << ", V" << y; break;
        case 0xA000: s << "LD   I, " << std::setw(3) << nnn; break;
        case 0xB000: s << "JP   V0, " << std::setw(3) << nnn; break;
        case 0xC000: s << "RND  V" << x << ", " << std::setw(2) << nn; break;
        case 0xD000: s << "DRW  V" << x << ", V" << y << ", " << n; break;
        case 0xE000:
            if(nn == 0x9E) s << "SKP  V" << x;
            else if(nn == 0xA1) s << "SKNP V" << x;
            else s << "DW   " << std::setw(4) << op;
            break;
        case 0xF000:
            switch(nn){
                case 0x07: s << "LD   V" << x << ", DT"; break;
                case 0x0A: s << "LD   V" << x << ", K"; break;
                case 0x15: s << "LD   DT, V" << x; break;
                case 0x18: s << "LD   ST, V" << x; break;
                case 0x1E: s << "ADD  I, V" << x; break;
                case 0x29: s << "LD   F, V" << x; break;
                case 0x33: s << "LD   B, V" << x; break;
                case 0x55: s << "LD   [I], V" << x; break;
                case 0x65: s << "LD   V" << x << ", [I]"; break;
                default:   s << "DW   " << std::setw(4) << op;
            }
            break;
    }
    return s.str();
}

// ---------------------------------------------------------------------------
//  Telemetry downlink -- full craft status to ground control (stdout)
// ---------------------------------------------------------------------------
void Chip8::cosmo_polo_telemetry() const{
    uint16_t next = (uint16_t)(memory[pc & 0xFFF] << 8 | memory[(pc+1) & 0xFFF]);
    std::cout << "\n==== COSMO-POLO TELEMETRY ====================================\n"
              << "Mission Control Status: Stellar\n"
              << std::hex << std::uppercase << std::setfill('0')
              << "PC: 0x" << std::setw(3) << pc
              << "   I: 0x" << std::setw(3) << index
              << "   SP: " << std::dec << (int)sp
              << "   DT: " << (int)delay_timer
              << "   ST: " << (int)sound_timer << "\n"
              << "Next burn: 0x" << std::hex << std::setw(4) << next
              << "  " << disassemble(next) << "\nThrusters:";
    for(int i=0; i<16; i++){
        if(i % 8 == 0) std::cout << "\n  ";
        std::cout << "V" << std::hex << i << "=" << std::setw(2) << (int)v[i] << " ";
    }
    std::cout << "\nOrbit stack:";
    for(int i=0; i<sp; i++) std::cout << " 0x" << std::setw(3) << std::hex << stack[i];
    std::cout << "\n==============================================================\n"
              << std::dec << std::setfill(' ') << std::endl;
}
