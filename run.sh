#!/bin/bash

ISO=$(find generated.x86_64 -maxdepth 1 -name "kintsugi*.iso" | head -1)

if [ -z "$ISO" ]; then
    ISO=$(find generated.x86_64 -maxdepth 1 -name "*.iso" | head -1)
fi

if [ -z "$ISO" ]; then
    echo "No ISO found in generated.x86_64/"
    exit 1
fi

echo "Booting: $ISO"

qemu-system-x86_64 \
    -enable-kvm \
    -cpu host \
    -smp 4 \
    -m 4G \
    -cdrom "$ISO" \
    -vga virtio \
    -display gtk,gl=on
