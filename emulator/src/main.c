#define _POSIX_C_SOURCE 200809L

#include <SDL.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DISPLAY_INTERVAL 100
#define CLOCK_SPEED 6000
#define GPR_COUNT 5
#define PAGE_SIZE 256
#define CANVAS_W 256
#define CANVAS_H 256
#define WINDOW_W 512
#define WINDOW_H 512

typedef struct {
    uint8_t opcode;
    int op1;
    int op2;
} Instr;

typedef struct {
    uint8_t **pages;
    size_t page_count;
} Ram;

typedef struct {
    int program_counter;
    int gpr[GPR_COUNT];
    int accumulator;
    Ram ram;
    int clock_speed;
    Instr *program;
    size_t program_len;
    size_t program_cap;
    int running;
    unsigned long cycle_count;

    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *canvas_tex;
    SDL_Surface *canvas;
} Emulator;

static void ram_init(Ram *r) {
    r->pages = NULL;
    r->page_count = 0;
}

static void ram_free(Ram *r) {
    for (size_t i = 0; i < r->page_count; i++) {
        free(r->pages[i]);
    }
    free(r->pages);
    r->pages = NULL;
    r->page_count = 0;
}

static int ram_ensure_page(Ram *r, size_t page) {
    if (page < r->page_count) {
        return 0;
    }
    size_t need = page - r->page_count + 1;
    size_t new_count = r->page_count + need;
    uint8_t **np = (uint8_t **)realloc(r->pages, new_count * sizeof(uint8_t *));
    if (!np) {
        return -1;
    }
    r->pages = np;
    for (size_t i = r->page_count; i < new_count; i++) {
        r->pages[i] = (uint8_t *)calloc(PAGE_SIZE, 1);
        if (!r->pages[i]) {
            for (size_t j = r->page_count; j < i; j++) {
                free(r->pages[j]);
            }
            r->pages = (uint8_t **)realloc(r->pages, r->page_count * sizeof(uint8_t *));
            return -1;
        }
    }
    r->page_count = new_count;
    return 0;
}

static void show_state(const Emulator *e) {
#ifdef _WIN32
    (void)system("cls");
#else
    fputs("\033[2J\033[H", stdout);
#endif
    fprintf(stdout, "PC: %02X\n", e->program_counter);
    fputs("Registers: [", stdout);
    for (int i = 0; i < GPR_COUNT; i++) {
        fprintf(stdout, "%02X%s", (unsigned)(e->gpr[i] & 0xFF), i + 1 < GPR_COUNT ? ", " : "");
    }
    fprintf(stdout, "]\nAccumulator: %02X\n", (unsigned)(e->accumulator & 0xFF));
    fflush(stdout);
}

static int load_program(Emulator *e, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        perror(path);
        return -1;
    }
    char line[512];
    while (fgets(line, sizeof line, f)) {
        char *semi = strchr(line, ';');
        if (semi) {
            *semi = '\0';
        }
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r' || line[n - 1] == ' ' || line[n - 1] == '\t')) {
            line[--n] = '\0';
        }
        char *p = line;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0') {
            continue;
        }

        char *tok1 = strtok(p, " \t");
        if (!tok1) {
            continue;
        }
        char *tok2 = strtok(NULL, " \t");
        char *tok3 = strtok(NULL, " \t");

        char *endp;
        errno = 0;
        long op = strtol(tok1, &endp, 16);
        if (errno || *endp != '\0' || op < 0 || op > 0xFF) {
            continue;
        }
        long o1 = 0, o2 = 0;
        if (tok2) {
            errno = 0;
            o1 = strtol(tok2, &endp, 16);
            if (errno || *endp != '\0') {
                continue;
            }
        }
        if (tok3) {
            errno = 0;
            o2 = strtol(tok3, &endp, 16);
            if (errno || *endp != '\0') {
                continue;
            }
        }

        if (e->program_len >= e->program_cap) {
            size_t nc = e->program_cap ? e->program_cap * 2 : 64;
            Instr *np = (Instr *)realloc(e->program, nc * sizeof(Instr));
            if (!np) {
                fclose(f);
                return -1;
            }
            e->program = np;
            e->program_cap = nc;
        }
        e->program[e->program_len].opcode = (uint8_t)op;
        e->program[e->program_len].op1 = (int)o1;
        e->program[e->program_len].op2 = (int)o2;
        e->program_len++;
    }
    fclose(f);
    return 0;
}

static void fill_circle_hline(Uint32 *pixels, int pitch_px, int surf_w, int surf_h, Uint32 color,
                              int x0, int x1, int yy) {
    if (yy < 0 || yy >= surf_h) {
        return;
    }
    if (x0 > x1) {
        int t = x0;
        x0 = x1;
        x1 = t;
    }
    if (x0 < 0) {
        x0 = 0;
    }
    if (x1 >= surf_w) {
        x1 = surf_w - 1;
    }
    for (int xi = x0; xi <= x1; xi++) {
        pixels[yy * pitch_px + xi] = color;
    }
}

