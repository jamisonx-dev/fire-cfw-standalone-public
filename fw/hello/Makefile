# Build on the Pi: make   -> hello.bin (exactly 98304 bytes for 0x08008000, token included)
DEPS    ?= $(HOME)/fire_fw/deps/tinyusb
TUSB    := $(DEPS)/src
ST      := $(DEPS)/hw/mcu/st/cmsis_device_f4
CMSIS   := $(DEPS)/lib/CMSIS_5/CMSIS/Core/Include

CC      := arm-none-eabi-gcc
OBJCOPY := arm-none-eabi-objcopy
SIZE    := arm-none-eabi-size

CPU     := -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
CFLAGS  := $(CPU) -Os -g -Wall -Wextra -ffunction-sections -fdata-sections \
           -DSTM32F401xC -DCFG_TUSB_MCU=OPT_MCU_STM32F4 \
           -I. -I$(TUSB) -I$(ST)/Include -I$(CMSIS)
LDFLAGS := $(CPU) -T fire_app.ld -Wl,--gc-sections -Wl,-Map=hello.map \
           --specs=nano.specs --specs=nosys.specs

SRC := main.c usb_descriptors.c token.c \
       $(ST)/Source/Templates/system_stm32f4xx.c \
       $(TUSB)/tusb.c $(TUSB)/common/tusb_fifo.c \
       $(TUSB)/device/usbd.c \
       $(TUSB)/class/midi/midi_device.c \
       $(TUSB)/portable/synopsys/dwc2/dcd_dwc2.c $(TUSB)/portable/synopsys/dwc2/dwc2_common.c
ASM := $(ST)/Source/Templates/gcc/startup_stm32f401xc.s

all: hello.bin

hello.elf: $(SRC) $(ASM) fire_app.ld tusb_config.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRC) $(ASM)
	$(SIZE) $@

hello.bin: hello.elf
	$(OBJCOPY) -O binary --gap-fill 0xFF $< $@
	@test $$(stat -c%s $@) -eq 98304 || (echo "hello.bin is not 98304 bytes" && false)
	@tail -c 16 $@ | grep -q EndOfFlashToken || (echo "token missing" && false)
	@echo "hello.bin OK: 98304 bytes, token present"

clean:
	rm -f hello.elf hello.bin hello.map
