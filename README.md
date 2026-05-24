# E8 8-Bit CPU Architecture

The **E8** is a custom 8-bit CPU architecture designed in [Logisim Evolution](https://github.com/logisim-evolution/logisim-evolution), paired with a functional software emulator written in Python. Together, they provide a complete platform for exploring low-level computing: from gate-level hardware design through instruction execution, memory access, and real-time RGB video output.

The architecture uses a fixed 32-bit instruction word, a small general-purpose register file, an accumulator for arithmetic, external RAM, and dedicated video registers for drawing to a display. Programs can be assembled into binary test files and executed on the emulator to observe register state, memory, and graphical output in action.

---

## Circuit Architecture

The E8 CPU is implemented as a digital logic circuit in Logisim Evolution. The schematic below shows the full datapath, control logic, and peripheral connections.

![E8 CPU circuit schematic in Logisim Evolution](images/circ.png)

The Logisim project file is available at [`circuit/e8.circ`](circuit/e8.circ).

---

## Registers & Instruction Format

### 32-Bit Instruction Layout

Every instruction occupies **32 bits**, structured as four 8-bit fields:

| Field       | Size     | Description                                      |
|-------------|----------|--------------------------------------------------|
| **Opcode**  | 8 bits   | Identifies the operation to perform              |
| **Operand 1** | 8 bits | First operand (register index, address, or value) |
| **Operand 2** | 8 bits | Second operand (value or RAM index)            |
| **Operand 3** | 8 bits | Reserved for future expansion *(currently unused)* |

```
┌────────────┬────────────┬────────────┬────────────┐
│  Opcode    │  Operand 1 │  Operand 2 │  Operand 3 │
│  [8 bits]  │  [8 bits]  │  [8 bits]  │  [8 bits]  │
└────────────┴────────────┴────────────┴────────────┘
```

### Register Map

| Register | Hex Index | Role |
|----------|-----------|------|
| **PC**   | —         | Program Counter — holds the address of the next instruction to fetch |
| **ACC**  | —         | Accumulator — destination for arithmetic results and data movement |
| **A**    | `0x1`     | General-purpose register / Video **Red** (R) channel |
| **B**    | `0x2`     | General-purpose register / Video **Green** (G) channel |
| **C**    | `0x3`     | General-purpose register / Video **Blue** (B) channel |
| **D**    | `0x4`     | General-purpose register / Video **X** coordinate |
| **E**    | `0x5`     | General-purpose register / Video **Y** coordinate |

Registers **A** through **E** serve dual roles: they participate in general computation and act as the dedicated interface for the RGB video output peripheral.

---

## Instruction Set Reference

### Data Movement & Control Flow

| Mnemonic | Opcode (Hex) | Operands | Description |
|----------|--------------|----------|-------------|
| **MOV**  | `0x00` | OPR1: Register index `[0x1–0x6]`, OPR2: Value | Move the value in OPR2 into the register specified by OPR1 |
| **JMP**  | `0x01` | OPR1: Address | Set the Program Counter (PC) to the value in OPR1 |
| **JMPG** | `0x08` | OPR1: Address | Set PC to OPR1 if ACC ≥ 1 |
| **JMPZ** | `0x09` | OPR1: Address | Set PC to OPR1 if ACC == 0 |

### Arithmetic

All arithmetic instructions read a register value and an immediate operand, then store the result in **ACC**.

| Mnemonic | Opcode (Hex) | Operands | Description |
|----------|--------------|----------|-------------|
| **ADD**  | `0x02` | OPR1: Register index `[0x1–0x5]`, OPR2: Value | Add OPR2 to the value of register OPR1 → store in ACC |
| **SUB**  | `0x03` | OPR1: Register index `[0x1–0x5]`, OPR2: Value | Subtract OPR2 from the value of register OPR1 → store in ACC |
| **MUL**  | `0x04` | OPR1: Register index `[0x1–0x5]`, OPR2: Value | Multiply register OPR1 by OPR2 → store in ACC |
| **DIV**  | `0x05` | OPR1: Register index `[0x1–0x5]`, OPR2: Value | Divide the value of register OPR1 by OPR2 → store in ACC |

### Accumulator Movement

| Mnemonic | Opcode (Hex) | Operands | Description |
|----------|--------------|----------|-------------|
| **LOAD**  | `0x06` | OPR1: Register index `[0x1–0x6]` | Load the value from register OPR1 into ACC |
| **STORE** | `0x07` | OPR1: Register index `[0x1–0x6]` | Store the value in ACC into register OPR1 |

### RAM Access

| Mnemonic | Opcode (Hex) | Operands | Description |
|----------|--------------|----------|-------------|
| **RAM_WRITE** | `0x0A` | OPR1: Memory address, OPR2: RAM index | Store the value in ACC into RAM at address OPR1 (bank OPR2) |
| **RAM_READ**  | `0x0B` | OPR1: Memory address, OPR2: RAM index | Load the value from RAM at address OPR1 (bank OPR2) into ACC |

### Display / Video

| Mnemonic | Opcode (Hex) | Operands | Description |
|----------|--------------|----------|-------------|
| **SCREEN_CLEAR** | `0x0C` | — | Reset the display screen to blank |
| **SCREEN_DRAW**  | `0x0D` | — *(uses registers A–E)* | Draw a pixel to the screen using the video registers |

---

## Peripherals / RGB Video Output

The E8 includes a built-in RGB framebuffer accessible through two dedicated instructions and five shared registers.

### `SCREEN_CLEAR` (`0x0C`)

Clears the entire display, resetting all pixels to the background state. No operands are required.

### `SCREEN_DRAW` (`0x0D`)

`SCREEN_DRAW` is a special instruction that does **not** take explicit operands in the instruction word. Instead, it reads pixel data directly from the general-purpose registers at execution time:

| Register | Channel / Axis | Purpose |
|----------|----------------|---------|
| **A** (`0x1`) | Red (R)   | Red colour component (0–255) |
| **B** (`0x2`) | Green (G) | Green colour component (0–255) |
| **C** (`0x3`) | Blue (B)  | Blue colour component (0–255) |
| **D** (`0x4`) | X         | Horizontal pixel coordinate |
| **E** (`0x5`) | Y         | Vertical pixel coordinate |

A typical drawing sequence looks like this:

1. Use **MOV** to set the desired R, G, and B values into registers **A**, **B**, and **C**.
2. Use **MOV** to set the target pixel coordinates into registers **D** (X) and **E** (Y).
3. Execute **SCREEN_DRAW** — the emulator reads the current contents of A–E and renders a single pixel at `(D, E)` with colour `(A, B, C)`.
4. Repeat steps 1–3 to build up an image, or call **SCREEN_CLEAR** to wipe the canvas before redrawing.

Because the video registers are ordinary general-purpose registers, the same values can participate in arithmetic and control-flow logic before being sent to the display — enabling programmatic graphics, animations, and colour effects entirely in E8 assembly.

---

## Emulator Gallery

The Python emulator loads assembled programs (`.bin` files), executes instructions cycle-by-cycle, and renders output to a Pygame window. Sample test programs are included in the [`tests/`](tests/) directory.

### Colour Demo

![E8 emulator running a colour demonstration program](images/colour.png)

### E8 Logo Render

![E8 emulator rendering the E8 logo on the display](images/e8.png)

---

## Project Structure

| Path | Description |
|------|-------------|
| [`circuit/e8.circ`](circuit/e8.circ) | Logisim Evolution circuit schematic |
| [`images/`](images/) | Circuit and emulator screenshots |
| [`src/Emulator.py`](src/Emulator.py) | Core CPU emulation logic |
| [`src/Main.py`](src/Main.py) | Emulator entry point and display loop |
| [`tests/`](tests/) | Sample assembled programs (`colour.bin`, `e8.bin`) |
| [`ISA`](ISA) | Plain-text ISA reference |

---

## License

See [`LICENSE`](LICENSE) for licensing details.
