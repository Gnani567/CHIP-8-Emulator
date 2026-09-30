#include "chip8.h"
#include <SDL2/SDL.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>

// ============================================================================
//  CHIP-8 Mission Control -- SDL2 ground station
//  Mission Control Status: Stellar
//
//  Bug log (anomalies found during pre-flight inspection):
//    [timing]  SDL_Delay(16) sat INSIDE the 10-cycle loop -> 160 ms per frame
//              (~6 FPS). Now: N cycles per frame, ONE 60 Hz frame delay.
//    [render]  Rows were drawn at (31 - y): the whole display was upside-down.
//    [audio]   Square wave flipped every 100 samples = 220 Hz, not 440 Hz.
//              Replaced with a phase accumulator at the true 440 Hz. The flag
//              shared with the audio thread is now atomic.
//    [input]   Keymap stored in uint8_t (truncating SDL keycodes); now uses
//              SDL_Keycode.
// ============================================================================

const int SCALE  = 10;              // Each CHIP-8 pixel is 10x10 screen pixels
const int WIDTH  = 64*SCALE;
const int HEIGHT = 32*SCALE;

// ---- Flight-deck keypad mapping -------------------------------------------
SDL_Keycode keymap[16] = {
    SDLK_x, SDLK_1, SDLK_2, SDLK_3,   // 0 1 2 3
    SDLK_q, SDLK_w, SDLK_e, SDLK_a,   // 4 5 6 7
    SDLK_s, SDLK_d, SDLK_z, SDLK_c,   // 8 9 A B
    SDLK_4, SDLK_r, SDLK_f, SDLK_v    // C D E F
};

// ---- Display palettes (Custom Display Color Schemes) -----------------------
struct Palette{ const char* name; uint8_t bg[3]; uint8_t fg[3]; };
const Palette palettes[] = {
    {"Classic Green Screen", {  0, 16,  0}, { 51, 255,  51}},
    {"Amber CRT",            { 20,  8,  0}, {255, 176,   0}},
    {"Neon High-Contrast",   {  8,  0, 24}, {  0, 255, 255}},
    {"Deep-Space Mono",      {  0,  0,  0}, {255, 255, 255}},
    {"Mars Dust",            { 30, 10,  5}, {255, 110,  60}},
};
const int NUM_PALETTES = sizeof(palettes)/sizeof(palettes[0]);

// ---- Audio thruster: 440 Hz square wave -------------------------------------
struct AudioState{ std::atomic<bool> beeping{false}; double phase = 0.0; };

void audio_callback(void* userdata, uint8_t* stream, int len){
    AudioState* st = (AudioState*) userdata;
    int16_t* buf = (int16_t*) stream;
    int samples = len/2;
    const double step = 440.0 / 44100.0;           // phase advance per sample
    for(int i=0; i<samples; i++){
        if(st->beeping.load()){
            buf[i] = (st->phase < 0.5) ? 3000 : -3000;
            st->phase += step;
            if(st->phase >= 1.0) st->phase -= 1.0;
        }
        else{
            buf[i] = 0; // Radio silence
            st->phase = 0.0;
        }
    }
}

