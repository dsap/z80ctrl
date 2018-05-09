#include <stdint.h>
#include <stdio.h>
#include <avr/pgmspace.h>

#include "z80.h"
#include "bus.h"
#include "diskemu.h"
#include "disasm.h"

// Breakpoints and watches
uint16_t bus_watch_start = 0xffff;
uint16_t bus_watch_end = 0;
uint16_t memrd_watch_start = 0xffff;
uint16_t memrd_watch_end = 0;
uint16_t memwr_watch_start = 0xffff;
uint16_t memwr_watch_end = 0;
uint8_t iord_watch_start = 0xff;
uint8_t iord_watch_end = 0;
uint8_t iowr_watch_start = 0xff;
uint8_t iowr_watch_end = 0;
uint16_t opfetch_watch_start = 0xffff;
uint16_t opfetch_watch_end = 0;
uint16_t memrd_break_start = 0xffff;
uint16_t memrd_break_end = 0;
uint16_t memwr_break_start = 0xffff;
uint16_t memwr_break_end = 0;
uint8_t iord_break_start = 0xff;
uint8_t iord_break_end = 0;
uint8_t iowr_break_start = 0xff;
uint8_t iowr_break_end = 0;
uint16_t opfetch_break_start = 0xffff;
uint16_t opfetch_break_end = 0;

// Reset the Z80
void z80_reset(uint16_t addr)
{
    uint8_t reset_vect[] = { 0xc3, (addr & 0xFF), (addr >> 8) };
    if (addr > 0x0002) {
        write_mem(0x0000, reset_vect, 3);
    }
    RESET_LO;
    clk_cycle(3);
    RESET_HI;
    IOACK_LO;
    IOACK_HI;
}

#define SIO0_STATUS 0x10
#define SIO0_DATA 0x11
#define SIOA_CONTROL 0x80
#define SIOA_DATA 0x81
#define SIOB_CONTROL 0x82
#define SIOB_DATA 0x83

// Handle Z80 IO request
void z80_iorq(void)
{
    switch (GET_ADDRLO) {
        case SIO0_STATUS:
            if (!GET_RD) {
                SET_DATA(((UCSR0A >> (UDRE0 - 1)) & 0x2) | ((UCSR0A >> RXC0) & 0x1));
                DATA_OUTPUT;
            }
            break;
        case SIOA_CONTROL:
            if (!GET_RD) {
                // CTS and DCD always high
                SET_DATA((1 << 3) | (1  << 5) | ((UCSR0A >> (UDRE0 - 2)) & 0x4) | ((UCSR0A >> RXC0) & 0x1));
                DATA_OUTPUT;
            }
            break;
        case SIOB_CONTROL:
            if (!GET_RD) {
                // CTS and DCD always high
                SET_DATA((1 << 3) | (1  << 5) | ((UCSR1A >> (UDRE1 - 2)) & 0x4) | ((UCSR1A >> RXC1) & 0x1));
                DATA_OUTPUT;
            }
            break;
        case SIO0_DATA:
        case SIOA_DATA:
            if (!GET_RD) {
                SET_DATA(UDR0);
                DATA_OUTPUT;
            } else if (!GET_WR) {
                UDR0 = GET_DATA;
            }
            break;
        case SIOB_DATA:
            if (!GET_RD) {
                SET_DATA(UDR1);
                DATA_OUTPUT;
            } else if (!GET_WR) {
                UDR1 = GET_DATA;
            }
            break;
        case DRIVE_STATUS:
            if (!GET_RD) {
                SET_DATA(drive_status());
                DATA_OUTPUT;
            } else if (!GET_WR) {
                drive_select(GET_DATA);
            }
            break;
        case DRIVE_CONTROL:
            if (!GET_RD) {
                SET_DATA(drive_sector());
                DATA_OUTPUT;
            } else if (!GET_WR) {
                drive_control(GET_DATA);
            }
            break;
        case DRIVE_DATA:
            if (!GET_RD) {
                SET_DATA(drive_read());
                DATA_OUTPUT;
            } else if (!GET_WR) {
                drive_write(GET_DATA);
            }
            break;
        default:
            if (!GET_RD) {
                SET_DATA(0xFF);
            }
    }
    IOACK_LO;
    while (!GET_IORQ) {
        CLK_TOGGLE;
    }
    DATA_INPUT;
    IOACK_HI;
}

// Run the Z80 at full speed
void z80_run(void)
{
    clk_run();
    while (GET_HALT) {
        if (!GET_IORQ) {
            clk_stop();
            z80_iorq();
            clk_run();
        }
    }
    clk_stop();
    CLK_LO;
}

#define HL(signal) ((signal) ? 'H' : 'L')

void z80_buslog(bus_stat status)
{
    printf_P(
        PSTR("clk=%c m1=%c mreq=%c iorq=%c ioack=%c rd=%c wr=%c rfsh=%c halt=%c "
        "int=%c nmi=%c reset=%c busrq=%c busack=%c "
#ifdef BANKMASK
        "bank=%X "
#endif
        "addr=%04X "
        "data=%02X %c\n"),
        HL(status.flags.bits.clk),
        HL(status.flags.bits.m1),
        HL(status.flags.bits.mreq),
        HL(status.flags.bits.iorq),
        HL(status.flags.bits.ioack),
        HL(status.flags.bits.rd),
        HL(status.flags.bits.wr),
        HL(status.flags.bits.rfsh),
        HL(status.flags.bits.halt), 
        HL(status.flags.bits.interrupt),
        HL(status.flags.bits.nmi),
        HL(status.flags.bits.reset),
        HL(status.flags.bits.busrq),
        HL(status.flags.bits.busack),
        status.addr,
        status.data,
        0x20 <= status.data && status.data <= 0x7e ? status.data : ' ');

        // wait until output is fully transmitted to avoid
        // interfering with UART status for running program
        loop_until_bit_is_set(UCSR0A, UDRE0);    
}

