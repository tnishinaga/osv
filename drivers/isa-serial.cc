#include "isa-serial.hh"
#include "arch/x64/processor.hh"

IsaSerialConsole::IsaSerialConsole() {
	reset();
}

void IsaSerialConsole::write(const char *str)
{
    while (*str) {
    	writeByte(*str++);
    }

}

void IsaSerialConsole::writeByte(const char letter)
{
	processor::x86::u8 lsr;

	do {
		lsr = processor::x86::inb(ioport + LSR_ADDRESS);
	} while (!(lsr & LSR_TRANSMIT_HOLD_EMPTY));

	processor::x86::outb(letter, ioport);
}

void IsaSerialConsole::newline()
{
	writeByte('\n');
	writeByte('\r');
}

void IsaSerialConsole::reset() {
	// Put the line control register in acceptance mode (latch)
	lcr = processor::x86::inb(ioport + LCR_ADDRESS);
	lcr |= 1 << LCR_DIVISOR_LATCH_ACCESS_BIT & LCR_DIVISOR_LATCH_ACCESS_BIT_HIGH;
	processor::x86::outb(lcr, ioport + LCR_ADDRESS);

	processor::x86::outb(1, ioport + BAUD_GEN0_ADDRESS);
    processor::x86::outb(0, ioport + BAUD_GEN1_ADDRESS);

	// Close the latch
	lcr = processor::x86::inb(ioport + LCR_ADDRESS);
	lcr &= ~(1 << LCR_DIVISOR_LATCH_ACCESS_BIT & LCR_DIVISOR_LATCH_ACCESS_BIT_HIGH);
    processor::x86::outb(lcr, ioport + LCR_ADDRESS);
}