void draw_graphics(SDL_Renderer* renderer, Chip8& chip8, const Palette& pal){
    SDL_SetRenderDrawColor(renderer, pal.bg[0], pal.bg[1], pal.bg[2], 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawColor(renderer, pal.fg[0], pal.fg[1], pal.fg[2], 255);
    for(int y=0; y<32; y++){
        for(int x=0; x<64; x++){
            if(chip8.display[x + (y*64)] == 1){
                SDL_Rect rect = {x*SCALE, y*SCALE, SCALE, SCALE}; // FIX: was (31-y)
                SDL_RenderFillRect(renderer, &rect);
            }
        }
    }
    SDL_RenderPresent(renderer);
}

// ---- Mission state shared between input handler and main loop ---------------
struct Mission{
    bool running = true;
    bool paused = false;
    int  cycles_per_frame = 10;   // Configurable Emulation Speed (thruster throttle)
    int  palette = 0;
    std::string save_path;
    std::string status_msg;       // Transient message shown in the title bar
    int status_ttl = 0;           // Frames left before the message fades
    void say(const std::string& s){ status_msg = s; status_ttl = 120; } // ~2 seconds
};

const int MIN_CPF = 1, MAX_CPF = 200;

void update_title(SDL_Window* window, const Mission& m){
    char title[256];
    snprintf(title, sizeof(title), "Chip-8 | %d cyc/frame (~%d Hz) | %s%s%s%s",
             m.cycles_per_frame, m.cycles_per_frame*60, palettes[m.palette].name,
             m.paused ? " | PAUSED" : "",
             m.status_msg.empty() ? "" : " | ", m.status_msg.c_str());
    SDL_SetWindowTitle(window, title);
}

void handle_input(Chip8& chip8, Mission& m){
    SDL_Event event;
    while(SDL_PollEvent(&event)){
        if(event.type == SDL_QUIT) m.running = false;
        if(event.type == SDL_KEYDOWN){
            SDL_Keycode k = event.key.keysym.sym;
            if(!event.key.repeat){ // Ignore OS key-repeat for control keys
                switch(k){
                    case SDLK_ESCAPE: m.running = false; break;
                    case SDLK_UP: // Throttle up
                        m.cycles_per_frame = std::min(MAX_CPF, m.cycles_per_frame + (m.cycles_per_frame >= 20 ? 5 : 1));
                        break;
                    case SDLK_DOWN: // Throttle down
                        m.cycles_per_frame = std::max(MIN_CPF, m.cycles_per_frame - (m.cycles_per_frame > 20 ? 5 : 1));
                        break;
                    case SDLK_BACKSPACE: m.cycles_per_frame = 10; m.say("speed reset"); break;
                    case SDLK_TAB: // Cycle palette
                        m.palette = (m.palette + 1) % NUM_PALETTES;
                        chip8.draw_flag = true; break;
                    case SDLK_p: m.paused = !m.paused; break;
                    case SDLK_F5:
                        m.say(chip8.save_state(m.save_path) ? "STATE SAVED" : "SAVE FAILED"); break;
                    case SDLK_F9:
                        m.say(chip8.load_state(m.save_path) ? "STATE LOADED" : "LOAD FAILED (no save?)"); break;
                    case SDLK_F1: chip8.cosmo_polo_telemetry(); break;
                    default: break;
                }
            }
            for(int i=0; i<16; i++) if(k == keymap[i]) chip8.key[i] = 1;
        }
        if(event.type == SDL_KEYUP){
            for(int i=0; i<16; i++) if(event.key.keysym.sym == keymap[i]) chip8.key[i] = 0;
        }
    }
}

int main(int argc, char** argv){
    if(argc < 2){
        std::cerr << "Usage: " << argv[0] << " <ROM file>" << std::endl;
        return 1;
    }
    std::cout << "Mission Control Status: Stellar" << std::endl;
    std::cout << "Controls: UP/DOWN speed | BACKSPACE reset speed | TAB palette | P pause\n"
                 "          F5 save state | F9 load state | F1 telemetry | ESC quit\n";

    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0){
        std::cerr << "SDL Error: " << SDL_GetError() << std::endl;
        return 1;
    }

    // Audio setup
    AudioState audio;
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    want.callback = audio_callback;
    want.userdata = &audio;

    SDL_AudioDeviceID audio_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if(audio_device == 0) std::cerr << "Failed to open audio: " << SDL_GetError() << std::endl;
    else SDL_PauseAudioDevice(audio_device, 0);

    SDL_Window* window = SDL_CreateWindow("Chip-8 Emulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIDTH, HEIGHT, SDL_WINDOW_SHOWN);
    if(!window){
        std::cerr << "Window error: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if(!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE); // fallback
    if(!renderer){
        std::cerr << "Renderer error: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    Chip8 chip8;
    if(!chip8.load_rom(argv[1])){
        SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit();
        return 1;
    }

    Mission m;
    m.save_path = std::string(argv[1]) + ".sav";

    const double frame_ms = 1000.0 / 60.0;
    const double freq = (double) SDL_GetPerformanceFrequency();
    update_title(window, m);

    while(m.running){
        uint64_t frame_start = SDL_GetPerformanceCounter();

        handle_input(chip8, m);

        if(!m.paused){
            for(int i=0; i<m.cycles_per_frame; i++) chip8.emulate_cycle(); // throttle burn
            chip8.tick_timers();  // FIX: exactly one 60 Hz tick per frame
        }

        audio.beeping.store(!m.paused && chip8.get_sound_timer() > 0);
        draw_graphics(renderer, chip8, palettes[m.palette]);
        update_title(window, m);
        if(m.status_ttl > 0 && --m.status_ttl == 0) m.status_msg.clear(); // message fades

        // FIX: one frame delay per FRAME (was per instruction), compensated for work time
        double elapsed = (SDL_GetPerformanceCounter() - frame_start) * 1000.0 / freq;
        if(elapsed < frame_ms) SDL_Delay((uint32_t)(frame_ms - elapsed));
    }

    if(audio_device != 0) SDL_CloseAudioDevice(audio_device);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
