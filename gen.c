#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define WINDOW_WIDTH 900
#define WINDOW_HEIGHT 600
#define WAVEFORM_TOP 80
#define WAVEFORM_HEIGHT 250
#define DISPLAY_DURATION 2.0
#define SAMPLE_RATE 48000
#define BUFFER_SAMPLES 1024
#define AMPLITUDE 0.35
#define DEFAULT_FREQ 440.0

typedef enum { SINE, SQUARE, SAWTOOTH, TRIANGLE, CUSTOM } WaveType;

WaveType current_type = SINE;
double current_freq = DEFAULT_FREQ;
int playing = 1;

double phase_accumulator = 0.0;  // Sample-accurate position in the buffer (0 to buffer_samples)
double phase_increment = 0.0;    // How much to advance per audio sample

float *waveform_buffer = NULL;
int buffer_samples = 0;

typedef struct {
    SDL_Rect rect;
    char label[32];
} Button;

Button buttons[6];
TTF_Font *font = NULL;

int drawing = 0;

void generate_classic_waveform() {
    if (!waveform_buffer) return;

    double total_cycles = current_freq * DISPLAY_DURATION;
    int num_cycles = (int)round(total_cycles);
    double actual_freq = num_cycles / DISPLAY_DURATION;
    current_freq = actual_freq;

    double samples_per_cycle = (double)buffer_samples / num_cycles;

    for (int i = 0; i < buffer_samples; i++) {
        double pos = i / samples_per_cycle;
        double phase = fmod(pos, 1.0);

        double sample;
        switch (current_type) {
            case SINE:     sample = sin(2.0 * M_PI * phase); break;
            case SQUARE:   sample = (phase < 0.5) ? 1.0 : -1.0; break;
            case SAWTOOTH: sample = 2.0 * phase - 1.0; break;
            case TRIANGLE:
                sample = (phase < 0.5) ? (4.0 * phase - 1.0) : (3.0 - 4.0 * phase);
                break;
            default: sample = 0.0;
        }
        waveform_buffer[i] = (float)(sample * AMPLITUDE);
    }
}

void audio_callback(void *userdata, Uint8 *stream, int len) {
    float *out = (float *)stream;
    int num_samples = len / sizeof(float);

    if (!playing || !waveform_buffer) {
        memset(stream, 0, len);
        return;
    }

    for (int i = 0; i < num_samples; i++) {
        // Get integer and fractional part
        int idx = (int)phase_accumulator;
        double frac = phase_accumulator - idx;

        // Wrap index
        idx %= buffer_samples;
        if (idx < 0) idx += buffer_samples;

        // Linear interpolation
        float a = waveform_buffer[idx];
        float b = waveform_buffer[(idx + 1) % buffer_samples];
        out[i] = (float)(a + (b - a) * frac);

        // Advance phase
        phase_accumulator += phase_increment;
        
        // Wrap accumulator to prevent precision loss over long time
        if (phase_accumulator >= buffer_samples) {
            phase_accumulator -= buffer_samples;
        }
    }
}

Button make_button(int x, int y, int w, int h, const char *label) {
    Button b;
    b.rect = (SDL_Rect){x, y, w, h};
    strncpy(b.label, label, 31);
    b.label[31] = '\0';
    return b;
}

void init_buttons() {
    int btn_w = 130, btn_h = 60;
    int start_x = 80;
    int y = WINDOW_HEIGHT - 140;

    buttons[0] = make_button(start_x + 0*(btn_w+30), y, btn_w, btn_h, "Sine");
    buttons[1] = make_button(start_x + 1*(btn_w+30), y, btn_w, btn_h, "Square");
    buttons[2] = make_button(start_x + 2*(btn_w+30), y, btn_w, btn_h, "Sawtooth");
    buttons[3] = make_button(start_x + 3*(btn_w+30), y, btn_w, btn_h, "Triangle");
    buttons[4] = make_button(start_x + 4*(btn_w+30) + 50, y, btn_w+40, btn_h, "Play / Pause");
    buttons[5] = make_button(WINDOW_WIDTH - 220, y + 80, 200, 50, "Freq: 440 Hz");
}

