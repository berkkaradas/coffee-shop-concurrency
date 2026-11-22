CXX      ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -pedantic -pthread
RUNS     ?= 100

.PHONY: all run stress sanitize clean

all: build/cafe

build/cafe: src/main.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) src/main.cpp -o build/cafe

# full-speed run (takes a few minutes: every coffee takes 1-10 seconds)
run: build/cafe
	./build/cafe

# many fast runs, checks for hangs and wrong counts
stress:
	bash scripts/stress.sh $(RUNS) plain

# same, with ThreadSanitizer and AddressSanitizer + UBSan
sanitize:
	bash scripts/stress.sh 20 thread
	bash scripts/stress.sh 20 address

clean:
	rm -rf build
