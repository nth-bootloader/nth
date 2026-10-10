#!/bin/bash
set -e

cd "$(dirname "$0")"

echo "Building bootloader..."
cd ..
mkdir -p build
cd build
cmake ..
make
cd ../iso_scripts

if [ -z "$1" ]; then
    read -p "Enter the path to the extracted Alpine ISO directory: " ALPINE_DIR
else
    ALPINE_DIR="$1"
fi

if [ ! -d "$ALPINE_DIR" ]; then
    echo "Error: Alpine directory not found at $ALPINE_DIR"
    echo "Usage: $0 [path_to_alpine_extracted_dir]"
    exit 1
fi

echo "Preparing ISO staging environment..."
rm -rf alpine_esp alpine_iso_root
mkdir -p alpine_esp/EFI/BOOT
mkdir -p alpine_esp/boot

cp ../build/BOOTX64.EFI alpine_esp/EFI/BOOT/BOOTX64.EFI

cat <<EOF > alpine_esp/nth.cfg
timeout=5
Alpine Linux=\boot\vmlinuz-lts|vmlinuz-lts initrd=\boot\initramfs-lts modules=loop,squashfs,sd-mod,usb-storage quiet
EOF

echo "Copying kernel and initramfs to EFI partition..."
cp "$ALPINE_DIR/boot/vmlinuz-lts" alpine_esp/boot/
cp "$ALPINE_DIR/boot/initramfs-lts" alpine_esp/boot/

REQ_SIZE_MB=$(du -sm alpine_esp | cut -f1)
IMG_SIZE_MB=$((REQ_SIZE_MB + 5))

echo "Creating EFI FAT image (${IMG_SIZE_MB}MB)..."
dd if=/dev/zero of=efi.img bs=1M count=${IMG_SIZE_MB} status=none
mkfs.vfat -F 32 efi.img > /dev/null
mcopy -s -i efi.img alpine_esp/* ::/

mkdir -p alpine_iso_root/boot
mv efi.img alpine_iso_root/efi.img

echo "Copying Alpine packages and modloop to ISO root..."
cp -r "$ALPINE_DIR/apks" alpine_iso_root/
cp "$ALPINE_DIR/boot/modloop-lts" alpine_iso_root/boot/
if [ -f "$ALPINE_DIR/.alpine-release" ]; then
    cp "$ALPINE_DIR/.alpine-release" alpine_iso_root/
fi

# Bundle the post-install script and a spare bootloader onto the CD for the user
echo "Bundling nth_install.sh into the ISO..."
cp nth_install.sh alpine_iso_root/
cp ../build/BOOTX64.EFI alpine_iso_root/

echo "Generating UEFI ISO with Alpine..."
xorriso -as mkisofs \
    -R -f \
    -e efi.img \
    -no-emul-boot \
    -isohybrid-gpt-basdat \
    -o nth_alpine.iso \
    alpine_iso_root > /dev/null 2>&1

echo "Success! Generated nth_alpine.iso"

echo "Booting Alpine ISO in QEMU..."
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
    echo "You can manually boot nth_alpine.iso in VirtualBox (make sure EFI is enabled)."
    exit 0
fi

qemu-system-x86_64 \
    -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" \
    -drive if=pflash,format=raw,file=OVMF_VARS_4M.fd \
    -cdrom nth_alpine.iso \
    -m 1024M
