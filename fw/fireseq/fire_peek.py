#!/usr/bin/env python3
"""Read registers / memory from the running Fire over host command 09 (read only, v0.17d).

    python3 fire_peek.py               # the DMA / ADC / SPI / timer / core report (what could write RAM behind our back)
    python3 fire_peek.py 0x20000000 4  # N words from an address

Why: v0.17c's crash records (2026-10-14) showed TIM4's stack canary overwritten with ADC-like halfwords while nothing
in the CPU could have written there; a DMA stream left running (by Akai's bootloader, or our own) would. Reading
SPI/ADC DATA registers (DR) would steal a byte from the code that owns them: this tool never does.
"""
import sys
import time

import mido

HDR = [0x7D, 0x4F, 0x52, 0x43]


def pack(a):
    return [(a >> (7 * i)) & 0x7F for i in range(5)]


def unpack(b):
    v = 0
    for x in reversed(list(b)[:5]):
        v = (v << 7) | (x & 0x7F)
    return v & 0xFFFFFFFF


class Fire:
    def __init__(self):
        self.out = mido.open_output(next(n for n in mido.get_output_names() if "ORC FW" in n))
        self.inp = mido.open_input(next(n for n in mido.get_input_names() if "ORC FW" in n))
        time.sleep(0.2)
        list(self.inp.iter_pending())

    def peek(self, addr):
        self.out.send(mido.Message("sysex", data=HDR + [0x09] + pack(addr)))
        t0 = time.time()
        while time.time() - t0 < 1.0:
            for m in self.inp.iter_pending():
                d = list(m.data) if m.type == "sysex" else []
                if len(d) == 16 and d[:5] == HDR + [0x09] and unpack(d[5:10]) == addr:
                    return unpack(d[10:15])
            time.sleep(0.002)
        raise SystemExit(f"no answer for 0x{addr:08x} (firmware before v0.17d, or an address the Fire refuses)")


DIRS = {0: "periph->MEMORY", 1: "memory->periph", 2: "memory->MEMORY", 3: "?"}
# STM32F401 DMA request map, (dma, stream, channel) -> request: a HINT written from memory of RM0368 tables 27/28 (check
# the manual before trusting a name). What decides the danger is the direction and the memory address, read from the chip.
REQ = {(1, 0, 0): "SPI3_RX", (1, 2, 0): "SPI3_RX", (1, 3, 0): "SPI2_RX", (1, 4, 0): "SPI2_TX", (1, 5, 0): "SPI3_TX",
       (1, 7, 0): "SPI3_TX", (1, 0, 1): "I2C1_RX", (1, 6, 2): "TIM4_UP", (1, 0, 2): "TIM4_CH1", (1, 3, 2): "TIM4_CH2",
       (1, 7, 2): "TIM4_CH3", (2, 0, 0): "ADC1", (2, 4, 0): "ADC1", (2, 0, 3): "SPI1_RX", (2, 2, 3): "SPI1_RX",
       (2, 3, 3): "SPI1_TX", (2, 5, 3): "SPI1_TX", (1, 1, 6): "TIM2_UP/CH3", (1, 7, 6): "TIM2_UP/CH4",
       (1, 5, 3): "TIM2_CH1", (1, 6, 3): "TIM2_CH2/CH4"}


def report(f):
    bad = 0
    rcc = f.peek(0x40023830)
    print(f"RCC AHB1ENR 0x{rcc:08x}: DMA1 clock {'ON' if rcc >> 21 & 1 else 'off'}, DMA2 clock {'ON' if rcc >> 22 & 1 else 'off'}")
    for dma, base in ((1, 0x40026000), (2, 0x40026400)):
        if not rcc >> (20 + dma) & 1:
            print(f"DMA{dma}: clock off (registers read as 0; nothing can run)")
            continue
        print(f"DMA{dma}: LISR 0x{f.peek(base):08x} HISR 0x{f.peek(base + 4):08x}")
        for s in range(8):
            r = base + 0x10 + 0x18 * s
            cr, ndtr, par, m0 = f.peek(r), f.peek(r + 4), f.peek(r + 8), f.peek(r + 12)
            ch = cr >> 25 & 7
            d = cr >> 6 & 3
            on = cr & 1
            what = REQ.get((dma, s, ch), "?")
            line = (f"  stream {s}: CR 0x{cr:08x} {'ENABLED' if on else 'off':7s} ch {ch} ({what}) {DIRS[d]}"
                    f"{' circular' if cr >> 8 & 1 else ''}{' minc' if cr >> 10 & 1 else ''} NDTR {ndtr} PAR 0x{par:08x} M0AR 0x{m0:08x}")
            danger = on and d in (0, 2) and 0x20000000 <= m0 < 0x20010000
            if danger:
                bad += 1
                line += "   <<< WRITES RAM"
            if on or cr or ndtr:
                print(line)
    adc_cr1, adc_cr2 = f.peek(0x40012004), f.peek(0x40012008)
    print(f"ADC1 CR1 0x{adc_cr1:08x} CR2 0x{adc_cr2:08x}: DMA requests {'ON' if adc_cr2 >> 8 & 1 else 'off'},"
          f" continuous {'ON' if adc_cr2 >> 1 & 1 else 'off'}, scan {'ON' if adc_cr1 >> 8 & 1 else 'off'}; CCR 0x{f.peek(0x40012304):08x}")
    for name, base in (("SPI2 (LED chain)", 0x40003800), ("SPI3 (OLED)", 0x40003C00), ("SPI1", 0x40013000)):
        c2 = f.peek(base + 4)
        print(f"{name} CR1 0x{f.peek(base):08x} CR2 0x{c2:08x}: RX DMA {'ON' if c2 & 1 else 'off'}, TX DMA {'ON' if c2 >> 1 & 1 else 'off'}")
    for name, base in (("TIM2", 0x40000000), ("TIM3", 0x40000400), ("TIM4", 0x40000800), ("TIM5", 0x40000C00), ("TIM1", 0x40010000)):
        dier = f.peek(base + 0x0C)
        print(f"{name} CR1 0x{f.peek(base):08x} DIER 0x{dier:08x}{'  (DMA requests on: bits 8-14)' if dier & 0x7F00 else ''}")
    ccr = f.peek(0xE000ED14)
    fpccr, fpcar = f.peek(0xE000EF34), f.peek(0xE000EF38)
    print(f"SCB CCR 0x{ccr:08x}: STKALIGN {ccr >> 9 & 1}; SHCSR 0x{f.peek(0xE000ED24):08x}; MPU CTRL 0x{f.peek(0xE000ED94):08x}")
    print(f"FPU FPCCR 0x{fpccr:08x}: ASPEN {fpccr >> 31 & 1} LSPEN {fpccr >> 30 & 1} LSPACT {fpccr & 1}; FPCAR 0x{fpcar:08x}")
    print(f"\n{bad} enabled DMA stream(s) writing into RAM" + ("" if not bad else ": compare M0AR with the stack (0x2000c268-0x20010000)"))


def main(argv):
    f = Fire()
    if len(argv) > 1:
        a, n = int(argv[1], 0), int(argv[2], 0) if len(argv) > 2 else 1
        for i in range(n):
            print(f"0x{a + 4 * i:08x}: 0x{f.peek(a + 4 * i):08x}")
    else:
        report(f)


if __name__ == "__main__":
    main(sys.argv)
