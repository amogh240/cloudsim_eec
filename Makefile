# Compiler
CXX = g++
# Compiler flags
CXXFLAGS = -Wall -std=c++17
# Include directories
INCLUDES = -I.

# Pre-compiled object files (do not recompile or delete these)
PRECOMPILED_OBJ = Init.o Machine.o main.o Simulator.o Task.o VM.o

# Source files to compile
SRC = Scheduler.cpp

# Object files from source
OBJ = $(SRC:.cpp=.o)

# Executable
TARGET = simulator

# Default target
all: $(TARGET)

# Build target: link precompiled objects with our compiled scheduler
$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $(TARGET) $(PRECOMPILED_OBJ) $(OBJ)

# Compile source files into object files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Clean up only our generated files (preserve precompiled .o files)
clean:
	rm -f Scheduler.o $(TARGET)