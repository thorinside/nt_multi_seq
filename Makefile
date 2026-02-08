# Makefile for nt_seq - Multi-Channel Sequencer Plugin for disting NT
#
# Dual build targets:
#   make hardware - Build ARM .o for disting NT hardware
#   make test     - Build native .dylib/.so for desktop testing in VCV Rack nt_emu
#   make clean    - Clean build artifacts

# Project configuration
PROJECT = nt_seq
DISTINGNT_API = distingNT_API

# Source files
SOURCES = \
	nt_seq.cpp \
	nt_seq_construct.cpp \
	nt_seq_step.cpp \
	nt_seq_draw.cpp \
	nt_seq_params.cpp \
	scale/ScaleQuantizer.cpp \
	clock/ClockProcessor.cpp \
	engines/SomaEngine.cpp \
	engines/AeSequencerEngine.cpp \
	engines/SeqMarkovEngine.cpp \
	engines/ThorpEngine.cpp

# Include paths
INCLUDES = \
	-I$(DISTINGNT_API)/include \
	-I.

DEFINES_COMMON = -DTEST -D_USE_MATH_DEFINES
DEFINES_HARDWARE = $(DEFINES_COMMON)
DEFINES_TEST = $(DEFINES_COMMON) -DNT_EMU_DEBUG

# Compiler flags - common
CXXFLAGS_COMMON = -std=c++11 -Wall -Wextra -fno-rtti -fno-exceptions -Wno-unused-parameter

# Compiler flags - hardware (ARM Cortex-M7)
CXX_ARM = arm-none-eabi-g++
CXXFLAGS_ARM = $(CXXFLAGS_COMMON) $(DEFINES_HARDWARE) \
	-mcpu=cortex-m7 \
	-mfpu=fpv5-d16 \
	-mfloat-abi=hard \
	-O3 \
	-ffast-math \
	-funroll-loops \
	-fdata-sections \
	-ffunction-sections

# Compiler flags - desktop test
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    CXX_TEST = clang++
    DYLIB_EXT = dylib
else
    CXX_TEST = g++
    DYLIB_EXT = so
endif

CXXFLAGS_TEST = $(CXXFLAGS_COMMON) $(DEFINES_TEST) -O2 -fPIC

# Output directories
PLUGINS_DIR = plugins
BUILD_DIR = build

# Targets
.PHONY: all hardware test unit-test clean

all: hardware test

# Hardware target - ARM .o for disting NT
hardware: $(PLUGINS_DIR)/$(PROJECT).o

OBJS = $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(notdir $(SOURCES)))

$(BUILD_DIR)/%.o: %.cpp | $(BUILD_DIR)
	$(CXX_ARM) $(CXXFLAGS_ARM) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/%.o: engines/%.cpp | $(BUILD_DIR)
	$(CXX_ARM) $(CXXFLAGS_ARM) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/%.o: scale/%.cpp | $(BUILD_DIR)
	$(CXX_ARM) $(CXXFLAGS_ARM) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/%.o: clock/%.cpp | $(BUILD_DIR)
	$(CXX_ARM) $(CXXFLAGS_ARM) $(INCLUDES) -c $< -o $@

$(PLUGINS_DIR)/$(PROJECT).o: $(OBJS) | $(PLUGINS_DIR)
	$(CXX_ARM) -r $(OBJS) -o $@
	@echo "Hardware build complete: $@"
	@ls -lh $@

# Test target - native .dylib/.so for VCV Rack nt_emu
test: $(PLUGINS_DIR)/$(PROJECT).$(DYLIB_EXT)

$(PLUGINS_DIR)/$(PROJECT).$(DYLIB_EXT): $(SOURCES) | $(PLUGINS_DIR) $(BUILD_DIR)
ifeq ($(UNAME_S),Darwin)
	$(CXX_TEST) $(CXXFLAGS_TEST) $(INCLUDES) -dynamiclib -undefined dynamic_lookup $(SOURCES) -o $@
else
	$(CXX_TEST) $(CXXFLAGS_TEST) $(INCLUDES) -shared $(SOURCES) -o $@
endif
	@echo "Desktop test build complete: $@"
	@ls -lh $@

# Create output directories
$(PLUGINS_DIR):
	mkdir -p $(PLUGINS_DIR)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Unit test target - standalone spec logic tests (no NT API dependency)
unit-test: $(BUILD_DIR)/test_spec_logic
	./$(BUILD_DIR)/test_spec_logic

$(BUILD_DIR)/test_spec_logic: tests/test_spec_logic.cpp spec_helpers.h | $(BUILD_DIR)
	$(CXX_TEST) -std=c++11 -Wall -Wextra -Wno-unused-parameter -I. tests/test_spec_logic.cpp -o $@

# Clean build artifacts
clean:
	rm -rf $(PLUGINS_DIR) $(BUILD_DIR)
	@echo "Build artifacts cleaned"
