CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O3 -Iinclude
LDFLAGS  :=

AR       := ar
ARFLAGS  := rcs

SRC_DIR  := src
INC_DIR  := include
OBJ_DIR  := build
BIN_DIR  := bin
LIB_NAME := gd-rom

ifeq ($(OS),Windows_NT)
	EXE_EXT    := .exe
	SHARED_EXT := .dll
	FPIC       :=
	MKDIR      := mkdir -p
	RM         := rm -rf
else
	EXE_EXT    :=
	SHARED_EXT := .so
	FPIC       := -fPIC
	MKDIR      := mkdir -p
	RM         := rm -rf
endif

CXXFLAGS += $(FPIC)

EXE_TARGET    := $(BIN_DIR)/$(LIB_NAME)$(EXE_EXT)
STATIC_TARGET := $(BIN_DIR)/lib$(LIB_NAME).a
SHARED_TARGET := $(BIN_DIR)/lib$(LIB_NAME)$(SHARED_EXT)

CORE_SRCS := $(SRC_DIR)/gd.cpp $(SRC_DIR)/elf.cpp $(SRC_DIR)/ip.cpp $(SRC_DIR)/scramble.cpp
EXE_SRCS  := $(SRC_DIR)/main.cpp $(CORE_SRCS)

CORE_OBJS := $(patsubst $(SRC_DIR)/%.cpp, $(OBJ_DIR)/%.o, $(CORE_SRCS))
EXE_OBJS  := $(patsubst $(SRC_DIR)/%.cpp, $(OBJ_DIR)/%.o, $(EXE_SRCS))

#
.PHONY: all exe static shared clean help

all: exe

exe: $(EXE_TARGET)

static: $(STATIC_TARGET)

shared: $(SHARED_TARGET)

$(EXE_TARGET): $(EXE_OBJS) | $(BIN_DIR)
	@echo "[LINK] Creating Executable: $@"
	@$(CXX) $(EXE_OBJS) -o $@ $(LDFLAGS)

$(STATIC_TARGET): $(CORE_OBJS) | $(BIN_DIR)
	@echo "[AR] Creating Static Library: $@"
	@$(AR) $(ARFLAGS) $@ $(CORE_OBJS)

$(SHARED_TARGET): $(CORE_OBJS) | $(BIN_DIR)
	@echo "[SHARED] Creating Shared Library: $@"
	@$(CXX) -shared $(CORE_OBJS) -o $@ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	@echo "[CXX] Compiling: $<"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR) $(BIN_DIR):
	@$(MKDIR) $@

clean:
	@echo "[CLEAN] Removing builds and object files..."
	@$(RM) $(OBJ_DIR) $(BIN_DIR)

help:
	@echo "Available build commands:"
	@echo "  make exe       - Build the 'gd-rom' executable (Default)"
	@echo "  make static    - Build 'libgd-rom.a' static library"
	@echo "  make shared    - Build 'libgd-rom$(SHARED_EXT)' dynamic library"
	@echo "  make all       - Build all three formats"
	@echo "  make clean     - Clear object files and outputs"