#include <stdio.h>
#include <stdint.h>
#include <signal.h>
/* unix only */
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/termios.h>
#include <sys/mman.h>

/* 
 *  LC-3 is a 16-bit architecture, allowing for 2^16 locations with 
 *  a 16-bit word in each location. We implement this as an array.
 */
#define MEMORY_MAX (1 << 16)
uint16_t memory[MEMORY_MAX]; // x0000 - xFFFF

/*
 *  Registers are where data is actually operated on. LC-3 has 10 registers.
 *  - R0-R7: general purpose registers for calculations
 *  - PC   : program counter (the address of the next instruction)
 *  - COND : condition codes, describing the sign of the previous result
 */
enum
{
    R_R0 = 0,
    R_R1,
    R_R2,
    R_R3,
    R_R4,
    R_R5,
    R_R6,
    R_R7,
    R_PC,
    R_COND,
    R_COUNT
};
uint16_t reg[R_COUNT]; // registers also stored as an array

/*
 *  The instruction set contains the fundamental operations the CPU can perform.
 *  An instruction has an opcode, indicating the operation, and parameters,
 *  telling the CPU what to operate on. In LC-3 we have 16 opcodes, so the
 *  first 4 bits (2^4 = 16) of the instruction serve as the opcode, while
 *  the remaining bits provide the parameters. We define them below.
 */
enum
{
    OP_BR = 0, /* branch */
    OP_ADD,    /* add  */
    OP_LD,     /* load */
    OP_ST,     /* store */
    OP_JSR,    /* jump register */
    OP_AND,    /* bitwise and */
    OP_LDR,    /* load register */
    OP_STR,    /* store register */
    OP_RTI,    /* unused */
    OP_NOT,    /* bitwise not */
    OP_LDI,    /* load indirect */
    OP_STI,    /* store indirect */
    OP_JMP,    /* jump */
    OP_RES,    /* reserved (unused) */
    OP_LEA,    /* load effective address */
    OP_TRAP    /* execute trap */
};

/*
 *  Condition codes store information about CPU operations. This is 
 *  the basis of conditional control flow.
 *  In LC-3, the condition codes indicate the sign of the previous
 *  result. These are stored in the R_COND register.
 */
enum
{
    FL_POS = 1 << 0, /* P */
    FL_ZRO = 1 << 1, /* Z */
    FL_NEG = 1 << 2, /* N */
};

/*
 *  TRAP codes are convenient ways to perform tasks, similar to OS system calls.
 *  In LC-3, these are I/O operations and halting the program. This is why the PC
 *  starts at 0x3000, to leave space for the TRAP routines. We utilize C functions
 *  to implement the TRAP routines, using the OS I/O instead of writing in assembly.
 *  However, they behave the same. 
 */
enum
{
    TRAP_GETC = 0x20,  /* get character from keyboard, not echoed onto the terminal */
    TRAP_OUT = 0x21,   /* output a character */
    TRAP_PUTS = 0x22,  /* output a word string */
    TRAP_IN = 0x23,    /* get character from keyboard, echoed onto the terminal */
    TRAP_PUTSP = 0x24, /* output a byte string */
    TRAP_HALT = 0x25   /* halt the program */
};

/* Memory Mapped Registers */
enum
{
    MR_KBSR = 0xFE00, /* keyboard status */
    MR_KBDR = 0xFE02  /* keyboard data */
};

/* ways to access keyboard */
struct termios original_tio;

void disable_input_buffering()
{
    tcgetattr(STDIN_FILENO, &original_tio);
    struct termios new_tio = original_tio;
    new_tio.c_lflag &= ~ICANON & ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
}

void restore_input_buffering()
{
    tcsetattr(STDIN_FILENO, TCSANOW, &original_tio);
}

uint16_t check_key()
{
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    return select(1, &readfds, NULL, NULL, &timeout) != 0;
}

void handle_interrupt(int signal)
{
    (void)signal; /* required by signal(), unused */
    restore_input_buffering();
    printf("\n");
    exit(-2);
}

/*
 *  Parametrized sign extension function, since immediate values are usually less than 16 bits.
 *  If positive (0), return x, no action needed.
 *  If negative (1), mask the bits higher than the immediate with 1s, to preserve the negative.
 */
uint16_t sign_extend(uint16_t x, int bit_count)
{
    if ((x >> (bit_count - 1)) & 1) {
        x |= (0xFFFF << bit_count);
    }
    return x;
}

/* LC-3 is big endian, but computers are little endian, need to swap */
uint16_t swap16(uint16_t x)
{
    return (x << 8) | (x >> 8);
}

