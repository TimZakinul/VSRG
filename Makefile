# VSRG Makefile
# Compiles the rhythm game with SFML

CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra
LDFLAGS = -lsfml-graphics -lsfml-window -lsfml-system -lsfml-audio -pthread

TARGET = vsrg
SRC = main.cpp

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)

# Run with a test audio file (provide your own music.wav)
run: $(TARGET)
	./$(TARGET) music.wav

# Debug build
debug: CXXFLAGS += -g -DDEBUG
debug: $(TARGET)

# Install SFML on Ubuntu/Debian
install-deps-ubuntu:
	sudo apt-get update
	sudo apt-get install -y libsfml-dev

# Install SFML on Fedora
install-deps-fedora:
	sudo dnf install -y SFML-devel

# Install SFML on Arch
install-deps-arch:
	sudo pacman -S sfml
