#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DISPLAY_INTERVAL 100
#define CLOCK_SPEED 6000
#define GPR_COUNT 5
#define RAM_BANK_SIZE 256
#define MAX_RAM_BANKS 256
#define CANVAS_SIZE 256
#define WINDOW_SIZE 512

typedef struct {
    uint8_t Opcode;
    int Op1, Op2;
} Instruction;

typedef struct {
    int ProgramCounter;
    int GeneralRegisters[GPR_COUNT];
    int Accumulator;
    uint8_t *RamMemory[MAX_RAM_BANKS];
    Instruction *ProgramInstructions;
    size_t ProgramLength, ProgramCapacity;
    int IsRunning;
    unsigned long CycleCount;

    SDL_Window *AppWindow;
    SDL_Renderer *AppRenderer;
    SDL_Texture *CanvasTexture;
    SDL_Surface *CanvasSurface;
} E8Emulator;

static int EnsureRamBank(E8Emulator *Emu, int BankIndex) {
    if (BankIndex < 0 || BankIndex >= MAX_RAM_BANKS) return -1;
    if (!Emu->RamMemory[BankIndex]) {
        Emu->RamMemory[BankIndex] = (uint8_t *)calloc(RAM_BANK_SIZE, 1);
        if (!Emu->RamMemory[BankIndex]) return -1;
    }
    return 0;
}

static void ShowCpuState(const E8Emulator *Emu) {
    printf("\033[2J\033[HPC: %02X\nRegisters: [", Emu->ProgramCounter);
    for (int i = 0; i < GPR_COUNT; i++) {
        printf("%02X%s", (unsigned)(Emu->GeneralRegisters[i] & 0xFF), i + 1 < GPR_COUNT ? ", " : "");
    }
    printf("]\nAccumulator: %02X\n", (unsigned)(Emu->Accumulator & 0xFF));
    fflush(stdout);
}

static int LoadProgramFile(E8Emulator *Emu, const char *FilePath) {
    FILE *File = fopen(FilePath, "r");
    if (!File) { perror(FilePath); return -1; }

    char LineBuffer[512];
    while (fgets(LineBuffer, sizeof(LineBuffer), File)) {
        char *Comment = strchr(LineBuffer, ';');
        if (Comment) *Comment = '\0';

        char *Token = LineBuffer;
        while (*Token == ' ' || *Token == '\t') Token++;
        if (*Token == '\0' || *Token == '\n' || *Token == '\r') continue;

        char *Tok1 = strtok(Token, " \t\r\n");
        char *Tok2 = strtok(NULL, " \t\r\n");
        char *Tok3 = strtok(NULL, " \t\r\n");
        if (!Tok1) continue;

        if (Emu->ProgramLength >= Emu->ProgramCapacity) {
            Emu->ProgramCapacity = Emu->ProgramCapacity ? Emu->ProgramCapacity * 2 : 64;
            Emu->ProgramInstructions = (Instruction *)realloc(Emu->ProgramInstructions, Emu->ProgramCapacity * sizeof(Instruction));
        }

        Emu->ProgramInstructions[Emu->ProgramLength].Opcode = (uint8_t)strtol(Tok1, NULL, 16);
        Emu->ProgramInstructions[Emu->ProgramLength].Op1 = Tok2 ? (int)strtol(Tok2, NULL, 16) : 0;
        Emu->ProgramInstructions[Emu->ProgramLength].Op2 = Tok3 ? (int)strtol(Tok3, NULL, 16) : 0;
        Emu->ProgramLength++;
    }
    fclose(File);
    return 0;
}

static void DrawPixel(SDL_Surface *Surface, int X, int Y, uint8_t R, uint8_t G, uint8_t B) {
    if (X < 0 || X >= Surface->w || Y < 0 || Y >= Surface->h) return;
    Uint32 ColorValue = SDL_MapRGBA(Surface->format, R, G, B, 255);
    Uint32 *Pixels = (Uint32 *)Surface->pixels;
    Pixels[Y * (Surface->pitch / sizeof(Uint32)) + X] = ColorValue;
}

