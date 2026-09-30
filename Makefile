CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2
SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
SDL_LIBS   := $(shell sdl2-config --libs 2>/dev/null || echo -lSDL2)
TARGET   = chip8
SOURCES  = src/main.cpp src/chip8.cpp
OBJECTS  = $(SOURCES:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJECTS) $(SDL_LIBS)

src/main.o: src/main.cpp src/chip8.h
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) -c $< -o $@

src/chip8.o: src/chip8.cpp src/chip8.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Headless pre-flight checks (no SDL / display required)
test: tests/test_opcodes
	./tests/test_opcodes

tests/test_opcodes: tests/test_opcodes.cpp src/chip8.cpp src/chip8.h
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_opcodes.cpp src/chip8.cpp

clean:
	rm -f $(OBJECTS) $(TARGET) tests/test_opcodes

.PHONY: all clean test
