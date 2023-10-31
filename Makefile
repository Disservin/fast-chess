CXX = g++
CXXFLAGS = -O3 -std=c++17 -Wall -Wextra -DNDEBUG
INCLUDES = -Isrc
DEPFLAGS = -MMD -MP
TMPDIR = tmp

# Detect Windows
ifeq ($(OS), Windows_NT)
	MKDIR    := mkdir
	uname_S  := Windows
	SUFFIX   := .exe
	LDFLAGS  := -static -static-libgcc -static-libstdc++ -Wl,--no-as-needed
else
ifeq ($(COMP), MINGW)
	MKDIR    := mkdir
	uname_S  := Windows
	SUFFIX   := .exe
else
	MKDIR   := mkdir -p
	LDFLAGS := -pthread
	uname_S := $(shell uname -s)
	SUFFIX  :=
endif
endif

SRC_FILES := $(wildcard src/*.cpp) $(wildcard src/*/*.cpp) $(wildcard src/*/*/*.cpp)
TEST_SRC  := $(wildcard tests/*.cpp)

ifeq ($(MAKECMDGOALS),tests)
	CXXFLAGS  := -O2 -std=c++17 $(INCLUDES) -g3 -fno-omit-frame-pointer -Wall -Wextra -DTESTS
	SRC_FILES := $(filter-out src/main.cpp, $(SRC_FILES)) $(TEST_SRC)
	OBJECTS   := $(patsubst %.cpp,$(TMPDIR)/%.o,$(SRC_FILES))
	DEPENDS   := $(patsubst %.cpp,$(TMPDIR)/%.d,$(SRC_FILES))
	TARGET    := fast-chess-tests
else
	CXXFLAGS  += $(INCLUDES)
	NATIVE 	  := -march=native
	OBJECTS   := $(patsubst %.cpp,$(TMPDIR)/%.o,$(SRC_FILES))
	DEPENDS   := $(patsubst %.cpp,$(TMPDIR)/%.d,$(SRC_FILES))
	TARGET    := fast-chess
endif

ifeq ($(build), debug)
	CXXFLAGS := -g3 -O3  $(INCLUDES) -std=c++17 -Wall -Wextra -pedantic
endif

ifeq ($(build), release)
	CXXFLAGS := -O3 -std=c++17  $(INCLUDES) -Wall -Wextra -pedantic -DNDEBUG
	LDFLAGS  := -lpthread -static -static-libgcc -static-libstdc++ -Wl,--no-as-needed
	NATIVE   := -march=x86-64
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

ifeq ($(san), thread)
	LDFLAGS += -fsanitize=thread
endif

# Versioning
GIT_SHA = $(shell git rev-parse --short HEAD 2>/dev/null)
ifneq ($(GIT_SHA), )
	CXXFLAGS += -DGIT_SHA=\"$(GIT_SHA)\"
endif

# Versioning
GIT_DATE = $(shell git show -s --date=format:'%Y%m%d' --format=%cd HEAD 2>/dev/null)
ifneq ($(GIT_DATE), )
	CXXFLAGS += -DGIT_DATE=\"$(GIT_DATE)\"
endif

# Compile with Cutechess output support
ifeq ($(USE_CUTE), true)
	CXXFLAGS += -DUSE_CUTE
endif

.PHONY: clean all tests FORCE

all: $(TARGET)

tests: $(TARGET)
	$(CXX) $(CXXFLAGS) ./tests/data/engine/dummy_engine.cpp -o ./tests/data/engine/dummy_engine$(SUFFIX) $(LDFLAGS)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(NATIVE) -MMD -MP -o $@ $^ $(LDFLAGS)

$(TMPDIR)/%.o: %.cpp | $(TMPDIR)
	$(CXX) $(CXXFLAGS) $(NATIVE) -MMD -MP -c $< -o $@ $(LDFLAGS)

$(TMPDIR):
	$(MKDIR) "$(TMPDIR)" "$(TMPDIR)/src" "$(TMPDIR)/src/engines" "$(TMPDIR)/src/chess" "$(TMPDIR)/src/matchmaking" "$(TMPDIR)/src/util" "$(TMPDIR)/tests"

clean:
	rm -rf $(TMPDIR)

-include $(DEPENDS)

tmp/src/options.o: FORCE
FORCE:
