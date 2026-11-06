# Host-side tests (no hardware, no Arduino toolchain needed): `make -C extras/test`
#
#  golden : replays frequency/register sequences and diffs every 24-bit SPI
#           word against output captured from v1.0.1 (hardware-verified).
#           Proves default behaviour has not changed.
#  api    : checks the v1.1.0 additions (Hz API, auto-MOD, validation,
#           uint8 INT-wrap regression, autoStart=false).
CXX   ?= g++
FLAGS := -std=c++17 -Wall -Wextra -Imock -I../../src

all: golden api

golden: golden_test.cpp ../../src/ADRF6655.cpp
	$(CXX) $(FLAGS) golden_test.cpp ../../src/ADRF6655.cpp -o golden_test
	./golden_test | diff - golden_v1.0.1.txt && echo "golden: IDENTICAL to v1.0.1"

api: api_test.cpp ../../src/ADRF6655.cpp
	$(CXX) $(FLAGS) api_test.cpp ../../src/ADRF6655.cpp -o api_test
	./api_test

clean:
	rm -f golden_test api_test
