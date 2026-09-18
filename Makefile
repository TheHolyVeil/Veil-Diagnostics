# Makefile - hwdiag.efi UEFI Application
# Built via `zig cc` (Clang) & `zig build-obj` with -O3/-OReleaseFast and -flto.

CC       := zig cc
TARGET   := x86_64-uefi
CFLAGS   := --target=$(TARGET) -std=c11 -ffreestanding -fno-stack-protector \
            -fshort-wchar -mno-stack-arg-probe -O3 -flto -Wall -Isrc
LDFLAGS  := --target=$(TARGET) -fuse-ld=lld -nostdlib -O3 -flto -Wl,-e,EfiMain

SRC_DIR   := src
BUILD_DIR := build
C_SRCS    := $(wildcard $(SRC_DIR)/*.c)
C_OBJS    := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(C_SRCS))
ZIG_SRCS  := $(wildcard $(SRC_DIR)/*.zig)
ZIG_OBJS  := $(patsubst $(SRC_DIR)/%.zig,$(BUILD_DIR)/%.o,$(ZIG_SRCS))
OBJS      := $(C_OBJS) $(ZIG_OBJS)
EFI       := $(BUILD_DIR)/hwdiag.efi

.PHONY: all clean

all: $(EFI)

$(EFI): $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(wildcard $(SRC_DIR)/*.h) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.zig | $(BUILD_DIR)
	zig build-obj -O ReleaseFast -target $(TARGET) $< -femit-bin=$@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)
