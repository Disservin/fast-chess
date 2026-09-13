#Compiler
CXX              := g++

TARGET           := fastchess
BINARY_PATH	  	 := .

# Source, Includes
SRCDIR           := src
THIRDPARTYDIR    := third_party
TESTDIR          := tests
BUILDDIR         := tmp
INCDIR           := src
RANDOM_MOVER      = $(TESTDIR)/mock/engine/random_mover$(SUFFIX)
DUMMY_ENGINE      = $(TESTDIR)/mock/engine/dummy_engine$(SUFFIX)

#Flags, Libraries and Includes
CXXFLAGS         := -O3 -std=c++17 -Wall -Wextra -DNDEBUG
CXXFLAGS_TEST    := -O0 -std=c++17 -Wall -Wextra -pedantic -Wuninitialized -fno-omit-frame-pointer
CXXFLAGS_COV     := -O0 --coverage -g -std=c++17 -Wall -Wextra -pedantic -Wuninitialized -g3 -fno-omit-frame-pointer
INC              := -I$(INCDIR) -Ithird_party -Ithird_party/fmt/include

# CLI defined..
EXTRA_LDFLAGS    :=

SRC_FILES        := \
	$(SRCDIR)/cli/cli.cpp \
	$(SRCDIR)/cli/sanitize.cpp \
	$(SRCDIR)/core/globals/globals.cpp \
	$(SRCDIR)/core/logger/logger.cpp \
	$(SRCDIR)/core/printing/printing.cpp \
	$(SRCDIR)/core/time/time.cpp \
	$(SRCDIR)/engine/uci_engine.cpp \
	$(SRCDIR)/game/book/epd_reader.cpp \
	$(SRCDIR)/game/book/opening_book.cpp \
	$(SRCDIR)/game/book/pgn_reader.cpp \
	$(SRCDIR)/game/pgn/pgn_builder.cpp \
	$(SRCDIR)/game/pgn/openings_data.cpp \
	$(SRCDIR)/game/timecontrol/timecontrol.cpp \
	$(SRCDIR)/main.cpp \
	$(SRCDIR)/matchmaking/syzygy.cpp \
	$(SRCDIR)/matchmaking/elo/elo_pentanomial.cpp \
	$(SRCDIR)/matchmaking/elo/elo_wdl.cpp \
	$(SRCDIR)/matchmaking/match/match.cpp \
	$(SRCDIR)/matchmaking/sprt/sprt.cpp \
	$(SRCDIR)/matchmaking/tournament/base/tournament.cpp \
	$(SRCDIR)/matchmaking/tournament/roundrobin/roundrobin.cpp \
	$(SRCDIR)/matchmaking/tournament/tournament_manager.cpp \
	$(THIRDPARTYDIR)/pyrrhic/tbprobe.cpp \
	$(THIRDPARTYDIR)/fmt/src/format.cc

SRC_FILES_TEST   := \
	$(TESTDIR)/cli_test.cpp \
	$(TESTDIR)/config_test.cpp \
	$(TESTDIR)/elo_test.cpp \
	$(TESTDIR)/epd_builder_test.cpp \
	$(TESTDIR)/epd_reader_test.cpp \
	$(TESTDIR)/fd_limit_test.cpp \
	$(TESTDIR)/functions_test.cpp \
	$(TESTDIR)/main.cpp \
	$(TESTDIR)/match_test.cpp \
	$(TESTDIR)/opening_test.cpp \
	$(TESTDIR)/option_test.cpp \
	$(TESTDIR)/output_factory_test.cpp \
	$(TESTDIR)/pgn_builder_test.cpp \
	$(TESTDIR)/pgn_reader_test.cpp \
	$(TESTDIR)/player.cpp \
	$(TESTDIR)/scoreboard_test.cpp \
	$(TESTDIR)/sprt_test.cpp \
	$(TESTDIR)/syzygy_test.cpp \
	$(TESTDIR)/timecontrol_test.cpp \
	$(TESTDIR)/tournament_manager_test.cpp \
	$(TESTDIR)/uci_engine_test.cpp \
	$(TESTDIR)/crc32.cpp

# Only used for formatting
HEADERS := $(shell find $(SRCDIR) -name "*.hpp") $(shell find $(TESTDIR) -maxdepth 1 -name "*.hpp")

# Windows file extension
SUFFIX           := .exe

NATIVE 	         := -march=native

ifeq ($(MAKECMDGOALS),$(TESTDIR))
	CXXFLAGS  := $(CXXFLAGS_TEST)
	SRC_FILES := $(filter-out src/main.cpp, $(SRC_FILES)) $(SRC_FILES_TEST)
	TARGET    := $(TARGET)-tests
	NATIVE    := 
endif

ifeq ($(MAKECMDGOALS), coverage)
	CXXFLAGS  := $(CXXFLAGS_COV)
	SRC_FILES := $(filter-out src/main.cpp, $(SRC_FILES)) $(SRC_FILES_TEST)
	TARGET    := $(TARGET)-tests
	NATIVE    := 
endif

ifeq ($(scan), yes)
	CXX := scan-build-19 clang++-19
endif

TARGET   := $(BINARY_PATH)/$(TARGET)

DEPFLAGS  := -MMD -MP
MKDIR	  := mkdir -p

ifeq ($(OS), Windows_NT)
	uname_S  := Windows
