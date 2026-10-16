#!/bin/sh
# Build boomerang.bin: a full 96 KB image for 0x08008000 (sectors 2-4), token included.
set -e
cd "$(dirname "$0")"
arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -Os -ffreestanding -nostdlib -Wall -Wextra \
    -T fire_app.ld -Wl,--gc-sections -o boomerang.elf main.c
# --gap-fill pads the space between code and token with 0xFF (= erased flash).
arm-none-eabi-objcopy -O binary --gap-fill 0xFF boomerang.elf boomerang.bin
arm-none-eabi-size boomerang.elf
ls -l boomerang.bin
