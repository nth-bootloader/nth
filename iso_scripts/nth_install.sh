#!/bin/sh
# nth_install.sh
# Run this script AFTER you run your setup and install to the disk.
# It rips out GRUB and replaces it with nth!

set -e

echo "Mounting VDI partitions..."
mkdir -p /mnt/efi
mkdir -p /mnt/root

# Mount the EFI (sda1) and Root (sda3) partitions
mount /dev/sda1 /mnt/efi
mount /dev/sda3 /mnt/root

echo "Removing GRUB..."
rm -rf /mnt/efi/EFI/alpine
rm -rf /mnt/efi/EFI/boot 2>/dev/null || true

echo "Installing nth bootloader..."
mkdir -p /mnt/efi/EFI/BOOT
# Grab the bootloader from the CD (which is where this script is running from)
DIR="$(dirname "$0")"
cp "$DIR/BOOTX64.EFI" /mnt/efi/EFI/BOOT/BOOTX64.EFI

echo "Copying Linux kernel to FAT32 EFI partition for nth..."
mkdir -p /mnt/efi/boot
cp /mnt/root/boot/vmlinuz-lts /mnt/efi/boot/
cp /mnt/root/boot/initramfs-lts /mnt/efi/boot/

echo "Generating nth.cfg..."
cat <<EOF > /mnt/efi/nth.cfg
timeout=5
Alpine Linux=\boot\vmlinuz-lts|vmlinuz-lts initrd=\boot\initramfs-lts root=/dev/sda3 modules=ext4 quiet
EOF

echo "Cleaning up..."
umount /mnt/efi
umount /mnt/root

echo "--------------------------------------------------------"
echo "Success! nth bootloader is now installed to your VDI!"
echo "Type 'poweroff', eject the ISO from VirtualBox, and reboot!"
echo "--------------------------------------------------------"
