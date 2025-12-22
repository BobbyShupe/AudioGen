#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define INITIAL_WINDOW_WIDTH 1400
#define INITIAL_WINDOW_HEIGHT 800
#define WINDOW_WIDTH 1400
#define WINDOW_HEIGHT 800
#define WAVEFORM_TOP 100
#define WAVEFORM_HEIGHT 340
#define DISPLAY_DURATION 2.0
#define SAMPLE_RATE 48000
#define BUFFER_SAMPLES 1024
#define AMPLITUDE 0.35
#define DEFAULT_FREQ 440.0

typedef enum { SINE, SQUARE, SAWTOOTH, TRIANGLE, CUSTOM } WaveType;
typedef enum { 
    DRAW_FREE, DRAW_LINE, DRAW_SINE, DRAW_SMOOTH,
    DRAW_ADD_FREE, DRAW_ADD_SMOOTH, DRAW_MULTIPLY, DRAW_AMPLIFY,
    DRAW_ADD_SINE, DRAW_ADD_SQUARE, DRAW_ADD_SAW, DRAW_ADD_TRIANGLE,
    DRAW_BLEND, DRAW_SMEAR
} DrawMode;

WaveType current_type = SINE;
DrawMode draw_mode = DRAW_FREE;
double current_freq = DEFAULT_FREQ;
int playing = 1;

double phase_accumulator = 0.0;
double phase_increment = 0.0;

float *waveform_buffer = NULL;
int buffer_samples = 0;

typedef struct {
    SDL_Rect rect;
    char label[32];
} Button;

Button wave_buttons[4];
Button tool_buttons[14];  // Now 14 tools including Amplify
Button control_buttons[2];
Button export_button;
Button intensity_bar;
Button smear_width_bar;

TTF_Font *font = NULL;

float brush_intensity = 0.7f;
float smear_width = 0.5f;

int drawing = 0;
int line_start_idx = -1;
float line_start_val = 0.0f;

int smear_start_idx = -1;
int smear_current_idx = -1;
float smear_max_distance = 0.0f;

SDL_AudioDeviceID audio_device = 0;
SDL_AudioSpec have;

SDL_Window *window = NULL;
SDL_Renderer *renderer = NULL;

int fullscreen = 0;

void reopen_audio_device(void);
void audio_callback(void *userdata, Uint8 *stream, int len);

void generate_classic_waveform() {
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
            case TRIANGLE: sample = (phase < 0.5) ? (4.0 * phase - 1.0) : (3.0 - 4.0 * phase); break;
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
        int idx = (int)phase_accumulator;
        double frac = phase_accumulator - idx;
        idx %= buffer_samples;
        if (idx < 0) idx += buffer_samples;

        float a = waveform_buffer[idx];
        float b = waveform_buffer[(idx + 1) % buffer_samples];
        out[i] = a + (b - a) * (float)frac;

        phase_accumulator += phase_increment;
        if (phase_accumulator >= buffer_samples)
            phase_accumulator -= buffer_samples;
    }
}