static void sleep_seconds(double sec) {
    if (sec <= 0.0) {
        return;
    }
    struct timespec ts;
    ts.tv_sec = (time_t)sec;
    ts.tv_nsec = (long)((sec - (double)ts.tv_sec) * 1e9);
    while (ts.tv_nsec >= 1000000000L) {
        ts.tv_nsec -= 1000000000L;
        ts.tv_sec++;
    }
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {
    }
}

static void draw_filled_circle_midpoint(SDL_Surface *surf, int cx, int cy, int rad,
                                        Uint8 R, Uint8 G, Uint8 B) {
    if (rad < 0) {
        return;
    }
    if (rad == 0) {
        if (cx >= 0 && cy >= 0 && cx < surf->w && cy < surf->h) {
            Uint32 c = SDL_MapRGBA(surf->format, R, G, B, 255);
            Uint32 *pixels = (Uint32 *)surf->pixels;
            int pitch = surf->pitch / (int)sizeof(Uint32);
            pixels[cy * pitch + cx] = c;
        }
        return;
    }

    int x = 0;
    int y = rad;
    int d = 3 - 2 * rad;

    Uint32 color = SDL_MapRGBA(surf->format, R, G, B, 255);
    Uint32 *pixels = (Uint32 *)surf->pixels;
    int pitch = surf->pitch / (int)sizeof(Uint32);

    while (y >= x) {
        fill_circle_hline(pixels, pitch, surf->w, surf->h, color, cx - x, cx + x, cy + y);
        fill_circle_hline(pixels, pitch, surf->w, surf->h, color, cx - x, cx + x, cy - y);
        fill_circle_hline(pixels, pitch, surf->w, surf->h, color, cx - y, cx + y, cy + x);
        fill_circle_hline(pixels, pitch, surf->w, surf->h, color, cx - y, cx + y, cy - x);
        x++;
        if (d > 0) {
            y--;
            d = d + 4 * (x - y) + 10;
        } else {
            d = d + 4 * x + 6;
        }
    }
}

static int emu_execute_batch(Emulator *e, int count) {
    int executed = 0;
    int running = e->running;
    int pc = e->program_counter;
    int acc = e->accumulator;

    while (executed < count && running) {
        if ((size_t)pc >= e->program_len) {
            running = 0;
            break;
        }

        Instr ins = e->program[pc];
        uint8_t opcode = ins.opcode;
        int operand1 = ins.op1;
        int operand2 = ins.op2;
        executed++;

        if (opcode == 0x00) {
            e->gpr[operand1 - 1] = operand2;
            pc++;
        } else if (opcode == 0x01) {
            pc = operand1;
        } else if (opcode == 0x08) {
            pc = acc >= 1 ? operand1 : pc + 1;
        } else if (opcode == 0x09) {
            pc = acc == 0 ? operand1 : pc + 1;
        } else if (opcode == 0x02) {
            acc = e->gpr[operand1 - 1] + operand2;
            pc++;
        } else if (opcode == 0x03) {
            acc = e->gpr[operand1 - 1] - operand2;
            pc++;
        } else if (opcode == 0x04) {
            acc = e->gpr[operand1 - 1] * operand2;
            pc++;
        } else if (opcode == 0x05) {
            acc = e->gpr[operand1 - 1] / operand2;
            pc++;
        } else if (opcode == 0x06) {
            acc = e->gpr[operand1 - 1];
            pc++;
        } else if (opcode == 0x07) {
            e->gpr[operand1 - 1] = acc;
            pc++;
        } else if (opcode == 0x0A) {
            if (ram_ensure_page(&e->ram, (size_t)operand1) != 0) {
                running = 0;
                break;
            }
            if (operand2 >= 0 && operand2 < PAGE_SIZE) {
                e->ram.pages[(size_t)operand1][(size_t)operand2] = (uint8_t)(acc & 0xFF);
            }
            pc++;
        } else if (opcode == 0x0B) {
            if (ram_ensure_page(&e->ram, (size_t)operand1) != 0) {
                running = 0;
                break;
            }
            if (operand2 >= 0 && operand2 < PAGE_SIZE) {
                acc = e->ram.pages[(size_t)operand1][(size_t)operand2];
            }
            pc++;
        } else if (opcode == 0x0C) {
            SDL_FillRect(e->canvas, NULL, SDL_MapRGBA(e->canvas->format, 0, 0, 0, 255));
            pc++;
        } else if (opcode == 0x0D) {
            if (SDL_MUSTLOCK(e->canvas)) {
                SDL_LockSurface(e->canvas);
            }
            draw_filled_circle_midpoint(e->canvas, e->gpr[3], e->gpr[4], 1,
                                        (Uint8)(e->gpr[0] & 0xFF), (Uint8)(e->gpr[1] & 0xFF), (Uint8)(e->gpr[2] & 0xFF));
            if (SDL_MUSTLOCK(e->canvas)) {
                SDL_UnlockSurface(e->canvas);
            }
            pc++;
        } else {
            pc++;
        }
    }

    e->program_counter = pc;
    e->accumulator = acc;
    e->cycle_count += (unsigned long)executed;
    e->running = running;
    return executed;
}