void read_image_file(FILE* file)
{
    /* the origin tells us where in memory to place the image */
    uint16_t origin;
    fread(&origin, sizeof(origin), 1, file);
    origin = swap16(origin);

    /* we know the maximum file size so we only need one fread */
    uint16_t max_read = MEMORY_MAX - origin;
    uint16_t* p = memory + origin;
    size_t read = fread(p, sizeof(uint16_t), max_read, file);

    /* swap to little endian */
    while (read-- > 0)
    {
        *p = swap16(*p);
        ++p;
    }
}

int read_image(const char* image_path)
{
    FILE* file = fopen(image_path, "rb");
    if (!file) { return 0; };
    read_image_file(file);
    fclose(file);
    return 1;
}

void mem_write(uint16_t address, uint16_t val)
{
    memory[address] = val;
}

uint16_t mem_read(uint16_t address)
{
    if (address == MR_KBSR)
    {
        if (check_key())
        {
            memory[MR_KBSR] = (1 << 15);
            memory[MR_KBDR] = getchar();
        }
        else
        {
            memory[MR_KBSR] = 0;
        }
    }
    return memory[address];
}

void update_flags(uint16_t r)
{
    if (reg[r] == 0)
    {
        reg[R_COND] = FL_ZRO;
    }
    else if (reg[r] >> 15) /* a 1 in the left-most bit indicates negative */
    {
        reg[R_COND] = FL_NEG;
    }
    else
    {
        reg[R_COND] = FL_POS;
    }
}

/*
 *  The main loop will follow this process.
 *  1. Load the instruction from the memory address given by the PC
 *  2. Incremement the PC
 *  3. Look at the opcode of the instruction to determine the instruction
 *  4. Perform the instruction based on the parameters given
 *  5. Repeat.
 */
