# LC-3 Virtual Machine
An implementation of the LC-3 instruction set architecture (ISA) written in C, built independently as a systems programming exercise.

## What is LC-3?
LC-3 is a 16-bit educational ISA used in ECE 120 and ECE 220 at UIUC. It has 8 general-purpose registers, 16 opcodes, memory-mapped I/O, and a simple condition code system, teaching students about fundamental systems concepts before moving onto complex languages.

In ECE 385 (Digital Systems Laboratory), we implemented a simplified version of the LC-3 directly on an FPGA in SystemVerilog. This included building the datapath, ALU, and control logic in hardware. Writing the same architecture as a software VM offers a different perspective. We use the same fetch-decode-execute cycle, the same register file, the same condition codes, but now expressed in C instead of logic gates. Implementing LC-3 in both ways helps understand the difference with hardware and software abstractions, both creating the same machine.

## What's implemented

* **Fetch-decode-execute cycle** — the core loop that drives the VM
* **Full register file** — R0–R7 (general purpose), PC (program counter), COND (condition codes)
* **14/16 opcodes** — ADD, AND, NOT, BR, JMP, JSR/JSRR, LD, LDI, LDR, LEA, ST, STI, STR, TRAP (RTI and RES unimplemented per spec)
* **Condition codes** — positive, zero, negative flags updated after every result-producing instruction
* **Memory-mapped I/O** — keyboard status (KBSR) and data (KBDR) registers at fixed addresses
* **TRAP routines** — GETC, OUT, PUTS, IN, PUTSP, HALT implemented directly in C using OS I/O

## How to build and run

**Requirements:** GCC, Make

Build with Make:
```
make
```

Test programs (2048, Rogue) are included in `tests/`.

```
./lc3-vm tests/2048.obj
./lc3-vm tests/rogue.obj
```

Exit with `Ctrl+C`.

## Design notes

**TRAP routines in C instead of assembly**

On real LC-3 hardware, TRAP routines are assembly code stored in lower memory addresses (0x0000–0x2FFF), which is why user programs start at 0x3000. In this VM, each routine is implemented as a C function that uses the OS's I/O. The behavior is identical from a program's perspective, using C functions simplifies the code.

**Memory-mapped I/O**

Rather than special-casing I/O instructions, the LC-3 exposes keyboard state through two reserved memory addresses: `0xFE00` (status) and `0xFE02` (data). The VM intercepts reads to these addresses inside `mem_read()` and polls the terminal in real time.

## Skills applied
* Comparing fetch-decode-execute cycles across hardware and software abstractions
* Bitmasking to extract opcodes, register indices, and immediate values from 16-bit instructions
* Implementing sign extension in software
* Intercepting reads to reserved addresses to poll hardware state using memory-mapped I/O
* Connecting TRAP routines to the broader concept of OS system calls

## Context

This project builds directly on ECE 120 and ECE 220, where the LC-3 ISA was used to teach assembly and computer organization. It also connects to ECE 385, where a simplified LC-3 datapath was implemented on an FPGA. The prior experience of building the hardware made implementing the software VM feel like translating between two representations of the same machine.

This serves as preparation for ECE 391 (Computer Systems Programming), a class I am taking in Fall 2026, where systems concepts like virtual memory, process management, and kernel-level I/O build on exactly the foundations explored here.

## Reference
Adapted from [Write your Own Virtual Machine](https://www.jmeiners.com/lc3-vm/) by Justin Meiners and Ryan Pendleton.