static void ExecuteBatch(E8Emulator *Emu, int Count) {
    int Executed = 0;
    while (Executed < Count && Emu->IsRunning) {
        if ((size_t)Emu->ProgramCounter >= Emu->ProgramLength) { Emu->IsRunning = 0; break; }

        Instruction CurrentInstr = Emu->ProgramInstructions[Emu->ProgramCounter];
        int RegIdx = CurrentInstr.Op1 - 1;
        Executed++;

        switch (CurrentInstr.Opcode) {
            case 0x00: if (RegIdx >= 0 && RegIdx < GPR_COUNT) Emu->GeneralRegisters[RegIdx] = CurrentInstr.Op2; Emu->ProgramCounter++; break;
            case 0x01: Emu->ProgramCounter = CurrentInstr.Op1; break;
            case 0x08: Emu->ProgramCounter = (Emu->Accumulator >= 1) ? CurrentInstr.Op1 : Emu->ProgramCounter + 1; break;
            case 0x09: Emu->ProgramCounter = (Emu->Accumulator == 0) ? CurrentInstr.Op1 : Emu->ProgramCounter + 1; break;
            case 0x02: Emu->Accumulator = Emu->GeneralRegisters[RegIdx] + CurrentInstr.Op2; Emu->ProgramCounter++; break;
            case 0x03: Emu->Accumulator = Emu->GeneralRegisters[RegIdx] - CurrentInstr.Op2; Emu->ProgramCounter++; break;
            case 0x04: Emu->Accumulator = Emu->GeneralRegisters[RegIdx] * CurrentInstr.Op2; Emu->ProgramCounter++; break;
            case 0x05: Emu->Accumulator = Emu->GeneralRegisters[RegIdx] / (CurrentInstr.Op2 ? CurrentInstr.Op2 : 1); Emu->ProgramCounter++; break;
            case 0x06: Emu->Accumulator = Emu->GeneralRegisters[RegIdx]; Emu->ProgramCounter++; break;
            case 0x07: Emu->GeneralRegisters[RegIdx] = Emu->Accumulator; Emu->ProgramCounter++; break;
            case 0x0A:
                if (EnsureRamBank(Emu, CurrentInstr.Op2) == 0 && CurrentInstr.Op1 >= 0 && CurrentInstr.Op1 < RAM_BANK_SIZE) {
                    Emu->RamMemory[CurrentInstr.Op2][CurrentInstr.Op1] = (uint8_t)(Emu->Accumulator & 0xFF);
                }
                Emu->ProgramCounter++; break;
            case 0x0B:
                if (EnsureRamBank(Emu, CurrentInstr.Op2) == 0 && CurrentInstr.Op1 >= 0 && CurrentInstr.Op1 < RAM_BANK_SIZE) {
                    Emu->Accumulator = Emu->RamMemory[CurrentInstr.Op2][CurrentInstr.Op1];
                }
                Emu->ProgramCounter++; break;
            case 0x0C: 
                SDL_FillRect(Emu->CanvasSurface, NULL, SDL_MapRGBA(Emu->CanvasSurface->format, 0, 0, 0, 255)); 
                Emu->ProgramCounter++; break;
            case 0x0D:
                if (SDL_MUSTLOCK(Emu->CanvasSurface)) SDL_LockSurface(Emu->CanvasSurface);
                DrawPixel(Emu->CanvasSurface, Emu->GeneralRegisters[3], Emu->GeneralRegisters[4], 
                          Emu->GeneralRegisters[0], Emu->GeneralRegisters[1], Emu->GeneralRegisters[2]);
                if (SDL_MUSTLOCK(Emu->CanvasSurface)) SDL_UnlockSurface(Emu->CanvasSurface);
                Emu->ProgramCounter++; break;
            default: Emu->ProgramCounter++; break;
        }
    }
    Emu->CycleCount += Executed;
}

static void CleanUpEmulator(E8Emulator *Emu) {
    if (Emu->CanvasTexture) SDL_DestroyTexture(Emu->CanvasTexture);
    if (Emu->CanvasSurface) SDL_FreeSurface(Emu->CanvasSurface);
    if (Emu->AppRenderer) SDL_DestroyRenderer(Emu->AppRenderer);
    if (Emu->AppWindow) SDL_DestroyWindow(Emu->AppWindow);
    free(Emu->ProgramInstructions);
    for (int i = 0; i < MAX_RAM_BANKS; i++) free(Emu->RamMemory[i]);
}

int main(int argc, char **argv) {
    const char *ProgPath = (argc > 1) ? argv[1] : "tests/e8.bin";
    E8Emulator Emu = {0};
    
    if (EnsureRamBank(&Emu, 0) != 0 || LoadProgramFile(&Emu, ProgPath) != 0) {
        CleanUpEmulator(&Emu); return 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return 1;
    Emu.AppWindow = SDL_CreateWindow("E8 Emulator", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, WINDOW_SIZE, WINDOW_SIZE, 0);
    Emu.AppRenderer = SDL_CreateRenderer(Emu.AppWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_SOFTWARE);
    Emu.CanvasSurface = SDL_CreateRGBSurfaceWithFormat(0, CANVAS_SIZE, CANVAS_SIZE, 32, SDL_PIXELFORMAT_RGBA32);
    Emu.CanvasTexture = SDL_CreateTexture(Emu.AppRenderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, CANVAS_SIZE, CANVAS_SIZE);
    
    SDL_FillRect(Emu.CanvasSurface, NULL, SDL_MapRGBA(Emu.CanvasSurface->format, 0, 0, 0, 255));
    Emu.IsRunning = 1;

    double DelayPerCycle = 1000.0 / (double)CLOCK_SPEED;
    while (Emu.IsRunning) {
        Uint32 FrameStartTicks = SDL_GetTicks();
        SDL_Event Event;
        while (SDL_PollEvent(&Event)) { if (Event.type == SDL_QUIT) Emu.IsRunning = 0; }

        ExecuteBatch(&Emu, DISPLAY_INTERVAL);

        if (Emu.CycleCount % DISPLAY_INTERVAL == 0) {
            SDL_UpdateTexture(Emu.CanvasTexture, NULL, Emu.CanvasSurface->pixels, Emu.CanvasSurface->pitch);
            SDL_RenderClear(Emu.AppRenderer);
            SDL_Rect DstRect = {0, 0, WINDOW_SIZE, WINDOW_SIZE};
            SDL_RenderCopy(Emu.AppRenderer, Emu.CanvasTexture, NULL, &DstRect);
            SDL_RenderPresent(Emu.AppRenderer);
            ShowCpuState(&Emu);
        }

        Uint32 FrameElapsedTicks = SDL_GetTicks() - FrameStartTicks;
        if (FrameElapsedTicks < DelayPerCycle) {
            SDL_Delay((Uint32)(DelayPerCycle - FrameElapsedTicks));
        }
    }

    CleanUpEmulator(&Emu);
    SDL_Quit();
    return 0;
}
