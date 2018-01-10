BIN=z80ctrl
OBJS=z80ctrl.o uart.o spi.o iox.o bus.o

CC=avr-gcc
OBJCOPY=avr-objcopy
AVRDUDE=avrdude
MCU=atmega1284p
F_CPU=20000000L
PROGRAMMER=arduino
PORT=/dev/ttyS9
BAUD=115200
CFLAGS=-O3 -DF_CPU=$(F_CPU) -mmcu=$(MCU)


$(BIN).hex: $(BIN).elf
	$(OBJCOPY) -j .text -j .data -O ihex $< $@

$(BIN).elf: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

install: $(BIN).hex
	$(AVRDUDE) -c $(PROGRAMMER) -p $(MCU) -P $(PORT) -b $(BAUD) -U flash:w:$<

clean:
	rm -f $(BIN).elf $(BIN).hex $(OBJS)