void z80_busshort(bus_stat status)
{
    printf_P(
        PSTR("\t%04x %02x %c    %s %s    %s %s %s %s %s %s %s %s\n"),
        status.addr,
        status.data,
        0x20 <= status.data && status.data <= 0x7e ? status.data : ' ',
        !status.flags.bits.rd ? "rd  " :
        !status.flags.bits.wr ? "wr  " :
        !status.flags.bits.rfsh ? "rfsh" : "    ",
        !status.flags.bits.mreq ? "mem" :
        !status.flags.bits.iorq ? "io " : "   ",
        !status.flags.bits.m1 ? "m1" : "  ",
        !status.flags.bits.halt ? "halt" : "    ", 
        !status.flags.bits.interrupt ? "int" : "   ",
        !status.flags.bits.nmi ? "nmi" : "   ",
        !status.flags.bits.reset ? "rst" : "   ",
        !status.flags.bits.busrq ? "busrq" : "     ",
        !status.flags.bits.busack ? "busack" : "      ",
        !status.flags.bits.ioack ? "ioack" : "     ");

        // wait until output is fully transmitted to avoid
        // interfering with UART status for running program
        loop_until_bit_is_set(UCSR0A, UDRE0);    
}

#define MAXREAD 64

#define INRANGE(start, end, test) ((start) <= (test) && (test) <= (end))

#define PREFIX(b) (b == 0xCB || b == 0xDD || b == 0xED || b == 0xFD)

// Determine if we are at the start of a new instruction
// Thanks to Alan Kamrowski II for working this logic out
uint8_t newinstr(uint8_t current)
{
  static uint8_t previous, previous2;
  uint8_t result;
 
    switch (previous) {
        case 0xcb:
            if (previous2 == 0xcb || previous2 == 0xdd || previous2 == 0xfd)
                // if previous2 is 0xcb, then previous must be an opcode
                // if previous2 is 0xdd or 0xfd, then previous must be a 2nd prefix
                // either way, current must be start of a new instruction
                result = 1;
            else 
                result = 0;
            break;
        case 0xed:
            if (previous2 == 0xcb)
                // previous must be an opcode, so current must be start of a new instruction
                result = 1;
            else 
                result = 0;
            break;
        case 0xdd:
        case 0xfd:
            if (previous2 == 0xcb)
                // previous must be an opcode, so current must be start of a new instruction
                result = 1;
            else
                if (current == 0xdd || current == 0xed || current == 0xfd)
                    // previous is essentially a nop, so current must be start of a new instruction
                    result = 1;
                else 
                    result = 0;
            break;
        default:
            // otherwise, previous must be an opcode, so current must be start of a new instruction
            result = 1;
            break;
    }

    // roll opcodes
    previous2 = previous;
    previous = current;

    return result;
}

uint8_t z80_tick()
{
    uint8_t lastrd = GET_RD;
    uint8_t lastwr = GET_WR;
    uint8_t logged = 0;
    uint8_t brk = 0;

    CLK_LO;
    CLK_HI;
    bus_stat status = bus_status();
    if (!status.flags.bits.mreq) {
        if (lastrd && !status.flags.bits.rd) {
            if (logged = INRANGE(memrd_watch_start, memrd_watch_end, status.addr))
                z80_busshort(status);
            brk = INRANGE(memrd_break_start, memrd_break_end, status.addr);
        } else if (lastwr && !status.flags.bits.wr) {
            if (logged = INRANGE(memwr_watch_start, memwr_watch_end, status.addr))
                z80_busshort(status);
            brk = INRANGE(memwr_break_start, memwr_break_end, status.addr);
        }
    } else if (!status.flags.bits.iorq) {
        if (lastrd && !status.flags.bits.rd) {
            if (logged = INRANGE(iord_watch_start, iord_watch_end, status.addr & 0xff))
                z80_busshort(status);
            brk = INRANGE(iord_break_start, iord_break_end, status.addr & 0xff);
        } else if (lastwr && !status.flags.bits.wr) {
            if (logged = INRANGE(iowr_watch_start, iowr_watch_end, status.addr & 0xff))
                z80_busshort(status);
            brk = INRANGE(iowr_break_start, iowr_break_end, status.addr & 0xff);
        }
        z80_iorq();
    }        
    if (!logged && INRANGE(bus_watch_start, bus_watch_end, status.addr))
        z80_busshort(status);
    
    return brk;
}

uint8_t z80_read()
{
    uint8_t data;
    while (GET_MREQ || GET_RD)
        z80_tick();
    data = GET_DATA;
    while (!GET_MREQ || !GET_RD)
        z80_tick();
    return data;
}

// Trace reads and writes for the specified number of instructions
void z80_debug(uint32_t cycles)
{
    char mnemonic[255];
    uint32_t c = 0;
    uint8_t brk = 0;

    while (GET_HALT && (cycles == 0 || c < cycles) && !brk) {
        brk = z80_tick();
        if (!GET_M1 && !GET_MREQ && !GET_RD) {
            uint16_t addr = GET_ADDR;
            c++;
            disasm(addr, z80_read, mnemonic);
            if (INRANGE(opfetch_watch_start, opfetch_watch_end, addr))
                printf("\t%04x\t%s\n", addr, mnemonic);
        }
    }
}