void reset_phase() {
    phase_accumulator = 0.0;
}

int main(int argc, char **argv) {
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
    TTF_Init();

    font = TTF_OpenFont("/usr/share/fonts/TTF/DejaVuSans.ttf", 22);
    if (!font) {
        fprintf(stderr, "Font not found. Continuing without text.\n");
    }

    SDL_Window *window = SDL_CreateWindow("Crystal Clear Waveform Editor",
                                          SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN);
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = SAMPLE_RATE;
    want.format = AUDIO_F32;
    want.channels = 1;
    want.samples = BUFFER_SAMPLES;
    want.callback = audio_callback;

    SDL_AudioDeviceID audio_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    SDL_PauseAudioDevice(audio_device, 0);

    buffer_samples = (int)(SAMPLE_RATE * DISPLAY_DURATION);
    waveform_buffer = calloc(buffer_samples, sizeof(float));
    if (!waveform_buffer) return 1;

    // Phase increment: how many buffer positions to advance per audio sample
    phase_increment = (double)buffer_samples / (SAMPLE_RATE * DISPLAY_DURATION);

    current_type = SINE;
    generate_classic_waveform();
    reset_phase();

    init_buttons();

    SDL_bool running = SDL_TRUE;
    SDL_Event event;

    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = SDL_FALSE;
            }
            else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                int mx = event.button.x;
                int my = event.button.y;

                for (int i = 0; i < 4; i++) {
                    if (SDL_PointInRect(&(SDL_Point){mx, my}, &buttons[i].rect)) {
                        current_type = (WaveType)i;
                        generate_classic_waveform();
                        reset_phase();
                        break;
                    }
                }
                if (SDL_PointInRect(&(SDL_Point){mx, my}, &buttons[4].rect)) {
                    playing = !playing;
                    strcpy(buttons[4].label, playing ? "Play / Pause" : "Paused");
                    if (playing) reset_phase();
                }

                if (my >= WAVEFORM_TOP && my < WAVEFORM_TOP + WAVEFORM_HEIGHT) {
                    drawing = 1;
                    current_type = CUSTOM;
                }
            }
            else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
                drawing = 0;
            }
            else if (event.type == SDL_MOUSEMOTION && drawing) {
                int mx = event.motion.x;
                if (mx < 0 || mx >= WINDOW_WIDTH) continue;

                int wave_y_center = WAVEFORM_TOP + WAVEFORM_HEIGHT / 2;
                double norm_y = (wave_y_center - event.motion.y) / (WAVEFORM_HEIGHT * 0.9);
                norm_y = fmax(-1.0, fmin(1.0, norm_y));
                float value = (float)(norm_y * AMPLITUDE);

                int center_idx = (int)((mx / (double)WINDOW_WIDTH) * buffer_samples);
                int radius = buffer_samples / WINDOW_WIDTH * 15;

                for (int i = fmax(0, center_idx - radius); i < fmin(buffer_samples, center_idx + radius + 1); i++) {
                    float dist = fabs(i - center_idx) / (float)radius;
                    if (dist < 1.0) {
                        float weight = 1.0f - dist*dist;
                        waveform_buffer[i] = waveform_buffer[i] * (1.0f - weight) + value * weight;
                    }
                }
            }
            else if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE || event.key.keysym.sym == SDLK_q)
                    running = SDL_FALSE;
                else if (event.key.keysym.sym == SDLK_SPACE) {
                    playing = !playing;
                    if (playing) reset_phase();
                }
                else if (event.key.keysym.sym == SDLK_UP && current_type != CUSTOM) {
                    current_freq *= 1.1;
                    generate_classic_waveform();
                    reset_phase();
                }
                else if (event.key.keysym.sym == SDLK_DOWN && current_type != CUSTOM) {
                    current_freq = fmax(20.0, current_freq / 1.1);
                    generate_classic_waveform();
                    reset_phase();
                }
                else if (event.key.keysym.sym == SDLK_c) {
                    current_type = CUSTOM;
                    memset(waveform_buffer, 0, buffer_samples * sizeof(float));
                    reset_phase();
                }
            }
        }

        // Update labels
        if (current_type == CUSTOM) {
            strcpy(buttons[5].label, "CUSTOM - Drawing");
        } else {
            snprintf(buttons[5].label, 32, "Freq: %.1f Hz", current_freq);
        }

        // Rendering
        SDL_SetRenderDrawColor(renderer, 20, 20, 40, 255);
        SDL_RenderClear(renderer);

        int wave_y_center = WAVEFORM_TOP + WAVEFORM_HEIGHT / 2;
        double scale_x = (double)WINDOW_WIDTH / buffer_samples;
        double scale_y = WAVEFORM_HEIGHT * 0.9;

        SDL_SetRenderDrawColor(renderer, 0, 255, 150, 255);
        for (int i = 1; i < buffer_samples; i += 2) {
            int x1 = (int)((i-1) * scale_x);
            int x2 = (int)(i * scale_x);
            int y1 = wave_y_center - (int)(waveform_buffer[i-1] / AMPLITUDE * scale_y);
            int y2 = wave_y_center - (int)(waveform_buffer[i] / AMPLITUDE * scale_y);
            SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
        }

        SDL_SetRenderDrawColor(renderer, 80, 80, 80, 255);
        SDL_RenderDrawLine(renderer, 0, wave_y_center, WINDOW_WIDTH, wave_y_center);

        // Visual playback cursor - derived from phase accumulator
        if (playing) {
            double visual_pos = phase_accumulator / buffer_samples;
            int cursor_x = (int)(visual_pos * WINDOW_WIDTH);

            SDL_SetRenderDrawColor(renderer, 255, 80, 80, 255);
            for (int o = -3; o <= 3; o++) {
                int x = cursor_x + o;
                if (x >= 0 && x < WINDOW_WIDTH)
                    SDL_RenderDrawLine(renderer, x, WAVEFORM_TOP, x, WAVEFORM_TOP + WAVEFORM_HEIGHT);
            }
        }

        // Buttons rendering (same as before)
        for (int i = 0; i < 6; i++) {
            Button *b = &buttons[i];
            int active = (i < 4 && current_type == i) ||
                         (i == 4 && playing) ||
                         (i == 5 && current_type == CUSTOM);

            SDL_SetRenderDrawColor(renderer, active ? 80 : 60,
                                   active ? 220 : 70,
                                   active ? 140 : 100, 255);
            SDL_RenderFillRect(renderer, &b->rect);
            SDL_SetRenderDrawColor(renderer, 220, 220, 255, 255);
            SDL_RenderDrawRect(renderer, &b->rect);

            if (font) {
                SDL_Surface *surf = TTF_RenderText_Blended(font, b->label, (SDL_Color){255,255,255,255});
                if (surf) {
                    SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
                    SDL_Rect dst = {b->rect.x + (b->rect.w - surf->w)/2,
                                    b->rect.y + (b->rect.h - surf->h)/2,
                                    surf->w, surf->h};
                    SDL_RenderCopy(renderer, tex, NULL, &dst);
                    SDL_DestroyTexture(tex);
                    SDL_FreeSurface(surf);
                }
            }
        }

        if (font) {
            const char *inst = "Click & drag to DRAW | ↑↓ freq | C clear | Space pause";
            SDL_Surface *surf = TTF_RenderText_Blended(font, inst, (SDL_Color){180,220,255,255});
            if (surf) {
                SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
                SDL_Rect r = {20, 45, surf->w, surf->h};
                SDL_RenderCopy(renderer, tex, NULL, &r);
                SDL_DestroyTexture(tex);
                SDL_FreeSurface(surf);
            }
        }

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    SDL_CloseAudioDevice(audio_device);
    free(waveform_buffer);
    if (font) TTF_CloseFont(font);
    TTF_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