else
ifeq ($(COMP), MINGW)
	uname_S  := Windows
else
	SUFFIX  :=
	LDFLAGS := -pthread -lstdc++fs
	uname_S := $(shell uname -s)
	uname_M := $(shell uname -m)
endif
endif

ifeq ($(build), debug)
	CXXFLAGS := -O0 -std=c++17 -Wall -Wextra -pedantic -Wuninitialized -g3 -D_GLIBCXX_ASSERTIONS
	ifeq ($(uname_S), Windows)
		LDFLAGS += -static
	endif
endif

ifeq ($(build), release)
	LDFLAGS  :=
	NATIVE   := -march=x86-64
	CXXFLAGS += -DRELEASE

	ifneq (,$(findstring clang,$(CXX)))
	else ifneq (,$(findstring g++,$(CXX)))
		LDFLAGS += -Wl,--no-as-needed -lstdc++fs 
	endif

	LDFLAGS += -lpthread -static -static-libgcc -static-libstdc++
else
# Not all architectures support -march=native
ifeq (,$(findstring $(uname_M),aarch64 arm arm64 x86 x86_64 i386 i686))
	NATIVE =
endif
endif

# Different native flag for macOS
ifeq ($(uname_S), Darwin)
	NATIVE =
	LDFLAGS =
endif

# Compile with address sanitizer
ifeq ($(san), asan)
	LDFLAGS += -fsanitize=address
endif

# Compile with memory sanitizer
ifeq ($(san), memory)
	LDFLAGS += -fsanitize=memory -fPIE -pie
endif

# Compile with undefined behavior sanitizer
ifeq ($(san), undefined)
	LDFLAGS += -fsanitize=undefined
endif

# Compile with thread sanitizer
ifeq ($(san), thread)
	LDFLAGS += -fsanitize=thread
endif

# Versioning
GIT_SHA = $(shell git rev-parse --short HEAD 2>/dev/null)
ifneq ($(GIT_SHA), )
	CXXFLAGS += -DGIT_SHA=\"$(GIT_SHA)\"
endif

GIT_DATE = $(shell git show -s --date=format:'%Y%m%d' --format=%cd HEAD 2>/dev/null)
ifneq ($(GIT_DATE), )
	CXXFLAGS += -DGIT_DATE=\"$(GIT_DATE)\"
endif

COMPILE_MSG=
ifneq ($(COMPILE_MSG),)
	CXXFLAGS += -DCOMPILE_MSG="\"$(COMPILE_MSG)\""
endif

# Compile with Cutechess output support
ifeq ($(USE_CUTE), true)
	CXXFLAGS += -DUSE_CUTE
endif

ifeq ($(ZLIB), true)
	CXXFLAGS += -DUSE_ZLIB
	LDFLAGS += -lz
endif

include ./configuration/spawn-check.mk

ifeq ($(HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR),1)
    CXXFLAGS += -DHAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR
endif
ifeq ($(HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR_NP),1)
    CXXFLAGS += -DHAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR_NP
endif

OBJECTS   := $(patsubst %.cpp,$(BUILDDIR)/%.o,$(filter %.cpp,$(SRC_FILES))) \
             $(patsubst %.cc,$(BUILDDIR)/%.o,$(filter %.cc,$(SRC_FILES)))
DEPENDS   := $(patsubst %.cpp,$(BUILDDIR)/%.d,$(filter %.cpp,$(SRC_FILES))) \
             $(patsubst %.cc,$(BUILDDIR)/%.d,$(filter %.cc,$(SRC_FILES)))

.PHONY: clean all tests FORCE coverage

all: $(TARGET)

coverage: tests
	cd .. && ./fastchess-tests
	mkdir -p coverage && cd coverage &&  gcovr --html-details coverage.html --gcov-ignore-errors=no_working_dir_found --gcov-ignore-parse-errors -f "../src" -r ../. ..

tests: $(TARGET) $(RANDOM_MOVER) $(DUMMY_ENGINE)

format: $(SRC_FILES) $(HEADERS)
	clang-format -i $^

tidy: $(SRC_FILES) $(HEADERS)
	clang-tidy $^

$(RANDOM_MOVER): $(TESTDIR)/mock/engine/random_mover.cpp
	$(CXX) -O3 -std=c++17 $< -o $@

$(DUMMY_ENGINE): $(TESTDIR)/mock/engine/dummy_engine.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS) $(EXTRA_LDFLAGS)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(NATIVE) $(INC) $(DEPFLAGS) -o $@ $^ $(LDFLAGS) $(EXTRA_LDFLAGS)

$(BUILDDIR)/%.o: %.cpp | build_directories
	$(CXX) $(CXXFLAGS) $(NATIVE) $(INC) $(DEPFLAGS) -c $< -o $@

$(BUILDDIR)/%.o: %.cc | build_directories
	$(CXX) $(CXXFLAGS) $(NATIVE) $(INC) $(DEPFLAGS) -c $< -o $@

build_directories:
	@$(shell mkdir -p $(dir $(OBJECTS)))

clean:
	rm -rf $(BUILDDIR) $(TARGET) $(TARGET)-tests $(TARGET)-tests.exe $(TARGET).exe ../random_mover $(RANDOM_MOVER) $(DUMMY_ENGINE)

-include $(DEPENDS)
