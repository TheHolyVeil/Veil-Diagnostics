#!/usr/bin/env bash
#
# build.sh - Build hwdiag.efi strictly within the local workspace
#            and boot it in QEMU with local OVMF firmware.

set -euo pipefail

# Ensure script operates strictly relative to its own location
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BUILD_ONLY=0
COPY_EFI=0

for arg in "$@"; do
  case "$arg" in
  --build-only) BUILD_ONLY=1 ;;
  --copy) COPY_EFI=1 ;;
  esac
done

# All assets, temporary mounts, and outputs live strictly inside ./build
OUT="$SCRIPT_DIR/build"
EFI="$OUT/hwdiag.efi"
mkdir -p "$OUT"

echo "=== Building with Makefile (make -> hwdiag.efi) ==="

command -v make >/dev/null || {
  echo "ERROR: make is not installed." >&2
  exit 1
}

command -v zig >/dev/null || {
  cat >&2 <<'EOF'
ERROR: zig is not installed (required for zig cc compiler driver).
Install it (e.g. https://ziglang.org/download/, or your distro's zig package):
  Arch/CachyOS:  sudo pacman -S zig
EOF
  exit 1
}

make

echo "=== Build Complete ==="
file "$EFI" || true

if [ "$COPY_EFI" = "1" ]; then
  echo "=== Copying $EFI to /boot/EFI/hwdiag.efi via sudo cp ==="
  sudo mkdir -p /boot/EFI
  sudo cp -f "$EFI" /boot/EFI/hwdiag.efi
  echo "Successfully copied to /boot/EFI/hwdiag.efi"
fi

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
echo '\EFI\BOOT\BOOTX64.EFI' >"$ESP/startup.nsh"

QEMU_ARGS=(
  -m 512
  -net none
  -device qemu-xhci
  -device usb-tablet
  -serial stdio
  -monitor unix:"$OUT/hwdiag-mon.sock",server,nowait
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
