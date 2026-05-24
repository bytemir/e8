import os
import sys
import time

import pygame

_CLEAR = "\033[2J\033[H" if os.name != "nt" else None
_DISPLAY_INTERVAL = 100


class Emulator:
    __slots__ = (
        "program_counter",
        "general_purpose_registers",
        "accumulator",
        "ram",
        "clock_speed",
        "program",
        "running",
        "cycle_count",
        "window",
        "canvas",
        "_scaled",
    )

    def __init__(self):
        self.program_counter = 0x00
        self.general_purpose_registers = [0x00] * 5
        self.accumulator = 0x00
        self.ram = [[0x00] * 256]
        self.clock_speed = 6000
        self.program = []
        self.running = False
        self.cycle_count = 0
        self.window = None
        self.canvas = None
        self._scaled = None

    def show_state(self):
        state_output = (
            f"PC: {self.program_counter:02X}\n"
            f"Registers: {[f'{reg:02X}' for reg in self.general_purpose_registers]}\n"
            f"Accumulator: {self.accumulator:02X}"
        )
        if _CLEAR is not None:
            sys.stdout.write(_CLEAR)
        else:
            os.system("cls")
        print(state_output, end="")

    def load_program(self, path):
        program = []
        append = program.append
        with open(path) as f:
            for line in f:
                clean_line = line.split(";", 1)[0].strip()
                if not clean_line:
                    continue
                tokens = clean_line.split()
                if not tokens:
                    continue
                try:
                    opcode = int(tokens[0], 16)
                    operand1 = int(tokens[1], 16) if len(tokens) > 1 else 0
                    operand2 = int(tokens[2], 16) if len(tokens) > 2 else 0
                    append((opcode, operand1, operand2))
                except ValueError:
                    continue
        self.program = program

    def _execute_batch(self, count):
        program = self.program
        program_len = len(program)
        pc = self.program_counter
        gpr = self.general_purpose_registers
        acc = self.accumulator
        ram = self.ram
        canvas = self.canvas
        running = True
        executed = 0

        while executed < count and running:
            if pc >= program_len:
                running = False
                break

            opcode, operand1, operand2 = program[pc]
            executed += 1

            if opcode == 0x00:
                gpr[operand1 - 1] = operand2
                pc += 1
            elif opcode == 0x01:
                pc = operand1
            elif opcode == 0x08:
                pc = operand1 if acc >= 1 else pc + 1
            elif opcode == 0x09:
                pc = operand1 if acc == 0 else pc + 1
            elif opcode == 0x02:
                acc = gpr[operand1 - 1] + operand2
                pc += 1
            elif opcode == 0x03:
                acc = gpr[operand1 - 1] - operand2
                pc += 1
            elif opcode == 0x04:
                acc = gpr[operand1 - 1] * operand2
                pc += 1
            elif opcode == 0x05:
                acc = gpr[operand1 - 1] // operand2
                pc += 1
            elif opcode == 0x06:
                acc = gpr[operand1 - 1]
                pc += 1
            elif opcode == 0x07:
                gpr[operand1 - 1] = acc
                pc += 1
            elif opcode == 0x0A:
                if operand1 >= len(ram):
                    ram.extend([[0x00] * 256] * (operand1 - len(ram) + 1))
                ram[operand1][operand2] = acc
                pc += 1
            elif opcode == 0x0B:
                if operand1 >= len(ram):
                    ram.extend([[0x00] * 256] * (operand1 - len(ram) + 1))
                acc = ram[operand1][operand2]
                pc += 1
            elif opcode == 0x0C:
                canvas.fill((0, 0, 0))
                pc += 1
            elif opcode == 0x0D:
                pygame.draw.circle(
                    canvas,
                    (gpr[0], gpr[1], gpr[2]),
                    (gpr[3], gpr[4]),
                    1,
                )
                pc += 1
            else:
                pc += 1

        self.program_counter = pc
        self.accumulator = acc
        self.cycle_count += executed
        self.running = running
        return executed

    def run(self):
        pygame.init()
        self.window = pygame.display.set_mode((512, 512))
        self.canvas = pygame.Surface((256, 256))
        pygame.display.set_caption("E8 Emulator")

        self.running = True
        sleep_duration = 1 / self.clock_speed
        use_sleep = sleep_duration >= 1e-9

        while self.running:
            start_time = time.perf_counter()

            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    self.running = False

            if not self.running:
                break

            self._execute_batch(_DISPLAY_INTERVAL)

            if self.cycle_count % _DISPLAY_INTERVAL == 0:
                self._scaled = pygame.transform.scale(self.canvas, (512, 512))
                self.window.blit(self._scaled, (0, 0))
                pygame.display.update()
                self.show_state()

            if use_sleep:
                elapsed = time.perf_counter() - start_time
                if elapsed < sleep_duration:
                    time.sleep(sleep_duration - elapsed)

        pygame.quit()
        sys.exit()


if __name__ == "__main__":
    instance = Emulator()
    instance.load_program("tests/e8.bin")
    instance.run()
