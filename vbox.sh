set -e
cd /home/abyss/info
mkdir -p vbox
IMG=vbox/hwdiag.img
ESP=vbox/esp.img

# 1. Raw 64MB disk image
truncate -s 64M "$IMG"

# 2. GPT with a single EFI System Partition starting at 1MiB
sgdisk --clear --new=1:2048:0 --typecode=1:ef00 --change-name=1:"EFI System" "$IMG"

# 3. Read back exact start/end sectors sgdisk actually used
START=$(sgdisk -i 1 "$IMG" | awk -F': ' '/First sector/{print $2}' | awk '{print $1}')
END=$(sgdisk -i 1 "$IMG" | awk -F': ' '/Last sector/{print $2}' | awk '{print $1}')
COUNT=$((END - START + 1))
echo "partition: start=$START end=$END count=$COUNT sectors"

# 4. Build the FAT32 ESP as a standalone file of exactly that size
dd if=/dev/zero of="$ESP" bs=512 count=$COUNT status=none
mkfs.fat -F32 "$ESP" >/dev/null

# 5. Drop the EFI binary into it via mtools (no mount, no sudo)
mmd -i "$ESP" ::EFI ::EFI/BOOT
mcopy -i "$ESP" build/hwdiag.efi ::EFI/BOOT/BOOTX64.EFI

# 6. Splice the formatted ESP back into the GPT image at the right offset
dd if="$ESP" of="$IMG" bs=512 seek=$START conv=notrunc status=none

echo "RAW_IMAGE_DONE"
ls -la "$IMG" "$ESP"
