# Builds the UNMODIFIED reference sketch natively to confirm its defects.
# -fpermissive is REQUIRED: the reference calls resetRemote(remoteNum, "80"),
# passing a const char* where an int is expected (defect R7).
CXX      ?= g++
CXXFLAGS ?= -std=c++11 -O0 -g -fpermissive -w -I.
SRC  = ref_defect_tests.cpp arduino_shim.cpp
BIN  = ref_defect_tests

all: $(BIN)
$(BIN): $(SRC) arduino_shim.h reference_sketch.ino.txt
	$(CXX) $(CXXFLAGS) -o $@ $(SRC)
run: $(BIN)
	./$(BIN)
clean:
	rm -f $(BIN)
.PHONY: all run clean