static void emulator_destroy(Emulator *e) {
    if (e->canvas_tex) {
        SDL_DestroyTexture(e->canvas_tex);
        e->canvas_tex = NULL;
    }
    if (e->canvas) {
        SDL_FreeSurface(e->canvas);
        e->canvas = NULL;
    }
    if (e->renderer) {
        SDL_DestroyRenderer(e->renderer);
        e->renderer = NULL;
    }
    if (e->window) {
        SDL_DestroyWindow(e->window);
        e->window = NULL;
    }
    free(e->program);
    e->program = NULL;
    e->program_len = e->program_cap = 0;
    ram_free(&e->ram);
}

static int emulator_run(Emulator *e) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    e->window = SDL_CreateWindow("E8 Emulator", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                 WINDOW_W, WINDOW_H, 0);
    if (!e->window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    e->renderer = SDL_CreateRenderer(e->window, -1, SDL_RENDERER_ACCELERATED);
    if (!e->renderer) {
        e->renderer = SDL_CreateRenderer(e->window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!e->renderer) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(e->window);
        e->window = NULL;
        SDL_Quit();
        return 1;
    }

    e->canvas = SDL_CreateRGBSurfaceWithFormat(0, CANVAS_W, CANVAS_H, 32, SDL_PIXELFORMAT_RGBA32);
    if (!e->canvas) {
        fprintf(stderr, "SDL_CreateRGBSurfaceWithFormat: %s\n", SDL_GetError());
        emulator_destroy(e);
        SDL_Quit();
        return 1;
    }
    SDL_FillRect(e->canvas, NULL, SDL_MapRGBA(e->canvas->format, 0, 0, 0, 255));

    e->canvas_tex = SDL_CreateTexture(e->renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                                       CANVAS_W, CANVAS_H);
    if (!e->canvas_tex) {
        fprintf(stderr, "SDL_CreateTexture(canvas): %s\n", SDL_GetError());
        emulator_destroy(e);
        SDL_Quit();
        return 1;
    }
    SDL_SetTextureBlendMode(e->canvas_tex, SDL_BLENDMODE_BLEND);

    e->running = 1;
    const double sleep_duration = 1.0 / (double)e->clock_speed;
    const int use_sleep = sleep_duration >= 1e-9;

    while (e->running) {
        Uint64 start_ticks = SDL_GetPerformanceCounter();

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                e->running = 0;
            }
        }
        if (!e->running) {
            break;
        }

        emu_execute_batch(e, DISPLAY_INTERVAL);

        if (e->cycle_count % (unsigned long)DISPLAY_INTERVAL == 0UL) {
            if (SDL_UpdateTexture(e->canvas_tex, NULL, e->canvas->pixels, e->canvas->pitch) != 0) {
                fprintf(stderr, "SDL_UpdateTexture: %s\n", SDL_GetError());
            }
            SDL_RenderClear(e->renderer);
            SDL_Rect dst = {0, 0, WINDOW_W, WINDOW_H};
            SDL_RenderCopy(e->renderer, e->canvas_tex, NULL, &dst);
            SDL_RenderPresent(e->renderer);
            show_state(e);
        }

        if (use_sleep) {
            Uint64 freq = SDL_GetPerformanceFrequency();
            double elapsed = (double)(SDL_GetPerformanceCounter() - start_ticks) / (double)freq;
            if (elapsed < sleep_duration) {
                sleep_seconds(sleep_duration - elapsed);
            }
        }
    }

    emulator_destroy(e);
    SDL_Quit();
    return 0;
}

int main(int argc, char **argv) {
    const char *prog_path = "tests/e8.bin";
    if (argc > 1) {
        prog_path = argv[1];
    }

    Emulator e;
    memset(&e, 0, sizeof e);
    e.program_counter = 0x00;
    for (int i = 0; i < GPR_COUNT; i++) {
        e.gpr[i] = 0x00;
    }
    e.accumulator = 0x00;
    ram_init(&e.ram);
    if (ram_ensure_page(&e.ram, 0) != 0) {
        fprintf(stderr, "initial RAM page OOM\n");
        return 1;
    }
    e.clock_speed = CLOCK_SPEED;
    e.program = NULL;
    e.program_len = e.program_cap = 0;

    if (load_program(&e, prog_path) != 0) {
        ram_free(&e.ram);
        return 1;
    }

    int rc = emulator_run(&e);
    return rc;
}
