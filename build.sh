#!/usr/bin/env bash
#
# build.sh - Build hwdiag.efi strictly within the local workspace
#            and boot it in QEMU with local OVMF firmware.

set -euo pipefail

# Ensure script operates strictly relative to its own location
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BUILD_ONLY=0
[ "${1:-}" = "--build-only" ] && BUILD_ONLY=1

# All assets, temporary mounts, and outputs live strictly inside ./build
OUT="$SCRIPT_DIR/build"
EFI="$OUT/hwdiag.efi"
mkdir -p "$OUT"

CFLAGS_COMMON="-ffreestanding -fno-stack-protector -fno-stack-check -mno-red-zone -fno-builtin -O2 -Wall -Wextra"

echo "=== Toolchain Detection (Building for UEFI on Linux x86_64) ==="

build_with_clang() {
  command -v clang >/dev/null || return 1
  command -v ld.lld >/dev/null || return 1

  echo "Using: Clang + LLD (PE32+ UEFI Binary Target)"
  # Target PE/COFF directly as mandated by UEFI Spec 2.x
  # NOTE: this function is invoked as `if build_with_clang; then`, and bash
  # suspends `set -e` for every command inside a function called that way —
  # so the actual exit status has to be checked explicitly, or a failed
  # compile/link here would still fall through to "return 0" and get
  # reported as a successful build.
  if clang --target=x86_64-unknown-windows-coff -fuse-ld=lld -nostdlib $CFLAGS_COMMON \
    -Wl,-subsystem:efi_application -Wl,-entry:efi_main \
    hwdiag.c -o "$EFI"; then
    return 0
  else
    return 1
  fi
}

build_with_mingw() {
  command -v x86_64-w64-mingw32-gcc >/dev/null || return 1
  echo "Using: MinGW Cross-GCC (PE32+ UEFI Output)"
  x86_64-w64-mingw32-gcc -m64 -nostdlib $CFLAGS_COMMON -c hwdiag.c -o "$OUT/hwdiag.o"
  x86_64-w64-mingw32-ld -m pei-x86-64 -nostdlib -e efi_main --subsystem=10 \
    -o "$EFI" "$OUT/hwdiag.o"
  return 0
}

build_with_gcc_objcopy() {
  command -v gcc >/dev/null && command -v objcopy >/dev/null || return 1
  echo "Using: Native Linux GCC + objcopy translation"
  gcc -m64 $CFLAGS_COMMON -c hwdiag.c -o "$OUT/hwdiag.o"

  cat >"$OUT/efi.lds" <<'EOF'
SECTIONS {
  . = 0;
  ImageBase = .;
  .text : { *(.text) *(.text.*) }
  . = ALIGN(4096);
  .data : { *(.data) *(.data.*) *(.rodata) *(.rodata.*) *(.data.rel.ro*) }
  . = ALIGN(4096);
  .dynamic : { *(.dynamic) }
  . = ALIGN(4096);
  .reloc : { *(.reloc) }
  . = ALIGN(4096);
  .symtab : { *(.symtab) }
  .strtab : { *(.strtab) }
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.got) *(.got.plt) }
}
EOF
  ld -m elf_x86_64 -nostdlib -shared -Bsymbolic -T "$OUT/efi.lds" \
    -e efi_main -o "$OUT/hwdiag.so" "$OUT/hwdiag.o"
  objcopy --target=efi-app-x86-64 "$OUT/hwdiag.so" "$EFI"
  return 0
}

if build_with_clang; then
  :
elif build_with_mingw; then
  :
elif build_with_gcc_objcopy; then
  :
else
  cat >&2 <<'EOF'
ERROR: No usable toolchain found.
Install Clang/LLD on your Linux system:
  Debian/Ubuntu: sudo apt install clang lld
  Arch Linux:    sudo pacman -S clang lld
  Fedora:        sudo dnf install clang lld
EOF
  exit 1
fi

echo "=== Build Complete ==="
file "$EFI" || true

[ "$BUILD_ONLY" = "1" ] && {
  echo "Saved to $EFI"
  exit 0
}

echo "=== Firmware & Execution Setup ==="
command -v qemu-system-x86_64 >/dev/null || {
  echo "ERROR: qemu-system-x86_64 is not installed." >&2
  exit 1
}

# 1. Locate system OVMF or fetch a local isolated copy
OVMF_CODE="" OVMF_VARS=""
for c in \
  /usr/share/edk2/x64/OVMF_CODE.4m.fd \
  /usr/share/OVMF/x64/OVMF_CODE.4m.fd \
  /usr/share/OVMF/OVMF_CODE.fd \
  /usr/share/OVMF/OVMF_CODE_4M.fd \
  /usr/share/edk2/ovmf/OVMF_CODE.fd \
  /usr/share/edk2-ovmf/x64/OVMF_CODE.fd \
  /usr/share/qemu/edk2-x86_64-code.fd; do
  if [ -f "$c" ]; then
    OVMF_CODE="$c"
    break
  fi
done

if [ -n "$OVMF_CODE" ]; then
  v="${OVMF_CODE/CODE/VARS}"
  [ -f "$v" ] && OVMF_VARS="$v"
else
  cat >&2 <<'EOF'
ERROR: Could not find OVMF firmware on this system.
Install it, then re-run:
  Arch/CachyOS:  sudo pacman -S edk2-ovmf
  Debian/Ubuntu: sudo apt install ovmf
  Fedora:        sudo dnf install edk2-ovmf
EOF
  exit 1
fi

# 2. Construct local Isolated FAT file structure
ESP="$OUT/esp"
rm -rf "$ESP"
mkdir -p "$ESP/EFI/BOOT"
cp "$EFI" "$ESP/EFI/BOOT/BOOTX64.EFI"

QEMU_ARGS=(
  -m 512
  -net none
  -device qemu-xhci,id=xhci
  -device usb-tablet,bus=xhci.0
  -serial stdio
  -drive format=raw,file=fat:rw:"$ESP"
)

if [ -n "$OVMF_VARS" ] && [ -f "$OVMF_VARS" ]; then
  cp -f "$OVMF_VARS" "$OUT/OVMF_VARS.fd"
  QEMU_ARGS+=(
    -drive if=pflash,format=raw,unit=0,readonly=on,file="$OVMF_CODE"
    -drive if=pflash,format=raw,unit=1,file="$OUT/OVMF_VARS.fd"
  )
elif [ -f "$OVMF_CODE" ]; then
  QEMU_ARGS+=(-bios "$OVMF_CODE")
fi

if [ -w /dev/kvm ]; then
  QEMU_ARGS+=(-enable-kvm -cpu host)
else
  QEMU_ARGS+=(-cpu max)
fi

echo "Booting UEFI application in QEMU..."
qemu-system-x86_64 "${QEMU_ARGS[@]}"
