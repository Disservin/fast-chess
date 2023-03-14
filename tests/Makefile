CXX      = g++
CXXFLAGS = -O2 -std=c++17 -g3 -fno-omit-frame-pointer -Wall -Wextra -MMD -MP
NATIVE   = -march=x86-64
SUFFIX   = .exe

# Detect Windows
ifeq ($(OS), Windows_NT)
	uname_S  := Windows
else
ifeq ($(COMP), MINGW)
	uname_S  := Windows
else
	LDFLAGS := -pthread
	uname_S := $(shell uname -s)
	SUFFIX  :=
endif
endif

# Different native flag for macOS
ifeq ($(uname_S), Darwin)
	NATIVE =	
	LDFLAGS = 
endif

ifeq ($(san), asan)
	LDFLAGS += -fsanitize=address
endif

ifeq ($(san), memory)
	LDFLAGS += -fsanitize=memory -fPIE -pie
endif

ifeq ($(san), undefined)
	LDFLAGS += -fsanitize=undefined
endif

TARGET := fast-chess-tests

# Find all .cpp files in subdirectories and specified directory
ALL_SOURCES = $(wildcard ../src/*.cpp ../src/chess/*.cpp ../src/engines/*.cpp *.cpp)
SRCS := $(filter-out ../src/main.cpp, $(ALL_SOURCES))

# Generate .o file names from .cpp file names
OBJS := $(SRCS:%.cpp=%.o)

# Generate .d file names from .cpp file names
DEPS := $(SRCS:%.cpp=%.d)

# Default target
all: $(TARGET)
	$(CXX) $(CXXFLAGS) $(NATIVE) ./data/engine/dummy_engine.cpp -o ./data/engine/dummy_engine$(SUFFIX) $(LDFLAGS)

# Link the .o files to generate the executable
$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# Compile the .cpp files to .o files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@ $(LDFLAGS)

# Include the .d files to avoid unnecessary recompilations
-include $(DEPS)

# Clean up intermediate files
clean:
	rm -f $(OBJS) $(DEPS)

.PHONY: all clean