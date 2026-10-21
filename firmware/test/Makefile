# Verification layers for the DART bridge firmware.
#
# The dart_core.cpp compiled here is byte-for-byte the file the Arduino build
# uses, so these suites exercise the real firmware logic, not a reimplementation.
#
#   make run        native regression suite (host g++)
#   make sanitize   same suite under AddressSanitizer + UndefinedBehaviorSanitizer
#   make reference  reproduce the ORIGINAL sketch's defects (vendored verbatim)
#   make pty_sim    virtual DART board on a PTY, for application integration
#   make vision     JAMboreeVision -> virtual serial integration test
#   make emulate    compiled-AVR execution of dart_core.cpp in simavr
#   make all-layers everything that needs no physical hardware
#
# None of these targets touch /dev/ttyACM0, the operator's base.txt, or any
# running service.
CXX      ?= g++
CXXFLAGS ?= -std=c++11 -O1 -g -Wall -Wextra -Wshadow -Werror \
            -DDART_HOST_BUILD=1 -I../dart_remote_bridge -I.
SRC  = dart_tests.cpp ../dart_remote_bridge/dart_core.cpp
BIN  = dart_tests
PYTHON ?= python3

all: $(BIN)

$(BIN): $(SRC) dart_test_harness.h ../dart_remote_bridge/dart_core.h \
        ../dart_remote_bridge/dart_board_config.h ../dart_remote_bridge/dart_hal.h
	$(CXX) $(CXXFLAGS) -o $@ $(SRC)

run: $(BIN)
	./$(BIN)

sanitize:
	$(CXX) -std=c++11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
	  -DDART_HOST_BUILD=1 -I../dart_remote_bridge -I. -o dart_tests_asan $(SRC)
	./dart_tests_asan

# Independent confirmation that the ORIGINAL sketch really has the defects the
# candidate claims to fix.  Requires -fpermissive: the original passes a
# const char* where an int millisecond delay is expected.
reference:
	$(MAKE) -C reference run

# Virtual DART board exposed on a PTY so the application can be driven
# end-to-end with no hardware.
pty_sim: pty_sim.cpp dart_test_harness.h ../dart_remote_bridge/dart_core.cpp
	$(CXX) -std=c++11 -O1 -g -Wall -Wextra -DDART_HOST_BUILD=1 \
	  -I../dart_remote_bridge -I. -o pty_sim pty_sim.cpp \
	  ../dart_remote_bridge/dart_core.cpp

# Drives real JAMboreeVision code paths against pty_sim using a throwaway
# JAMBOREE_BASE / JAMBOREE_VISION_DATA.  Never reads the live configuration.
vision: pty_sim
	cd ../.. && $(PYTHON) firmware/test/vision_virtual_serial_test.py

# Compiled-MCU emulation.  Exits 77 (skip) if simavr/avr-gdb are absent.
emulate:
	$(MAKE) -C emu run

all-layers: run sanitize reference emulate vision

clean:
	rm -f $(BIN) dart_tests_asan pty_sim
	$(MAKE) -C reference clean
	$(MAKE) -C emu clean

.PHONY: all run sanitize reference vision emulate all-layers clean