void reopen_audio_device(void) {
    if (audio_device != 0) {
        SDL_CloseAudioDevice(audio_device);
        audio_device = 0;
    }

    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq = SAMPLE_RATE;
    want.format = AUDIO_F32;
    want.channels = 1;
    want.samples = BUFFER_SAMPLES;
    want.callback = audio_callback;

    audio_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (audio_device == 0) {
        fprintf(stderr, "Failed to open audio: %s\n", SDL_GetError());
    } else {
        SDL_PauseAudioDevice(audio_device, playing ? 0 : 1);
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
    int btn_w = 105;
    int left_btn_h = 18;                 // Text (~16px) + exactly 2px extra total
    int spacing_horizontal = 15;
    int start_x = 30;

    // Vertical spacing between rows: exactly 2px
    int vertical_step = left_btn_h + 2;   // 18 + 2 = 20px from top of one row to top of next

    // Anchor the bottom row (Row 3) near the bottom of the window
    // Original bottom row started at WINDOW_HEIGHT - 15, we'll keep similar placement
    int y4 = WINDOW_HEIGHT - 15;         // Top of bottom row

    // Calculate upwards
    int y3 = y4 - vertical_step;
    int y2 = y3 - vertical_step;
    int y1 = y2 - vertical_step;         // Wave buttons row

    // Wave types row
    wave_buttons[0] = make_button(start_x + 0*(btn_w+spacing_horizontal), y1, btn_w, left_btn_h, "Sine");
    wave_buttons[1] = make_button(start_x + 1*(btn_w+spacing_horizontal), y1, btn_w, left_btn_h, "Square");
    wave_buttons[2] = make_button(start_x + 2*(btn_w+spacing_horizontal), y1, btn_w, left_btn_h, "Sawtooth");
    wave_buttons[3] = make_button(start_x + 3*(btn_w+spacing_horizontal), y1, btn_w, left_btn_h, "Triangle");

    // Row 1
    tool_buttons[0] = make_button(start_x + 0*(btn_w+spacing_horizontal), y2, btn_w, left_btn_h, "Free Draw");
    tool_buttons[1] = make_button(start_x + 1*(btn_w+spacing_horizontal), y2, btn_w, left_btn_h, "Line");
    tool_buttons[2] = make_button(start_x + 2*(btn_w+spacing_horizontal), y2, btn_w, left_btn_h, "Sine Seg");
    tool_buttons[3] = make_button(start_x + 3*(btn_w+spacing_horizontal), y2, btn_w, left_btn_h, "Smooth");
    tool_buttons[4] = make_button(start_x + 4*(btn_w+spacing_horizontal), y2, btn_w, left_btn_h, "Add Free");

    // Row 2
    tool_buttons[5] = make_button(start_x + 0*(btn_w+spacing_horizontal), y3, btn_w, left_btn_h, "Add Smooth");
    tool_buttons[6] = make_button(start_x + 1*(btn_w+spacing_horizontal), y3, btn_w, left_btn_h, "Multiply");
    tool_buttons[7] = make_button(start_x + 2*(btn_w+spacing_horizontal), y3, btn_w, left_btn_h, "Amplify");
    tool_buttons[8] = make_button(start_x + 3*(btn_w+spacing_horizontal), y3, btn_w, left_btn_h, "Add Sine");
    tool_buttons[9] = make_button(start_x + 4*(btn_w+spacing_horizontal), y3, btn_w, left_btn_h, "Add Square");

    // Row 3 (bottom row)
    tool_buttons[10] = make_button(start_x + 0*(btn_w+spacing_horizontal), y4, btn_w, left_btn_h, "Add Saw");
    tool_buttons[11] = make_button(start_x + 1*(btn_w+spacing_horizontal), y4, btn_w, left_btn_h, "Add Tri");
    tool_buttons[12] = make_button(start_x + 2*(btn_w+spacing_horizontal), y4, btn_w, left_btn_h, "Blend");
    tool_buttons[13] = make_button(start_x + 3*(btn_w+spacing_horizontal), y4, btn_w, left_btn_h, "Smear");

    // Right-side controls — unchanged, keep their original larger sizes
    control_buttons[0] = make_button(WINDOW_WIDTH - 260, WINDOW_HEIGHT - 140, 190, 60, "Play / Pause");
    control_buttons[1] = make_button(WINDOW_WIDTH - 260, WINDOW_HEIGHT - 60, 240, 50, "Freq: 440.0 Hz");
    export_button = make_button(WINDOW_WIDTH - 260, WINDOW_HEIGHT - 220, 240, 60, "Export WAV");
    intensity_bar = make_button(WINDOW_WIDTH - 300, WINDOW_HEIGHT - 200, 250, 20, "Intensity");
    smear_width_bar = make_button(WINDOW_WIDTH - 300, WINDOW_HEIGHT - 150, 250, 20, "Smear Width");
}

void export_wav() {
    uint32_t sample_rate = SAMPLE_RATE;
    static int export_count = 0;
    char filename[64];
    FILE *test;

    do {
        export_count++;
        snprintf(filename, sizeof(filename), "waveform_%03d.wav", export_count);
        test = fopen(filename, "rb");
        if (test) fclose(test);
    } while (test);

    FILE *f = fopen(filename, "wb");
    if (!f) {
        fprintf(stderr, "Could not open %s for writing\n", filename);
        return;
    }

    uint32_t num_samples = buffer_samples;
    uint32_t byte_rate = SAMPLE_RATE * 4;
    uint32_t data_size = num_samples * 4;

    fwrite("RIFF", 1, 4, f);
    uint32_t chunk_size = 36 + data_size;
    fwrite(&chunk_size, 4, 1, f);
    fwrite("WAVE", 1, 4, f);

    fwrite("fmt ", 1, 4, f);
    uint32_t fmt_size = 16;
    fwrite(&fmt_size, 4, 1, f);
    uint16_t audio_format = 3;
    uint16_t num_channels = 1;
    uint16_t bits_per_sample = 32;
    uint16_t block_align = 4;
    fwrite(&audio_format, 2, 1, f);
    fwrite(&num_channels, 2, 1, f);
    fwrite(&sample_rate, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    fwrite(&block_align, 2, 1, f);
    fwrite(&bits_per_sample, 2, 1, f);

    fwrite("data", 1, 4, f);
    fwrite(&data_size, 4, 1, f);
    fwrite(waveform_buffer, 4, num_samples, f);

    fclose(f);

    snprintf(export_button.label, 32, "Saved %03d.wav", export_count);
}

void apply_smear(int curr_idx) {
    if (smear_start_idx == -1) return;

    int direction = (curr_idx > smear_start_idx) ? 1 : -1;
    int current_distance = abs(curr_idx - smear_start_idx);

    if (current_distance < 30) return;

    if (current_distance > smear_max_distance) {
        smear_max_distance = current_distance;
    }

    float max_fade = buffer_samples * 0.35f;
    float fade_factor = 1.0f - fmin(1.0f, smear_max_distance / max_fade);
    float intensity = brush_intensity * fade_factor * 1.2f;

    if (intensity <= 0.01f) return;

    int copy_half_len = (int)(50 + smear_width * 350);
    int capture_start = smear_start_idx - copy_half_len;
    int capture_end = smear_start_idx + copy_half_len;
    capture_start = fmax(0, capture_start);
    capture_end = fmin(buffer_samples - 1, capture_end);
    int copy_len = capture_end - capture_start + 1;

    int paste_start = smear_start_idx + direction * current_distance;

    for (int offset = 0; offset < copy_len; offset++) {
        int src_idx = capture_start + offset;
        int dst_idx = paste_start + offset * direction;

        if (dst_idx < 0 || dst_idx >= buffer_samples) continue;

        float dist_ratio = (float)abs(dst_idx - smear_start_idx) / (current_distance + copy_len);
        float weight = intensity * (1.0f - dist_ratio);

        if (weight > 0.01f) {
            float copied = waveform_buffer[src_idx];
            waveform_buffer[dst_idx] += copied * weight;

            if (waveform_buffer[dst_idx] > AMPLITUDE) waveform_buffer[dst_idx] = AMPLITUDE;
            if (waveform_buffer[dst_idx] < -AMPLITUDE) waveform_buffer[dst_idx] = -AMPLITUDE;
        }
    }
}

void apply_brush(int center_idx, float target_val, int radius, float base_strength, int mode) {
    float strength = base_strength * brush_intensity;
    int start = fmax(0, center_idx - radius);
    int end = fmin(buffer_samples - 1, center_idx + radius);

    for (int i = start; i <= end; i++) {
        float dist = fabsf(i - center_idx) / (float)radius;
        if (dist < 1.0f) {
            float weight = strength * (1.0f - dist * dist);
            float current = waveform_buffer[i];
            float new_val;
            if (mode == 0) new_val = current * (1.0f - weight) + target_val * weight;
            else if (mode == 1) new_val = current + target_val * weight;
            else new_val = current + (target_val - current) * weight * 0.7f;

            if (new_val > AMPLITUDE) new_val = AMPLITUDE;
            if (new_val < -AMPLITUDE) new_val = -AMPLITUDE;
            waveform_buffer[i] = new_val;
        }
    }
}

void apply_multiply(int center_idx, float factor, int radius) {
    float strength = brush_intensity;
    int start = fmax(0, center_idx - radius);
    int end = fmin(buffer_samples - 1, center_idx + radius);

    for (int i = start; i <= end; i++) {
        float dist = fabsf(i - center_idx) / (float)radius;
        if (dist < 1.0f) {
            float weight = strength * (1.0f - dist * dist);
            waveform_buffer[i] *= (1.0f + (factor - 1.0f) * weight);
            if (waveform_buffer[i] > AMPLITUDE) waveform_buffer[i] = AMPLITUDE;
            if (waveform_buffer[i] < -AMPLITUDE) waveform_buffer[i] = -AMPLITUDE;
        }
    }
}

void apply_additive_wave(int center_idx, float pitch_norm, int radius, int wave_type) {
    float strength = brush_intensity * 0.8f;
    int start = fmax(0, center_idx - radius);
    int end = fmin(buffer_samples - 1, center_idx + radius);

    double base_freq = 50.0 + pitch_norm * 400.0;

    for (int i = start; i <= end; i++) {
        float dist = fabsf(i - center_idx) / (float)radius;
        if (dist >= 1.0f) continue;
        float weight = strength * (1.0f - dist * dist);

        double pos = (i - start) / (double)(end - start + 1);
        double phase = pos * 2.0 * M_PI;

        float sample = 0.0f;
        if (wave_type == 0) sample = (float)sin(phase + base_freq * pos * 0.1);
        else if (wave_type == 1) sample = (fmod(phase * base_freq * 0.05, 2.0 * M_PI) < M_PI) ? 1.0f : -1.0f;
        else if (wave_type == 2) sample = (float)(2.0 * fmod(phase * base_freq * 0.05 / (2.0 * M_PI), 1.0) - 1.0);
        else if (wave_type == 3) {
            double tri = fmod(phase * base_freq * 0.05 / (2.0 * M_PI), 1.0);
            sample = (tri < 0.5) ? (4.0f * tri - 1.0f) : (3.0f - 4.0f * tri);
        }

        waveform_buffer[i] += sample * weight * AMPLITUDE * 0.6f;

        if (waveform_buffer[i] > AMPLITUDE) waveform_buffer[i] = AMPLITUDE;
        if (waveform_buffer[i] < -AMPLITUDE) waveform_buffer[i] = -AMPLITUDE;
    }
}

void draw_line(int start_idx, float start_val, int end_idx, float end_val) {
    int steps = abs(end_idx - start_idx);
    if (steps == 0) return;

    float dx = (float)(end_idx - start_idx);
    for (int i = 0; i <= steps; i++) {
        float t = i / (float)steps;
        int idx = (int)roundf(start_idx + t * dx);
        if (idx < 0 || idx >= buffer_samples) continue;
        float val = start_val + t * (end_val - start_val);
        waveform_buffer[idx] = val * brush_intensity + waveform_buffer[idx] * (1.0f - brush_intensity);
    }
}

void draw_sine_segment(int start_idx, float start_val, int end_idx, float end_val, int additive) {
    int steps = abs(end_idx - start_idx);
    if (steps < 10) {
        draw_line(start_idx, start_val, end_idx, end_val);
        return;
    }

    float offset = (start_val + end_val) / 2.0f;
    float amplitude = fabsf(start_val - end_val) / 2.0f + 0.05f * AMPLITUDE;

    float dx = (float)(end_idx - start_idx);
    for (int i = 0; i <= steps; i++) {
        float t = i / (float)steps;
        int idx = (int)roundf(start_idx + t * dx);
        if (idx < 0 || idx >= buffer_samples) continue;
        double phase = t * 2.0 * M_PI;
        float val = offset + (float)sin(phase) * amplitude;
        if (additive) {
            waveform_buffer[idx] += val * brush_intensity;
        } else {
            waveform_buffer[idx] = val * brush_intensity + waveform_buffer[idx] * (1.0f - brush_intensity);
        }
        if (waveform_buffer[idx] > AMPLITUDE) waveform_buffer[idx] = AMPLITUDE;
        if (waveform_buffer[idx] < -AMPLITUDE) waveform_buffer[idx] = -AMPLITUDE;
    }
}

void render_buttons(SDL_Renderer *renderer, Button *buttons, int count, int active_idx) {
    for (int i = 0; i < count; i++) {
        Button *b = &buttons[i];
        int active = (i == active_idx);
        SDL_SetRenderDrawColor(renderer, active ? 100 : 60,
                               active ? 220 : 70,
                               active ? 160 : 100, 255);
        SDL_RenderFillRect(renderer, &b->rect);
        SDL_SetRenderDrawColor(renderer, 220, 220, 255, 255);
        SDL_RenderDrawRect(renderer, &b->rect);

        if (font) {
            SDL_Surface *surf = TTF_RenderText_Shaded(font, b->label, (SDL_Color){255,255,255,255}, (SDL_Color){0,0,0,0});
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
}

void toggle_fullscreen() {
    fullscreen = !fullscreen;
    if (fullscreen) {
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    } else {
        SDL_SetWindowFullscreen(window, 0);
        SDL_SetWindowSize(window, INITIAL_WINDOW_WIDTH, INITIAL_WINDOW_HEIGHT);
        SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
}

int main(int argc, char **argv) {
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
    TTF_Init();

    font = TTF_OpenFont("/usr/share/fonts/TTF/DejaVuSans.ttf", 14);
    if (!font) fprintf(stderr, "Font not loaded.\n");

    window = SDL_CreateWindow("Waveform Editor - Amplify Added",
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              INITIAL_WINDOW_WIDTH, INITIAL_WINDOW_HEIGHT,
                              SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

    buffer_samples = (int)(SAMPLE_RATE * DISPLAY_DURATION);
    waveform_buffer = calloc(buffer_samples, sizeof(float));
    phase_increment = (double)buffer_samples / (SAMPLE_RATE * DISPLAY_DURATION);

    generate_classic_waveform();
    phase_accumulator = 0.0;

    init_buttons();

    reopen_audio_device();

    SDL_bool running = SDL_TRUE;
    SDL_Event event;

    static Uint32 export_time = 0;

    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = SDL_FALSE;

            else if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_f || event.key.keysym.sym == SDLK_F11) {
                    toggle_fullscreen();
                }
                else if (event.key.keysym.sym == SDLK_ESCAPE || event.key.keysym.sym == SDLK_q) running = SDL_FALSE;
                else if (event.key.keysym.sym == SDLK_SPACE) {
                    playing = !playing;
                    if (playing) phase_accumulator = 0.0;
                }
                else if (event.key.keysym.sym == SDLK_c) {
                    current_type = CUSTOM;
                    memset(waveform_buffer, 0, buffer_samples * sizeof(float));
                }
                else if (current_type != CUSTOM) {
                    if (event.key.keysym.sym == SDLK_UP) {
                        current_freq *= 1.1;
                        generate_classic_waveform();
                        phase_accumulator = 0.0;
                    }
                    if (event.key.keysym.sym == SDLK_DOWN) {
                        current_freq = fmax(20.0, current_freq / 1.1);
                        generate_classic_waveform();
                        phase_accumulator = 0.0;
                    }
                }
            }

            else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                int mx = event.button.x;
                int my = event.button.y;

                int button_clicked = 0;

                for (int i = 0; i < 4; i++) {
                    if (SDL_PointInRect(&(SDL_Point){mx,my}, &wave_buttons[i].rect)) {
                        current_type = (WaveType)i;
                        generate_classic_waveform();
                        phase_accumulator = 0.0;
                        button_clicked = 1;
                    }
                }

                for (int i = 0; i < 14; i++) {
                    if (SDL_PointInRect(&(SDL_Point){mx,my}, &tool_buttons[i].rect)) {
                        draw_mode = (DrawMode)i;
                        line_start_idx = -1;
                        smear_start_idx = -1;
                        smear_max_distance = 0.0f;
                        button_clicked = 1;
                    }
                }

                if (SDL_PointInRect(&(SDL_Point){mx,my}, &control_buttons[0].rect)) {
                    playing = !playing;
                    snprintf(control_buttons[0].label, 32, playing ? "Play / Pause" : "Paused");
                    if (playing) phase_accumulator = 0.0;
                    button_clicked = 1;
                }

                if (SDL_PointInRect(&(SDL_Point){mx,my}, &export_button.rect)) {
                    export_wav();
                    export_time = SDL_GetTicks();
                    button_clicked = 1;
                }

                if (SDL_PointInRect(&(SDL_Point){mx,my}, &intensity_bar.rect)) {
                    brush_intensity = (mx - intensity_bar.rect.x) / (float)intensity_bar.rect.w;
                    brush_intensity = fmax(0.0f, fmin(1.0f, brush_intensity));
                    button_clicked = 1;
                }

                if (SDL_PointInRect(&(SDL_Point){mx,my}, &smear_width_bar.rect)) {
                    smear_width = (mx - smear_width_bar.rect.x) / (float)smear_width_bar.rect.w;
                    smear_width = fmax(0.0f, fmin(1.0f, smear_width));
                    button_clicked = 1;
                }

                if (!button_clicked && my >= WAVEFORM_TOP && my < WAVEFORM_TOP + WAVEFORM_HEIGHT) {
                    current_type = CUSTOM;

                    int idx = (int)((mx / (double)WINDOW_WIDTH) * buffer_samples);

                    if (draw_mode == DRAW_LINE || draw_mode == DRAW_SINE) {
                        if (line_start_idx == -1) {
                            line_start_idx = idx;
                            int wave_y_center = WAVEFORM_TOP + WAVEFORM_HEIGHT / 2;
                            double norm_y = (wave_y_center - my) / (WAVEFORM_HEIGHT * 0.9);
                            norm_y = fmax(-1.0, fmin(1.0, norm_y));
                            line_start_val = (float)(norm_y * AMPLITUDE * 0.8);
                        } else {
                            int wave_y_center = WAVEFORM_TOP + WAVEFORM_HEIGHT / 2;
                            double norm_y = (wave_y_center - my) / (WAVEFORM_HEIGHT * 0.9);
                            norm_y = fmax(-1.0, fmin(1.0, norm_y));
                            float end_val = (float)(norm_y * AMPLITUDE * 0.8);
                            if (draw_mode == DRAW_LINE)
                                draw_line(line_start_idx, line_start_val, idx, end_val);
                            else
                                draw_sine_segment(line_start_idx, line_start_val, idx, end_val, 0);
                            line_start_idx = -1;
                        }
                    } else {
                        drawing = 1;
                        if (draw_mode == DRAW_SMEAR) {
                            smear_start_idx = idx;
                            smear_current_idx = idx;
                            smear_max_distance = 0.0f;
                        }
                    }
                }
            }
            else if (event.type == SDL_MOUSEMOTION && event.motion.state & SDL_BUTTON_LMASK) {
                int mx = event.motion.x;
                int my = event.motion.y;

                if (SDL_PointInRect(&(SDL_Point){mx,my}, &intensity_bar.rect)) {
                    brush_intensity = (mx - intensity_bar.rect.x) / (float)intensity_bar.rect.w;
                    brush_intensity = fmax(0.0f, fmin(1.0f, brush_intensity));
                }

                if (SDL_PointInRect(&(SDL_Point){mx,my}, &smear_width_bar.rect)) {
                    smear_width = (mx - smear_width_bar.rect.x) / (float)smear_width_bar.rect.w;
                    smear_width = fmax(0.0f, fmin(1.0f, smear_width));
                }

                if (drawing) {
                    int idx = (int)((mx / (double)WINDOW_WIDTH) * buffer_samples);

                    if (draw_mode == DRAW_SMEAR) {
                        smear_current_idx = idx;
                        apply_smear(idx);
                    } else if (draw_mode >= DRAW_ADD_SINE && draw_mode <= DRAW_ADD_TRIANGLE) {
                        int wave_y_center = WAVEFORM_TOP + WAVEFORM_HEIGHT / 2;
                        double norm_y = (wave_y_center - my) / (WAVEFORM_HEIGHT * 0.9);
                        norm_y = fmax(-1.0, fmin(1.0, norm_y));
                        float pitch_norm = (norm_y + 1.0) / 2.0;
                        int radius = buffer_samples / WINDOW_WIDTH * 30;
                        int wave_type = draw_mode - DRAW_ADD_SINE;
                        apply_additive_wave(idx, pitch_norm, radius, wave_type);
                    } else if (draw_mode == DRAW_MULTIPLY || draw_mode == DRAW_AMPLIFY) {
                        int wave_y_center = WAVEFORM_TOP + WAVEFORM_HEIGHT / 2;
                        double norm_y = (wave_y_center - my) / (WAVEFORM_HEIGHT * 0.9);
                        norm_y = fmax(-1.0, fmin(1.0, norm_y));

                        float factor;
                        if (draw_mode == DRAW_AMPLIFY) {
                            factor = (norm_y > 0) ? (1.0f + norm_y * 3.0f) : (1.0f + norm_y * 0.8f);
                        } else {
                            factor = (norm_y > 0) ? 1.5f : 0.7f;
                        }
                        int radius = buffer_samples / WINDOW_WIDTH * 25;
                        apply_multiply(idx, factor, radius);
                    } else if (draw_mode != DRAW_LINE && draw_mode != DRAW_SINE) {
                        int wave_y_center = WAVEFORM_TOP + WAVEFORM_HEIGHT / 2;
                        double norm_y = (wave_y_center - my) / (WAVEFORM_HEIGHT * 0.9);
                        norm_y = fmax(-1.0, fmin(1.0, norm_y));
                        float value = (float)(norm_y * AMPLITUDE * 0.8);
                        int radius = buffer_samples / WINDOW_WIDTH * ((draw_mode == DRAW_SMOOTH || draw_mode == DRAW_ADD_SMOOTH || draw_mode == DRAW_BLEND) ? 25 : 15);
                        float strength = (draw_mode == DRAW_SMOOTH || draw_mode == DRAW_ADD_SMOOTH || draw_mode == DRAW_BLEND) ? 0.6f : 1.0f;
                        int mode = (draw_mode == DRAW_BLEND) ? 2 : (draw_mode == DRAW_ADD_FREE || draw_mode == DRAW_ADD_SMOOTH) ? 1 : 0;
                        apply_brush(idx, value, radius, strength, mode);
                    }
                }
            }
            else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
                drawing = 0;
                smear_start_idx = -1;
                smear_max_distance = 0.0f;
            }
        }

        if (export_time > 0 && SDL_GetTicks() - export_time > 2000) {
            strncpy(export_button.label, "Export WAV", 31);
            export_time = 0;
        }

        if (current_type == CUSTOM)
            snprintf(control_buttons[1].label, 32, "CUSTOM - All Tools Ready!");
        else
            snprintf(control_buttons[1].label, 32, "Freq: %.1f Hz", current_freq);

        SDL_SetRenderDrawColor(renderer, 20, 20, 40, 255);
        SDL_RenderClear(renderer);

        int wave_y_center = WAVEFORM_TOP + WAVEFORM_HEIGHT / 2;
        double scale_x = (double)WINDOW_WIDTH / buffer_samples;
        double scale_y = WAVEFORM_HEIGHT * 0.9;

        SDL_SetRenderDrawColor(renderer, 0, 255, 200, 255);
        for (int i = 1; i < buffer_samples; i += 2) {
            int x1 = (int)((i-1) * scale_x);
            int x2 = (int)(i * scale_x);
            int y1 = wave_y_center - (int)(waveform_buffer[i-1] / AMPLITUDE * scale_y);
            int y2 = wave_y_center - (int)(waveform_buffer[i] / AMPLITUDE * scale_y);
            SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
        }

        SDL_SetRenderDrawColor(renderer, 80, 80, 80, 255);
        SDL_RenderDrawLine(renderer, 0, wave_y_center, WINDOW_WIDTH, wave_y_center);

        if (playing) {
            double pos = phase_accumulator / buffer_samples;
            int cursor_x = (int)(pos * WINDOW_WIDTH);
            SDL_SetRenderDrawColor(renderer, 255, 80, 80, 255);
            for (int o = -3; o <= 3; o++) {
                int x = cursor_x + o;
                if (x >= 0 && x < WINDOW_WIDTH)
                    SDL_RenderDrawLine(renderer, x, WAVEFORM_TOP, x, WAVEFORM_TOP + WAVEFORM_HEIGHT);
            }
        }

        render_buttons(renderer, wave_buttons, 4, current_type);
        render_buttons(renderer, tool_buttons, 14, draw_mode);
        render_buttons(renderer, control_buttons, 2, playing ? 0 : -1);

        // Export button
        SDL_SetRenderDrawColor(renderer, 80, 180, 100, 255);
        SDL_RenderFillRect(renderer, &export_button.rect);
        SDL_SetRenderDrawColor(renderer, 220, 220, 255, 255);
        SDL_RenderDrawRect(renderer, &export_button.rect);

        if (font) {
            SDL_Surface *surf = TTF_RenderText_Shaded(font, export_button.label, (SDL_Color){255,255,255,255}, (SDL_Color){0,0,0,0});
            if (surf) {
                SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
                SDL_Rect dst = {export_button.rect.x + (export_button.rect.w - surf->w)/2,
                                export_button.rect.y + (export_button.rect.h - surf->h)/2,
                                surf->w, surf->h};
                SDL_RenderCopy(renderer, tex, NULL, &dst);
                SDL_DestroyTexture(tex);
                SDL_FreeSurface(surf);
            }
        }

        // Intensity bar
        SDL_SetRenderDrawColor(renderer, 70, 70, 100, 255);
        SDL_RenderFillRect(renderer, &intensity_bar.rect);
        SDL_SetRenderDrawColor(renderer, 100, 200, 255, 255);
        SDL_Rect fill = intensity_bar.rect;
        fill.w = (int)(fill.w * brush_intensity);
        SDL_RenderFillRect(renderer, &fill);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderDrawRect(renderer, &intensity_bar.rect);

        // Smear Width bar
        SDL_SetRenderDrawColor(renderer, 70, 70, 100, 255);
        SDL_RenderFillRect(renderer, &smear_width_bar.rect);
        SDL_SetRenderDrawColor(renderer, 255, 150, 100, 255);
        fill = smear_width_bar.rect;
        fill.w = (int)(fill.w * smear_width);
        SDL_RenderFillRect(renderer, &fill);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderDrawRect(renderer, &smear_width_bar.rect);

        if (font) {
            char intens_text[32];
            snprintf(intens_text, 32, "Brush Intensity: %.0f%%", brush_intensity * 100);
            SDL_Surface *surf = TTF_RenderText_Shaded(font, intens_text, (SDL_Color){200,255,200,255}, (SDL_Color){0,0,0,0});
            if (surf) {
                SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
                SDL_Rect r = {intensity_bar.rect.x, intensity_bar.rect.y - 30, surf->w, surf->h};
                SDL_RenderCopy(renderer, tex, NULL, &r);
                SDL_DestroyTexture(tex);
                SDL_FreeSurface(surf);
            }

            int copy_samples = (int)(50 + smear_width * 350);
            char width_text[32];
            snprintf(width_text, 32, "Smear Width: %d samples", copy_samples);
            surf = TTF_RenderText_Shaded(font, width_text, (SDL_Color){255,200,100,255}, (SDL_Color){0,0,0,0});
            if (surf) {
                SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
                SDL_Rect r = {smear_width_bar.rect.x, smear_width_bar.rect.y - 30, surf->w, surf->h};
                SDL_RenderCopy(renderer, tex, NULL, &r);
                SDL_DestroyTexture(tex);
                SDL_FreeSurface(surf);
            }

            const char *inst = "Amplify tool added — brush up to boost volume!";
            surf = TTF_RenderText_Shaded(font, inst, (SDL_Color){100,255,200,255}, (SDL_Color){0,0,0,0});
            if (surf) {
                SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
                SDL_Rect r = {20, 50, surf->w, surf->h};
                SDL_RenderCopy(renderer, tex, NULL, &r);
                SDL_DestroyTexture(tex);
                SDL_FreeSurface(surf);
            }
        }

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    if (audio_device != 0) SDL_CloseAudioDevice(audio_device);
    free(waveform_buffer);
    if (font) TTF_CloseFont(font);
    TTF_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