int main(int argc, const char* argv[])
{
    if (argc < 2){ //
        // Print how to run the program if arguments are missing
        printf("lc3 [image-file1] ...\n");
        exit(2);
    }
    for (int j = 1; j < argc; ++j){
        if (!read_image(argv[j]))
        {
            printf("failed to load image: %s\n", argv[j]);
            exit(1);
        }
    }

    // adjusts buffering for input
    signal(SIGINT, handle_interrupt);
    disable_input_buffering();

    /* since exactly one condition flag should be set at any given time, set the Z flag */
    reg[R_COND] = FL_ZRO;

    /* set the PC to starting position */
    /* 0x3000 is the default */
    enum { PC_START = 0x3000 };
    reg[R_PC] = PC_START;

    int running = 1;
    while (running)
    {
        /* FETCH */
        uint16_t instr = mem_read(reg[R_PC]++);
        uint16_t op = instr >> 12; // looks at bits [15:12]

        switch (op)
        {
            case OP_ADD: /* addition*/ {
                /* destination register (DR) */
                uint16_t dr = (instr >> 9) & 0x7;
                /* first operand (SR1) */
                uint16_t sr1 = (instr >> 6) & 0x7;
                /* whether we are in immediate mode */
                uint16_t imm_flag = (instr >> 5) & 0x1;

                if (imm_flag)
                {
                    uint16_t imm5 = sign_extend(instr & 0x1F, 5);
                    reg[dr] = reg[sr1] + imm5;
                }
                else
                {
                    uint16_t sr2 = instr & 0x7;
                    reg[dr] = reg[sr1] + reg[sr2];
                }

                update_flags(dr);
                break;
            }
            case OP_AND: /* bitwise logical AND */ {
                /* destination register (DR) */
                uint16_t dr = (instr >> 9) & 0x7;
                /* first operand (SR1) */
                uint16_t sr1 = (instr >> 6) & 0x7;
                /* whether we are in immediate mode */
                uint16_t imm_flag = (instr >> 5) & 0x1;

                if (imm_flag)
                {
                    uint16_t imm5 = sign_extend(instr & 0x1F, 5);
                    reg[dr] = reg[sr1] & imm5;
                }
                else
                {
                    uint16_t sr2 = instr & 0x7;
                    reg[dr] = reg[sr1] & reg[sr2];
                }

                update_flags(dr);
                break;
            }
            case OP_NOT: /* bitwise complement */ {
                uint16_t dr = (instr >> 9) & 0x7;
                uint16_t sr = (instr >> 6) & 0x7;
                reg[dr] = ~reg[sr];
                update_flags(dr);
                break;
            }
            case OP_BR: /* conditional branch */ {
                uint16_t pc_offset9 = sign_extend(instr & 0x1FF, 9);
                uint16_t cond_flag = (instr >> 9) & 0x7;
                if (cond_flag & reg[R_COND])
                    reg[R_PC] += pc_offset9;
                break;
            }
            case OP_JMP: /* jump / return from subroutine */ {
                /* RET is just the special cause of JMP when BaseR is R7 */
                uint16_t base_r = (instr >> 6) & 0x7;
                reg[R_PC] = reg[base_r];
                break;
            }
            case OP_JSR: /* jump to subroutine */ {
                uint16_t r_flag = (instr >> 11) & 1; /* JSR or JSRR */
                reg[R_R7] = reg[R_PC];
                if (r_flag) /* JSR */
                {
                    uint16_t pc_offset11 = sign_extend(instr & 0x7FF, 11);
                    reg[R_PC] += pc_offset11;
                }
                else /* JSRR */
                {
                    uint16_t base_r = (instr >> 6) & 0x7;
                    reg[R_PC] = reg[base_r];
                }
                break;
            }
            case OP_LD: /* load */ {
                uint16_t dr = (instr >> 9) & 0x7;
                uint16_t pc_offset9 = sign_extend(instr & 0x1FF, 9);
                reg[dr] = mem_read(reg[R_PC] + pc_offset9);
                update_flags(dr);
                break;
            }
            case OP_LDI: /* load indirect */ {
                /* destination register (DR) */
                uint16_t dr = (instr >> 9) & 0x7;
                uint16_t pc_offset9 = sign_extend(instr & 0x1FF, 9);
                /* add pc_offset to the current PC, look at that memory location to get the final address */
                reg[dr] = mem_read(mem_read(reg[R_PC] + pc_offset9));
                update_flags(dr);
                break;
            }
            case OP_LDR: /* load Base + offset */ {
                uint16_t dr = (instr >> 9) & 0x7;
                uint16_t base_r = (instr >> 6) & 0x7;
                uint16_t offset6 = sign_extend(instr & 0x3F, 6);
                reg[dr] = mem_read(reg[base_r] + offset6);
                update_flags(dr);
                break;
            }
            case OP_LEA: /* load effective address */ {
                uint16_t dr = (instr >> 9) & 0x7;
                uint16_t pc_offset9 = sign_extend(instr & 0x1FF, 9);
                reg[dr] = reg[R_PC] + pc_offset9;
                update_flags(dr);
                break;
            }
            case OP_ST: /* store */ {
                uint16_t sr = (instr >> 9) & 0x7;
                uint16_t pc_offset9 = sign_extend(instr & 0x1FF, 9);
                mem_write(reg[R_PC] + pc_offset9, reg[sr]);
                break;
            }
            case OP_STI: /* store indirect */ {
                uint16_t sr = (instr >> 9) & 0x7;
                uint16_t pc_offset9 = sign_extend(instr & 0x1FF, 9);
                mem_write(mem_read(reg[R_PC] + pc_offset9), reg[sr]);
                break;
            }
            case OP_STR: /* store Base + offset */ {
                uint16_t sr = (instr >> 9) & 0x7;
                uint16_t base_r = (instr >> 6) & 0x7;
                uint16_t offset6 = sign_extend(instr & 0x3F, 6);
                mem_write(reg[base_r] + offset6, reg[sr]);
                break;
            }
            case OP_TRAP: {
                reg[R_R7] = reg[R_PC];
                switch (instr & 0xFF)
                {
                    case TRAP_GETC: { /* get character from keyboard, not echoed onto the terminal */
                        reg[R_R0] = (uint16_t)getchar();
                        update_flags(R_R0);
                        break;
                    }
                    case TRAP_OUT: { /* output a character */
                        putc((char)reg[R_R0], stdout);
                        fflush(stdout);
                        break;
                    }
                    case TRAP_PUTS: { /* output a word string */
                        /* one char per word */
                        uint16_t* c = &memory[reg[R_R0]];
                        while (*c)
                        {
                            putc((char)*c, stdout);
                            c++;
                        }
                        fflush(stdout);
                        break;
                    }
                    case TRAP_IN: { /* get character from keyboard, echoed onto the terminal */
                        printf("Enter a character: ");
                        char c = getchar();
                        putc(c, stdout);
                        fflush(stdout);
                        reg[R_R0] = (uint16_t)c;
                        update_flags(R_R0);
                        break;
                    }
                    case TRAP_PUTSP: { /* output a byte string */
                        /* two chars per word, big endian */
                        uint16_t* c = &memory[reg[R_R0]];
                        while (*c) /* while char is not null*/
                        {
                            char char1 = (*c) & 0xFF; /* low byte */
                            putc(char1, stdout);
                            char char2 = (*c) >> 8; /* high byte (but check if not null) */
                            if (char2) putc(char2, stdout);
                            c++;
                        }
                        fflush(stdout);
                        break;
                    }
                    case TRAP_HALT: { /* halt the program */
                        puts("HALT");
                        fflush(stdout);
                        running = 0;
                        break;
                    }
                }
                break;
            }
            case OP_RES: /* reserved */
            case OP_RTI: /* unused */
            default: {
                abort();
                break;
            }
        }
    }
    restore_input_buffering();
}
