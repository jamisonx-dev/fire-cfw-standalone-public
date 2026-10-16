# Build on the Pi: make   -> fireseq.bin (exactly 98304 bytes for 0x08008000, token included)
DEPS    ?= $(HOME)/fire_fw/deps/tinyusb
TUSB    := $(DEPS)/src
ST      := $(DEPS)/hw/mcu/st/cmsis_device_f4
CMSIS   := $(DEPS)/lib/CMSIS_5/CMSIS/Core/Include

CC      := arm-none-eabi-gcc
OBJCOPY := arm-none-eabi-objcopy
SIZE    := arm-none-eabi-size

CPU     := -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
# -fstack-protector-strong: a buffer overrun is caught at the function's return (crash cause "stack smash", main.c)
CFLAGS  := $(CPU) -Os -g -Wall -Wextra -ffunction-sections -fdata-sections -fstack-protector-strong \
           -DSTM32F401xC -DCFG_TUSB_MCU=OPT_MCU_STM32F4 \
           -I. -I$(TUSB) -I$(ST)/Include -I$(CMSIS)
LDFLAGS := $(CPU) -T fire_app.ld -Wl,--gc-sections -Wl,-Map=fireseq.map \
           --specs=nano.specs --specs=nosys.specs

SRC := main.c usb_descriptors.c token.c fire_hw.c oled.c draw.c store.c store2.c store_hw.c steps.c play.c lights.c screens.c crash.c ctl.c \
       $(ST)/Source/Templates/system_stm32f4xx.c \
       $(TUSB)/tusb.c $(TUSB)/common/tusb_fifo.c \
       $(TUSB)/device/usbd.c \
       $(TUSB)/class/midi/midi_device.c \
       $(TUSB)/portable/synopsys/dwc2/dcd_dwc2.c $(TUSB)/portable/synopsys/dwc2/dwc2_common.c
ASM := $(ST)/Source/Templates/gcc/startup_stm32f401xc.s

# build id = a hash of the image's own sources (the local .c files in SRC, every local .h, the linker script; CR
# stripped so a PC-copied CRLF file hashes like its LF twin). Same sources -> same image; test files don't count.
# A crash record carries it, and host_crash.py refuses to symbolise a crash against an ELF with another id.
BUILD_ID := $(shell cat $(filter-out $(DEPS)/%,$(SRC)) $(sort $(wildcard *.h)) fire_app.ld | tr -d '\r' | md5sum | cut -c1-8)
CFLAGS  += -DBUILD_ID=0x$(BUILD_ID)u -DBUILD_ID_STR=\"$(BUILD_ID)\"

all: fireseq.bin

# every local header + this file: a header-only change must rebuild (one once left a stale bin that still "verified")
fireseq.elf: $(SRC) $(ASM) fire_app.ld $(wildcard *.h) Makefile
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRC) $(ASM) -lm
	$(SIZE) $@

fireseq.bin: fireseq.elf
	$(OBJCOPY) -O binary --gap-fill 0xFF $< $@
	@test $$(stat -c%s $@) -eq 98304 || (echo "fireseq.bin is not 98304 bytes" && false)
	@tail -c 16 $@ | grep -q EndOfFlashToken || (echo "token missing" && false)
	@echo "fireseq.bin OK: 98304 bytes, token present"

clean:
	rm -f fireseq.elf fireseq.bin fireseq.map
