#!/bin/bash
set -e

cd "$(dirname "$0")"

echo "Building nth bootloader..."
cd ..
mkdir -p build
cd build
cmake ..
make
cd ../iso_scripts

if [ -z "$1" ]; then
    read -p "Enter the path to the extracted Windows ISO directory: " WIN_DIR
else
    WIN_DIR="$1"
fi

if [ -z "$WIN_DIR" ] || [ ! -d "$WIN_DIR" ]; then
    echo "Error: Extracted Windows ISO directory not found."
    echo "Usage: $0 [path_to_extracted_windows_iso]"
    exit 1
fi

echo "Preparing ISO staging environment..."
rm -rf win_esp win_iso_root
mkdir -p win_esp/EFI/BOOT
mkdir -p win_esp/efi/microsoft/boot

# Copy the BCD store, fonts, and resources required by the Windows Boot Manager
if [ -d "$WIN_DIR/efi/microsoft/boot" ]; then
    cp -r "$WIN_DIR/efi/microsoft/boot/"* win_esp/efi/microsoft/boot/
fi

# The original Windows bootloader is usually at efi/boot/bootx64.efi
# We will copy it as win_boot.efi so we can chainload it
if [ -f "$WIN_DIR/efi/boot/bootx64.efi" ]; then
    cp "$WIN_DIR/efi/boot/bootx64.efi" win_esp/EFI/BOOT/win_boot.efi
else
    echo "Could not find efi/boot/bootx64.efi in the Windows directory!"
    exit 1
fi

cp ../build/BOOTX64.EFI win_esp/EFI/BOOT/BOOTX64.EFI

cat <<EOF > win_esp/nth.cfg
timeout=5
Windows Setup=\EFI\BOOT\win_boot.efi
EOF

REQ_SIZE_MB=$(du -sm win_esp | cut -f1)
IMG_SIZE_MB=$((REQ_SIZE_MB + 40))

echo "Creating EFI FAT image (${IMG_SIZE_MB}MB)..."
dd if=/dev/zero of=efi.img bs=1M count=${IMG_SIZE_MB} status=none
mkfs.vfat -F 32 efi.img > /dev/null
mcopy -s -i efi.img win_esp/* ::/

mkdir -p win_iso_root
mv efi.img win_iso_root/efi.img

echo "Copying Windows installation files to ISO root..."
echo "This might take a while depending on the size of the Windows ISO..."
# We use rsync or cp to copy everything except the original bootx64.efi to avoid confusion
cp -r "$WIN_DIR"/* win_iso_root/

echo "Generating UEFI ISO with Windows..."
# Note: Windows ISOs typically use UDF, but for a basic UEFI-only bootable ISO with xorriso, 
# this standard UEFI ISO generation will often work for the setup phase in QEMU/Virtualbox.
xorriso -as mkisofs \
    -iso-level 3 \
    -R -f \
    -e efi.img \
    -no-emul-boot \
    -isohybrid-gpt-basdat \
    -o nth_windows.iso \
    win_iso_root > /dev/null 2>&1

echo "Success! Generated nth_windows.iso"

echo "Booting Windows ISO in QEMU..."
cp /usr/share/OVMF/OVMF_VARS_4M.fd . 2>/dev/null || true

OVMF_CODE=""
for p in /usr/share/OVMF/OVMF_CODE_4M.fd /usr/share/OVMF/OVMF_CODE.fd /usr/share/edk2-ovmf/x64/OVMF_CODE.fd; do
    if [ -f "$p" ]; then
        OVMF_CODE="$p"
        break
    fi
done

if [ -z "$OVMF_CODE" ]; then
    echo "Warning: OVMF_CODE not found. Skipping QEMU boot."
    echo "You can manually boot nth_windows.iso in VirtualBox (make sure EFI is enabled)."
    exit 0
fi

qemu-system-x86_64 \
    -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" \
    -drive if=pflash,format=raw,file=OVMF_VARS_4M.fd \
    -cdrom nth_windows.iso \
    -m 4096M
