#include "defines.h"

#include "bus.h"
#include "iox.h"

#include <avr/interrupt.h>
#include <util/delay.h>

#include <stdio.h>

void clk_cycle(uint8_t cycles)
{
    CLK_LO;
    uint8_t i;
    for (i = 0; i < cycles; i++) {
        CLK_HI;
        CLK_LO;
    }
}

// Run the Z80's clock
void clk_run(void)
{
    // Fast PWM mode with adjustable top and no prescaler
    TCCR2A |= (1 << COM2B1) | (1 << WGM21) | (1 << WGM20);
    TCCR2B |= (1 << WGM22) | (1 << CS20);
    OCR2A = 1;
    OCR2B = 0;
}

// Stop the Z80's clock
void clk_stop()
{
    TCCR2A = 0;
    TCCR2B = 0;
    OCR2A = 0;
    OCR2B = 0;
}

// Request control of the bus from the Z80
void bus_master(void)
{
    BUSRQ_LO;           // request bus
    IOACK_LO;           // make sure not in WAIT state
    IOACK_HI;
    while (GET_BUSACK)  // wait for BUSACK to go low
        CLK_TOGGLE;
    CTRL_HI;
    CTRL_OUTPUT;        
    ADDR_OUTPUT;
    DATA_INPUT;
}

// Return control of the bus to the Z80
void bus_slave(void)
{
    CTRL_INPUT;
    ADDR_INPUT;
    DATA_INPUT;
    BUSRQ_HI;
}

// Initialize bus
void bus_init(void)
{
    // Initialize I/O expander
    iox_init();

    // Configure bus signal direction
    INT_OUTPUT;
    NMI_OUTPUT;
    RESET_OUTPUT;
    BUSRQ_OUTPUT;
    BANK_OUTPUT;
    IOACK_OUTPUT;
    CLK_OUTPUT;

    IORQ_INPUT;
    BUSACK_INPUT;
    M1_INPUT;
    RFSH_INPUT;
    HALT_INPUT;
    
    // Pullup on halt so it can be used with a switch
    HALT_PULLUP;

    // Set defaults for output bus signals
    INT_HI;
    NMI_HI;
    BUSRQ_HI;
    SET_BANK(0);

    // Reset the processor
    z80_reset();

    // Make bidirectional signals inputs
    bus_slave();
}

#define HL(signal) ((signal) ? 'H' : 'L')

// Log current bus status
char *bus_status()
{
    static char buf[256];
    uint8_t ctrlx = iox_read(CTRLX_GPIO);
    uint8_t data = GET_DATA;

    sprintf(buf,
        "CLK=%c M1=%c MREQ=%c IORQ=%c IOACK=%c RD=%c WR=%c RFSH=%c HALT=%c "
        "INT=%c NMI=%c RESET=%c BUSRQ=%c BUSACK=%c BANK=%X ADDR=%04X "
        "DATA=%02X %c\n",
        HL(GET_CLK), 
        HL(GET_M1), 
        HL(GET_MREQ), 
        HL(GET_IORQ), 
        HL(GET_IOACK),
        HL(GET_RD), 
        HL(GET_WR), 
#ifdef RFSH
        HL(GET_RFSH), 
#else
        'X',
#endif
        HL(GET_HALT), 
        HL(ctrlx & (1 << INTERRUPT)), 
        HL(ctrlx & (1 << NMI)), 
        HL(ctrlx & (1 << RESET)), 
        HL(ctrlx & (1 << BUSRQ)), 
        HL(ctrlx & (1 << BUSACK)),
        ((ctrlx & BANKMASK) >> BANKADDR), 
        GET_ADDR, 
        data,
        ((0x20 <= data && data <= 0x7e) ? data : ' '));

    return buf;
}