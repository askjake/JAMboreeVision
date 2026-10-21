# Compiled-MCU emulation layer.
#
# Builds the REAL dart_core.cpp as AVR machine code and executes it instruction
# by instruction in simavr, observing results over the gdb remote protocol.
#
# IMPORTANT SCOPE LIMIT: the emulated MCU is ATmega644P, NOT the ATmega4809
# target.  No available emulator implements the megaAVR-0 series (simavr 1.6
# --list-cores has no atmega4809; qemu-system-avr models only ATmega328P and
# ATmega2560 machines).  ATmega644P is the closest available core: same 8-bit
# AVR ABI, 16-bit int, 2-byte data pointers, <=64 KiB flash, 4 KiB SRAM.
# No peripherals are modelled: TWI/USART/timers are unused, the I2C callbacks
# are invoked directly and millis() is a software counter.
#
# Prerequisites (override as needed):
#   AVR_GXX  avr-g++   from the Arduino megaAVR toolchain
#   AVR_GDB  avr-gdb   from the same toolchain
#   SIMAVR   simavr    1.6+
MCU      ?= atmega644p
F_CPU    ?= 16000000
ACORE    ?= $(HOME)/.arduino15/packages/arduino/tools/avr-gcc/7.3.0-atmel3.6.1-arduino5/bin
AVR_GXX  ?= $(ACORE)/avr-g++
AVR_SIZE ?= $(ACORE)/avr-size
# Prefer an avr-gdb on PATH.  The Arduino-bundled avr-gdb 7.8 is linked against
# libncurses.so.5 / libtinfo.so.5, which modern distributions no longer ship;
# either install the ncurses5 compat libraries or put a working avr-gdb on PATH.
AVR_GDB  ?= $(shell command -v avr-gdb 2>/dev/null || echo $(ACORE)/avr-gdb)
SIMAVR   ?= simavr

CXXFLAGS = -mmcu=$(MCU) -Os -g -std=gnu++11 -fno-exceptions \
           -fno-threadsafe-statics -Wall -Wextra \
           -DDART_HOST_BUILD=1 -DDART_BOARD_HOST_TEST=1 \
           -I../../dart_remote_bridge
SRC = avr_emu_tests.cpp ../../dart_remote_bridge/dart_core.cpp
ELF = avr_emu_tests_$(MCU).elf

all: $(ELF)

$(ELF): $(SRC) ../../dart_remote_bridge/dart_core.h \
        ../../dart_remote_bridge/dart_board_config.h \
        ../../dart_remote_bridge/dart_hal.h
	$(AVR_GXX) $(CXXFLAGS) -o $@ $(SRC)
	$(AVR_SIZE) $@

run: $(ELF)
	MCU=$(MCU) F_CPU=$(F_CPU) AVR_GDB=$(AVR_GDB) SIMAVR=$(SIMAVR) \
	  ./run_emulation.sh $(ELF)

clean:
	rm -f *.elf *.su

.PHONY: all run clean
