/* "ORC FW seq" v0.14: the Fire as a standalone step sequencer + live pads, ORC-style controls.
 *
 * 32 tracks x up to 128 steps, each track with its OWN length (polyrhythms), 32 patterns, all saved.
 *   T1-T24 drum tracks (one note each, or a CC: hold row + knob 4); M1-M8 melodic tracks (every step has its own note).
 *   SHIFT + REC       undo the last edit of a pattern's steps (again = redo)
 *   hold KNOB MODE + pad  the ROLLER: left 8 pads all tracks, right 8 that row's track (1 bar .. 1/16 loops, 1/32 - 1/64 rolls)
 *   Pads              4 visible tracks x 16 visible steps: tap toggles a step
 *   SHIFT + pad       velocity tier on a lit step: accent (127) -> soft (60) -> normal (100)
 *   ALT + pad         that track's length = up to this step (1-128)
 *   hold pad + knob 1 ratchet 1-4x;  hold pad + knob 2 probability 100/75/50/25 %  (tap a lit pad = off on release)
 *   hold pad + knob 3 the step's note (melodic tracks);  hold pad + knob 4 gate 25/50/75/95 % / TIE / SLIDE
 *   KNOB MODE button  step knob page 2 (MIXER lit): knob 1 condition, 2 lock value, 3 chord (melodic)
 *   hold row + knob 3 that track's lock CC;  hold PERFORM = FILL (for FILL conditions)
 *   tap PERFORM       FADERS: up to 32 CC faders, 4 rows at a time, each its own CC / channel / colour
 *   tap NOTE again    UNIVERSAL (v0.31; v0.38 moved here from PERFORM x2; NOTE again = back): 8 pages of pads + knobs stored ON THE FIRE (uni.h), set up from
 *                     the ORC web page (host 10-15) or here. GRID < > page; KNOB MODE = knob bank 1-4; SHIFT held = each
 *                     pad's SHIFT assignment (UNI_LAYER_BTN). Editing (UNI_EDIT_BTN = ALT held):
 *     ALT + tap pad   edit it (it plays, so you hear it); + SHIFT = its SHIFT assignment. Knobs: 1 type, 2 channel,
 *                     3 number, 4 value; KNOB MODE = page 2: 1 off value, 2 colour. Type "off" = clear it.
 *     ALT + hold A, tap B  copy pad A to pad B;   ALT + turn a knob = edit that knob (1 type, 2 ch, 3 CC, 4 start)
 *     let ALT go      done: saved like any edit (3 s later; AUTOSAVE off = on SAVE NOW)
 *   tap PERFORM again the DAW page (v0.31d; v0.38: was the 3rd tap), only while the host has defined one (1A-1C): up to 9 rows (8 tracks +
 *                     master) with the host's colours + names, played EXACTLY like FADERS (tap, knobs 1-4, SELECT /
 *                     SHIFT + PATTERN scroll); moves go to the host as 1D, scrolling past the ends asks for another bank
 *                     (1E). RAM only. Row buttons do nothing there. A 3rd tap (or the host removing it) = back; no DAW page = the 2nd tap is back.
 *   GRID < >          step page (1-16, 17-32 ... 113-128)
 *   SELECT turn       scroll tracks one row        SHIFT + SELECT: tempo (SHIFT+ALT: x10)
 *   SHIFT + PATTERN   scroll tracks a page (4 rows)
 *   PATTERN up/down   pattern 1-32 (queued to the 16-step bar while playing); ALT + it = duplicate into it
 *   hold PATTERN      the pattern picker: tap one of patterns 1-32 (top two rows); knob 1 = its colour
 *   PLAY / STOP       start-stop / stop + rewind;  SHIFT + PLAY = MIDI clock out on/off;  ALT + STEP (tapped) = tap tempo
 *   PATTERN/SONG      the song editor (top row slots, 2nd row pattern | bars);  SHIFT + it = song mode on/off
 *   Row buttons       tap = mute that track; hold + knob 1 = its channel, hold + knob 2 = its note
 *   Knob 1            swing 50-75 %      Knobs 2-4: relative CC 17-19 on channel 1 (65 = +1, 63 = -1)
 *   STEP / NOTE / DRUM  what the pads do: edit steps / play a keyboard / play the 16 tracks (the sequencer keeps running)
 *   NOTE mode         rows a fourth apart, C lit; GRID < > octave (SHIFT: semitone); SELECT = which melodic
 *                     track it plays (M1-M8, on that track's channel), ALT + SELECT = that channel; REC = record into it
 *   DRUM mode         four 4x4 grids side by side (pad 1 bottom-left), each with its own channel + notes
 *                     (chromatic from a base note, or the MPC's panel pads: CC on ch 14); ALT + tap a group, then knobs 1-3 edit it;
 *                     REC = record hits into the pattern (onto the track with that note + channel, else a free track)
 *   Live pads         fixed velocity 100, SHIFT held = accent 127
 *   MIDI in           follows external clock (F8 / FA / FB / FC); F0 47 7F 43 72 00 00 F7 = reboot to DFU
 * Steps are stored sparsely (steps.c: only lit steps take memory, a shared pool for all patterns).
 * Saved to flash sector 5 (store2.c), 3 s after the last edit; only changed patterns are written.
 * Moving around (pattern choice, mutes, octave ...) doesn't start a save; it rides along with the next one.
 * Safety nets: watchdog (~2 s), watchdog reset -> DFU, faults -> DFU.
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "stm32f4xx.h"
#include "tusb.h"
#include "draw.h"
#include "oled.h"
#include "fire_hw.h"
#include "store2.h"
#include "store.h"                                  /* legacy (v1/v2) format: migration only */
#include "steps.h"
#include "play.h"
#include "screens.h"
#include "crash.h"
#include "ctl.h"                                    /* the control decisions (tested on the host: ctl_test.c) */
#include "uni.h"                                    /* v0.31 UNIVERSAL mode logic (uni_test.c) */
#include "compact.h"                                /* v0.31 the save area clean-up (compact_test.c) */
#include "hostm.h"                                  /* v0.33 ORC mode = host mode (hostm_test.c) */
#include "daw.h"                                    /* v0.36 DAW mode: Mackie Control / HUI (daw_test.c) */
#include "paint.h"                                  /* v0.37 PAINT mode (paint_test.c) */

/* noinline on the functions main() calls that own buffers or parse input (render, draw_*, host_cmd, midi_*,
   handle_events, paint): main() never returns, so anything inlined into it is never canary-checked
   (-fstack-protector-strong checks at a function's return). Found by the 2026-10-14 crash hunt. */
/* The build id: a hash of the sources (Makefile), so the same sources always give the same image. It goes into a
   crash record, and host_crash.py finds this tag in the ELF to refuse to symbolise against the wrong build. */
#ifndef BUILD_ID
#define BUILD_ID 0u
#define BUILD_ID_STR "00000000"
#endif
__attribute__((used)) static const char BUILD_TAG[] = "ORCBUILD=" BUILD_ID_STR;
/* The release name BROWSER > About shows (v0.35, 2026-11-12: a tester's photo of that page says which build they run).
   Bump it with every release; the build id above changes by itself. */
#define FW_VERSION "v0.39"
#include <math.h>
#include <stddef.h>

#define DTRACKS 16                                  /* drum tracks T1-T16: one note per track */
#define MTRACKS 8                                   /* melodic tracks M1-M8 (tracks 16-23): a note per step */
#define XTRACKS 8                                   /* v0.29: drum tracks T17-T24 = tracks 24-31 (after the melodic ones, so older saves keep their places) */
#define TRACKS (DTRACKS + MTRACKS + XTRACKS)
#define TRACKS_V28 24                               /* the per-track arrays of the settings record up to v0.28 */
#define MAXSTEPS 128
#define PATTERNS 32
#define VIEW_ROWS 4
#define PAGE 16
#define PAGES (MAXSTEPS / PAGE)

volatile uint32_t ms_ticks;
void SysTick_Handler(void) { ms_ticks++; }
uint32_t tusb_time_millis_api(void) { return ms_ticks; }
void OTG_FS_IRQHandler(void) { tusb_int_handler(0, true); }

static void bk_enable(void)                         /* the RTC backup registers: they survive a reset */
{
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    (void)RCC->APB1ENR;
    PWR->CR |= PWR_CR_DBP;
}

static void __attribute__((noreturn)) reboot_to_dfu(void)
{
    __disable_irq();
    bk_enable();
    RTC->BKP0R = 1u;                                /* the Akai bootloader reads this: 1 = go to DFU (update mode) */
    NVIC_SystemReset();
}

/* Crash recorder (crash.h). Backup registers: 0 = DFU flag, 1 = 0xC0DE0000 | cause (a valid record), 2 = the main-loop
   stage right now, 3-8 = PC, LR, xPSR, CFSR, HFSR, fault address, 9 = uptime ms at the crash, 10 = consecutive early
   crashes, 11 = total, 12 = heartbeat (ms, written every second), 13 = the stage the crash happened in. */
#define BK_MAGIC 0xC0DE0000u
static inline void crash_stage(uint32_t st) { RTC->BKP2R = st; }
static inline bool bk_valid(void) { return (RTC->BKP1R & 0xFFFF0000u) == BK_MAGIC; }

static void crash_record(uint32_t cause, uint32_t pc, uint32_t lr, uint32_t psr, uint32_t cfsr, uint32_t hfsr, uint32_t addr,
                         uint32_t up_ms, uint32_t stage, bool *dfu)
{
    uint32_t prev = bk_valid() ? RTC->BKP10R : 0, total = bk_valid() ? RTC->BKP11R : 0;
    crash_decision_t d = crash_decide(prev, up_ms);
    RTC->BKP3R = pc; RTC->BKP4R = lr; RTC->BKP5R = psr; RTC->BKP6R = cfsr; RTC->BKP7R = hfsr; RTC->BKP8R = addr;
    RTC->BKP9R = up_ms; RTC->BKP10R = d.count; RTC->BKP11R = total + 1; RTC->BKP13R = stage;
    RTC->BKP14R = BUILD_ID;                         /* which image crashed: an address only means something against ITS ELF */
    RTC->BKP1R = BK_MAGIC | cause;
    *dfu = d.dfu != 0;
}

/* Stack high-water mark: at boot the free RAM between the end of .bss and the stack is filled with a pattern; the
   lowest word that no longer holds it is the deepest the stack has reached (RAM survives a reset, so a watchdog
   restart can still read the previous run's mark before painting again). BKP15 = SP at a fault, 16 = the low point. */
extern uint32_t _enoinit, _estack;                  /* the stack's free space runs from _enoinit (after .bss + .noinit) */
#define STACK_PAINT 0xA5C3A5C3u
static void stack_paint(void)
{
    uint32_t *p = &_enoinit, *lim = (uint32_t *)(__get_MSP() - 256);
    while (p < lim) *p++ = STACK_PAINT;
}
static uint32_t stack_low(void)                     /* address of the deepest stack word ever written (the top = none) */
{
    const uint32_t *p = &_enoinit;
    while (p < &_estack && *p == STACK_PAINT) p++;
    return (uint32_t)p;
}

/* The stack around the last fault, kept across the reset (.noinit: not zeroed, not painted): the exception frame and
   above it the interrupted code's saved registers and return addresses = a call chain (host command 08). */
#define SNAP_MAGIC 0x5AC4F00Du
static struct { uint32_t magic, base, w[CRASH_STACK_WORDS]; } crash_snap __attribute__((section(".noinit")));
static void snap_take(const uint32_t *from)
{
    crash_snap.magic = 0;
    const uint32_t *top = &_estack;
    if (from < (const uint32_t *)0x20000000u || from >= top) return;   /* not a RAM stack address: nothing to keep */
    for (int i = 0; i < CRASH_STACK_WORDS; i++) crash_snap.w[i] = from + i < top ? from[i] : 0;
    crash_snap.base = (uint32_t)from;
    crash_snap.magic = SNAP_MAGIC;
}

/* SRAM is never code here (.data is data only), so the MPU marks it execute-never: a jump into RAM (a smashed return
   address or function pointer, the 2026-10-13 22:09 crash) faults on its FIRST instruction as a MemManage fault with
   PC = that RAM address, while LR still names the call / return that went wrong. Everything else keeps the default
   map (PRIVDEFENA); the firmware runs privileged throughout. */
static void mpu_ram_xn(void)
{
    MPU->CTRL = 0;
    MPU->RNR = 0;
    MPU->RBAR = 0x20000000u;
    MPU->RASR = MPU_RASR_XN_Msk | (3u << MPU_RASR_AP_Pos) | (1u << MPU_RASR_TEX_Pos) | MPU_RASR_C_Msk | MPU_RASR_B_Msk
              | (15u << MPU_RASR_SIZE_Pos) | MPU_RASR_ENABLE_Msk;   /* 64 KB (2^(15+1)), full access, normal WBWA, XN */
    MPU->CTRL = MPU_CTRL_PRIVDEFENA_Msk | MPU_CTRL_ENABLE_Msk;
    SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk;
    __DSB(); __ISB();
}

/* A fault: record it and restart the app (a crash LOOP, 3 early crashes in a row, still goes to DFU). */
void __attribute__((noreturn, used)) crash_from_fault(const uint32_t *f, uint32_t cause)
{
    __disable_irq();
    bk_enable();
    uint32_t cfsr = SCB->CFSR;
    uint32_t addr = (cfsr & (1u << 15)) ? SCB->BFAR : (cfsr & (1u << 7)) ? SCB->MMFAR : 0;   /* BFARVALID / MMARVALID */
    if (cause == CRASH_WATCH) { addr = DWT->COMP0; DWT->FUNCTION0 = 0; }   /* the watched word that was written */
    bool dfu;
    crash_record(cause, f ? f[6] : 0, f ? f[5] : 0, f ? f[7] : 0, cfsr, SCB->HFSR, addr, ms_ticks, RTC->BKP2R, &dfu);
    RTC->BKP15R = (uint32_t)f; RTC->BKP16R = stack_low();
    snap_take(f);
    RTC->BKP0R = dfu ? 1u : 0u;
    NVIC_SystemReset();
}

/* -fstack-protector-strong: every function with a local array checks a guard word on return. A buffer that overran
   lands here (cause "stack smash") with PC = the function whose buffer it was, instead of returning into garbage
   (the 2026-10-13 22:09 crash: HardFault, invalid state, PC 0x30, LR in the stack = a smashed return address). */
uint32_t __stack_chk_guard = 0xE2B7C94Du;
void __attribute__((noreturn, used)) __stack_chk_fail(void)
{
    uint32_t ra = (uint32_t)__builtin_return_address(0);
    __disable_irq();
    bk_enable();
    bool dfu;
    crash_record(CRASH_STACK, ra, ra, __get_xPSR(), 0, 0, 0, ms_ticks, RTC->BKP2R, &dfu);
    RTC->BKP15R = __get_MSP(); RTC->BKP16R = stack_low();
    snap_take((const uint32_t *)__get_MSP());
    RTC->BKP0R = dfu ? 1u : 0u;
    NVIC_SystemReset();
}

/* v0.29d diagnostic: TIM4's own stack check failed (fire_hw.c, kinds in fire_hw.h). Recorded like a fault, but with
   the fields reused: PC / LR / xPSR = the INTERRUPTED code's (from the frame at TIM4's entry SP), CFSR = the kind,
   HFSR = TIM4's SP as it started, addr = TIM4's entry SP, SP = TIM4's SP now. The snapshot starts at the lower of the
   two SPs, so it holds TIM4's own frame and the interrupted code's frame above it. */
void __attribute__((noreturn, used)) tim4_slip(uint32_t kind, uint32_t sp_in, uint32_t sp0, uint32_t sp_now)
{
    __disable_irq();
    bk_enable();
    const volatile uint32_t *f = (const volatile uint32_t *)sp_in;
    bool dfu;
    crash_record(CRASH_SLIP, f[6], f[5], f[7], kind, sp0, sp_in, ms_ticks, RTC->BKP2R, &dfu);
    RTC->BKP15R = sp_now; RTC->BKP16R = stack_low();
    snap_take((const uint32_t *)(sp_now < sp0 ? sp_now : sp0));
    RTC->BKP0R = dfu ? 1u : 0u;
    NVIC_SystemReset();
}

/* The handlers read which stack the fault came from, hand its frame (R0-R3, R12, LR, PC, xPSR) to crash_from_fault. */
#define CRASH_VECTOR(name, cause) \
    void __attribute__((naked)) name(void) \
    { __asm volatile("tst lr, #4\n\tite eq\n\tmrseq r0, msp\n\tmrsne r0, psp\n\tmovs r1, #" #cause "\n\tb crash_from_fault\n\t"); }
CRASH_VECTOR(HardFault_Handler, 1)                  /* CRASH_HARD */
CRASH_VECTOR(MemManage_Handler, 2)                  /* CRASH_MEM */
CRASH_VECTOR(BusFault_Handler, 3)                   /* CRASH_BUS */
CRASH_VECTOR(UsageFault_Handler, 4)                 /* CRASH_USAGE */
CRASH_VECTOR(DebugMon_Handler, 7)                   /* CRASH_WATCH: the DWT canary watchpoint (fire_hw.c TIM4) fired: stacked PC =
                                                       just after the writing instruction, xPSR = the context that wrote */

/* The debug monitor (no debugger needed): DWT watchpoints raise DebugMon. Priority 0, so it preempts TIM4 itself. */
static void dbgmon_init(void)
{
    DWT->FUNCTION0 = 0;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk | CoreDebug_DEMCR_MON_EN_Msk;
    NVIC_SetPriority(DebugMonitor_IRQn, 0);
}

static void clock_init(void)
{
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY)) {}
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    PWR->CR = (PWR->CR & ~PWR_CR_VOS) | PWR_CR_VOS_1;
    /* v0.33b: 3 wait states (was 2). 2 is only in spec at 84 MHz while VDD stays above 2.7 V, with no margin, and the
       2026-11-11 crashes were an instruction fetched with one bit wrong (crash.h T4V_ENTRY). More is always legal. */
    FLASH->ACR = FLASH_ACR_LATENCY_3WS | FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN;
    RCC->PLLCFGR = 0x07415408u;
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) {}
    RCC->CFGR = RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {}
    SystemCoreClock = 84000000u;
}

static void usb_pins_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    (void)RCC->AHB1ENR;
    GPIOA->MODER   = (GPIOA->MODER & ~(0xFu << 22)) | (0xAu << 22);
    GPIOA->OSPEEDR |= 0xFu << 22;
    GPIOA->AFR[1]  = (GPIOA->AFR[1] & ~(0xFFu << 12)) | (0xAAu << 12);
    RCC->AHB2ENR |= RCC_AHB2ENR_OTGFSEN;
    (void)RCC->AHB2ENR;
}

/* 1 MHz free-running microsecond clock (TIM2, 32-bit) */
static void us_clock_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
    (void)RCC->APB1ENR;
    TIM2->PSC = 83;
    TIM2->ARR = 0xFFFFFFFFu;
    TIM2->EGR = 1;
    TIM2->CR1 = TIM_CR1_CEN;
}
static inline uint32_t us_now(void) { return TIM2->CNT; }

/* ---------- tracks, patterns, settings ---------- */

static const uint8_t DEF_NOTE[DTRACKS] = {36, 38, 42, 46, 39, 37, 41, 45, 48, 49, 51, 56, 54, 70, 75, 63};
static const uint8_t XDEF_NOTE[XTRACKS] = {60, 61, 62, 64, 65, 66, 67, 68};   /* T17-T24 (none of T1-T16's) */
/* track colours, R,G,B brightness 0-7 (repeats every 8 tracks) */
static const uint8_t TRACK_RGB[8][3] = {
    {7, 0, 0}, {6, 4, 0}, {0, 7, 0}, {0, 3, 7}, {6, 0, 6}, {0, 6, 6}, {6, 6, 0}, {4, 4, 7},
};
enum { LV_OFF, LV_SOFT, LV_NORMAL, LV_ACCENT };
static const uint8_t LEVEL_VEL[4] = {0, 60, 100, 127};
static const uint8_t LEVEL_BRIGHT[4] = {0, 2, 4, 7};

/* Saved global record. Fields are only ever appended; zero = default.
   (Patterns: steps.c. Old full-size pattern records, v0.4-v0.9, are converted at boot: load_state.) */
typedef struct {
    uint16_t bpm;
    uint8_t cur_pat, swing, clock_out, reserved0;
    uint16_t mute_bits;                             /* T1-T16 */
    uint8_t note[DTRACKS];                          /* 0 = default */
    uint8_t chan[DTRACKS];                          /* 1-16, 0 = 10 */
    uint8_t lock_cc[TRACKS_V28];                    /* v0.11 (was reserved, always 0): the CC a track's step locks send, 0 = none (T17-T24: x_lock_cc) */
    /* v0.7: song mode (appended: older saves load these as zero = song off, empty) */
    uint8_t song_on;                                /* 1 = playback follows the song */
    uint8_t song_pat[16];                           /* slot -> pattern 1-32, 0 = empty (the song ends there) */
    uint8_t song_rep[16];                           /* slot -> bars 1-8, 0 = 1 */
    /* v0.8: live keyboard (taken from the reserved bytes: older saves read zero = defaults) */
    uint8_t kb_track;                               /* NOTE mode plays melodic track M(1 + this), 0-7 */
    uint8_t kb_base;                                /* NOTE mode bottom-left note, 0 = 48 (C3) */
    /* v0.8: DRUM mode = four 4x4 pad grids side by side, each with its own channel and notes */
    uint8_t grid_chan[4];                           /* 1-16, 0 = default (10, 1, 2, 3) */
    uint8_t grid_base[4];                           /* chromatic layout: pad 1's note 1-112, 0 = 36 */
    uint8_t grid_layout[4];                         /* (unused since v0.29b: groups always play notes; an old 1 = MPC pads / 2 = 16 levels loads as 0) */
    /* v0.9: melodic tracks */
    uint8_t m_chan[MTRACKS];                        /* 1-16, 0 = default (M1 = 1 ... M8 = 8) */
    uint8_t m_mute;                                 /* bit i = M(i+1) muted */
    uint8_t reserved2[7];
    /* v0.13: faders (appended: an older save reads fad_init 0 -> the default 16 are set up) */
    uint8_t fad_init;                               /* 1 = the list below has been set up */
    uint8_t fad_n;                                  /* how many faders, 0-32 */
    uint8_t fad_cc[32], fad_ch[32], fad_col[32], fad_val[32];   /* CC 0-127, channel 1-16, colour 0-15, value 0-127 */
    /* v0.15: lights + menu (taken from the reserved bytes: an older save reads lt_init 0 -> defaults) */
    uint8_t lt_init;                                /* 1 = the lights settings below have been set up */
    uint8_t lt_mode;                                /* (unused since v0.31r: the shows are gone; kept so the save layout never moves) */
    uint8_t lt_speed;                               /* (unused since v0.31r, ditto) */
    uint8_t lt_idle;                                /* (unused since v0.31r: nothing starts by itself) */
    uint8_t lt_smooth;                              /* 1 = temporal dithering between pad levels */
    uint8_t autosave_off;                           /* 1 = nothing saves until SAVE NOW (menu) */
    uint8_t lt_bars;                                /* VU bar look: 0 snappy, 1 smooth, 2 whole, 3 heat, 4 heat smooth (0 = what an older save reads) */
    uint8_t lt_flip;                                /* 1 = the heat looks are flipped: cool base, hot tip (0 = what an older save reads) */
    uint8_t lt_idle_v18;                            /* v0.18: 1 = Idle start was switched OFF once (the owner never wanted the screen saver) */
    uint8_t pad_hold;                               /* v0.28: index into PAD_HOLD_MS, how long a live pad must stay up before its release counts */
    uint8_t pad_hold_v28;                           /* v0.28: 1 = pad_hold has been set up (an older save reads 0 -> the default) */
    uint8_t daw_proto;                              /* v0.36: DAW mode's protocol, DAW_AUTO / DAW_MCU / DAW_HUI (0 = auto) */
    uint8_t reserved3[2];
    /* v0.14: 32 patterns, each with a colour (appended) */
    uint8_t pat_col[32];                            /* colour 0-15 + 1, 0 = default (pattern number mod 16) */
    /* v0.20 (appended: an older save reads zeros = exactly the old behaviour) */
    uint8_t knob_bank;                              /* free knobs: bank 0-3 (KNOB MODE with nothing held; the 4 lights show it) */
    uint8_t kb_fn[4][4];                            /* [bank][knob]: 0 = default, 1-127 = CC n, KB_SWING, KB_TEMPO */
    uint8_t kb_ch[4][4];                            /* 0 = channel 1, 1-16 */
    uint8_t kb_abs[4][4];                           /* 0 = relative (64 +- steps), 1 = absolute (a value 0-127 the knob moves) */
    uint8_t mark_off;                               /* 1 = no beat / bar marks on the step grid */
    uint8_t mark_col;                               /* index into MARK_RGB (0 = dim blue) */
    uint8_t mark_space;                             /* 0 = every beat, 1 = every bar, 2 = both (the bar one brighter) */
    /* v0.23 (appended: an older save reads zeros = every track at full volume, exactly the old behaviour) */
    uint8_t trk_lvl[TRACKS_V28];                    /* per-track volume (the STEP faders): 0 = full, else level 0-126 + 1 (ctl_trk_level) */
    /* v0.29: tracks T17-T24 (tracks 24-31) + CC tracks (appended: an older save reads zeros = defaults, every track plays notes) */
    uint8_t x_note[XTRACKS];                        /* T17-T24's note (a CC track: its CC number), 0 = default */
    uint8_t x_chan[XTRACKS];                        /* 1-16, 0 = 10 */
    uint8_t x_mute;                                 /* bit i = T(17+i) muted */
    uint8_t x_lock_cc[XTRACKS];                     /* as lock_cc */
    uint8_t x_trk_lvl[XTRACKS];                     /* as trk_lvl */
    uint8_t cc_trk[4];                              /* bit t (any drum track 0-31) = the track sends a CC (its note = the CC number), not a note */
    uint8_t reserved4[1];                           /* (keeps the size even: no padding byte that a struct copy could leave out) */
    /* v0.29: the free-knob banks PER SECTION (appended): [0] NOTE mode, [1] DRUM mode, [2] the MPC remote; STEP mode keeps
       knob_bank / kb_fn / kb_ch / kb_abs above. kbx_init 0 (an older save) = each starts as a copy of the STEP banks. */
    uint8_t kbx_bank[3];
    uint8_t kbx_fn[3][4][4], kbx_ch[3][4][4], kbx_abs[3][4][4];
    uint8_t kbx_init;
    /* v0.31 (appended: an older save reads zeros = page 1, bank 1): UNIVERSAL mode's page (0-7) and knob bank (0-3) */
    uint8_t uni_page, uni_bank;
} global_t;

_Static_assert(TRACKS == ST_TRACKS && DTRACKS == ST_MEL_FIRST && DTRACKS + MTRACKS == ST_MEL_END && TRACKS_V28 == ST_TRACKS_V2
               && PATTERNS == ST_PATTERNS && MAXSTEPS == ST_MAXSTEPS, "steps.h sizes");
_Static_assert(ST_REC_HDR + ST_POOL <= S2_MAX_RECORD, "a pattern must fit one save record");
_Static_assert(sizeof(global_t) == 568,"global_t changed size: only APPEND fields (older saves zero-fill)");
static global_t G;
static global_t G_saved;                            /* what the newest global record holds (skip identical saves) */
static global_t G_snap;                             /* the settings record being written: a copy, so edits never tear it */
static uint32_t saving_size;                        /* pool bytes of the pattern being written (-> steps_saved when done) */
static uint32_t pat_stuck;                          /* bit p: pattern p's saved record didn't fit at boot: never overwrite it */
#define KIND_PAT(p) (17 + (p))                      /* pattern records (steps.c v2, self-describing) */
#define KIND_UNI(p) (49 + (p))                      /* v0.31: UNIVERSAL pages 0-7 (uni.h), kinds 49-56 */
#define IS_KIND_PAT(k) ((k) >= KIND_PAT(0) && (k) < KIND_PAT(PATTERNS))
#define IS_KIND_UNI(k) ((k) >= KIND_UNI(0) && (k) < KIND_UNI(UNI_PAGES))
_Static_assert(KIND_UNI(UNI_PAGES) <= S2_KINDS && KIND_UNI(0) == KIND_PAT(PATTERNS), "store2 kinds");
#define KIND_PAINT(s) (57 + (s))                    /* v0.37: PAINT mode's saved paintings 0-3, kinds 57-60 */
#define IS_KIND_PAINT(k) ((k) >= KIND_PAINT(0) && (k) < KIND_PAINT(PAINT_SLOTS))
#define KIND_CRASH 61                               /* v0.39: the last crash, kept in flash through a power-off */
_Static_assert(KIND_PAINT(0) == KIND_UNI(UNI_PAGES) && KIND_PAINT(PAINT_SLOTS) <= S2_KINDS, "store2 paint kinds");
/* v0.37 PAINT mode (paint.h). The canvas lives in RAM until power-off; the four saved paintings are mirrored in RAM too,
   so the save area's clean-up (compact.c) can write them back without borrowing scratch (its scratch is full of pages). */
static bool paintm;                                 /* PAINT mode is on */
static uint8_t paint_cur = 1;                       /* the picked colour (red) */
static uint8_t paint_cv[PAINT_LEN];                 /* the canvas */
static uint8_t paint_saved[PAINT_SLOTS][PAINT_LEN]; /* the saved paintings, as in flash */
static uint8_t paint_has;                           /* bit s: slot s holds a painting */
static uint8_t paint_pend;                          /* bit s: slot s waits to be written to flash */
static paint_btn_t paint_b = {-1, 0, 0};            /* the row button being held */
static void pm_paint(void);
static void pm_screen(void);
static bool uni_dirty(void);                        /* (UNIVERSAL section, below) */
#define KIND_PAT_V1(p) (9 + (p))                    /* v0.10's sparse records; kinds 1-8 = the old full-size ones */
static uint32_t pat_dirty;                          /* bit p = pattern p needs saving */
static bool global_dirty;

#define BPM     G.bpm
#define CURPAT  G.cur_pat
static inline bool is_mel(int t) { return t >= DTRACKS && t < DTRACKS + MTRACKS; }
static inline bool is_x(int t) { return t >= DTRACKS + MTRACKS; }   /* T17-T24 */
static inline int x_of(int t) { return t - DTRACKS - MTRACKS; }
/* A drum track can be a CC track (v0.29): its steps send CC <its note> = the step's lock value, else the level's velocity. */
static inline bool is_cc(int t) { return !is_mel(t) && (G.cc_trk[t >> 3] >> (t & 7) & 1); }
static inline int tlen(int p, int t) { int l = steps_len[p][t]; return l ? l : 16; }
static inline int level(int p, int t, int s) { return steps_get(p, t, s) & 3; }
static inline uint8_t tnote(int t)                  /* drum tracks only */
{
    if (is_x(t)) return G.x_note[x_of(t)] ? G.x_note[x_of(t)] : XDEF_NOTE[x_of(t)];
    return G.note[t] ? G.note[t] : DEF_NOTE[t];
}
static inline void set_tnote(int t, uint8_t n) { if (is_x(t)) G.x_note[x_of(t)] = n; else G.note[t] = n; }
static inline uint8_t tchan(int t)
{
    if (is_mel(t)) { uint8_t c = G.m_chan[t - DTRACKS]; return (c >= 1 && c <= 16) ? c : (uint8_t)(t - DTRACKS + 1); }
    uint8_t c = is_x(t) ? G.x_chan[x_of(t)] : G.chan[t];
    return (c >= 1 && c <= 16) ? c : 10;
}
static inline void set_tchan(int t, uint8_t c) { if (is_mel(t)) G.m_chan[t - DTRACKS] = c; else if (is_x(t)) G.x_chan[x_of(t)] = c; else G.chan[t] = c; }
static inline uint8_t *lock_cc_p(int t) { return is_x(t) ? &G.x_lock_cc[x_of(t)] : &G.lock_cc[t]; }
static inline uint8_t *trk_lvl_p(int t) { return is_x(t) ? &G.x_trk_lvl[x_of(t)] : &G.trk_lvl[t]; }
static inline bool muted(int t)
{
    if (is_mel(t)) return G.m_mute >> (t - DTRACKS) & 1;
    if (is_x(t)) return G.x_mute >> x_of(t) & 1;
    return G.mute_bits >> t & 1;
}
static inline void toggle_mute(int t)
{
    if (is_mel(t)) G.m_mute ^= (uint8_t)(1u << (t - DTRACKS));
    else if (is_x(t)) G.x_mute ^= (uint8_t)(1u << x_of(t));
    else G.mute_bits ^= (uint16_t)(1u << t);
}
/* The track on display row i (0-31): T1-T16, T17-T24, then M1-M8 (storage keeps M1-M8 at 16-23, where older saves have them). */
static inline int vtrack(int i) { return i < DTRACKS ? i : i < DTRACKS + XTRACKS ? i + MTRACKS : i - XTRACKS; }
/* the note a step plays: a melodic step's own note, a drum track's note */
static inline uint8_t step_note(int p, int t, int s) { if (!is_mel(t)) return tnote(t); uint8_t n = steps_note(p, t, s); return n ? n : 60; }
static inline int kb_track(void) { return DTRACKS + G.kb_track % MTRACKS; }   /* NOTE mode's melodic track */
static inline int kb_base(void) { return (G.kb_base >= 1 && G.kb_base <= 96) ? G.kb_base : 48; }

/* DRUM groups ("grids" in the code): group g = columns 4g..4g+3; pad 1 bottom-left, pad 16 top-right (MPC numbering). */
static const uint8_t GRID_DEF_CHAN[4] = {10, 1, 2, 3};
/* The MPC remote's channel (the owner's JJOS2XL panel map). v0.29b: the groups' own MPC-pads map is gone (the remote, DRUM
   again, presses the MPC's pads and everything else). */
#define MPC_PANEL_CH 14
/* 16 LEVELS (v0.29b, like the MPC's): the top row button in DRUM mode turns it on / off. It takes the LAST PAD HIT: all
   16 pads of that pad's group play that pad's note, pad 1 softest .. pad 16 = 127. Not saved (off at power-up). */
static bool lv16_on;
static int lv16_g;                                  /* the group it is on */
static uint8_t lv16_note;                           /* the note all its pads play */
static int last_g = -1;                             /* the last pad hit in DRUM mode (a notes group): its group, note */
static uint8_t last_note;
static inline bool grid_lvl(int g) { return lv16_on && g == lv16_g; }
static inline uint8_t level16_vel(int p) { return (uint8_t)(((p + 1) * 127 + 8) / 16); }   /* pad 0-15 -> 8, 16 ... 127 */
static inline uint8_t grid_chan(int g) { return (G.grid_chan[g] >= 1 && G.grid_chan[g] <= 16) ? G.grid_chan[g] : GRID_DEF_CHAN[g]; }
static inline int grid_base(int g) { return (G.grid_base[g] >= 1 && G.grid_base[g] <= 112) ? G.grid_base[g] : 36; }
static inline int grid_padnum(int r, int c) { return (3 - r) * 4 + c % 4; }          /* 0-15 = pad 1-16 */
static inline uint8_t grid_note(int g, int p) { return grid_lvl(g) ? lv16_note : (uint8_t)(grid_base(g) + p); }

/* v0.28: DRUM pressed again = the MPC REMOTE: the owner's whole MPC1000 / JJOS2XL face on the 64 pads (his layout,
   2026-10-23; his panel map on channel 14, notes counted C3 = 60 like the MPC). Each pad IS that MPC button: press and
   release, never recorded. The 16 pad CCs are the MPC's physical pads, so they do whatever the MPC's screen makes them
   do (play the bank, pick a NEXT SEQ, 16 levels ...). Index = row * 16 + column, row 0 = the top. */
enum { MR_NONE, MR_NOTE, MR_CC };
/* Colours = the owner's, pad by pad (2026-10-23, picked in a mock-up): the fader colours (FAD_RGB order) + grey. */
enum { K_RED, K_ORANGE, K_AMBER, K_YELLOW, K_LIME, K_GREEN, K_MINT, K_TEAL, K_CYAN, K_SKY, K_BLUE, K_INDIGO, K_PURPLE,
       K_MAGENTA, K_PINK, K_WHITE, K_GREY };
#define MRN(v, c) {MR_NOTE, v, c}
#define MRC(v, c) {MR_CC, v, c}
#define MRX {MR_NONE, 0, 0}
static const struct { uint8_t type, val, col; } MPCREM[64] = {
    /* pads 13-16 | TAP, ERASE, UNDO SEQ, NUMERIC | FULL LEVEL, BANK A | F1-F6 */
    MRC(52, K_BLUE), MRC(53, K_BLUE), MRC(54, K_BLUE), MRC(55, K_BLUE),
    MRN(55, K_GREY), MRN(56, K_WHITE), MRN(54, K_RED), MRN(61, K_MINT), MRN(57, K_ORANGE), MRC(17, K_AMBER),
    MRN(38, K_INDIGO), MRN(39, K_PURPLE), MRN(40, K_INDIGO), MRN(41, K_PURPLE), MRN(42, K_INDIGO), MRN(43, K_PURPLE),
    /* pads 9-12 | MODE, MAIN, WINDOW, AFTER 1 | 16 LEVELS, BANK B | -, STEP back, STEP forward, -, -, - */
    MRC(28, K_BLUE), MRC(29, K_BLUE), MRC(30, K_BLUE), MRC(31, K_BLUE),
    MRN(49, K_GREEN), MRN(52, K_GREY), MRN(53, K_YELLOW), MRN(48, K_INDIGO), MRN(58, K_ORANGE), MRC(18, K_AMBER),
    MRX, MRN(65, K_WHITE), MRN(64, K_WHITE), MRX, MRX, MRX,
    /* pads 5-8 | DATA -, UP, DATA +, AFTER 2 | NEXT SEQ, BANK C | -, LOCATE back, LOCATE forward, -, -, PLAY START */
    MRC(24, K_BLUE), MRC(25, K_BLUE), MRC(26, K_BLUE), MRC(27, K_BLUE),
    MRN(36, K_TEAL), MRN(44, K_WHITE), MRN(37, K_TEAL), MRN(51, K_INDIGO), MRN(59, K_ORANGE), MRC(19, K_AMBER),
    MRX, MRN(63, K_YELLOW), MRN(62, K_YELLOW), MRX, MRX, MRC(16, K_WHITE),
    /* pads 1-4 | LEFT, DOWN, RIGHT, SHIFT | TRACK MUTE, BANK D | -, -, REC, OVERDUB, STOP, PLAY */
    MRC(20, K_BLUE), MRC(21, K_BLUE), MRC(22, K_BLUE), MRC(23, K_BLUE),
    MRN(46, K_WHITE), MRN(45, K_WHITE), MRN(47, K_WHITE), MRN(50, K_SKY), MRN(60, K_ORANGE), MRC(9, K_AMBER),
    MRX, MRX, MRC(12, K_RED), MRC(13, K_RED), MRC(14, K_GREY), MRC(15, K_WHITE),
};
#undef MRN
#undef MRC
#undef MRX

/* Tracks are named by number only (T1-T16): what a note sounds like depends on where it's sent,
   so the firmware never guesses a drum name. Notes are shown as musical names (60 = C4). */
static const char *track_name(int t)
{
    static char nbuf[6];
    if (is_mel(t)) { nbuf[0] = 'M'; nbuf[1] = (char)('1' + t - DTRACKS); nbuf[2] = 0; return nbuf; }
    int n = is_x(t) ? DTRACKS + 1 + x_of(t) : t + 1;   /* T1-T16, T17-T24 */
    nbuf[0] = 'T';
    if (n >= 10) { nbuf[1] = (char)('0' + n / 10); nbuf[2] = (char)('0' + n % 10); nbuf[3] = 0; }
    else { nbuf[1] = (char)('0' + n); nbuf[2] = 0; }
    return nbuf;
}

static const char *note_name(uint8_t n)
{
    static const char *const NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static char nbuf[6];
    const char *b = NAMES[n % 12];
    int k = 0, oct = n / 12 - 1;
    while (*b) nbuf[k++] = *b++;
    if (oct < 0) { nbuf[k++] = '-'; oct = -oct; }
    nbuf[k++] = (char)('0' + oct);
    nbuf[k] = 0;
    return nbuf;
}

/* step byte: bits 0-1 level, 2-3 probability (0 = 100 %, 1 = 75, 2 = 50, 3 = 25), 4-5 ratchet (0 = 1x .. 3 = 4x),
   6-7 gate (0 = 50 %, 1 = 75, 2 = 95, 3 = 25: zero stays the old half-step gate) */
static inline int step_prob(uint8_t b) { return b >> 2 & 3; }
static inline int step_ratchet(uint8_t b) { return (b >> 4 & 3) + 1; }
static const uint8_t PROB_PCT[4] = {100, 75, 50, 25};
static const uint8_t GATE_PCT[4] = {50, 75, 95, 25};
static const uint8_t GATE_RANK[4] = {1, 2, 3, 0};   /* code -> position short..long */
static const uint8_t GATE_CODE[4] = {3, 0, 1, 2};   /* position -> code */
static inline int step_gate(uint8_t b) { return GATE_PCT[b >> 6 & 3]; }

/* Chord shapes for knob 3 on page 2 (semitones above the step's note). Recording can store others. */
static const struct { const char *name; int8_t a, b; } CHORDS[] = {
    {"--", 0, 0}, {"oct", 12, 0}, {"5th", 7, 0}, {"maj", 4, 7}, {"min", 3, 7}, {"sus2", 2, 7}, {"sus4", 5, 7},
    {"dim", 3, 6}, {"aug", 4, 8}, {"maj7", 4, 11}, {"m7", 3, 10}, {"7", 4, 10}, {"5+oct", 7, 12},
};
#define NCHORDS ((int)(sizeof CHORDS / sizeof CHORDS[0]))
static int chord_index(int8_t a, int8_t b) { for (int i = 0; i < NCHORDS; i++) if (CHORDS[i].a == a && CHORDS[i].b == b) return i; return -1; }

static uint32_t rng = 0x12345678u;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

/* ---------- runtime state ---------- */

static uint32_t loop_last_us, loop_max_us;          /* v0.31: the main loop's longest pass (us), reported by 07 */
static int queued_pat = -1;                         /* pattern (or, in song mode, slot) to switch to at the bar */
static bool bar_msgs;                               /* v0.30: host 0E 01 = send 0F at each bar start (off at power-on) */
#define SLOTS 16
static bool song_edit;                              /* song editor on the pads */
static bool trkvol;                                 /* v0.23: STEP pressed again = one volume fader per track (4 rows) instead of the steps */
static bool mpcrem;                                 /* v0.28: DRUM pressed again = the MPC remote (MPCREM) instead of the four groups */
static int song_sel;                                /* slot selected in the editor */
static int song_slot, song_bar;                     /* playback position in the song */
static int view_top;                                /* first visible track */
static int page;                                    /* visible step page */
static bool playing;
static uint32_t gstep;                              /* steps played since the start; next step to play */
static bool has_played;                             /* false after a rewind: playheads hidden */
static uint32_t grid_us, step_start_us;
static int pending = -1;                            /* gstep whose (possibly swung) notes are still due */
static int clock_k = 6;
static bool ext_clock, ext_playing;
static uint32_t ext_last_ms, ext_ticks;
static char status_line[22] = "";
/* v0.37d: a BRIEF message ("DAW off", "paint off") clears itself after BRIEF_MS, unless another message replaced it.
   Every other message still stays until replaced (warnings like FULL / save first must not vanish). the owner 2026-11-13:
   "DAW off" stayed for good and hid UNIVERSAL's page name, so the header read "Univ DAW off". */
#define BRIEF_MS 2000u
static uint32_t brief_until;                        /* ms_ticks the brief message goes (0 = none) */
static const char *brief_msg;                       /* the brief message's text (a string literal) */
static int held_row = -1, held_row_t = -1;          /* row button held, and the track it was on when pressed */
static int held_pad = -1;                           /* the step pad being held (its pad number), and where it was: */
static int hold_view, hold_page, hold_pat;          /* if the view, page or pattern moves, the hold is dropped */
static bool hold_songed;
static int held_t = -1, held_s = -1;               /* step pad being held (step settings) */
static bool held_pad_turned, held_pad_was_lit;
static bool held_row_turned;
static uint8_t row_consumed;                        /* rows whose mute tap was counted when another row landed on them (ctl.h) */
static int row_trk[4];                              /* the track each row button was on when it went down (VIEW_ROWS rows) */
static uint32_t save_due;
static bool save_waiting_compact;
static int saving_kind = -1;                        /* record being written, -1 none */
static bool save_run;                               /* a save pass is in progress */
static bool dirty = true, dfu_requested;
/* v0.37d: m = a string literal, shorter than status_line */
static void status_brief(const char *m)
{
    strcpy(status_line, m);
    brief_msg = m;
    brief_until = ms_ticks + BRIEF_MS; if (!brief_until) brief_until = 1;
    dirty = true;
}
/* main loop: the brief message's time is up -> gone, if still on screen */
static void status_brief_tick(void)
{
    if (!brief_until || (int32_t)(ms_ticks - brief_until) < 0) return;
    brief_until = 0;
    if (brief_msg && !strcmp(status_line, brief_msg)) { status_line[0] = 0; dirty = true; }
}
static bool save_force;                             /* save even with AUTOSAVE off (SAVE NOW, or the setting itself) */
static bool save_forced_run;                        /* the running save was a forced one (a retry / re-arm stays forced) */
static uint32_t save_lazy_due;                      /* settings changed while stopped: saved when this is due (0 = none) */
static uint8_t saves_global;                        /* settings records written since boot (mod 128; host status 07) */

/* the PI VU bars + the BROWSER menu (state; the code is below, after the text helpers) */
/* v0.36: the PI VU bars are gone (the owner 2026-11-12, to make room for DAW mode; orc1-firevu was already off). The
   G.lt_* save bytes stay where they are (the save layout never changes) and are simply unused. */
static bool menu_on, menu_edit;
static int pop_kind;                                /* a big-number popup after a change: 1 tempo, 2 swing */
static uint32_t pop_ms;
static int menu_sel, menu_grp;                      /* the selected row, and which menu page is open (0 = the top) */
static char menu_toast[22];
static uint32_t menu_toast_ms;

/* live play */
enum { MODE_STEP, MODE_NOTE, MODE_DRUM, MODE_FADER, MODE_UNI, MODE_DAW };   /* MODE_DAW = v0.31d the host's DAW page */
static int mode = MODE_STEP;
#define LIVE_VEL 100
#define LIVE_ACCENT 127
#define KB_SPAN 31                                  /* NOTE mode: 16 columns + 3 rows a fourth apart */
static struct { uint8_t note, chan; bool keys, on, cc; uint8_t off; } live[64];   /* what each pad's press sent, so its release matches (cc = a CC, not a note: its release sends CC note = off) */
/* v0.36: the ORC pads page (the 0A / 0B pad surface, v0.19-v0.35) is gone (the owner 2026-11-12: "an orc page that does
   nothing"; ORC mode replaced it). 0A / 0B are still accepted and ignored so an older ORC does no harm; 07 reports 0, 0
   where it reported the surface. Its RAM (colours 192 B + a flag per pad) went to DAW mode. */
static uint8_t notes_held[128];                     /* NOTE mode: pads holding each note (a note can sit on 2 pads) */
static int grid_sel;                                /* DRUM mode: grid being edited (ALT + tap a grid) */
static uint8_t m_cur[MTRACKS] = {60, 60, 60, 60, 60, 60, 60, 60};   /* the note a newly lit melodic step gets */
static uint32_t flash_ms[TRACKS];                   /* DRUM mode: when the sequencer last played each track */
static bool rec_arm;
static int32_t rec_skip[TRACKS];                    /* gstep + 1 whose note this track skips (just recorded early) */
static uint32_t ext_step_at, ext_even_at, ext_step_len;   /* external clock: last step's time, last even step's, step length (us) */
#define MAXFADERS 32
static int fad_top;                                 /* first visible fader */
static int mode_before_faders = MODE_STEP;          /* where a tap on PERFORM goes back to */
/* v0.31d: the DAW page = a second FADER SET the host defines (1A / 1B / 1C), RAM only, never saved. The fader code (pads,
   knobs 1-4, scrolling, the screen) works on the set on screen: fset() = the saved FADERS (G.fad_*) or the DAW page. Only
   what a level change sends (FADERS: its CC + the lazy save; the DAW page: 1D to the host), the colours / names and the
   row-button gestures (FADERS only) differ. */
static ctl_daw_t daw;
/* v0.36 DAW MODE: the Fire as a Mackie Control / HUI surface for a computer's DAW, on its own USB port (cable 1, "FLAME
   DAW"). BROWSER > MCU/HUI/DAW enters, SHIFT + BROWSER leaves; the sequencer keeps playing underneath on its own port.
   FADER view = the DAW page's rows (the same code: 16-pad level bars, knobs fine, SELECT scrolls), fed and read by the
   DAW instead of the Pi. BUTTON view = 8 strips x 2 pads, rows REC / SOLO / MUTE / SELECT, knobs = V-pots.
   STEP = buttons, PERFORM = faders, PLAY / STOP / REC = the DAW's transport, PATTERN = bank of 8, GRID = one track. */
static bool dawm;                                   /* DAW mode is on */
static uint8_t dawm_view;                           /* 0 = faders, 1 = buttons */
static uint8_t dawm_half;                           /* button view: the knobs are strips 1-4 (0) or 5-8 (1); SELECT push swaps */
static uint32_t dawm_seen;                          /* ms_ticks the DAW last said something (0 = not yet) */
static uint16_t dawm_lvl[DAW_STRIPS];               /* v0.36b: the DAW's last level per strip, kept while out of DAW mode */
static uint8_t dawm_proto_seen;                     /* v0.38b: the protocol the rows were last laid out for */
static uint8_t dawm_held[IN_COUNT];                 /* v0.38b: the DAW button each input's press sent (id + 1): its release
                                                       goes to THAT button even after a view change, a scroll or leaving */
static void paint_dawm(void);
static void draw_dawm_screen(void);
static int daw_top;                                 /* first visible DAW row */
static ctl_daw_t pi_page;                           /* v0.37c: the Pi's DAW page, kept aside while DAW mode uses `daw` (136 B) */
static int pi_top;                                  /* v0.37c: its first visible row */
typedef struct { uint8_t *val; int n; int *top; bool daw; } fset_t;
static fset_t fset(void)
{
    fset_t s = {G.fad_val, G.fad_n, &fad_top, false};
    if (mode == MODE_DAW) { s.val = daw.val; s.n = daw.n; s.top = &daw_top; s.daw = true; }
    return s;
}
static bool fader_mode(void) { return mode == MODE_FADER || mode == MODE_DAW; }
static uint32_t perform_down_ms;                    /* a short press of PERFORM = faders, a hold = FILL */
static const uint8_t FAD_RGB[16][3] = {             /* fader colours, R,G,B brightness 0-7 */
    {7, 0, 0}, {7, 2, 0}, {7, 4, 0}, {6, 6, 0}, {3, 7, 0}, {0, 7, 0}, {0, 7, 3}, {0, 6, 5},
    {0, 5, 7}, {0, 3, 7}, {0, 0, 7}, {3, 0, 7}, {5, 0, 7}, {7, 0, 6}, {7, 2, 4}, {6, 6, 6},
};
static const char *const FAD_COL_NAME[16] = {"red", "orange", "amber", "yellow", "lime", "green", "mint", "teal",
    "cyan", "sky", "blue", "indigo", "purple", "magenta", "pink", "white"};
static int knob_page;
static int pat_btn = -1;                            /* PATTERN up/down being held for the picker (its input), -1 none */
static bool picker_used;                            /* a pattern was tapped / recoloured during this hold */
static uint32_t pat_down_ms;                        /* when it was pressed: the picker shows after 0.2 s */
static int picker_sel;                              /* the pattern knob 1 recolours */                               /* step knobs: 0 = ratchet/chance/note/gate, 1 = condition/lock/chord */
static held_notes_t held_notes[TRACKS];             /* notes a TIE / SLIDE step is holding (play.c) */
static int rec_chord_t = -1; static uint32_t rec_chord_g;   /* NOTE-mode recording: notes on the same step form a chord */
static bool ext_odd_due;                            /* external clock: the swung (odd) step of this pair hasn't played */
static uint32_t held_since[TRACKS];                 /* step a TIE / SLIDE hold began (a hold lasts at most one loop) */

/* UNDO (v0.29): SHIFT + REC puts a pattern's steps + lengths back to before its last edit; again = redo. An edit is one
   GESTURE: a step pad from press to release (with everything the knobs did to it), one tap, a pattern copy (the pattern
   copied INTO), or a whole REC take (from the first recorded hit until REC is off or playing stops). The snapshot is
   the pattern's own save record (steps.c), so it holds exactly what a save would. Settings (channels, notes, mutes,
   tempo, CC tracks ...) are not undone. A gesture's snapshot goes into the spare buffer and only becomes the undo point
   once the gesture really changes the pattern (holding a step to look at it keeps the undo there was). */
#define UNDO_MAX 1536                               /* one pattern's record: 548 head + ~330 drum / ~160 melodic steps (RAM: 2 of these) */
static uint8_t undo_buf[2][UNDO_MAX];
static uint16_t undo_len[2];
static int undo_cur;                                /* the buffer holding the state SHIFT + REC goes back to */
static int undo_pat = -1;                           /* its pattern, -1 = nothing to undo */
static bool undo_redo;                              /* that state is what an undo replaced: SHIFT + REC = redo */
static bool undo_open, undo_used;                   /* a gesture is under way / it has changed its pattern */
static int undo_gpat;                               /* the gesture's pattern */
static bool undo_gok;                               /* its snapshot fitted the spare buffer */
static bool undo_rec;                               /* the gesture is a REC take (record_step opened it): it lasts while REC records */
static bool undo_big;                               /* the last edit had no undo: its pattern was too big for the buffer */
/* pattern p's record into buffer b */
static bool undo_take(int p, int b)
{
    uint32_t n = steps_record_len(p);
    if (n > UNDO_MAX) { undo_len[b] = 0; return false; }
    steps_record_copy(p, undo_buf[b]);              /* (a bulk copy: a press must not stall the step clock) */
    undo_len[b] = (uint16_t)n;
    return true;
}
/* before a gesture's first change to pattern p */
static void undo_begin(int p, bool rec)
{
    if (undo_open && undo_gpat == p) { if (rec) undo_rec = true; return; }
    undo_open = true; undo_used = false; undo_gpat = p; undo_rec = rec;
    undo_gok = undo_take(p, undo_cur ^ 1);
}
/* the pattern really changed (edited_pattern / touched_pattern) */
static void undo_edited(int p)
{
    if (!undo_open || undo_used || p != undo_gpat) return;
    undo_used = true; undo_redo = false;
    undo_big = !undo_gok;
    if (undo_gok) { undo_cur ^= 1; undo_pat = p; }
    else undo_pat = -1;                             /* too big to keep: this edit has no undo (the screen says so on SHIFT + REC) */
}

/* ROLLER / LOOPER (v0.29, like the BeatStep Pro's): hold KNOB MODE and the pads become the roller. In every row the left 8
   pads roll ALL tracks, the right 8 only that row's track; left to right 1 bar, 1/2, 1/4, 1/8, 1/16 (a loop of that many
   steps from where it started), then 1/32, 1/48, 1/64 (the step repeated 2 / 3 / 4 times). It lasts while the pad is held
   (KNOB MODE may be let go); on release the track plays from where it would have been (ctl_roll_g: the step counter never
   stopped). Several pads at once: a track's own pad beats the all-tracks ones, the newest wins. */
static ctl_roll_pad_t roll_pad[64];                 /* what each held roller pad does (code -1 = not a roller pad) */
static uint64_t roll_pads;                          /* roller pads held */
static uint32_t roll_seq;
static int8_t roll_c1[TRACKS];                      /* each track's roll now: code + 1, 0 = none (zero at boot) */
static uint32_t roll_at[TRACKS];                    /* where its roll started (a step number, like gstep) */
static bool roll_key;                               /* KNOB MODE is down as the roller key (not flipping a held step's page) */
static bool roll_key_used;                          /* ... and a roller pad was pressed: its release does not step the knob bank */
static uint32_t roll_key_ms;                        /* when it went down: the roller shows on the pads after 150 ms (a tap is a bank step) */
static inline uint32_t roll_g(int t, uint32_t g) { return ctl_roll_g(g, roll_at[t], roll_c1[t] - 1); }   /* the step track t plays for step g */
static bool roll_shown(void);                       /* (below, with the pad handling) */
static int kctx(void);                              /* (below, with the knob banks) */
static uint8_t *kc_bank(int c);

/* A save reads the live data, so an edit to the record being written cancels it (it never counts);
   the save runs again 3 s after the last edit. */
static void save_soon(void) { save_due = ms_ticks + 3000; if (!save_due) save_due = 1; }
/* A pattern save reads the live steps, so an edit to that pattern cancels it (it never counts) and
   queues it again. (The settings record is written from a snapshot, so nothing cancels it.) */
static void cancel_save_of(int kind)
{
    if (store2_busy_kind() != kind) return;
    store2_cancel(); saving_kind = -1; save_run = false;
    if (kind >= KIND_PAT(0)) pat_dirty |= (uint32_t)(1u << (kind - KIND_PAT(0)));
    if (save_forced_run) save_force = true;         /* a SAVE NOW that was cut short still saves with AUTOSAVE off */
    if (!save_due) save_soon();
}
static void edited_pattern(int p) { undo_edited(p); cancel_save_of(KIND_PAT(p)); pat_dirty |= (uint32_t)(1u << p); save_soon(); }
static void edited_global(void) { global_dirty = true; save_soon(); }
/* Moving around (pattern choice, mutes, octave, song on/off ...) and live-play gestures (tempo, swing,
   clock out, recorded hits): remembered with the next save, but doesn't start one by itself while
   playing. STOP starts one if anything is waiting (a calm moment). While STOPPED nothing may come
   (a tempo set before pressing PLAY, a light show picked), so a change then is saved
   CTL_LAZY_SAVE_MS after the LAST such change. */
static void touched_lazy(void) { if (!playing && !ext_playing) save_lazy_due = ctl_lazy_arm(ms_ticks); }
static void touched_global(void) { global_dirty = true; touched_lazy(); }
static inline int trk_level(int t) { return ctl_trk_level(*trk_lvl_p(t)); }
static ctl_tap_t tap_state;
/* ALT + STEP (v0.24): tap the beat; 2 taps set the tempo, more refine it (ctl_tap). */
static void tap_tempo(void)
{
    if (ext_clock) return;                          /* an external clock owns the tempo */
    int b = ctl_tap(&tap_state, ms_ticks);
    if (b) { BPM = (uint16_t)b; touched_global(); pop_kind = 1; pop_ms = ms_ticks; }   /* the big tempo number, like SHIFT + SELECT */
    dirty = true;
}
static void trk_level_set(int t, int lv)            /* a new track volume (v0.23): saved with the next save, the screen repaints */
{
    lv = lv < 0 ? 0 : lv > 127 ? 127 : lv;
    if (trk_level(t) == lv) return;
    *trk_lvl_p(t) = ctl_trk_store(lv);
    touched_global();
    dirty = true;
}
/* Same for a pattern: a save of it that is mid-write reads live data, so it is cancelled (it never
   counts); the pattern then waits for the next save instead of re-arming the timer. */
static void touched_pattern(int p)
{
    undo_edited(p);
    if (store2_busy_kind() == KIND_PAT(p)) { store2_cancel(); saving_kind = -1; save_run = false; }
    pat_dirty |= (uint32_t)(1u << p);
    touched_lazy();
}
/* Anything waiting: save it now-ish (STOP, and STOP while lights run, which shows the next show instead). */
static void commit_pending(void)
{
    save_lazy_due = 0;
    if ((global_dirty || (pat_dirty & ~pat_stuck) || uni_dirty()) && !save_due && !save_run) save_soon();
}
static void pool_full(void) { memcpy(status_line, "full", 5); dirty = true; }   /* the step pool is out of room */


/* Pattern colours: the fader palette; an empty pattern shows uncoloured (dim grey). */
static int pat_col_idx(int p) { return G.pat_col[p] ? (G.pat_col[p] - 1) & 15 : p % 16; }
static bool pat_empty(int p) { return steps_pattern_size(p) == 0; }
static void pat_rgb(int p, int k, int *R, int *Gn, int *B)   /* brightness k 1-7; empty = grey */
{
    if (pat_empty(p)) { *R = *Gn = *B = k > 3 ? 3 : 1; return; }
    const uint8_t *rgb = FAD_RGB[pat_col_idx(p)];
    *R = (rgb[0] * k + 6) / 7; *Gn = (rgb[1] * k + 6) / 7; *B = (rgb[2] * k + 6) / 7;
}
static void pat_recolour(int p, int d)
{
    if (p < 0 || p >= PATTERNS) return;
    int k = pat_col_idx(p) + d;
    G.pat_col[p] = (uint8_t)((k < 0 ? 0 : k > 15 ? 15 : k) + 1);
    edited_global();
    dirty = true;
}
static bool picker_on(void) { return pat_btn >= 0 && fire_held(pat_btn) && ms_ticks - pat_down_ms >= 200; }

#define NOFFS 128
#define NRATQ 128
static struct { uint8_t note, chan; uint32_t at_us; bool live; } offs[NOFFS];
static struct { uint8_t note, chan, vel; uint32_t at_us, gate_us; bool live; } ratq[NRATQ];   /* later ratchet hits */

static void add_off(uint8_t note, uint8_t ch, uint32_t at)
{
    for (int i = 0; i < NOFFS; i++)
        if (!offs[i].live) { offs[i].note = note; offs[i].chan = ch; offs[i].at_us = at; offs[i].live = true; return; }
}

static void midi_send3(uint8_t a, uint8_t b, uint8_t c)
{
    if (!tud_midi_mounted()) return;
    uint8_t m[3] = {a, b, c};
    tud_midi_stream_write(0, m, 3);
}
/* A real-time byte (clock F8, start FA, continue FB, stop FC) as its own USB-MIDI packet, CIN 0xF
   (single byte). TinyUSB's stream_write packs it as CIN 0x5 (SysEx end / system common): Windows
   decodes that, but the Kenton USB MIDI host dropped every one (2026-10-23, measured on the DIN). */
static void midi_send1(uint8_t a)
{
    if (!tud_midi_mounted()) return;
    const uint8_t pkt[4] = {0x0F, a, 0, 0};
    tud_midi_packet_write(pkt);
}

/* ---------- UNIVERSAL mode (v0.31): pages of pads + knobs stored ON THE FIRE (uni.h) ----------
   The pages live in flash only (store2 kinds 49-56; RAM has no room for them). An edit made on the Fire is a PATCH slot
   in RAM (uni_ps) that the pads use at once; the saver writes the page with its patches applied, read through a cursor
   (O(1) per flash word). A host write streams into flash as the chunks arrive (uw, uni_poll). Knob values, toggle
   states and latched notes are RAM only (not saved). Every lookup the main loop makes is O(1) (the index) or a cached
   number (uni_cache): nothing here walks a page per word, per pad or per knob detent (review 2026-10-27). */
#define UNI_LAYER_BTN IN_SHIFT                      /* held = each pad's second assignment (the owner: one constant, so ALT is a one-line change) */
#define UNI_EDIT_BTN IN_ALT                         /* held = edit what you touch */
#define UNI_ALL ((1u << US_EDIT) | (1u << US_PEND) | (1u << US_SAVE))
static const uint8_t UNI_EMPTY[UNI_HDR] = {UNI_VER, ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
static const int UNI_ID[UNI_PAGES] = {0, 1, 2, 3, 4, 5, 6, 7};
static uni_patch_t uni_ps[UNI_SLOTS];               /* edits made on the Fire, until saved */
static uint8_t uni_ix[UNI_KEYS];                    /* entry number of every key (base, SHIFT, knob) in the page uni_ix_* was built from */
static const uint8_t *uni_ix_rec; static uint32_t uni_ix_len, uni_ix_gen; static int uni_ix_page = -1;
static uint32_t uni_gen;                            /* +1 on every page record written and every compaction (the index rebuilds) */
static uint16_t uni_pver[UNI_PAGES];                /* +1 when a page's record or its edits change (its cached numbers are redone) */
static struct { uint16_t ver, len, sum, stored; bool ok; } uni_cache[UNI_PAGES];   /* per page: with its edits / as stored */
static uni_cur_t uni_wcur;                          /* the saver's cursor over the page it is writing */
static uint8_t uni_kv[UNI_PAGES][UNI_KNOBS];        /* knob values (RAM: at power-on each knob starts at its start value) */
static uint64_t uni_tog[UNI_PAGES][2];              /* CC toggle pads that are on, per layer (RAM: off at power-on) */
static uni_latch_t uni_lat[UNI_LATCH_MAX];          /* latched notes: what each latched pad SENT (current page only) */
static uint64_t uni_held;                           /* held pads whose release sends nothing (lit while held) */
static int uni_ed, uni_ed_idx, uni_ed_layer, uni_ed_kpage;   /* on-Fire edit: uni_ed 0 none, 1 a pad, 2 a knob */
static int uni_copy_src = -1;                       /* ALT + a pad held: tapping another pad copies it there */
static int uni_last_knob = -1;                      /* the knob slot turned last (marked on the screen) */
static struct {                                     /* a host page write (10 / 11 / 12) or clear (15) */
    uint8_t state, page, ack;                       /* state 0 idle, 1 receiving chunks, 2 committing; ack = the command to answer */
    bool gen;                                       /* the clear: the payload is the empty page */
    uint16_t len, done, cbase, clen;                /* done = payload bytes in flash; the chunk in RAM covers cbase .. +clen */
    uint32_t last_ms;
    uint8_t chunk[UNI_CHUNK];
} uw;
static char *cat(char *o, const char *str);
static bool compact_all(void);
static void midi_sendn(const uint8_t *m, int n) { if (n && tud_midi_mounted()) tud_midi_stream_write(0, m, (uint32_t)n); }

/* the stored page (0 = none, or a version this firmware doesn't know) */
static const uint8_t *uni_base(int page, uint32_t *len)
{
    const uint8_t *r = store2_ptr(KIND_UNI(page), len);
    if (r && uni_len_ok(*len) && r[0] == UNI_VER) return r;
    *len = 0;
    return 0;
}
static int uni_slot(int page, int key)
{
    for (int i = 0; i < UNI_SLOTS; i++) if (uni_ps[i].state != US_FREE && uni_ps[i].page == page && uni_ps[i].key == key) return i;
    return -1;
}
static bool uni_has_slots(int page, unsigned mask)
{
    for (int i = 0; i < UNI_SLOTS; i++) if (uni_ps[i].state != US_FREE && uni_ps[i].page == page && (mask >> uni_ps[i].state & 1)) return true;
    return false;
}
/* The entry that counts for (page, key): an edit not saved yet, else the stored page, by the index (O(1) for every key,
   SHIFT ones too: the pads repaint every pass). The index follows the page asked for. */
static const uint8_t *uni_entry(int page, int key)
{
    int s = uni_slot(page, key);
    if (s >= 0) return uni_ps[s].del ? 0 : uni_ps[s].e;
    uint32_t len;
    const uint8_t *r = uni_base(page, &len);
    if (!r || key < 0 || key >= UNI_KEYS) return 0;
    if (page != uni_ix_page || r != uni_ix_rec || len != uni_ix_len || uni_gen != uni_ix_gen) {   /* (re)build the index */
        uni_index(r, len, uni_ix);
        uni_ix_page = page; uni_ix_rec = r; uni_ix_len = len; uni_ix_gen = uni_gen;
    }
    return uni_ix[key] == 255 ? 0 : r + UNI_HDR + UNI_ENT * (uint32_t)uni_ix[key];
}
/* a pad's assignment on a layer; a SHIFT press on a pad with no SHIFT assignment plays its base one (*layer = which) */
static bool uni_pad_get(int page, int pad, int *layer, uni_pad_t *p)
{
    const uint8_t *e = *layer ? uni_entry(page, uni_pad_key(pad, UL_SHIFT)) : 0;
    if (!e) { *layer = UL_BASE; e = uni_entry(page, uni_pad_key(pad, UL_BASE)); }
    if (!e) return false;
    *p = uni_pad_of(e);
    return true;
}
static bool uni_knob_get(int page, int slot, uni_knob_t *k)
{
    const uint8_t *e = uni_entry(page, uni_knob_key(slot));
    if (!e) return false;
    *k = uni_knob_of(e);
    return k->type != UK_NONE;
}
/* latched notes off: exactly what each latched pad sent (pad < 0: all of them) */
static void uni_latch_off(int pad)
{
    uint8_t m[3 * UNI_LATCH_MAX];
    int n = uni_latch_release(uni_lat, pad, m, sizeof m);
    if (n) { midi_sendn(m, n); dirty = true; }
}
static void uni_values_reset(int page)              /* knobs to their start values, toggles off (a host rewrote the page) */
{
    for (int s = 0; s < UNI_KNOBS; s++) { uni_knob_t k; uni_kv[page][s] = uni_knob_get(page, s, &k) ? k.init : 64; }
    uni_tog[page][0] = uni_tog[page][1] = 0;
}
static void uni_touch(int page) { uni_pver[page]++; dirty = true; }   /* its cached numbers are redone when next asked */
/* a page's length + checksum with its edits, and as stored: one pass (uni_patched_*), only when the page changed */
static void uni_cached(int page)
{
    if (uni_cache[page].ok && uni_cache[page].ver == uni_pver[page]) return;
    uint32_t bl;
    const uint8_t *b = uni_base(page, &bl);
    uni_cache[page].len = (uint16_t)uni_patched_len(b, bl, uni_ps, UNI_SLOTS, page, UNI_ALL);
    uni_cache[page].sum = uni_patched_sum(b, bl, uni_ps, UNI_SLOTS, page, UNI_ALL);
    uni_cache[page].stored = (uint16_t)bl;
    uni_cache[page].ver = uni_pver[page]; uni_cache[page].ok = true;
}
/* bytes a page takes in the budget: the largest of what is stored, what it will be with its edits, and a host write of
   it under way (a Fire edit elsewhere must not take the room that write was promised: review 2026-10-27) */
static uint32_t uni_size(int page)
{
    uni_cached(page);
    uint32_t s = uni_cache[page].stored;
    if (s || uni_has_slots(page, UNI_ALL)) s = uni_cache[page].len > s ? uni_cache[page].len : s;
    if (uw.state && uw.page == page && uw.len > s) s = uw.len;
    return s;
}
static uint32_t uni_used_others(int page) { uint32_t u = 0; for (int p = 0; p < UNI_PAGES; p++) if (p != page) u += uni_size(p); return u; }
static bool uni_dirty(void) { for (int i = 0; i < UNI_SLOTS; i++) if (uni_ps[i].state == US_PEND) return true; return false; }
static bool uni_unsaved(void) { for (int i = 0; i < UNI_SLOTS; i++) if (uni_ps[i].state != US_FREE) return true; return false; }
static void uni_slots_state(int page, int from, int to)
{
    for (int i = 0; i < UNI_SLOTS; i++) if (uni_ps[i].state == from && uni_ps[i].page == page) uni_ps[i].state = (uint8_t)to;
    uni_touch(page);
}
static void save_soon(void);
/* an edit to a page the saver is writing: that write never counts, its edits wait for the next save */
static void uni_save_cancel(int page)
{
    if (saving_kind == KIND_UNI(page) && store2_busy_kind() == KIND_UNI(page)) { store2_cancel(); saving_kind = -1; save_run = false; }
    uni_slots_state(page, US_SAVE, US_PEND);
    if (!save_due) save_soon();
}
/* (page, key) = e, or removed (del), as an edit under way. false = no free slot, or the pages would not fit the budget.
   A pad that changes ends its latched notes first (with what they sent). */
static bool uni_set(int page, int key, const uint8_t *e, bool del)
{
    int s = uni_slot(page, key);
    if (s >= 0 && uni_ps[s].state == US_SAVE) uni_save_cancel(page);
    for (int i = 0; i < UNI_SLOTS && s < 0; i++) if (uni_ps[i].state == US_FREE) s = i;
    if (s < 0) { cat(status_line, "save first"); dirty = true; return false; }   /* 8 edits waiting (AUTOSAVE off): SAVE NOW */
    if (page == G.uni_page && key < 2 * UNI_PADS) uni_latch_off(key >> 1);
    uni_patch_t old = uni_ps[s];
    uni_ps[s].page = (int8_t)page; uni_ps[s].key = (uint8_t)key; uni_ps[s].del = del; uni_ps[s].state = US_EDIT;
    if (!del) memcpy(uni_ps[s].e, e, UNI_ENT);
    uni_touch(page);
    if (!del && uni_size(page) + uni_used_others(page) > UNI_BUDGET) {
        uni_ps[s] = old;
        uni_touch(page);
        cat(status_line, "pages full"); dirty = true;
        return false;
    }
    return true;
}
/* ALT let go (or the edit left): the edits are done, saved like any edit */
static void uni_edit_end(void)
{
    bool any = false;
    for (int i = 0; i < UNI_SLOTS; i++) if (uni_ps[i].state == US_EDIT) { uni_ps[i].state = US_PEND; any = true; }
    if (any) save_soon();
    uni_ed = 0; uni_copy_src = -1; uni_ed_kpage = 0;
    dirty = true;
}
/* a page's unsaved Fire edits are thrown away (the host replaced the page, or they can no longer fit: USER_GUIDE) */
static void uni_drop_edits(int page, const char *why)
{
    if (page == G.uni_page) { uni_latch_off(-1); uni_ed = 0; uni_copy_src = -1; }
    bool any = false;
    for (int i = 0; i < UNI_SLOTS; i++) if (uni_ps[i].state != US_FREE && uni_ps[i].page == page) { uni_ps[i].state = US_FREE; any = true; }
    uni_touch(page);
    if (any) cat(status_line, why);
}

/* host write / clear: the answer F0 7D 'ORC' <cmd> <result> <page> <done hi> <done lo> F7 */
static void uni_ack(int cmd, int res, int page)
{
    if (!tud_midi_mounted()) return;
    const uint8_t m[11] = {0xF0, 0x7D, 0x4F, 0x52, 0x43, (uint8_t)cmd, (uint8_t)res, (uint8_t)(page & 0x7F),
                           (uint8_t)(uw.done >> 7 & 0x7F), (uint8_t)(uw.done & 0x7F), 0xF7};
    tud_midi_stream_write(0, m, sizeof m);
}
static void uw_reader(const void *ctx, uint32_t off, uint8_t *dst, uint32_t n)
{
    (void)ctx;
    for (uint32_t i = 0; i < n; i++, off++)
        dst[i] = uw.gen ? (off < UNI_HDR ? UNI_EMPTY[off] : 0)
                        : (off >= uw.cbase && off < (uint32_t)uw.cbase + uw.clen) ? uw.chunk[off - uw.cbase] : 0xFF;
}
/* claim the space for a page record; no room = compact first (stopped only: it stalls ~1-2 s), else UR_ROOM */
static int uni_begin_write(int page, uint32_t len)
{
    if (store2_begin_src(KIND_UNI(page), len, uw_reader, 0)) return UR_OK;
    if (playing || ext_playing) return UR_ROOM;
    if (compact_all() && store2_begin_src(KIND_UNI(page), len, uw_reader, 0)) return UR_OK;
    return UR_FAIL;
}
/* busy = a commit / clear under way, or the writer is someone else's: answered BUSY with nothing touched (review
   2026-10-27: resetting first let a clear + a begin in one pass commit a 9-byte record of 0xFF) */
static bool uni_host_busy(void) { return uw.state == 2 || (uw.state == 0 && store2_busy()); }
static void uni_host_begin(int page, uint32_t len)  /* 10 */
{
    if (uni_host_busy()) { uni_ack(0x10, UR_BUSY, page); return; }
    if (uw.state == 1) { store2_cancel(); uw.state = 0; }   /* a new begin restarts: the old write never counts */
    uw.done = 0; uw.clen = 0; uw.gen = false;
    int r = uni_begin_check(page, len, uni_used_others(page));
    if (r == UR_OK) r = uni_begin_write(page, len);
    if (r == UR_OK) { uw.state = 1; uw.page = (uint8_t)page; uw.len = (uint16_t)len; uw.cbase = 0; uw.ack = 0x10; uw.last_ms = ms_ticks; }
    uni_ack(0x10, r, page);
}
static void uni_host_chunk(uint32_t off, const uint8_t *d, uint32_t n)   /* 11: answered once the chunk is in flash */
{
    int r = uw.state != 1 ? UR_ORDER : uw.clen ? UR_BUSY : uni_chunk_check(off, n, uw.done, uw.len);
    if (r != UR_OK) { uni_ack(0x11, r, uw.state ? uw.page : 127); return; }
    memcpy(uw.chunk, d, n); uw.cbase = uw.done; uw.clen = (uint16_t)n; uw.ack = 0x11; uw.last_ms = ms_ticks;
}
static void uni_host_commit(uint32_t sum)           /* 12: checked IN FLASH, then the commit word; answered when it counts */
{
    int r = (uw.state != 1 || uw.clen || uw.done != uw.len) ? UR_ORDER : UR_OK;
    if (r == UR_OK) {
        const uint8_t *pl = store2_busy_payload();
        r = !pl ? UR_FAIL : uni_sum(pl, uw.len) != sum ? UR_SUM : uni_check(pl, uw.len);
        if (r != UR_OK) { store2_cancel(); uw.state = 0; }   /* a bad page never counts */
    }
    if (r != UR_OK) { uni_ack(0x12, r, uw.page); return; }
    uw.state = 2; uw.ack = 0x12; uw.last_ms = ms_ticks;
}
static void uni_host_clear(int page)                /* 15: the empty page (9 bytes) */
{
    if (uni_host_busy()) { uni_ack(0x15, UR_BUSY, page); return; }
    if (uw.state == 1) { store2_cancel(); uw.state = 0; }
    uw.done = 0; uw.clen = 0; uw.gen = true;
    int r = uni_begin_write(page, UNI_HDR);
    if (r != UR_OK) { uw.gen = false; uni_ack(0x15, r, page); return; }
    uw.state = 2; uw.page = (uint8_t)page; uw.len = UNI_HDR; uw.done = UNI_HDR; uw.ack = 0x15; uw.last_ms = ms_ticks;
}
/* the main loop: a few flash words per quiet moment (like the saver), never more than the host has sent */
static void uni_poll(bool quiet)
{
    if (!uw.state) return;
    if (!store2_busy()) {                           /* the commit word is in (state 2), or a flash write failed */
        bool ok = uw.state == 2 && !store2_last_failed();
        int p = uw.page;
        uw.state = 0; uw.gen = false;
        if (ok) {                                   /* the host's page replaces any edit made here (USER_GUIDE) */
            uni_drop_edits(p, "edits replaced");
            if (G.uni_page == p) uni_latch_off(-1);
            uni_gen++;
            uni_values_reset(p);
        }
        uni_touch(p);
        uni_ack(uw.ack, ok ? UR_OK : UR_FAIL, p);
        return;
    }
    if (uw.state == 1 && ms_ticks - uw.last_ms > 3000) { store2_cancel(); uw.state = 0; uni_touch(uw.page); return; }   /* the host went quiet */
    if (!quiet) return;
    uint32_t have = uw.clen ? (uint32_t)uw.cbase + uw.clen : uw.done;
    uint32_t target = uw.state == 2 ? store2_words_total() : 8u + (have == uw.len ? (have + 3u) / 4u : have / 4u);
    uint32_t wd = store2_words_done();
    if (wd < target) store2_step(target - wd > 8 ? 8 : target - wd);
    if (uw.state == 1 && uw.clen && store2_busy() && store2_words_done() >= target) {
        uw.done = (uint16_t)(uw.cbase + uw.clen); uw.clen = 0;
        uni_ack(0x11, UR_OK, uw.page);
    }
}

static uint32_t step_us(void) { return 60000000u / (uint32_t)(BPM * 4); }
static uint32_t swing_us(void) { return (uint32_t)((G.swing - 50) * 2) * step_us() / 100; }
static inline int track_pos(int t, uint32_t g) { return (int)(g % (uint32_t)tlen(CURPAT, t)); }
/* Step length and swing delay as they really are: the host's when following external clock. */
static int ext_late(void) { return (G.swing - 50) * 6 / 50; }   /* swung step: this many clock ticks late */
static uint32_t cur_step_us(void) { return (ext_playing && ext_step_len) ? ext_step_len : step_us(); }
static uint32_t cur_swing_us(void) { return (ext_playing && ext_step_len) ? ext_step_len * (uint32_t)ext_late() / 6 : swing_us(); }

/* ---------- song ---------- */

static int song_len(void) { int n = 0; while (n < SLOTS && G.song_pat[n]) n++; return n; }
static int slot_reps(int i) { return G.song_rep[i] ? G.song_rep[i] : 1; }
static bool song_active(void) { return G.song_on && song_len() > 0; }

static void song_goto(int slot)
{
    int n = song_len();
    if (!n) return;
    song_slot = ((slot % n) + n) % n;
    song_bar = 0;
    CURPAT = (uint8_t)(G.song_pat[song_slot] - 1);
    touched_global();
}

/* Called on every 16-step bar boundary while playing (not on the very first step). */
static void song_bar_end(void)
{
    if (queued_pat >= 0) { song_goto(queued_pat); queued_pat = -1; return; }   /* PATTERN up/down in song mode = jump */
    if (++song_bar >= slot_reps(song_slot)) song_goto(song_slot + 1);
}

/* A step's notes are collected and handed to USB in ONE write, so they leave in one packet.
   (One write per note let the first go out alone and the rest a USB frame later, 2-3 ms.) */
static uint8_t batch[1024];                         /* up to 341 messages: 32 tracks x (lock + 3 notes + their offs + ends) = 30 bytes each */
static uint32_t batch_n;
static void batch_add(uint8_t a, uint8_t b, uint8_t c) { if (batch_n + 3 <= sizeof batch) { batch[batch_n++] = a; batch[batch_n++] = b; batch[batch_n++] = c; } }
static void batch_flush(void) { if (batch_n && tud_midi_mounted()) tud_midi_stream_write(0, batch, batch_n); batch_n = 0; }

static void seq_note_on(uint8_t ch, uint8_t note, uint8_t vel)
{
    for (int i = 0; i < NOFFS; i++)
        if (offs[i].live && offs[i].note == note && offs[i].chan == ch) { batch_add((uint8_t)(0x80 | ch), note, 0); offs[i].live = false; }
    batch_add((uint8_t)(0x90 | ch), note, vel);
}

static void out_cc(uint8_t ch, uint8_t cc, uint8_t v) { batch_add((uint8_t)(0xB0 | ch), cc, v); }
static void out_off(uint8_t ch, uint8_t n) { batch_add((uint8_t)(0x80 | ch), n, 0); }
static const play_out_t OUT = {out_cc, seq_note_on, out_off};

static void play_step(uint32_t g, uint32_t now)
{
    int was_pat = CURPAT;
    if (g % PAGE == 0 && g > 0 && song_active()) song_bar_end();
    else if (g % PAGE == 0 && queued_pat >= 0) { CURPAT = (uint8_t)queued_pat; queued_pat = -1; touched_global(); }
    if (CURPAT != was_pat)                          /* a new pattern: nothing carries over from a TIE / SLIDE */
        for (int t = 0; t < TRACKS; t++) if (held_notes[t].n) play_release(&held_notes[t], &OUT);
    /* how long this step really lasts until the next one sounds (a swung step is shorter) */
    uint32_t span = cur_step_us() - ((g & 1) ? cur_swing_us() : 0);
    for (int t = 0; t < TRACKS; t++) {
        if (muted(t)) { if (held_notes[t].n) play_release(&held_notes[t], &OUT); continue; }
        if (held_notes[t].n && g - held_since[t] > (uint32_t)tlen(CURPAT, t))
            play_release(&held_notes[t], &OUT);     /* a hold that found no next note within a loop ends */
        uint32_t gp = roll_g(t, g);                 /* the roller: the step this track plays instead (= g when it isn't rolling) */
        step_t v;
        if (!steps_read(CURPAT, t, track_pos(t, gp), &v)) continue;
        int lv = v.b & 3;
        if (rec_skip[t] == (int32_t)g + 1) { rec_skip[t] = 0; continue; }   /* hit live a moment ago */
        if (!cond_ok(cond_of(v.cond), gp / (uint32_t)tlen(CURPAT, t), fire_held(IN_PERFORM))) continue;   /* condition said no */
        if (step_prob(v.b) && rnd() % 100 >= PROB_PCT[step_prob(v.b)]) continue;   /* chance said no */
        uint8_t ch = (uint8_t)(tchan(t) - 1), notes[3];
        if (is_cc(t)) {                             /* a CC track: CC <its note> = the step's lock value, else the level's velocity */
            out_cc(ch, tnote(t), v.lock ? (uint8_t)(v.lock - 1) : LEVEL_VEL[lv]);
            flash_ms[t] = ms_ticks ? ms_ticks : 1;
            continue;
        }
        uint8_t tvel = (uint8_t)ctl_trk_vel(LEVEL_VEL[lv], trk_level(t));   /* the track's volume fader scales its velocity */
        int nn = play_step_notes(&v, is_mel(t), is_mel(t) ? 0 : tnote(t), ch, *lock_cc_p(t), tvel, &held_notes[t], &OUT, notes);
        flash_ms[t] = ms_ticks ? ms_ticks : 1;
        if (!nn) { held_since[t] = g; continue; }   /* TIE / SLIDE: held until this track's next note */
        int nr = ctl_roll_hits(roll_c1[t] - 1);     /* a 1/32 - 1/64 roll: that many hits per step, else the step's ratchet */
        if (!nr) nr = step_ratchet(v.b);
        uint32_t sub = span / (uint32_t)nr, gate = sub * (uint32_t)step_gate(v.b) / 100;
        for (int i = 0; i < nn; i++) {
            add_off(notes[i], ch, now + gate);
            for (int k = 1; k < nr; k++)            /* ratchets: the remaining hits, evenly spaced in the step */
                for (int q = 0; q < NRATQ; q++)
                    if (!ratq[q].live) {
                        ratq[q].note = notes[i]; ratq[q].chan = ch; ratq[q].vel = tvel;
                        ratq[q].at_us = now + (uint32_t)k * sub; ratq[q].gate_us = gate; ratq[q].live = true;
                        break;
                    }
        }
    }
    batch_flush();
    if (bar_msgs && tud_midi_mounted()) {           /* v0.30: tell the host a bar starts (after the notes: they go first) */
        uint8_t bm[CTL_BAR_LEN];
        if (ctl_bar_msg(bm, g, PAGE, CURPAT, song_active() ? song_slot : CTL_NO_SLOT)) tud_midi_stream_write(0, bm, sizeof bm);
    }
    has_played = true;
    dirty = true;
}

static void all_notes_off(void)
{
    for (int i = 0; i < NOFFS; i++)
        if (offs[i].live) { batch_add((uint8_t)(0x80 | offs[i].chan), offs[i].note, 0); offs[i].live = false; }
    for (int i = 0; i < NRATQ; i++) ratq[i].live = false;
    for (int t = 0; t < TRACKS; t++) if (held_notes[t].n) play_release(&held_notes[t], &OUT);
    for (int t = 0; t < TRACKS; t++) rec_skip[t] = 0;
    batch_flush();
}

static void stop(bool rewind)
{
    if (playing && G.clock_out) midi_send1(0xFC);
    playing = false;
    ext_playing = false;
    all_notes_off();
    pending = -1;
    rec_chord_t = -1;
    if (rewind) { gstep = 0; has_played = false; memset(roll_at, 0, sizeof roll_at); }   /* (a held roll restarts from the top too) */
    commit_pending();                               /* live-play changes waiting */
    dirty = true;
}

static void start(uint32_t now)
{
    if (G.clock_out) midi_send1(gstep == 0 ? 0xFA : 0xFB);
    if (gstep == 0 && song_active()) song_goto(0);  /* after a rewind the song starts from the top */
    playing = true;
    grid_us = now;
    pending = -1;
    rec_chord_t = -1;
    clock_k = 6;
    dirty = true;
}

/* ---------- LEDs ---------- */

static void paint_song_editor(void)
{
    for (int r = 0; r < VIEW_ROWS; r++)
        for (int c = 0; c < PAGE; c++) {
            int R = 0, Gn = 0, B = 0;
            if (r == 0) {                           /* slots: coloured by their pattern, selected = white */
                int pt = G.song_pat[c];
                if (c == song_sel) R = Gn = B = 7;
                else if (pt) pat_rgb(pt - 1, 5, &R, &Gn, &B);
                else if (c == song_len()) R = Gn = B = 1;       /* where the next slot would go */
                if (song_active() && (playing || ext_playing) && c == song_slot && c != song_sel && ((ms_ticks / 200) & 1)) R = Gn = B = 7;
            } else if (r == 1 || r == 2) {          /* patterns 1-16 / 17-32 for the selected slot */
                int p = (r - 1) * 16 + c;
                pat_rgb(p, G.song_pat[song_sel] == p + 1 ? 7 : 2, &R, &Gn, &B);
            } else if (c < 8) {                     /* bottom row: bars 1-8 */
                int reps = G.song_pat[song_sel] ? slot_reps(song_sel) : 0;
                Gn = (c < reps) ? (c == reps - 1 ? 7 : 3) : 1;
            }
            led_set(led_pad(r, c, 0), R);
            led_set(led_pad(r, c, 1), Gn);
            led_set(led_pad(r, c, 2), B);
        }
    for (int r = 0; r < VIEW_ROWS; r++) { led_set(LED_ROW_G[r], 0); led_set(LED_ROWSIDE_R[r], 0); led_set(LED_ROWSIDE_G[r], 0); }
}

/* NOTE mode: rows a fourth apart, bottom row lowest. */
static int kb_note(int r, int c) { return kb_base() + (VIEW_ROWS - 1 - r) * 5 + c; }

static void paint_keys(void)
{
    for (int r = 0; r < VIEW_ROWS; r++)
        for (int c = 0; c < PAGE; c++) {
            int n = kb_note(r, c), pc = n % 12, R = 0, Gn = 0, B = 0;
            if (notes_held[n]) Gn = 7;
            else if (pc == 0) { Gn = 3; B = 7; }    /* C */
            else if (pc != 1 && pc != 3 && pc != 6 && pc != 8 && pc != 10) R = Gn = B = 1;   /* white keys */
            led_set(led_pad(r, c, 0), R);
            led_set(led_pad(r, c, 1), Gn);
            led_set(led_pad(r, c, 2), B);
        }
    for (int r = 0; r < VIEW_ROWS; r++) { led_set(LED_ROW_G[r], 0); led_set(LED_ROWSIDE_R[r], 0); led_set(LED_ROWSIDE_G[r], 0); }
}

/* DRUM mode: four 4x4 grids, one colour each; pad 1 (bottom-left) a little brighter. */
static const uint8_t GRID_RGB[4][3] = {{7, 0, 0}, {0, 7, 0}, {0, 3, 7}, {6, 0, 6}};

static void paint_drums(void)
{
    bool editing = fire_held(IN_ALT);
    for (int r = 0; r < VIEW_ROWS; r++)
        for (int c = 0; c < PAGE; c++) {
            int g = c / 4, p = grid_padnum(r, c), R, Gn, B;
            uint8_t note = grid_note(g, p), ch = grid_chan(g);
            const uint8_t *rgb = GRID_RGB[g];
            int k = grid_lvl(g) ? 1 + p * 4 / 15 : p == 0 ? 3 : 2;   /* 16 levels: dim (soft) .. bright (hard) */
            if (editing && g != grid_sel) k = 1;     /* ALT held: the grid being edited stands out */
            for (int t = 0; t < TRACKS; t++)        /* the sequencer just played this pad's note (drum tracks, not CC ones) */
                if (!is_mel(t) && !is_cc(t) && flash_ms[t] && ms_ticks - flash_ms[t] < 90 && tnote(t) == note && tchan(t) == ch) { k = 7; break; }
            R = (rgb[0] * k + 6) / 7; Gn = (rgb[1] * k + 6) / 7; B = (rgb[2] * k + 6) / 7;
            if (live[r * 16 + c].on) R = Gn = B = 7;
            led_set(led_pad(r, c, 0), R);
            led_set(led_pad(r, c, 1), Gn);
            led_set(led_pad(r, c, 2), B);
        }
    for (int r = 0; r < VIEW_ROWS; r++) { led_set(LED_ROW_G[r], 0); led_set(LED_ROWSIDE_R[r], 0); led_set(LED_ROWSIDE_G[r], 0); }
    led_set(LED_ROW_G[0], lv16_on ? 7 : 1);         /* the top row button = 16 levels: lit while on */
}

/* the MPC remote: each button in its own colour, white while held, unused pads dark */
static const uint8_t MR_GREY[3] = {3, 3, 3};
static void paint_mpcrem(void)
{
    for (int pad = 0; pad < 64; pad++) {
        int r = pad / 16, c = pad % 16, R = 0, Gn = 0, B = 0;
        if (MPCREM[pad].type != MR_NONE) {
            int k = MPCREM[pad].col;
            const uint8_t *rgb = k < 16 ? FAD_RGB[k] : MR_GREY;
            R = (rgb[0] * 4 + 6) / 7; Gn = (rgb[1] * 4 + 6) / 7; B = (rgb[2] * 4 + 6) / 7;
            if (live[pad].on) R = Gn = B = 7;
        }
        led_set(led_pad(r, c, 0), R); led_set(led_pad(r, c, 1), Gn); led_set(led_pad(r, c, 2), B);
    }
    for (int r = 0; r < VIEW_ROWS; r++) { led_set(LED_ROW_G[r], 0); led_set(LED_ROWSIDE_R[r], 0); led_set(LED_ROWSIDE_G[r], 0); }
}

/* UNIVERSAL: each assigned pad dim in its colour (its base assignment's), bright while held or while a latch / toggle is
   on (SHIFT held: the SHIFT layer's state); the pad being edited blinks white; unassigned pads dark. */
static void paint_uni(void)
{
    int pg = G.uni_page, want = fire_held(UNI_LAYER_BTN) ? UL_SHIFT : UL_BASE;
    bool blink = (ms_ticks / 200) & 1;
    for (int pad = 0; pad < 64; pad++) {
        int r = pad / 16, c = pad % 16, R = 0, Gn = 0, B = 0, ly = want;
        uni_pad_t p;
        if (uni_pad_get(pg, pad, &ly, &p)) {
            const uint8_t *be = uni_entry(pg, uni_pad_key(pad, UL_BASE));
            int col = be ? uni_pad_of(be).col : UNI_COL_GREY;
            const uint8_t *rgb = col < 16 ? FAD_RGB[col] : MR_GREY;
            bool on = live[pad].on || (uni_held >> pad & 1) || (p.type == UT_LATCH && uni_latch_on(uni_lat, uni_pad_key(pad, ly)))
                      || (p.type == UT_TOGGLE && (uni_tog[pg][ly] >> pad & 1));
            int k = on ? 7 : 2;
            R = (rgb[0] * k + 6) / 7; Gn = (rgb[1] * k + 6) / 7; B = (rgb[2] * k + 6) / 7;
        }
        if (uni_ed == 1 && uni_ed_idx == pad && blink) R = Gn = B = 7;
        led_set(led_pad(r, c, 0), R); led_set(led_pad(r, c, 1), Gn); led_set(led_pad(r, c, 2), B);
    }
    for (int r = 0; r < VIEW_ROWS; r++) { led_set(LED_ROW_G[r], 0); led_set(LED_ROWSIDE_R[r], 0); led_set(LED_ROWSIDE_G[r], 0); }
}

/* FADERS: each row a fader in its colour, lit up to its level, dim beyond. The row after the last
   fader (if there's room) shows a dim mark: its row button adds a fader. */
/* beat / bar mark colours: which channels light (at level 1, or 2 for "both"'s bar mark) */
#define MARK_N 7
static const uint8_t MARK_RGB[MARK_N][3] = {{0, 0, 1}, {1, 1, 1}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 1}, {0, 1, 1}};
static const char *const NM_MARKCOL[MARK_N] = {"blue", "white", "red", "green", "yellow", "purple", "cyan"};

/* the set on screen (fset): the saved FADERS or (v0.31d) the DAW page */
static void paint_faders(void)
{
    fset_t s = fset();
    for (int r = 0; r < VIEW_ROWS; r++) {
        int f = *s.top + r;
        bool add = !s.daw && f == s.n;              /* FADERS' "+ add" row (the DAW page has none) */
        for (int c = 0; c < PAGE; c++) {
            int R = 0, Gn = 0, B = 0;
            if (f < s.n) {
                const uint8_t *rgb = s.daw ? daw.rgb[f] : FAD_RGB[G.fad_col[f] & 15];
                int pst = ctl_fader_pad(c, s.val[f]);           /* bright up to the level's tip pad, the tip dim, dark above */
                if (pst == 2) { R = rgb[0]; Gn = rgb[1]; B = rgb[2]; }
                else if (pst == 1) { R = rgb[0] ? 1 : 0; Gn = rgb[1] ? 1 : 0; B = rgb[2] ? 1 : 0; }
            } else if (add && c == 0) R = Gn = B = 1;   /* "+": add a fader here */
            led_set(led_pad(r, c, 0), R);
            led_set(led_pad(r, c, 1), Gn);
            led_set(led_pad(r, c, 2), B);
        }
        led_set(LED_ROW_G[r], !s.daw && held_row == r ? 7 : (f < s.n ? 3 : add ? 1 : 0));
        led_set(LED_ROWSIDE_R[r], 0); led_set(LED_ROWSIDE_G[r], 0);
    }
}

/* STEP pressed again (v0.23): one volume fader per track, 4 rows at a time. Each row is lit up to the track's level in the
   track's own colour; the row buttons still mute (same lights as the step view). */
static void paint_trkvol(void)
{
    for (int r = 0; r < VIEW_ROWS; r++) {
        int t = vtrack(view_top + r);
        const uint8_t *rgb = TRACK_RGB[t % 8];
        int tl = trk_level(t);
        for (int c = 0; c < PAGE; c++) {
            int R, Gn, B, pst = ctl_fader_pad(c, tl);       /* bright up to the level's tip pad, the tip dim, dark above */
            if (pst == 2) { R = rgb[0]; Gn = rgb[1]; B = rgb[2]; }
            else if (pst == 1) { R = rgb[0] ? 1 : 0; Gn = rgb[1] ? 1 : 0; B = rgb[2] ? 1 : 0; }
            else { R = Gn = B = 0; }
            if (muted(t)) { R = R ? 1 : 0; Gn = Gn ? 1 : 0; B = B ? 1 : 0; }
            led_set(led_pad(r, c, 0), R);
            led_set(led_pad(r, c, 1), Gn);
            led_set(led_pad(r, c, 2), B);
        }
        led_set(LED_ROW_G[r], held_row == r ? 7 : (muted(t) ? 0 : 3));
        led_set(LED_ROWSIDE_R[r], muted(t) ? 7 : 0);
        led_set(LED_ROWSIDE_G[r], 0);
    }
}

/* The roller (KNOB MODE held): in each row the left 8 pads = all tracks (loops amber, rolls cyan), the right 8 = that row's
   track in its colour (rolls brighter); a held pad white. */
static void paint_roller(void)
{
    for (int r = 0; r < VIEW_ROWS; r++) {
        int t = vtrack(view_top + r);
        for (int c = 0; c < PAGE; c++) {
            int pad = r * 16 + c, code = c % 8, R, Gn, B;
            if (roll_pads >> pad & 1) R = Gn = B = 7;
            else if (c < 8) { if (code < 5) { R = 5; Gn = 2; B = 0; } else { R = 0; Gn = 3; B = 5; } }
            else {
                const uint8_t *rgb = TRACK_RGB[t % 8];
                int k = code < 5 ? 2 : 5;
                R = (rgb[0] * k + 6) / 7; Gn = (rgb[1] * k + 6) / 7; B = (rgb[2] * k + 6) / 7;
            }
            led_set(led_pad(r, c, 0), R);
            led_set(led_pad(r, c, 1), Gn);
            led_set(led_pad(r, c, 2), B);
        }
        led_set(LED_ROW_G[r], muted(t) ? 0 : 3);
        led_set(LED_ROWSIDE_R[r], muted(t) ? 7 : 0);
        led_set(LED_ROWSIDE_G[r], roll_c1[t] ? 7 : 0);      /* this row's track is rolling */
    }
}

/* Hold PATTERN: patterns 1-16 on the top row, 17-32 on the second. Bright = the current one (or, in
   song mode, the one picked for recolouring), blinking = queued, grey = empty. */
static void paint_picker(void)
{
    int cur = song_active() ? picker_sel : CURPAT;
    bool blink = (ms_ticks / 150) & 1;
    for (int r = 0; r < VIEW_ROWS; r++)
        for (int c = 0; c < PAGE; c++) {
            int R = 0, Gn = 0, B = 0;
            if (r < 2) {
                int p = r * 16 + c;
                int k = p == cur ? 7 : (!song_active() && p == queued_pat) ? (blink ? 7 : 2) : 2;
                pat_rgb(p, k, &R, &Gn, &B);
            }
            led_set(led_pad(r, c, 0), R);
            led_set(led_pad(r, c, 1), Gn);
            led_set(led_pad(r, c, 2), B);
        }
    for (int r = 0; r < VIEW_ROWS; r++) { led_set(LED_ROW_G[r], 0); led_set(LED_ROWSIDE_R[r], 0); led_set(LED_ROWSIDE_G[r], 0); }
}

static void __attribute__((noinline)) paint(void)
{
    if (paintm) { pm_paint(); return; }             /* v0.37 PAINT mode owns every light */
    bool run = playing || ext_playing;
    if (dawm && dawm_view) paint_dawm();             /* v0.36 DAW mode's button view */
    else if (picker_on()) paint_picker();
    else if (roll_shown()) paint_roller();
    else if (fader_mode()) paint_faders();        /* FADERS or the DAW page */
    else if (mode == MODE_UNI) paint_uni();
    else if (mode == MODE_NOTE) paint_keys();
    else if (mode == MODE_DRUM && mpcrem) paint_mpcrem();
    else if (mode == MODE_DRUM) paint_drums();
    else if (song_edit) paint_song_editor();
    else if (trkvol) paint_trkvol();
    else {
    uint32_t last = gstep ? gstep - 1 : 0;          /* the step that most recently played */
    for (int r = 0; r < VIEW_ROWS; r++) {
        int t = vtrack(view_top + r);
        int len = tlen(CURPAT, t), head = track_pos(t, roll_g(t, last));   /* (a rolling track: its loop position) */
        const uint8_t *rgb = TRACK_RGB[t % 8];
        for (int c = 0; c < PAGE; c++) {
            int s = page * PAGE + c, R = 0, Gn = 0, B = 0;
            if (s < len) {
                int lv = level(CURPAT, t, s);
                bool is_head = has_played && run && s == head;
                if (lv != LV_OFF) {
                    int k = LEVEL_BRIGHT[lv];
                    step_t sv; steps_read(CURPAT, t, s, &sv);
                    uint8_t sb = sv.b;
                    bool special = step_prob(sb) || step_ratchet(sb) > 1 || (sb >> 6) || sv.cond || sv.lock || sv.ch2 || sv.ch3;
                    if (special && ((ms_ticks / 300) & 1)) k = k > 2 ? k - 2 : 1;   /* gentle pulse: the step has settings */
                    R = (rgb[0] * k + 6) / 7; Gn = (rgb[1] * k + 6) / 7; B = (rgb[2] * k + 6) / 7;
                    if (muted(t)) { R = R ? 1 : 0; Gn = Gn ? 1 : 0; B = B ? 1 : 0; }
                    if (is_head) R = Gn = B = 7;
                } else if (is_head) {
                    R = Gn = B = 2;
                } else if (!G.mark_off && (G.mark_space == 1 ? s % 16 == 0 : s % 4 == 0)) {
                    /* beat / bar marks (BROWSER > Display): level 1 = the LED driver's lowest; dim blue by default (white =
                       3 channels at level 1 was too bright, the owner 2026-10-14). "both": the bar mark at level 2. */
                    const uint8_t *mc = MARK_RGB[G.mark_col % MARK_N];
                    int ml = (G.mark_space == 2 && s % 16 == 0) ? 2 : 1;
                    R = mc[0] * ml; Gn = mc[1] * ml; B = mc[2] * ml;
                }
            }
            led_set(led_pad(r, c, 0), R);
            led_set(led_pad(r, c, 1), Gn);
            led_set(led_pad(r, c, 2), B);
        }
        led_set(LED_ROW_G[r], held_row == r ? 7 : (muted(t) ? 0 : 3));
        led_set(LED_ROWSIDE_R[r], muted(t) ? 7 : 0);
        led_set(LED_ROWSIDE_G[r], 0);
    }
    }
    led_set(LED_PATSONG_G, G.song_on ? 7 : 0);
    led_set(LED_PATSONG_Y, song_edit ? (((ms_ticks / 300) & 1) ? 7 : 2) : 0);
    led_set(LED_PLAY_G, run ? 7 : 0);
    led_set(LED_PLAY_Y, run ? 0 : 2);
    led_set(LED_STOP_Y, run ? 2 : 0);
    led_set(LED_SHIFT_Y, fire_held(IN_SHIFT) ? 7 : 1);
    led_set(LED_ALT_Y, fire_held(IN_ALT) ? 7 : 1);
    led_set(LED_STEP_Y, mode == MODE_STEP ? (trkvol && ((ms_ticks / 300) & 1) ? 2 : 7) : 1);   /* blinks while the track faders are up */
    led_set(LED_NOTE_Y, mode == MODE_NOTE ? 7 : mode == MODE_UNI ? (((ms_ticks / 300) & 1) ? 7 : 2) : 1);   /* v0.38: its 2nd (UNIVERSAL) blinks */
    led_set(LED_DRUM_Y, mode == MODE_DRUM ? (mpcrem && ((ms_ticks / 300) & 1) ? 2 : 7) : 1);   /* blinks while the MPC remote is up */
    led_set(LED_REC_Y, rec_arm ? 7 : 0);
    /* The four lights beside KNOB MODE (CHANNEL / MIXER / USER 1 / USER 2): only the selected one is lit. The other three
       were at level 1 until v0.34c and the owner could hardly tell which was on (2026-11-12: "the mode lights are blaring
       constantly so hard you almost can't tell which you're on ... the corner ones"; even 1 of 21 steps was "still too
       bright"). ORC mode and Akai's firmware light one of them too. */
    if (held_t >= 0 && held_row < 0) {             /* a step held: which page its knobs are on (CHANNEL = 1, MIXER = 2) */
        led_set(LED_CHANNEL, knob_page == 0 ? 5 : 0); led_set(LED_MIXER, knob_page == 1 ? 5 : 0);
        led_set(LED_USER1, 0); led_set(LED_USER2, 0);
    } else if (mode == MODE_UNI && uni_ed == 1) {  /* editing a pad: which knob page (CHANNEL = 1, MIXER = 2) */
        led_set(LED_CHANNEL, uni_ed_kpage == 0 ? 5 : 0); led_set(LED_MIXER, uni_ed_kpage == 1 ? 5 : 0);
        led_set(LED_USER1, 0); led_set(LED_USER2, 0);
    } else {                                        /* otherwise: the free-knob bank, 1-4 (UNIVERSAL: its own) */
        int kb = (mode == MODE_UNI ? G.uni_bank : *kc_bank(kctx())) & 3;
        led_set(LED_CHANNEL, kb == 0 ? 5 : 0); led_set(LED_MIXER, kb == 1 ? 5 : 0);
        led_set(LED_USER1, kb == 2 ? 5 : 0); led_set(LED_USER2, kb == 3 ? 5 : 0);
    }
    led_set(LED_PERFORM_Y, mode == MODE_FADER ? 7 : mode == MODE_DAW ? (((ms_ticks / 300) & 1) ? 7 : 2) : 1);   /* v0.38: faders lit, its 2nd (the DAW page) blinks */
    led_set(LED_PERFORM_R, fire_held(IN_PERFORM) ? 7 : 0);  /* FILL while held */
    if (mode == MODE_NOTE) {                        /* GRID = octave */
        led_set(LED_GRID_L, kb_base() > 1 ? 2 : 0);
        led_set(LED_GRID_R, kb_base() < 96 ? 2 : 0);
    } else if (mode == MODE_UNI) {                 /* GRID = page 1-8 */
        led_set(LED_GRID_L, G.uni_page > 0 ? 2 : 0);
        led_set(LED_GRID_R, G.uni_page < UNI_PAGES - 1 ? 2 : 0);
    } else {
        led_set(LED_GRID_L, mode == MODE_STEP && page > 0 ? 2 : 0);
        led_set(LED_GRID_R, mode == MODE_STEP && page < PAGES - 1 ? 2 : 0);
    }
    bool blink = (ms_ticks / 150) & 1;
    led_set(LED_PAT_UP, queued_pat >= 0 && queued_pat > CURPAT ? (blink ? 7 : 0) : 1);
    led_set(LED_PAT_DN, queued_pat >= 0 && queued_pat < CURPAT ? (blink ? 7 : 0) : 1);
    led_set(LED_BROWSER, menu_on ? 7 : 1);   /* the menu button: lit while the menu is open */
    if (dawm) {                                     /* v0.36 DAW mode: the transport shows the DAW's, the views their buttons */
        led_set(LED_PLAY_G, daw_led(DB_PLAY) ? 7 : 0); led_set(LED_PLAY_Y, daw_led(DB_PLAY) ? 0 : 2);
        led_set(LED_STOP_Y, daw_led(DB_STOP) ? 7 : 2); led_set(LED_REC_Y, daw_led(DB_RECORD) ? 7 : 1);
        led_set(LED_STEP_Y, dawm_view ? 7 : 1); led_set(LED_PERFORM_Y, dawm_view ? 1 : 7);
        led_set(LED_NOTE_Y, 0); led_set(LED_DRUM_Y, 0);
        led_set(LED_GRID_L, 2); led_set(LED_GRID_R, 2); led_set(LED_PAT_UP, 2); led_set(LED_PAT_DN, 2);
    }
}

/* ---------- screen ---------- */

static void fmt_int(char *o, int v) { char t[12]; int n = 0; if (v < 0) { *o++ = '-'; v = -v; } do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v); while (n) *o++ = t[--n]; *o = 0; }
static char *cat(char *o, const char *str) { while (*str) *o++ = *str++; *o = 0; return o; }
static char *cat_int(char *o, int v) { fmt_int(o, v); return o + strlen(o); }

/* ---------- the PI VU bars + the BROWSER menu ----------
   PI VU: the pads show the Pi's audio as 16 bars instead of the steps. They start by hand (menu: Start Pi VU) or when
   the host asks (02 01); nothing starts them by itself. The first touch of any pad, button or knob only ends them (it is
   not also an edit); SHIFT, ALT, PLAY, STOP and BROWSER (the menu) work as usual. Started by hand, they wait through a
   quiet Pi (the bars fall to dark, then come back with the levels); started by the host, its silence ends them.
   v0.31r: the light SHOWS that used to live here are gone (lights.h).
   A frame is drawn 2 columns at a time, only when the next step is not about to be due (the rule a
   flash write follows), and committed to the LEDs when all 16 columns are done. */

static void saver_start(void);                      /* (save / load section, below) */
static bool save_paint_only;                        /* v0.38b: this run writes paintings (v0.39: and the crash record) only:
                                                       never the sequencer's unsaved edits when AUTOSAVE is off */
/* The menu. Items edit G fields directly; SELECT scrolls, pushing it opens / confirms, BROWSER backs out. */
/* Free-knob banks (v0.20): with nothing held, KNOB MODE steps banks 1-4 and the knobs do what the bank says. Bank 1 out
   of the box = the old behaviour (knob 1 swing, knobs 2-4 relative CC 17 / 18 / 19 on ch 1); banks 2-4 = CC 20-31. */
#define KB_SWING 128
#define KB_TEMPO 129
/* v0.29: each SECTION has its own 4 banks: STEP, NOTE, DRUM, the MPC remote (kctx: the one the pads are in now). */
enum { KC_STEP, KC_NOTE, KC_DRUM, KC_MPC, KC_N };
static const char *const NM_KCTX[KC_N] = {"step", "note", "drum", "MPC"};
static int kctx(void) { return mode == MODE_NOTE ? KC_NOTE : mode == MODE_DRUM ? (mpcrem ? KC_MPC : KC_DRUM) : KC_STEP; }
static uint8_t *kc_bank(int c) { return c > 0 && c < KC_N ? &G.kbx_bank[c - 1] : &G.knob_bank; }
static uint8_t *kc_fn(int c, int b, int k) { return c > 0 && c < KC_N ? &G.kbx_fn[c - 1][b & 3][k & 3] : &G.kb_fn[b & 3][k & 3]; }
static uint8_t *kc_ch(int c, int b, int k) { return c > 0 && c < KC_N ? &G.kbx_ch[c - 1][b & 3][k & 3] : &G.kb_ch[b & 3][k & 3]; }
static uint8_t *kc_abs(int c, int b, int k) { return c > 0 && c < KC_N ? &G.kbx_abs[c - 1][b & 3][k & 3] : &G.kb_abs[b & 3][k & 3]; }
static uint8_t kb_val[KC_N][4][4];                  /* absolute knobs' values (64 at boot: main) */
static int kb_fn(int c, int b, int k)
{
    int f = *kc_fn(c, b, k);
    if (f >= 1 && f <= KB_TEMPO) return f;
    if ((b & 3) == 0) return (k & 3) == 0 ? KB_SWING : 16 + (k & 3);
    return 16 + 4 * (b & 3) + (k & 3);
}
static int kb_chan(int c, int b, int k) { int ch = *kc_ch(c, b, k); return ch >= 1 && ch <= 16 ? ch : 1; }
/* v0.29, once: NOTE / DRUM / MPC start as copies of the STEP banks (so nothing changes until one is edited) */
static void kc_init_copies(void)
{
    if (G.kbx_init) return;
    for (int c = 0; c < KC_N - 1; c++) {
        G.kbx_bank[c] = G.knob_bank;
        memcpy(G.kbx_fn[c], G.kb_fn, sizeof G.kb_fn); memcpy(G.kbx_ch[c], G.kb_ch, sizeof G.kb_ch); memcpy(G.kbx_abs[c], G.kb_abs, sizeof G.kb_abs);
    }
    G.kbx_init = 1;
}

/* MIDI clock out on / off: SHIFT + PLAY and BROWSER > Playing (a running clock gets a STOP / CONTINUE so gear follows) */
static void clock_out_toggle(void)
{
    if (playing && G.clock_out) midi_send1(0xFC);
    G.clock_out = !G.clock_out;
    if (playing && G.clock_out) midi_send1(0xFB);
    cat(status_line, G.clock_out ? "clk out" : "clk off");
    dirty = true; touched_global();
}

enum { MK_ENUM, MK_ACT, MK_SUB,
       MK_INFO,                                     /* a read-only line (the shortcut pages): the name is the whole text */
       MK_TEMPO, MK_SWING, MK_CLOCK,                /* Playing: the same values SHIFT + SELECT, knob 1, SHIFT + PLAY change */
       MK_KSEC, MK_KBANK, MK_KSEL, MK_KFN, MK_KCH, MK_KMODE,     /* Knobs: the section, the bank, which knob, what it sends, channel, mode */
       MK_ROOM,                                         /* Memory (v0.26): read-only, n = 0 drum steps left, 1 note steps left, 2 % used */
       MK_ABOUT };                                      /* About (v0.35): read-only, n = ABOUT_* (crash.h crash_about_value) */
/* grp = the menu page the item is on (0 = the top); a MK_SUB item opens page n. */
typedef struct { const char *name; uint8_t grp, kind; uint8_t *v; uint8_t n; const char *const *names; void (*act)(void); } mitem_t;

static crash_info_t crash_boot;                     /* (defined with the crash code below; the About page reads it) */
static void menu_save_now(void);
static void menu_host_now(void);
static void menu_dawm_now(void);
static void menu_paint_now(void);
static void set_mode(int m);
static int menu_knob;                               /* Knobs page: the knob being set up (0-3) */
static int menu_kc;                                 /* Knobs page: the section being set up (the one the pads were in when the menu opened) */
static const char *const NM_AUTOSAVE[2] = {"on", "off"};   /* autosave_off = 0 means ON */
static const char *const NM_DAWP[3] = {"auto", "MCU", "HUI"};
static const char *const NM_MARKS[2] = {"on", "off"};   /* mark_off 0 = marks on */
static const char *const NM_SPACING[3] = {"every beat", "every bar", "beat+bar"};
static const char *const NM_KMODE[2] = {"relative", "absolute"};
static const char *const NM_PADHOLD[6] = {"off", "10 ms", "20 ms", "30 ms", "40 ms", "60 ms"};   /* = PAD_HOLD_MS */
static const mitem_t MENU[] = {
    /* the top page */
    {"Autosave",    0, MK_ENUM,   &G.autosave_off, 2, NM_AUTOSAVE, 0},
    {"Save now",    0, MK_ACT,    0, 0, 0, menu_save_now},
    {"ORC mode",    0, MK_ACT,    0, 0, 0, menu_host_now},      /* v0.33: the ORC takes the whole Fire (hostm.h) */
    {"MCU/HUI/DAW", 0, MK_ACT,    0, 0, 0, menu_dawm_now},      /* v0.36: Mackie Control / HUI for a computer (daw.h) */
    {"Paint",       0, MK_ACT,    0, 0, 0, menu_paint_now},     /* v0.37: paint the pads (paint.h) */
    {"DAW protocol",0, MK_ENUM,   &G.daw_proto, 3, NM_DAWP, 0},
    {"Playing",     0, MK_SUB,    0, 2, 0, 0},
    {"Knobs",       0, MK_SUB,    0, 3, 0, 0},
    {"Display",     0, MK_SUB,    0, 4, 0, 0},
    {"Memory",      0, MK_SUB,    0, 16, 0, 0},
    {"Shortcuts",   0, MK_SUB,    0, 5, 0, 0},
    {"About",       0, MK_SUB,    0, 17, 0, 0},     /* v0.35: version + the crash on record, for a tester's photo */
    /* page 1: was the Pi VU bars (gone in v0.36) */
    /* page 2: playing (the shortcuts still work) */
    {"Tempo",       2, MK_TEMPO,  0, 0, 0, 0},
    {"Swing",       2, MK_SWING,  0, 0, 0, 0},
    {"Clock out",   2, MK_CLOCK,  0, 0, 0, 0},
    {"Pad release", 2, MK_ENUM,   &G.pad_hold, 6, NM_PADHOLD, 0},
    /* page 3: knobs (the free knobs, nothing held; KNOB MODE steps the bank) */
    {"Section",     3, MK_KSEC,   0, 0, 0, 0},
    {"Bank",        3, MK_KBANK,  0, 0, 0, 0},
    {"Knob",        3, MK_KSEL,   0, 0, 0, 0},
    {"Sends",       3, MK_KFN,    0, 0, 0, 0},
    {"Channel",     3, MK_KCH,    0, 0, 0, 0},
    {"Mode",        3, MK_KMODE,  0, 0, 0, 0},
    /* page 4: display */
    {"Beat marks",  4, MK_ENUM,   &G.mark_off, 2, NM_MARKS, 0},
    {"Colour",      4, MK_ENUM,   &G.mark_col, MARK_N, NM_MARKCOL, 0},
    {"Spacing",     4, MK_ENUM,   &G.mark_space, 3, NM_SPACING, 0},
    /* page 5: shortcuts (each a page of read-only lines, <= 19 characters: the screen's width beside the scroll bar) */
    {"Steps",       5, MK_SUB,    0, 6, 0, 0},
    {"Hold a step", 5, MK_SUB,    0, 7, 0, 0},
    {"Hold a row",  5, MK_SUB,    0, 8, 0, 0},
    {"Patterns",    5, MK_SUB,    0, 9, 0, 0},
    {"Song editor", 5, MK_SUB,    0, 10, 0, 0},
    {"Playing",     5, MK_SUB,    0, 11, 0, 0},
    {"Note mode",   5, MK_SUB,    0, 12, 0, 0},
    {"Drum mode",   5, MK_SUB,    0, 13, 0, 0},
    {"Faders",      5, MK_SUB,    0, 14, 0, 0},
    {"Knobs",       5, MK_SUB,    0, 15, 0, 0},
    {"Press: step on/off", 6, MK_INFO, 0, 0, 0, 0}, {"SH+pad: accent", 6, MK_INFO, 0, 0, 0, 0},
    {"ALT+pad: length", 6, MK_INFO, 0, 0, 0, 0}, {"GRID: step page", 6, MK_INFO, 0, 0, 0, 0},
    {"SELECT: scroll rows", 6, MK_INFO, 0, 0, 0, 0}, {"SH+PAT: 4 rows", 6, MK_INFO, 0, 0, 0, 0},
    {"Row button: mute", 6, MK_INFO, 0, 0, 0, 0}, {"Tap PERFORM: faders", 6, MK_INFO, 0, 0, 0, 0},
    {"Hold PERFORM: fills", 6, MK_INFO, 0, 0, 0, 0},
    {"KNOB MODE: 1 <-> 2", 7, MK_INFO, 0, 0, 0, 0}, {"1A: ratchet", 7, MK_INFO, 0, 0, 0, 0},
    {"1B: chance", 7, MK_INFO, 0, 0, 0, 0}, {"1C: note", 7, MK_INFO, 0, 0, 0, 0},
    {"1D: gate 25-95%", 7, MK_INFO, 0, 0, 0, 0}, {"  then tie, slide", 7, MK_INFO, 0, 0, 0, 0},
    {"2A: condition", 7, MK_INFO, 0, 0, 0, 0}, {"  fill=hold PERFORM", 7, MK_INFO, 0, 0, 0, 0}, {"2B: lock value", 7, MK_INFO, 0, 0, 0, 0},
    {"2C: chord", 7, MK_INFO, 0, 0, 0, 0},
    {"1A: track channel", 8, MK_INFO, 0, 0, 0, 0}, {"1B: track note", 8, MK_INFO, 0, 0, 0, 0},
    {"1C: lock CC", 8, MK_INFO, 0, 0, 0, 0}, {"M row 1B: new note", 8, MK_INFO, 0, 0, 0, 0},
    {"1D: note / CC track", 8, MK_INFO, 0, 0, 0, 0},
    {"PAT: next/previous", 9, MK_INFO, 0, 0, 0, 0}, {"Hold PAT: picker", 9, MK_INFO, 0, 0, 0, 0},
    {"PAT+1A: color", 9, MK_INFO, 0, 0, 0, 0}, {"ALT+PAT: copy", 9, MK_INFO, 0, 0, 0, 0},
    {"PAT/SONG: editor", 9, MK_INFO, 0, 0, 0, 0}, {"SH+PAT/SONG: on/off", 9, MK_INFO, 0, 0, 0, 0},
    {"Top row: slot", 10, MK_INFO, 0, 0, 0, 0}, {"Rows 2-3: pattern", 10, MK_INFO, 0, 0, 0, 0},
    {"Bottom row: bars", 10, MK_INFO, 0, 0, 0, 0}, {"1A: slot color", 10, MK_INFO, 0, 0, 0, 0},
    {"SH+slot: clear", 10, MK_INFO, 0, 0, 0, 0},
    {"PLAY: start/stop", 11, MK_INFO, 0, 0, 0, 0}, {"STOP: stop, rewind", 11, MK_INFO, 0, 0, 0, 0},
    {"SH+SELECT: tempo", 11, MK_INFO, 0, 0, 0, 0}, {"SH+ALT+SELECT: x10", 11, MK_INFO, 0, 0, 0, 0},
    {"SH+PLAY: clock out", 11, MK_INFO, 0, 0, 0, 0}, {"ALT+STEP: tap tempo", 11, MK_INFO, 0, 0, 0, 0}, {"REC: record live", 11, MK_INFO, 0, 0, 0, 0},
    {"STEP again: levels", 11, MK_INFO, 0, 0, 0, 0}, {"  tap/1A-1D: volume", 11, MK_INFO, 0, 0, 0, 0},
    {"SH+REC: undo/redo", 11, MK_INFO, 0, 0, 0, 0}, {"KNOB MODE+pad: roll", 11, MK_INFO, 0, 0, 0, 0},
    {"  left 8: all trks", 11, MK_INFO, 0, 0, 0, 0}, {"  right 8: that row", 11, MK_INFO, 0, 0, 0, 0},
    {"GRID: octave", 12, MK_INFO, 0, 0, 0, 0}, {"SH+GRID: semitone", 12, MK_INFO, 0, 0, 0, 0},
    {"SELECT: track M1-8", 12, MK_INFO, 0, 0, 0, 0}, {"ALT+SELECT: channel", 12, MK_INFO, 0, 0, 0, 0},
    {"SH+pad: accent", 12, MK_INFO, 0, 0, 0, 0}, {"REC: record notes", 12, MK_INFO, 0, 0, 0, 0},
    {"NOTE again: UNIV", 12, MK_INFO, 0, 0, 0, 0},  /* v0.38 */
    {"ALT+tap: pick group", 13, MK_INFO, 0, 0, 0, 0}, {"ALT+1A: channel", 13, MK_INFO, 0, 0, 0, 0},
    {"ALT+1B: first note", 13, MK_INFO, 0, 0, 0, 0}, {"Top row btn: 16 lv", 13, MK_INFO, 0, 0, 0, 0},
    {"SH+pad: accent", 13, MK_INFO, 0, 0, 0, 0}, {"REC: record hits", 13, MK_INFO, 0, 0, 0, 0},
    {"DRUM again: MPC", 13, MK_INFO, 0, 0, 0, 0},
    {"Tap PERFORM: open", 14, MK_INFO, 0, 0, 0, 0}, {"Tap column: level", 14, MK_INFO, 0, 0, 0, 0},
    {"1A-1D: fine tune", 14, MK_INFO, 0, 0, 0, 0}, {"SELECT: scroll", 14, MK_INFO, 0, 0, 0, 0},
    {"Empty row btn: add", 14, MK_INFO, 0, 0, 0, 0}, {"SH+row: remove", 14, MK_INFO, 0, 0, 0, 0},
    {"Row+1A-C: CC/ch/col", 14, MK_INFO, 0, 0, 0, 0}, {"PERFORM x2: DAW pg", 14, MK_INFO, 0, 0, 0, 0},
    {"KNOB MODE: bank 1-4", 15, MK_INFO, 0, 0, 0, 0}, {"  tap; hold = roll", 15, MK_INFO, 0, 0, 0, 0},
    {"Lit light: bank", 15, MK_INFO, 0, 0, 0, 0}, {"Own banks per mode", 15, MK_INFO, 0, 0, 0, 0}, {"BROWSER>Knobs: edit", 15, MK_INFO, 0, 0, 0, 0},
    /* page 16: memory (v0.26): how much room the steps have left (read-only) */
    {"Drum steps left", 16, MK_ROOM, 0, 0, 0, 0}, {"Note steps left", 16, MK_ROOM, 0, 1, 0, 0},
    {"Memory used", 16, MK_ROOM, 0, 2, 0, 0},
    /* page 17: about (v0.35): which firmware, and the crash on record (read-only; a photo of it is a bug report) */
    /* v0.35b: the first screenful (6 rows) holds everything a crash needs (build + PC + LR = where in the code), so ONE photo
       is enough; the counts come after */
    {"Version",  17, MK_ABOUT, 0, ABOUT_VERSION, 0, 0}, {"Build",    17, MK_ABOUT, 0, ABOUT_BUILD, 0, 0},
    {"Crash",    17, MK_ABOUT, 0, ABOUT_CAUSE, 0, 0},   {"In",       17, MK_ABOUT, 0, ABOUT_STAGE, 0, 0},
    {"PC",       17, MK_ABOUT, 0, ABOUT_PC, 0, 0},      {"LR",       17, MK_ABOUT, 0, ABOUT_LR, 0, 0},
    {"Crashes",  17, MK_ABOUT, 0, ABOUT_TOTAL, 0, 0},   {"Uptime",   17, MK_ABOUT, 0, ABOUT_UPTIME, 0, 0},
    {"Misreads", 17, MK_ABOUT, 0, ABOUT_MISREADS, 0, 0},
};
#define MENU_ALL ((int)(sizeof MENU / sizeof MENU[0]))
#define MENU_PAGES 18
#define MENU_PAGE_MAX 16                            /* the most items one page may hold (draw_menu's arrays) */
static const char *const MENU_TITLE[MENU_PAGES] = {"Menu", "(unused)", "Playing", "Knobs", "Display", "Shortcuts",
    "Steps", "Hold step", "Hold row", "Patterns", "Song edit", "Playing", "Note mode", "Drum mode", "Faders", "Knobs", "Memory",
    "About"};   /* <= 10 characters: room before "BRW=back" */
static const uint8_t MENU_PARENT[MENU_PAGES] = {0, 0, 0, 0, 0, 0, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 0, 0};   /* where BROWSER goes back to */

static int menu_count(void)                         /* items on the open page */
{
    int n = 0;
    for (int k = 0; k < MENU_ALL; k++) if (MENU[k].grp == menu_grp) n++;
    return n;
}
static const mitem_t *menu_it(int i)                /* the i-th item of the open page */
{
    for (int k = 0; k < MENU_ALL; k++) if (MENU[k].grp == menu_grp && i-- == 0) return &MENU[k];
    return &MENU[0];
}

static void menu_toast_set(const char *s) { cat(menu_toast, s); menu_toast_ms = ms_ticks ? ms_ticks : 1; dirty = true; }

static void menu_save_now(void)
{
    bool any = global_dirty || (pat_dirty & ~pat_stuck) || uni_dirty();
    if (save_run || store2_busy()) menu_toast_set("busy, retry");
    else if (!any) menu_toast_set("nothing new");
    else { save_force = true; saver_start(); menu_toast_set("saving..."); }
}

/* (v0.36b: BROWSER > DAW page is gone: PERFORM's third tap reaches the Pi's DAW page, and only once the Pi set one up) */
static void menu_value(const mitem_t *m, char *o)
{
    *o = 0;
    switch (m->kind) {
    case MK_SUB: cat(o, ">"); break;
    case MK_ENUM: cat(o, m->names[*m->v < m->n ? *m->v : 0]); break;
    case MK_TEMPO: o = cat_int(o, BPM); cat(o, " bpm"); break;
    case MK_SWING: o = cat_int(o, G.swing); cat(o, "%"); break;
    case MK_CLOCK: cat(o, G.clock_out ? "on" : "off"); break;
    case MK_ROOM: {                                 /* Memory page: what the shared step pool still has room for */
        uint32_t room = steps_room();
        if (m->n == 0) cat_int(o, (int)(room / ST_DRUM_SIZE));
        else if (m->n == 1) cat_int(o, (int)(room / ST_MEL_SIZE));
        else { o = cat_int(o, (int)(((ST_POOL - room) * 100u + ST_POOL - 1u) / ST_POOL)); cat(o, "%"); }   /* rounded up: full = 100 */
        break;
    }
    case MK_ABOUT:                                  /* About page: the crash read at boot (06 01 clears it) + the live misread count */
        crash_about_value(m->n, &crash_boot, t4_note.magic == T4_NOTE_MAGIC ? t4_note.count : 0u, FW_VERSION, BUILD_ID, o);
        break;
    case MK_KSEC: cat(o, NM_KCTX[menu_kc % KC_N]); break;
    case MK_KBANK: cat_int(o, (*kc_bank(menu_kc) & 3) + 1); break;
    case MK_KSEL: { char kn[3] = {(char)('1' + (*kc_bank(menu_kc) & 3)), (char)('A' + (menu_knob & 3)), 0}; cat(o, kn); break; }   /* bank + knob: 3C */
    case MK_KFN: {
        int f = kb_fn(menu_kc, *kc_bank(menu_kc), menu_knob);
        if (f == KB_SWING) cat(o, "swing"); else if (f == KB_TEMPO) cat(o, "tempo"); else { o = cat(o, "CC "); cat_int(o, f); }
        break;
    }
    case MK_KCH: o = cat(o, "ch "); cat_int(o, kb_chan(menu_kc, *kc_bank(menu_kc), menu_knob)); break;
    case MK_KMODE: cat(o, NM_KMODE[*kc_abs(menu_kc, *kc_bank(menu_kc), menu_knob) ? 1 : 0]); break;
    default: break;
    }
}

static void menu_turn(int d)
{
    if (!menu_edit) {
        menu_sel += d > 0 ? 1 : -1;
        if (menu_sel < 0) menu_sel = 0;
        if (menu_sel > menu_count() - 1) menu_sel = menu_count() - 1;
        return;
    }
    const mitem_t *m = menu_it(menu_sel);
    int b = *kc_bank(menu_kc) & 3, big = fire_held(IN_SHIFT) ? 10 : 1;   /* SHIFT = steps of 10 (tempo, CC numbers) */
    switch (m->kind) {
    case MK_TEMPO: { int t = (int)BPM + (d > 0 ? big : -big); BPM = (uint16_t)(t < 40 ? 40 : t > 300 ? 300 : t); touched_global(); return; }
    case MK_SWING: { int s = G.swing + (d > 0 ? 1 : -1); G.swing = (uint8_t)(s < 50 ? 50 : s > 75 ? 75 : s); touched_global(); return; }
    case MK_CLOCK: if ((d > 0) != (G.clock_out != 0)) clock_out_toggle(); return;
    case MK_KSEC: { int c = menu_kc + (d > 0 ? 1 : -1); menu_kc = c < 0 ? 0 : c > KC_N - 1 ? KC_N - 1 : c; return; }
    case MK_KBANK: { int nb = b + (d > 0 ? 1 : -1); *kc_bank(menu_kc) = (uint8_t)(nb < 0 ? 0 : nb > 3 ? 3 : nb); touched_global(); return; }
    case MK_KSEL: { int k = menu_knob + (d > 0 ? 1 : -1); menu_knob = k < 0 ? 0 : k > 3 ? 3 : k; return; }
    case MK_KFN: {                                  /* the list: swing, tempo, CC 1 .. CC 127 */
        int f = kb_fn(menu_kc, b, menu_knob), i = f == KB_SWING ? 0 : f == KB_TEMPO ? 1 : f + 1;
        i += d > 0 ? (i >= 2 ? big : 1) : (i > 2 ? -big : -1);
        i = i < 0 ? 0 : i > 128 ? 128 : i;
        *kc_fn(menu_kc, b, menu_knob) = (uint8_t)(i == 0 ? KB_SWING : i == 1 ? KB_TEMPO : i - 1);
        edited_global(); return;
    }
    case MK_KCH: { int c = kb_chan(menu_kc, b, menu_knob) + (d > 0 ? 1 : -1); *kc_ch(menu_kc, b, menu_knob) = (uint8_t)(c < 1 ? 1 : c > 16 ? 16 : c); edited_global(); return; }
    case MK_KMODE: *kc_abs(menu_kc, b, menu_knob) = d > 0 ? 1 : 0; edited_global(); return;
    default: break;
    }
    uint8_t was = *m->v;
    int v = (int)*m->v + (d > 0 ? 1 : -1);
    if (v < 0) v = 0;
    if (v > (int)m->n - 1) v = (int)m->n - 1;
    *m->v = (uint8_t)v;
    if (m->v == &G.daw_proto && *m->v != was) {   /* a new protocol choice starts the translator over (v0.38b: only a real change) */
        daw_init(G.daw_proto % 3); memset(dawm_lvl, 0, sizeof dawm_lvl); dawm_proto_seen = (uint8_t)daw_proto();
    }
    if (m->v == &G.autosave_off) { save_force = true; edited_global(); }   /* the setting itself always saves */
    else edited_global();
}

static void menu_push(void)                         /* SELECT pushed */
{
    const mitem_t *m = menu_it(menu_sel);
    if (m->kind == MK_ACT) m->act();
    else if (m->kind == MK_SUB) { menu_grp = m->n; menu_sel = 0; menu_edit = false; }
    else if (m->kind == MK_INFO || m->kind == MK_ROOM || m->kind == MK_ABOUT) return;   /* a shortcut line / a readout: nothing to change */
    else menu_edit = !menu_edit;
}

static void menu_back(void)                         /* BROWSER: finish an edit, else up a page, else close */
{
    if (menu_edit) menu_edit = false;
    else if (menu_grp) {
        int from = menu_grp;
        menu_grp = MENU_PARENT[from < MENU_PAGES ? from : 0]; menu_sel = 0;
        for (int i = 0; i < menu_count(); i++) { const mitem_t *m = menu_it(i); if (m->kind == MK_SUB && m->n == from) menu_sel = i; }
    } else menu_on = false;
}

/* A press while a menu or lights are showing. true = it was used here (the normal handler must not see it). */
static bool overlay_event(int idx, int pad)
{
    if (menu_on) {
        if (idx == IN_BROWSER) menu_back();
        else if (idx == IN_SELECT) menu_push();
        else if (idx == IN_PLAY || idx == IN_STOP || idx == IN_SHIFT || idx == IN_ALT) return false;   /* transport + modifiers still work */
        dirty = true;
        return true;
    }
    if (idx == IN_BROWSER) { menu_on = true; menu_edit = false; menu_grp = 0; menu_sel = 0; menu_kc = kctx(); dirty = true; return true; }
    (void)pad;
    return false;
}

/* unsaved changes: a save pending / running, or (with AUTOSAVE off) anything waiting for SAVE NOW */
static bool unsaved(void)
{
    return save_due || save_lazy_due || save_run || save_waiting_compact || uni_unsaved() || (G.autosave_off && (global_dirty || (pat_dirty & ~pat_stuck)));
}

static void __attribute__((noinline)) draw_menu(void)
{
    scr_menu_row_t rows[MENU_PAGE_MAX];
    char vals[MENU_PAGE_MAX][14];
    int n = menu_count();
    if (n > MENU_PAGE_MAX) n = MENU_PAGE_MAX;       /* (wiring_test keeps every page within it) */
    for (int i = 0; i < n; i++) {
        const mitem_t *m = menu_it(i);
        menu_value(m, vals[i]);
        rows[i].name = m->name; rows[i].value = vals[i];
    }
    const char *note = (menu_toast_ms && ms_ticks - menu_toast_ms < 2000) ? menu_toast : menu_edit ? "SEL=set" : "BRW=back";
    scr_menu(MENU_TITLE[menu_grp], note, rows, n, menu_sel, menu_edit, unsaved());
}

static void __attribute__((noinline)) render(void)
{
    char buf[24], *o;
    draw_clear();
    if (menu_on) { draw_menu(); return; }
    if (paintm) { pm_screen(); return; }            /* v0.37 PAINT mode */
    if (dawm && dawm_view) { draw_dawm_screen(); return; }   /* v0.36 DAW mode's button view */
    if (pop_kind && ms_ticks - pop_ms < 1200) {      /* tempo / swing: the number, big, for a moment after a change */
        if (pop_kind == 1) scr_popup("Tempo", BPM, "bpm", BPM, 40, 300, unsaved());
        else scr_popup("Swing", G.swing, "%", G.swing, 50, 75, unsaved());
        return;
    }
    pop_kind = 0;
    char left[24], ctx[16], stat[24];
    if (song_active()) {                            /* S<slot>/<len> P<pattern> <bar>/<bars> */
        o = cat(left, "S"); o = cat_int(o, song_slot + 1); o = cat(o, "/"); o = cat_int(o, song_len());
        o = cat(o, " P"); o = cat_int(o, CURPAT + 1); o = cat(o, " "); o = cat_int(o, song_bar + 1);
        o = cat(o, "/"); o = cat_int(o, slot_reps(song_slot));
        if (queued_pat >= 0) { o = cat(o, ">S"); cat_int(o, queued_pat + 1); }
    } else {
        o = cat(left, "PAT "); o = cat_int(o, CURPAT + 1);
        if (queued_pat >= 0) { o = cat(o, ">"); o = cat_int(o, queued_pat + 1); }
        if (G.song_on) cat(o, " nosong");
    }
    bool step_held = held_t >= 0 && held_row < 0 && (steps_get(CURPAT, held_t, held_s) & 3);
    ctx[0] = stat[0] = 0;
    cat(stat, status_line);
    if (picker_on()) cat(ctx, "Patterns");
    else if (roll_shown()) cat(ctx, "Roll");
    else if (fader_mode()) {                        /* FADERS, or (v0.31d) the DAW page: "DAW F1-4/9" */
        if (mode == MODE_FADER && held_row >= 0 && held_row_t < G.fad_n) { cat(ctx, "Fader"); stat[0] = 0; }
        else {
            fset_t s = fset();
            int rows = s.n + (!s.daw && s.n < MAXFADERS ? 1 : 0), top = *s.top;
            cat(ctx, s.daw ? "DAW" : "Faders");
            o = cat(stat, "F"); o = cat_int(o, top + 1); o = cat(o, "-"); o = cat_int(o, top + VIEW_ROWS < rows ? top + VIEW_ROWS : rows);
            o = cat(o, "/"); cat_int(o, s.n);
        }
    }
    else if (mode == MODE_UNI) {                    /* v0.31: "Univ P3/8 name", or what is being edited */
        cat(ctx, uni_ed ? "Edit" : "Univ");
        if (uni_ed == 1) {
            o = cat(stat, "pad "); o = cat_int(o, uni_ed_idx / 16 + 1); o = cat(o, "-"); o = cat_int(o, uni_ed_idx % 16 + 1);
            if (uni_ed_layer) cat(o, " SHIFT");
        } else if (uni_ed == 2) {
            o = cat(stat, "knob "); *o++ = (char)('1' + (uni_ed_idx >> 2)); *o++ = (char)('A' + (uni_ed_idx & 3)); *o = 0;
        } else if (!status_line[0]) {
            o = cat(stat, "P"); o = cat_int(o, G.uni_page + 1); o = cat(o, "/8 ");
            if (fire_held(UNI_LAYER_BTN)) o = cat(o, "SHIFT ");
            uint32_t ln; const uint8_t *r = uni_base(G.uni_page, &ln);
            int n = UNI_NAME;
            while (r && n > 0 && r[n] == ' ') n--;  /* (name bytes 1-8, trailing spaces off) */
            for (int i = 1; r && i <= n; i++) *o++ = (char)r[i];
            *o = 0;
        }
    }
    else if (mode == MODE_NOTE) cat(ctx, "NOTE");
    else if (mode == MODE_DRUM) cat(ctx, mpcrem ? "MPC" : "DRUM");
    else if (song_edit) cat(ctx, "Song");
    else if (step_held) {
        o = cat(ctx, track_name(held_t)); o = cat(o, " step "); cat_int(o, held_s + 1);
        cat(stat, knob_page ? "pg2" : "pg1");
    }
    else if (held_row >= 0) { cat(ctx, "Track "); cat(ctx, track_name(held_row_t)); stat[0] = 0; }
    else if (trkvol) {                              /* v0.23: the track volume faders; which tracks are on the rows */
        cat(ctx, "Volume");
        o = cat(stat, track_name(vtrack(view_top))); o = cat(o, "-"); cat(o, track_name(vtrack(view_top + VIEW_ROWS - 1)));
    }
    else { o = cat(ctx, "P"); o = cat_int(o, page + 1); o = cat(o, "/"); cat_int(o, PAGES); }
    {
        scr_head_t hd = {left, (int)BPM, G.swing, ext_clock ? 2 : G.clock_out ? 1 : 0, playing || ext_playing, unsaved(), ctx, stat};
        scr_header(&hd);
    }
    if (picker_on()) {                              /* the pattern picker */
        int p = song_active() ? picker_sel : CURPAT;
        draw_text(0, 28, "Patterns: tap one", 1);
        o = cat(buf, "P"); o = cat_int(o, p + 1); o = cat(o, pat_empty(p) ? " (empty)" : "");
        draw_text(0, 37, buf, 1);
        o = cat(buf, "1A color "); cat(o, FAD_COL_NAME[pat_col_idx(p)]);
        draw_text(0, 46, buf, 1);
        if (song_active()) draw_text(0, 55, "song on: tap = pick", 1);
    } else if (roll_shown()) {                      /* the roller: what the newest held pad does, and the layout */
        int newest = -1;
        for (int pad = 0; pad < 64; pad++)
            if ((roll_pads >> pad & 1) && (newest < 0 || roll_pad[pad].seq > roll_pad[newest].seq)) newest = pad;
        if (newest < 0) cat(buf, "ROLL: hold a pad");
        else {
            o = cat(buf, "ROLL "); o = cat(o, roll_pad[newest].track < 0 ? "all" : track_name(roll_pad[newest].track));
            o = cat(o, " "); cat(o, CTL_ROLL_NAME[roll_pad[newest].code]);
        }
        draw_text(0, 28, buf, 1);
        draw_text(0, 37, "left 8: all tracks", 1);
        draw_text(0, 46, "right 8: this row", 1);
        draw_text(0, 55, "bar..1/16 | 1/32-64", 1);
    } else if (mode == MODE_FADER && held_row >= 0 && held_row_t < G.fad_n) {   /* setting up a fader */
        int f = held_row_t;
        o = cat(buf, "Fader "); cat_int(o, f + 1);
        draw_text(0, 28, buf, 1);
        o = cat(buf, "1A CC "); cat_int(o, G.fad_cc[f]);
        draw_text(0, 37, buf, 1);
        o = cat(buf, "1B ch "); cat_int(o, G.fad_ch[f]);
        draw_text(0, 46, buf, 1);
        o = cat(buf, "1C "); cat(o, FAD_COL_NAME[G.fad_col[f] & 15]);
        draw_text(0, 55, buf, 1);
        draw_text(50, 28, "SH+row=del", 1);
    } else if (fader_mode()) {                      /* the 4 visible faders, as bars (the DAW page: the host's names) */
        fset_t s = fset();
        scr_fader_t fl[4];
        const char *nm[4];
        for (int r = 0; r < VIEW_ROWS; r++) {
            int f = *s.top + r;
            fl[r].used = f < s.n ? 1 : !s.daw && f == s.n ? 0 : -1;
            fl[r].num = f + 1; fl[r].cc = f < s.n && !s.daw ? G.fad_cc[f] : 0; fl[r].val = f < s.n ? s.val[f] : 0;
            nm[r] = f < s.n && s.daw ? daw.name[f] : 0;
        }
        scr_fader_rows(fl, s.daw ? nm : 0);
    } else if (mode == MODE_UNI && uni_ed) {        /* v0.31: editing a pad / a knob on the Fire */
        char L[4][20];
        if (uni_ed == 1) {
            const uint8_t *e = uni_entry(G.uni_page, uni_pad_key(uni_ed_idx, uni_ed_layer));
            uni_pad_lines(e ? uni_pad_of(e) : uni_pad_blank(), uni_ed_kpage, uni_ed_layer, L);
        } else {
            const uint8_t *e = uni_entry(G.uni_page, uni_knob_key(uni_ed_idx));
            uni_knob_lines(e ? uni_knob_of(e) : uni_knob_blank(), L);
        }
        const char *const lp[4] = {L[0], L[1], L[2], L[3]};
        scr_lines(lp);
    } else if (mode == MODE_UNI) {                  /* v0.31: the knob bank's 4 knobs */
        char lab[4][20];
        const char *lp[4];
        int val[4], used[4], any = 0;
        for (int k = 0; k < 4; k++) {
            int slot = (G.uni_bank & 3) * 4 + k;
            uni_knob_t kn;
            used[k] = uni_knob_get(G.uni_page, slot, &kn);
            if (!used[k]) kn = uni_knob_blank();
            uni_knob_row(kn, slot, lab[k]);
            val[k] = uni_kv[G.uni_page][slot];
            lp[k] = lab[k];
            any |= used[k];
        }
        uint32_t ln;
        if (!any && !uni_base(G.uni_page, &ln) && !uni_has_slots(G.uni_page, UNI_ALL)) {
            static const char *const EMPTY[4] = {"(empty page)", "set up on the ORC", "or ALT + tap a pad", "GRID < > = page"};
            scr_lines(EMPTY);
        } else {
            int mark = -1;
            if (uni_last_knob >= 0 && uni_last_knob >> 2 == (G.uni_bank & 3)) mark = uni_last_knob & 3;
            scr_uni_knobs(lp, val, used, mark);
        }
    } else if (mode == MODE_NOTE) {                        /* keyboard */
        draw_text(0, 28, "NOTE", 2);
        o = cat(buf, track_name(kb_track())); o = cat(o, " ch"); cat_int(o, tchan(kb_track()));
        draw_text(56, 28, buf, 1);
        if (rec_arm) draw_text(104, 28, "REC", 1);
        o = cat(buf, note_name((uint8_t)kb_base())); o = cat(o, "-"); cat(o, note_name((uint8_t)(kb_base() + KB_SPAN)));
        draw_text(56, 37, buf, 1);
        draw_text(0, 46, "GRID oct  SH=accent", 1);
        draw_text(0, 55, "SEL trk  ALT+SEL ch", 1);
    } else if (mode == MODE_DRUM && mpcrem) {       /* the MPC remote */
        draw_text(0, 28, "MPC", 2);
        draw_text(56, 28, "remote ch 14", 1);
        draw_text(56, 37, "the MPC face", 1);
        draw_text(0, 46, "pads = MPC buttons", 1);
        draw_text(0, 55, "DRUM: back to groups", 1);
    } else if (mode == MODE_DRUM && fire_held(IN_ALT)) {   /* editing a grid */
        int g = grid_sel;
        o = cat(buf, "Group "); cat_int(o, g + 1);
        draw_text(0, 28, buf, 1);
        draw_text(48, 28, "tap a group", 1);
        o = cat(buf, "1A ch "); cat_int(o, grid_chan(g));
        draw_text(0, 37, buf, 1);
        { o = cat(buf, "1B pad 1 "); o = cat(o, note_name((uint8_t)grid_base(g))); o = cat(o, " ("); o = cat_int(o, grid_base(g)); cat(o, ")"); }
        draw_text(0, 46, buf, 1);
    } else if (mode == MODE_DRUM) {                 /* the four grids */
        for (int g = 0; g < 4; g++) {
            o = cat(buf, "G"); o = cat_int(o, g + 1); o = cat(o, " ch"); cat_int(o, grid_chan(g));
            draw_text(0, 28 + g * 9, buf, 1);
            if (grid_lvl(g)) { o = cat(buf, note_name(lv16_note)); cat(o, " 16 lv"); }
            else { o = cat(buf, note_name((uint8_t)grid_base(g))); o = cat(o, "-"); cat(o, note_name((uint8_t)(grid_base(g) + 15))); }
            draw_text(42, 28 + g * 9, buf, 1);
        }
        draw_text(98, 28, rec_arm ? "REC" : "", 1);
        draw_text(98, 37, "ALT", 1);
        draw_text(98, 46, "=edit", 1);
    } else if (song_edit) {                         /* song editor help */
        o = cat(buf, "Song edit  slot "); cat_int(o, song_sel + 1);
        draw_text(0, 28, buf, 1);
        if (G.song_pat[song_sel]) { o = cat(buf, "pattern "); o = cat_int(o, G.song_pat[song_sel]); o = cat(o, "  bars "); cat_int(o, slot_reps(song_sel)); }
        else cat(buf, "(empty)");
        draw_text(0, 37, buf, 1);
        draw_text(0, 46, "1:slot 2-3:pat 4:bars", 1);
        draw_text(0, 55, "SH+slot=clr  1A=col", 1);
    } else if (step_held) {                         /* step settings (the header names the step and the page) */
        step_t sv; steps_read(CURPAT, held_t, held_s, &sv);
        uint8_t b = sv.b;
        if (knob_page == 0) {
            int hm = hold_of(sv.cond);
            scr_step_t ss = {step_ratchet(b), PROB_PCT[step_prob(b)], is_mel(held_t), "", hm == H_TIE ? 1 : hm == H_SLIDE ? 2 : 0, step_gate(b)};
            char nb[16];
            if (is_mel(held_t)) {
                uint8_t n = step_note(CURPAT, held_t, held_s);
                o = cat(nb, note_name(n)); o = cat(o, " ("); o = cat_int(o, n); cat(o, ")");
                ss.note = nb;
            }
            scr_step_bars(&ss);
        } else {
            o = cat(buf, "2A cond "); cat(o, COND_NAME[cond_of(sv.cond)]);
            draw_text(0, SCR_BODY_Y, buf, 1);
            uint8_t lcc = *lock_cc_p(held_t);
            if (is_cc(held_t)) {                    /* a CC track: the step's own value (else the level's velocity) */
                o = cat(buf, "2B value "); o = cat_int(o, sv.lock ? sv.lock - 1 : LEVEL_VEL[sv.b & 3]); if (!sv.lock) cat(o, " (lvl)");
            }
            else if (!lcc) cat(buf, "2B lock: set CC 1st");
            else if (!sv.lock) { o = cat(buf, "2B CC"); o = cat_int(o, lcc); cat(o, " --"); }
            else { o = cat(buf, "2B CC"); o = cat_int(o, lcc); o = cat(o, " = "); cat_int(o, sv.lock - 1); }
            draw_text(0, SCR_BODY_Y + SCR_ROW_H, buf, 1);
            if (is_mel(held_t)) {
                int ci = chord_index(sv.ch2, sv.ch3);
                o = cat(buf, "2C chord ");
                if (ci >= 0) cat(o, CHORDS[ci].name);
                else { if (sv.ch2 >= 0) o = cat(o, "+"); o = cat_int(o, sv.ch2); o = cat(o, " "); if (sv.ch3 >= 0) o = cat(o, "+"); cat_int(o, sv.ch3); }
                draw_text(0, SCR_BODY_Y + 2 * SCR_ROW_H, buf, 1);
            }
        }
    } else if (held_row >= 0) {
        int t = held_row_t;
        o = cat(buf, "TRACK "); o = cat(o, track_name(t));
        if (!is_mel(t)) cat(o, is_cc(t) ? "  1D CC" : "  1D note");   /* v0.29: knob 4 = a note or a CC track */
        draw_text(0, 28, buf, 1);
        uint8_t n = is_mel(t) ? m_cur[t - DTRACKS] : tnote(t);
        o = cat(buf, "1A ch "); o = cat_int(o, tchan(t)); o = cat(o, "  len "); cat_int(o, tlen(CURPAT, t));
        draw_text(0, 37, buf, 1);
        if (is_cc(t)) { o = cat(buf, "1B sends CC "); cat_int(o, n); }
        else { o = cat(buf, is_mel(t) ? "1B new " : "1B note "); o = cat(o, note_name(n)); o = cat(o, " ("); o = cat_int(o, n); cat(o, ")"); }
        draw_text(0, 46, buf, 1);
        uint8_t lcc = *lock_cc_p(t);
        if (is_cc(t)) cat(buf, "2B on a step: value");
        else if (lcc) { o = cat(buf, "1C lock CC "); cat_int(o, lcc); } else cat(buf, "1C lock CC: none");
        draw_text(0, 55, buf, 1);
    } else if (trkvol) {                            /* v0.23: one volume bar per track */
        char nm[4][6];
        const char *names[4];
        uint8_t val[4], mu[4];
        for (int r = 0; r < VIEW_ROWS; r++) {
            int t = vtrack(view_top + r);
            strcpy(nm[r], track_name(t)); names[r] = nm[r];
            val[r] = (uint8_t)trk_level(t); mu[r] = muted(t);
        }
        scr_levels(names, val, mu);
    } else {                                        /* the step view: dots per step, a playhead per track */
        char nm[4][6];
        const char *names[4];
        uint8_t lv[4][16], mu[4];
        int8_t hd[4];
        bool run = playing || ext_playing;
        uint32_t last = gstep ? gstep - 1 : 0;
        for (int r = 0; r < VIEW_ROWS; r++) {
            int t = vtrack(view_top + r), len = tlen(CURPAT, t);
            strcpy(nm[r], track_name(t)); names[r] = nm[r];
            mu[r] = muted(t);
            for (int c = 0; c < PAGE; c++) { int st = page * PAGE + c; lv[r][c] = st < len ? (uint8_t)level(CURPAT, t, st) : 255; }
            int hs = track_pos(t, roll_g(t, last)) - page * PAGE;
            hd[r] = (has_played && run && hs >= 0 && hs < PAGE) ? (int8_t)hs : -1;
        }
        scr_tracks(names, lv, mu, hd);
    }
}

/* ---------- faders ---------- */

static int fad_max_top(void) { fset_t s = fset(); return ctl_fset_max_top(s.n, !s.daw, MAXFADERS, VIEW_ROWS); }
/* v0.31f: 1E <dir> <count> = the user turned SELECT on the DAW page: the host moves the list (one track per detent) */
static void daw_page_bank(int dir, int count)
{
    uint8_t m[CTL_DAW_BANK_LEN];
    midi_sendn(m, ctl_daw_bank_msg(m, dir, count));
}
/* scroll the set on screen. v0.31f: the DAW page never scrolls itself: every detent asks the host to move its list
   (the owner, 2026-11-09: tracks then MASTER as one list, one track at a time, instead of banks of 8 + MASTER) */
static void scroll_faders(int d)
{
    fset_t s = fset();
    if (s.daw && !dawm) { if (d) daw_page_bank(d > 0 ? 1 : -1, d < 0 ? -d : d); return; }
    int edge = ctl_fset_scroll(s.top, d, fad_max_top());
    if (s.daw && edge) daw_channel(edge);           /* v0.36 DAW mode: past the last / first row the DAW moves one track */
    dirty = true;
}
static void fader_send(int f) { midi_send3((uint8_t)(0xB0 | (G.fad_ch[f] - 1)), G.fad_cc[f], G.fad_val[f]); }
/* a new level from the Fire (tap / knob) for row f of the set on screen: FADERS sends its CC (the level rides along with
   the next save), the DAW page sends 1D to the host (RAM only). Nothing is sent when the level did not change. */
/* v0.31f: a DAW row's 14-bit level from the Fire (tap / knob): stored, sent as 1D, never saved */
static void daw_set_fine(int f, int fine)
{
    if (!ctl_daw_level(&daw, f, fine)) return;
    if (dawm) { daw_fader(f, daw.fine[f], ms_ticks); dirty = true; return; }   /* v0.36 DAW mode: to the DAW (touch + level) */
    uint8_t m[CTL_DAW_LEVEL_LEN];
    midi_sendn(m, ctl_daw_level_msg(m, f, daw.fine[f]));
    dirty = true;
}
/* v0.31f: knob k turned v detents on DAW row `row`: full resolution, faster turns take bigger steps */
static void daw_knob(int k, int row, int v)
{
    static uint32_t last[4];
    uint32_t now = ms_ticks;
    int step = ctl_daw_knob_step(now - last[k & 3]);
    last[k & 3] = now;
    daw_set_fine(row, daw.fine[row] + v * step);
}
static void fader_set(int f, int v)
{
    fset_t s = fset();
    if (s.daw) { daw_set_fine(f, ctl_daw_tap_fine(v)); return; }   /* a tap: the column's level, exactly as before */
    if (!ctl_fset_set(s.val, f, v)) return;
    fader_send(f); touched_global();
    dirty = true;
}
/* v0.31d: a host change to DAW row `row` repaints only when the DAW page is on screen and that row is visible */
static void daw_dirty(int row) { if (mode == MODE_DAW && row >= daw_top && row < daw_top + VIEW_ROWS) dirty = true; }
/* v0.31c: a CC FROM THE HOST sets every fader on its channel + CC (e.g. REAPER's faders through the Pi). The host is the
   one telling us, so nothing is sent back, and it is not a save of its own: the level rides along with the next save.
   Only a repaint, and only when a changed fader is on screen (paint is coalesced: one per main-loop pass at most). */
static void fader_follow(uint8_t st, uint8_t num, uint8_t val)
{
    uint32_t m = ctl_fader_follow(G.fad_val, G.fad_ch, G.fad_cc, G.fad_n, st, num, val);
    if (m && mode == MODE_FADER && (m >> fad_top & ((1u << VIEW_ROWS) - 1u))) dirty = true;
}
static void fader_add(void)                         /* a new fader at the end: next channel, CC 7, next colour */
{
    if (G.fad_n >= MAXFADERS) return;
    int f = G.fad_n, prev = f ? f - 1 : -1;
    G.fad_cc[f] = 7;
    G.fad_ch[f] = (uint8_t)(prev >= 0 ? G.fad_ch[prev] % 16 + 1 : 1);
    G.fad_col[f] = (uint8_t)(prev >= 0 ? (G.fad_col[prev] + 1) % 16 : 0);
    G.fad_val[f] = 100;
    G.fad_n++;
    edited_global();
}
static void fader_remove(int f)                     /* the faders below move up */
{
    if (f < 0 || f >= G.fad_n) return;
    for (int i = f; i < G.fad_n - 1; i++) { G.fad_cc[i] = G.fad_cc[i + 1]; G.fad_ch[i] = G.fad_ch[i + 1]; G.fad_col[i] = G.fad_col[i + 1]; G.fad_val[i] = G.fad_val[i + 1]; }
    G.fad_n--;
    scroll_faders(0);
    edited_global();
}

/* ---------- inputs ---------- */

static void scroll_tracks(int d)
{
    if (fader_mode()) { scroll_faders(d); return; }
    view_top += d;
    if (view_top < 0) view_top = 0;
    if (view_top > TRACKS - VIEW_ROWS) view_top = TRACKS - VIEW_ROWS;
    dirty = true;
}

/* DRUM mode + REC while playing: write the hit into the current pattern at the nearest step.
   A hit in the second half of a step lands on the next one. A step that hasn't played yet then
   skips its note once (the live note already sounded).
   The hit goes to the track playing that note on that channel; if there is none, the first track
   with no steps in any pattern takes over that note and channel. */
static bool track_unused(int t) { return steps_track_empty(t); }   /* no steps in any pattern */

static void record_step(int t, uint8_t note, int lv, uint32_t now);

static void record_hit(uint8_t note, uint8_t chan1, int lv, uint32_t now)
{
    if (!(playing || ext_playing) || !gstep) return;
    int t = -1;
    for (int d = 0; d < DTRACKS + XTRACKS && t < 0; d++) {   /* the drum tracks in display order: T1-T16, T17-T24 (not CC tracks) */
        int i = vtrack(d);
        if (!is_cc(i) && tnote(i) == note && tchan(i) == chan1 && (!is_x(i) || !track_unused(i) || G.x_note[x_of(i)])) t = i;   /* (an empty T17-T24 still on its default note doesn't catch it) */
    }
    if (t < 0) {
        for (int d = 0; d < DTRACKS + XTRACKS && t < 0; d++) { int i = vtrack(d); if (!is_cc(i) && track_unused(i)) t = i; }
        if (t < 0) { cat(status_line, "no track"); return; }
        set_tnote(t, note); set_tchan(t, chan1);
        if (muted(t)) toggle_mute(t);
        edited_global();
        char *o = cat(status_line, "rec>"); cat(o, track_name(t));
    }
    record_step(t, note, lv, now);
}

/* Write a live note into track t at the nearest step (melodic tracks also keep its note). */
/* The pattern that will be playing on step g (g at most one step ahead: a bar start may switch). */
static int pattern_for_step(uint32_t g)
{
    if (g < gstep || g % PAGE || g == 0) return CURPAT;
    if (song_active()) {
        int slot = queued_pat >= 0 ? queued_pat : (song_bar + 1 >= slot_reps(song_slot) ? (song_slot + 1) % song_len() : song_slot);
        return G.song_pat[slot] - 1;
    }
    return queued_pat >= 0 ? queued_pat : CURPAT;
}

static void record_step(int t, uint8_t note, int lv, uint32_t now)
{
    if (!(playing || ext_playing) || !gstep) return;
    /* The nearest step by when it SOUNDS (swing moves odd steps later): look at the step that last
       started, the one before and the next one. */
    uint32_t r = gstep - 1, S = cur_step_us(), W = cur_swing_us(), tr;
    if (playing) tr = step_start_us;                /* grid time of step r */
    else { if (!ext_step_len) { S = 0; } tr = ext_step_at - ((r & 1) ? W : 0); }
    uint32_t g = S ? nearest_sounding_step(r, tr, S, W, now) : r;
    if (g >= gstep || (playing && pending >= 0 && (uint32_t)pending == g))
        rec_skip[t] = (int32_t)g + 1;               /* that step hasn't played yet: don't play it twice */
    int P = pattern_for_step(g);                    /* a hit just before a bar that switches pattern goes into the NEW one */
    undo_begin(P, true);                            /* the whole REC take is one edit (the gesture stays open while REC records) */
    int s = (int)(g % (uint32_t)tlen(P, t));
    uint8_t old = steps_get(P, t, s);
    bool ok = true;
    if (is_mel(t)) {
        step_t v; steps_read(P, t, s, &v);
        int iv = (int)note - (int)v.note;
        if (rec_chord_t == t && rec_chord_g == g && (v.b & 3) && iv && iv >= -48 && iv <= 48) {
            /* another note on the same step while recording: add it to the chord (up to 3 notes) */
            if (iv != v.ch2 && iv != v.ch3) {
                if (!v.ch2) v.ch2 = (int8_t)iv; else if (!v.ch3) v.ch3 = (int8_t)iv;
                ok = steps_write(P, t, s, &v);
            }
        } else {                                    /* the first note: replaces the step's note and chord */
            v.b = (uint8_t)((v.b & ~3) | lv); v.note = note; v.ch2 = v.ch3 = 0;
            ok = steps_write(P, t, s, &v);
        }
        rec_chord_t = t; rec_chord_g = g;
    }
    else if ((old & 3) < lv) ok = steps_set_byte(P, t, s, (uint8_t)((old & ~3) | lv));   /* drums: never lowers an accent */
    if (!ok) { pool_full(); return; }
    touched_pattern(P);                             /* a live hit: saved with the next save / at STOP */
}

/* 16 levels on / off: on = the last pad hit's group plays that pad's note on all 16 pads (none hit yet: the first notes
   group's pad 1). Pads held as it switches still end their own notes (live[] keeps what each sent). */
static void lv16_toggle(void)
{
    if (lv16_on) { lv16_on = false; cat(status_line, "16 lv off"); dirty = true; return; }
    int g = last_g, n = last_note;
    if (g < 0) { g = 0; n = grid_base(0); }         /* nothing hit yet: group 1's pad 1 */
    lv16_on = true; lv16_g = g; lv16_note = (uint8_t)n;
    char *o = cat(status_line, "16 lv "); cat(o, note_name(lv16_note));
    dirty = true;
}

/* SHIFT + REC: back to the undo point (the state there now becomes the redo). */
static void undo_do(void)
{
    undo_open = false;                              /* a running gesture (a REC take) ends here: its next change is a new edit */
    if (undo_pat < 0 || !undo_len[undo_cur]) { cat(status_line, undo_big ? "undo: too big" : "no undo"); dirty = true; return; }
    int p = undo_pat, spare = undo_cur ^ 1;
    bool keep = undo_take(p, spare);                /* what is there now: the redo */
    cancel_save_of(KIND_PAT(p));                    /* a save of p reads the live steps: it must not see them change */
    if (!steps_record_load(p, undo_buf[undo_cur], undo_len[undo_cur])) { pool_full(); return; }   /* (refused = nothing changed) */
    held_t = held_s = -1; held_pad = -1;
    edited_pattern(p);                              /* saved like any edit (undo_open is off: not an undo point of its own) */
    char *o = cat(status_line, undo_redo ? "redo P" : "undo P"); cat_int(o, p + 1);
    undo_redo = !undo_redo;
    if (keep) undo_cur = spare; else undo_pat = -1;
    dirty = true;
}

static void set_mode(int m)
{
    if (m == mode) return;
    mode = m;
    held_t = held_s = -1; held_pad = -1;            /* a held step pad no longer means anything */
    row_consumed = 0;                               /* nor does a pending chord of row buttons */
    if (m != MODE_STEP) { song_edit = false; trkvol = false; }
    if (m != MODE_DRUM) mpcrem = false;
    if (m != MODE_UNI) { if (uni_ed || uni_copy_src >= 0 || uni_unsaved()) uni_edit_end(); uni_held = 0; uni_latch_off(-1); }   /* v0.31: latched notes end too */
    dirty = true;
}

/* PATTERN up / down (copy = ALT: duplicate the current pattern into the next / previous one). */
static void pattern_step(int dir, bool copy)
{
    if (song_active() && !copy) {                   /* song mode: PATTERN = next / previous slot */
        int base = queued_pat >= 0 ? queued_pat : song_slot;
        int to = (base + (dir > 0 ? 1 : song_len() - 1)) % song_len();
        if (playing || ext_playing) queued_pat = to; else song_goto(to);
        dirty = true;
        return;
    }
    int base = (queued_pat >= 0 && !song_active()) ? queued_pat : CURPAT;   /* (song mode queues slots) */
    int to = (base + (dir > 0 ? 1 : PATTERNS - 1)) % PATTERNS;
    if (copy) {
        undo_begin(to, false);                      /* undo = the pattern copied into, as it was */
        cancel_save_of(KIND_PAT(to));
        if (!steps_copy_pattern(CURPAT, to)) { pool_full(); return; }
        G.pat_col[to] = (uint8_t)(pat_col_idx(CURPAT) + 1);   /* the copy takes the colour too */
        edited_pattern(to); edited_global();
        if (song_active()) {                        /* song mode: copy only; the song decides what plays */
            char *o = cat(status_line, "copy>P"); cat_int(o, to + 1);
            dirty = true; return;
        }
    }
    if (playing || ext_playing) queued_pat = to;
    else { CURPAT = (uint8_t)to; queued_pat = -1; }
    dirty = true; touched_global();
}

/* A held step pad belongs to the step it was pressed on: if the view, page or pattern moves under
   it (scrolling, GRID, a bar-boundary pattern switch, the song), the hold is dropped, so its release
   or a knob can't edit some other step. */
static void check_hold(void)
{
    if (held_t >= 0 && (view_top != hold_view || page != hold_page || CURPAT != hold_pat || song_edit != hold_songed)) {
        held_t = held_s = -1; held_pad = -1; dirty = true;
    }
}

/* a live pad let go: end exactly the note it started, whatever the mode is now */
static void live_release(int pad)
{
    if (!live[pad].on) return;
    live[pad].on = false;
    dirty = true;
    if (live[pad].cc) {                             /* an MPC panel pad (off 0) / a UNIVERSAL CC hold pad: its off value */
        live[pad].cc = false;
        midi_send3((uint8_t)(0xB0 | live[pad].chan), live[pad].note, live[pad].off);
        return;
    }
    if (live[pad].keys && notes_held[live[pad].note]) notes_held[live[pad].note]--;
    bool other = false;                             /* another pad still holding the same NOTE + channel? (a held CC or surface pad with that number is not one: v0.28) */
    for (int q = 0; q < 64; q++) if (live[q].on && !live[q].cc && live[q].note == live[pad].note && live[q].chan == live[pad].chan) other = true;
    if (!other) midi_send3((uint8_t)(0x80 | live[pad].chan), live[pad].note, 0);
}

/* v0.28 pad release hold-off (the owner: drum pads "retrigger too easily"). The pad contacts are only debounced for
   0.5 ms (two 2 kHz scans), so a hit that flickers as it lands or rolls off sent note-off + a fresh note-on = a
   second hit. Now a live pad's release waits until the pad has stayed up for PAD_HOLD_MS; back down sooner = the
   same hit, nothing sent. The press itself is still instant. Done here, not in the chain interrupt (TIM4). */
static const uint8_t PAD_HOLD_MS[6] = {0, 10, 20, 30, 40, 60};
#define PAD_HOLD_DEFAULT 2                          /* 20 ms */
static uint64_t up_wait;                            /* live pads let go whose release has not counted yet */
static uint32_t up_ms[64];                          /* when each of them went up */
static inline unsigned pad_hold_ms(void) { return PAD_HOLD_MS[G.pad_hold % 6]; }

/* the roller (see roll_pad): which step is about to sound, and what the held pads make each track do */
static uint32_t roll_anchor(void) { return (playing && pending >= 0) ? (uint32_t)pending : gstep; }
static bool roll_ready(void) { return roll_key && fire_held(IN_KNOBMODE) && mode == MODE_STEP && !song_edit && !picker_on(); }
static bool roll_shown(void)                        /* the pads show the roller (after 150 ms of KNOB MODE, or while a roller pad is held) */
{
    if (mode != MODE_STEP || song_edit || picker_on()) return false;
    return roll_pads || (roll_ready() && (roll_key_used || ms_ticks - roll_key_ms >= 150));
}
static void roll_update(void)
{
    ctl_roll_pad_t held[64];
    int n = 0;
    for (int pad = 0; pad < 64; pad++) if (roll_pads >> pad & 1) held[n++] = roll_pad[pad];
    for (int t = 0; t < TRACKS; t++) {
        int c = ctl_roll_code(held, n, t);
        if (c >= 0 && !roll_c1[t]) roll_at[t] = roll_anchor();   /* a roll starts at the next step; a new length keeps that start */
        roll_c1[t] = (int8_t)(c + 1);
    }
    dirty = true;
}
static void roll_press(int pad)
{
    int r = pad / 16, c = pad % 16;
    roll_pad[pad].code = (int8_t)(c % 8);
    roll_pad[pad].track = (int8_t)(c < 8 ? -1 : vtrack(view_top + r));   /* left half: all tracks; right half: this row's */
    roll_pad[pad].seq = ++roll_seq;
    roll_pads |= (uint64_t)1 << pad;
    roll_key_used = true;
    roll_update();
}
static void roll_release(int pad) { roll_pads &= ~((uint64_t)1 << pad); roll_update(); }

static void pads_up_due(void)
{
    if (!up_wait) return;
    for (int pad = 0; pad < 64; pad++)
        if ((up_wait >> pad & 1) && ms_ticks - up_ms[pad] >= pad_hold_ms()) {
            up_wait &= ~((uint64_t)1 << pad);
            if (roll_pads >> pad & 1) roll_release(pad); else live_release(pad);
        }
}

/* ---------- ORC mode = HOST mode (v0.33, hostm.h) ----------
   The ORC owns the pads, buttons, knobs, SELECT and the screen; the sequencer above keeps playing (play_step never
   touches a light or the screen). While it is on: hostm_events replaces handle_events (every press goes to the ORC as
   SysEx 22 / 23, nothing reaches the sequencer's controls), paint() and render() are skipped (the ORC's colours and
   bitmaps land straight in led_level / fb through hm_byte), and the ORC must say "still here" (21 02) at least every
   HM_QUIET_MS or the Fire goes back to its sequencer by itself. SHIFT + BROWSER always leaves. */
static bool hostm;                                  /* ORC mode is on */
static bool hostm_flush;                            /* the ORC changed the screen: send fb when the OLED is free */
static uint32_t hostm_seen;                         /* ms_ticks of the host's last 21 */
static uint8_t hostm_eat[(IN_COUNT + 7) / 8];       /* inputs that were down when ORC mode began: their release is not sent */

static void hostm_send(uint8_t cmd, uint8_t a, uint8_t b)
{
    if (!tud_midi_mounted()) return;
    uint8_t m[9] = {0xF0, 0x7D, 0x4F, 0x52, 0x43, cmd, (uint8_t)(a & 0x7F), (uint8_t)(b & 0x7F), 0xF7};
    tud_midi_stream_write(0, m, sizeof m);
}
static void hostm_say(void)                         /* 21 <0|1>: the state now (sent on every change and as the answer to a 21) */
{
    if (!tud_midi_mounted()) return;
    uint8_t m[8] = {0xF0, 0x7D, 0x4F, 0x52, 0x43, 0x21, hostm, 0xF7};
    tud_midi_stream_write(0, m, sizeof m);
}
static void hostm_enter(void)
{
    if (hostm) return;
    if (paintm || dawm) { hostm_say(); return; }    /* v0.37 / v0.38b: never over PAINT or DAW mode (the ORC hears "off"; under
                                                       DAW mode a touched fader would stay touched in the DAW all along) */
    if (!tud_midi_mounted()) { menu_toast_set("no ORC connected"); return; }
    menu_on = menu_edit = false; menu_grp = 0; menu_sel = 0;
    /* let go of everything the sequencer's own controls were holding: their releases will go to the ORC, not here */
    for (int pad = 0; pad < 64; pad++) {
        if (roll_pads >> pad & 1) roll_release(pad);
        live_release(pad);
    }
    up_wait = 0;
    if (uni_ed || uni_copy_src >= 0 || uni_unsaved()) uni_edit_end();
    uni_held = 0; uni_latch_off(-1);
    song_edit = false; trkvol = false;
    held_t = held_s = -1; held_pad = -1; held_row = -1; held_row_t = -1; row_consumed = 0;
    led_dual = 1;                                   /* v0.34b: TIM4 runs the short LED slot from its next pass on */
    hostm = true; hostm_seen = ms_ticks;
    memset(hostm_eat, 0, sizeof hostm_eat);         /* what is down right now was pressed for the sequencer (SELECT on the menu
                                                       row, a held SHIFT): the ORC never saw the press, so it gets no release */
    for (int i = 0; i < IN_COUNT; i++) if (fire_held(i)) hostm_eat[i >> 3] |= (uint8_t)(1u << (i & 7));
    hm_reset();
    leds_clear();
    hm_buttons();
    draw_clear();                                   /* shown until the ORC's first screen arrives */
    draw_text(0, 0, "ORC mode", 2);
    draw_text(0, 24, "waiting for the ORC", 1);
    draw_text(0, 46, "SHIFT + BROWSER", 1);
    draw_text(0, 55, "= back to the seq", 1);
    hostm_flush = true;
    hostm_say();
}
static void hostm_leave(const char *why)
{
    if (!hostm) return;
    hostm = false; hostm_flush = false;
    led_dual = 0;                                   /* v0.34b: one LED slot again (the sequencer lights both alike) */
    leds_clear();
    cat(status_line, why);
    dirty = true;                                   /* paint() + render() draw the sequencer again */
    hostm_say();
}
static void menu_host_now(void) { hostm_enter(); }

/* ---------- PAINT mode (v0.37, paint.h): "for kids and art's sake" (the owner, 2026-11-12) ----------
   Tap a pad = paint it the picked colour (again = wipe it). Hold SHIFT = the palette on the pads; tap one to pick it.
   MUTE row buttons = the four saved paintings: tap = load, hold PAINT_HOLD_MS = save. SHIFT + STOP = clear the canvas,
   SHIFT + BROWSER = back to the sequencer, which keeps playing underneath. Colours go through ORC mode's curve (hm_duty),
   so the second LED slot runs (led_dual) as in ORC mode. No computer needed. */
static void pm_slot_msg(const char *what, int s)
{
    char *o = cat(status_line, what);
    *o++ = (char)('1' + s); *o = 0;
    dirty = true;
}
static void pm_save(int s)
{
    if (saving_kind == KIND_PAINT(s) && store2_busy_kind() == KIND_PAINT(s)) { store2_cancel(); saving_kind = -1; }   /* its bytes change now */
    memcpy(paint_saved[s], paint_cv, PAINT_LEN);
    paint_has |= (uint8_t)(1u << s);
    paint_pend |= (uint8_t)(1u << s);
    if (!save_run) { saver_start(); save_paint_only = G.autosave_off && !save_forced_run; }   /* (written a few words a pass;
                                                       v0.38b: with AUTOSAVE off, never the sequencer's unsaved edits too) */
    pm_slot_msg("saved in ", s);
}
static void pm_load(int s)
{
    if (!(paint_has >> s & 1)) { pm_slot_msg("nothing in ", s); return; }
    memcpy(paint_cv, paint_saved[s], PAINT_LEN);
    pm_slot_msg("painting ", s);
}
static void paintm_enter(void)
{
    if (paintm || hostm || dawm) return;
    menu_on = menu_edit = false; menu_grp = 0; menu_sel = 0;
    for (int pad = 0; pad < 64; pad++) {            /* let go of everything the sequencer's own controls were holding */
        if (roll_pads >> pad & 1) roll_release(pad);
        live_release(pad);
    }
    up_wait = 0;
    if (uni_ed || uni_copy_src >= 0 || uni_unsaved()) uni_edit_end();
    uni_held = 0; uni_latch_off(-1);
    song_edit = false; trkvol = false;
    held_t = held_s = -1; held_pad = -1; held_row = -1; held_row_t = -1; row_consumed = 0;
    paint_btn_init(&paint_b);
    led_dual = 1;                                   /* the short LED slot, as ORC mode (hm_duty splits each colour into both) */
    paintm = true;
    leds_clear();
    cat(status_line, "tap pads to paint");
    dirty = true;
}
static void paintm_leave(void)
{
    if (!paintm) return;
    paintm = false;
    led_dual = 0;
    leds_clear();
    status_brief("paint off");                      /* v0.37d: gone again after 2 s */
    dirty = true;                                   /* paint() + render() draw the sequencer again */
}
static void menu_paint_now(void) { paintm_enter(); }

static void __attribute__((noinline)) paintm_events(void)
{
    rin_tick(&rin, ms_ticks);                       /* the host's remote input (20) works here too (the bench tests) */
    int e, s = 0;
    while ((e = fire_event()) >= 0) {
        if (hm_exit(e, fire_held(IN_SHIFT))) { paintm_leave(); return; }   /* SHIFT + BROWSER: out */
        int idx = e & 0x7F, pad = pad_of_input(idx);
        bool down = e & 0x80;
        if (pad >= 0) {
            if (!down) continue;
            if (fire_held(IN_SHIFT)) paint_cur = (uint8_t)paint_swatch(pad);   /* the palette: pick */
            else paint_tap(paint_cv, pad, paint_cur);                           /* paint, or wipe the same colour */
            dirty = true;
            continue;
        }
        int r = -1;
        for (int i = 0; i < 4; i++) if (idx == IN_ROWBTN[i]) r = i;
        if (r >= 0) {
            int a = paint_btn(&paint_b, r, down, ms_ticks, &s);
            if (a == PB_LOAD) pm_load(s); else if (a == PB_SAVE) pm_save(s);
            continue;
        }
        if (idx == IN_STOP && down && fire_held(IN_SHIFT)) { memset(paint_cv, 0, sizeof paint_cv); cat(status_line, "cleared"); }
        dirty = true;                               /* (SHIFT shows / hides the palette) */
    }
    if (paint_btn_tick(&paint_b, ms_ticks, &s) == PB_SAVE) pm_save(s);   /* held long enough: saved while still down */
    (void)fire_encoder_take();                      /* the knobs do nothing here */
    for (int k = 0; k < 4; k++) (void)fire_knob_take(k);
    if (t4_gap_us) { int w = (int)((t4_gap_us * 256u + 250u) / 500u); if (w != hm_weight()) { hm_set_weight(w); dirty = true; } }   /* as ORC mode */
}

static void __attribute__((noinline)) pm_paint(void)
{
    bool pal = fire_held(IN_SHIFT);
    for (int pad = 0; pad < PAINT_PADS; pad++) {
        uint8_t rgb[3];
        paint_pad_rgb(paint_cv, pad, paint_cur, pal, rgb);
        for (int j = 0; j < 3; j++) {
            uint8_t m, f;
            hm_duty(rgb[j], &m, &f);
            int i = led_pad(pad / 16, pad % 16, j);
            led_level[i] = m; led_fine[i] = f;
        }
    }
    for (int r = 0; r < 4; r++) { led_set(LED_ROW_G[r], paint_has >> r & 1 ? 7 : 1); led_set(LED_ROWSIDE_R[r], 0); led_set(LED_ROWSIDE_G[r], 0); }
    led_set(LED_SHIFT_Y, pal ? 7 : 1);
    led_set(LED_BROWSER, 1);
}
static void __attribute__((noinline)) pm_screen(void)
{
    char buf[24];
    draw_text(0, 0, "Paint", 2);
    draw_text(66, 4, "SH+BRW=out", 1);
    cat(cat(buf, "colour: "), PAINT_NAME[paint_cur & (PAINT_COLS - 1)]);
    draw_text(0, 18, buf, 1);
    draw_text(0, 28, status_line, 1);
    draw_text(0, 38, "SHIFT = colours", 1);
    draw_text(0, 47, "SHIFT+STOP = clear", 1);
    draw_text(0, 56, "row:load  hold:save", 1);
}

static void __attribute__((noinline)) hostm_events(void)
{
    rin_tick(&rin, ms_ticks);                       /* the host's remote input (20) works here too: its presses go the same way */
    int e;
    while ((e = fire_event()) >= 0) {
        if (hm_exit(e, fire_held(IN_SHIFT))) { hostm_leave("ORC off"); return; }   /* SHIFT + BROWSER: never sent */
        int ei = e & 0x7F;
        if (ei < IN_COUNT && (hostm_eat[ei >> 3] >> (ei & 7) & 1)) {              /* down since before ORC mode */
            hostm_eat[ei >> 3] &= (uint8_t)~(1u << (ei & 7));
            if (!(e & 0x80)) continue;              /* its release: not the ORC's (a new press of it is) */
        }
        uint8_t m[2];
        if (hm_input(e, m)) hostm_send(0x22, m[0], m[1]);
    }
    int d = fire_encoder_take();
    if (d) hostm_send(0x23, 4, hm_rel(d));
    for (int k = 0; k < 4; k++) {
        d = fire_knob_take(k);
        if (d) hostm_send(0x23, (uint8_t)k, hm_rel(d));
    }
    if (hm_quiet(ms_ticks, hostm_seen)) { hostm_leave("ORC gone"); return; }
    /* v0.34b: the short slot's share of a pass, from its MEASURED length (67 us on the owner's Fire = 34 / 256, the
       default). Measured only while the slot runs, so it is taken here, a pass or more after ORC mode began. */
    if (t4_gap_us) { int w = (int)((t4_gap_us * 256u + 250u) / 500u); if (w != hm_weight()) hm_set_weight(w); }
}

static void __attribute__((noinline)) handle_events(uint32_t now)
{
    rin_tick(&rin, ms_ticks);                       /* v0.31r: the host's remote input went quiet: its held inputs are released (events below) */
    check_hold();
    pads_up_due();
    /* v0.29c: a knob turned while a row button is held counts as "turned" BEFORE the events below, which can include that
       button's release. The knob loop runs after the events, so a turn right before letting go used to look like a plain tap
       and muted the track (the owner, 2026-10-25). */
    if (held_row >= 0) for (int k = 0; k < 4; k++) if (fire_knob_pending(k)) held_row_turned = true;
    int e;
    while ((e = fire_event()) >= 0) {
        int idx = e & 0x7F;
        bool down = e & 0x80;
        int pad = pad_of_input(idx);
        if (pad >= 0 && (up_wait >> pad & 1)) {     /* a pad inside its release hold-off */
            if (down) up_wait &= ~((uint64_t)1 << pad);   /* back down: the same hit goes on, nothing sent */
            else up_ms[pad] = ms_ticks;
            continue;
        }
        if (down && overlay_event(idx, pad)) continue;   /* the menu, or lights showing: BROWSER, and the wake-up touch */
        if (pad >= 0 && !down && live[pad].on) {    /* a live pad let go: its release counts once it stays up (above) */
            if (pad_hold_ms()) { up_wait |= (uint64_t)1 << pad; up_ms[pad] = ms_ticks; }
            else live_release(pad);
            continue;
        }
        if (pad >= 0 && !down && (roll_pads >> pad & 1)) {   /* a roller pad let go: the same hold-off as a live pad */
            if (pad_hold_ms()) { up_wait |= (uint64_t)1 << pad; up_ms[pad] = ms_ticks; }
            else roll_release(pad);
            continue;
        }
        if (pad >= 0 && down && (roll_ready() || (roll_pads && roll_shown()))) { roll_press(pad); continue; }   /* KNOB MODE held (or a roll held): the pads are the roller */
        if (pad >= 0 && picker_on()) {              /* the pattern picker: tap a pattern (top two rows) */
            if (!down) continue;
            if (pad >= 32) continue;
            int p = pad;                            /* row 0 = 1-16, row 1 = 17-32 */
            picker_sel = p; picker_used = true;
            if (!song_active()) {
                if (playing || ext_playing) queued_pat = p == CURPAT ? -1 : p;
                else { CURPAT = (uint8_t)p; queued_pat = -1; }
                touched_global();
            }
            dirty = true;
            continue;
        }
        if (pad >= 0 && mode == MODE_STEP && trkvol) {   /* tap a column: that track's volume (0 left .. full right) */
            if (down) trk_level_set(vtrack(view_top + pad / 16), ctl_trk_pad_level(pad % 16));
            continue;
        }
        if (pad >= 0 && mode == MODE_UNI) {         /* v0.31 UNIVERSAL: the page's pads (a live pad's release was handled above) */
            uint64_t bit = (uint64_t)1 << pad;
            if (!down) { uni_held &= ~bit; dirty = true; continue; }
            int pg = G.uni_page;
            if (fire_held(UNI_EDIT_BTN)) {          /* ALT held: edit */
                int src = uni_copy_src;
                if (src >= 0 && src != pad && fire_held(pad_of_input(src))) {   /* ALT + hold A, tap B: B = A (pad_of_input is its own inverse) */
                    const uint8_t *se = uni_entry(pg, uni_pad_key(src, uni_ed_layer));
                    uint8_t e[UNI_ENT] = {0};
                    if (se) { memcpy(e, se, UNI_ENT); e[0] = (uint8_t)pad; }
                    if (uni_set(pg, uni_pad_key(pad, uni_ed_layer), e, !se)) { char *o = cat(status_line, "copy > "); o = cat_int(o, pad / 16 + 1); o = cat(o, "-"); cat_int(o, pad % 16 + 1); }
                    dirty = true;
                    continue;
                }
                uni_ed = 1; uni_ed_idx = pad; uni_ed_layer = fire_held(UNI_LAYER_BTN) ? UL_SHIFT : UL_BASE; uni_ed_kpage = 0;
                uni_copy_src = pad;                 /* (and it plays below, so you hear what it does) */
            }
            int ly = fire_held(UNI_LAYER_BTN) ? UL_SHIFT : UL_BASE;
            uni_pad_t p;
            if (!uni_pad_get(pg, pad, &ly, &p)) { dirty = true; continue; }
            uint8_t m[3], tg = (uint8_t)(uni_tog[pg][ly] >> pad & 1);
            if (p.type == UT_LATCH) {               /* a note latch: on, or exactly the note it sent off (uni_latch_press) */
                int nl = uni_latch_press(uni_lat, uni_pad_key(pad, ly), p, m);
                if (!nl) cat(status_line, "16 latched");
                midi_sendn(m, nl);
                uni_held |= bit;
                dirty = true;
                continue;
            }
            int n, rel = uni_press(p, &tg, m, &n);
            uni_tog[pg][ly] = (uni_tog[pg][ly] & ~bit) | ((uint64_t)tg << pad);
            midi_sendn(m, n);
            if (rel != UREL_NONE) {                 /* a note / CC hold: its release ends it (live_release), whatever happens meanwhile */
                live[pad].note = p.num; live[pad].chan = p.ch; live[pad].keys = false;
                live[pad].cc = rel == UREL_CC; live[pad].off = p.off; live[pad].on = true;
            } else uni_held |= bit;
            dirty = true;
            continue;
        }
        if (pad >= 0 && fader_mode()) {             /* tap a column: that fader's level (0 left .. 127 right); FADERS or the DAW page */
            if (!down) continue;
            fset_t s = fset();
            int f = *s.top + pad / 16;
            if (f < s.n) fader_set(f, ((pad % 16) * 127 + 7) / 15);
            continue;
        }
        if (pad >= 0 && mode != MODE_STEP) {
            if (!down) continue;
            int r = pad / 16, c = pad % 16;
            if (mode == MODE_DRUM && mpcrem) {      /* the MPC remote: press that MPC button (never recorded) */
                int ty = MPCREM[pad].type;
                if (ty == MR_NONE) continue;
                uint8_t ch = MPC_PANEL_CH - 1, v = MPCREM[pad].val;
                midi_send3((uint8_t)((ty == MR_CC ? 0xB0 : 0x90) | ch), v, 127);
                live[pad].note = v; live[pad].chan = ch; live[pad].keys = false; live[pad].cc = ty == MR_CC; live[pad].off = 0; live[pad].on = true;
                dirty = true;
                continue;
            }
            if (mode == MODE_DRUM && fire_held(IN_ALT)) { grid_sel = c / 4; dirty = true; continue; }   /* ALT + tap: pick a group to edit */
            uint8_t vel = fire_held(IN_SHIFT) ? LIVE_ACCENT : LIVE_VEL, note, ch;
            if (mode == MODE_NOTE) {
                note = (uint8_t)kb_note(r, c); ch = (uint8_t)(tchan(kb_track()) - 1);
                notes_held[note]++;
                m_cur[kb_track() - DTRACKS] = note;     /* steps lit by hand on this track get this note */
                if (rec_arm) record_step(kb_track(), note, vel == LIVE_ACCENT ? LV_ACCENT : LV_NORMAL, now);
            } else {
                int g = c / 4;
                note = grid_note(g, grid_padnum(r, c)); ch = (uint8_t)(grid_chan(g) - 1);
                int lv = vel == LIVE_ACCENT ? LV_ACCENT : LV_NORMAL;
                if (grid_lvl(g)) {                  /* 16 levels: the pad IS the velocity (SHIFT does nothing); recorded at the nearest level */
                    vel = level16_vel(grid_padnum(r, c));
                    lv = vel < 80 ? LV_SOFT : vel < 114 ? LV_NORMAL : LV_ACCENT;
                } else { last_g = g; last_note = note; }   /* the pad the next 16 levels takes */
                if (rec_arm) record_hit(note, (uint8_t)(ch + 1), lv, now);
            }
            midi_send3((uint8_t)(0x90 | ch), note, vel);
            live[pad].note = note; live[pad].chan = ch; live[pad].keys = mode == MODE_NOTE; live[pad].cc = false; live[pad].on = true;
            dirty = true;
            continue;
        }
        if (pad >= 0 && song_edit) {                /* song editor */
            if (!down) continue;
            int r = pad / 16, c = pad % 16;
            if (r == 0) {
                if (fire_held(IN_SHIFT)) {          /* SHIFT + slot: clear it (later slots move up) */
                    for (int i = c; i < SLOTS - 1; i++) { G.song_pat[i] = G.song_pat[i + 1]; G.song_rep[i] = G.song_rep[i + 1]; }
                    G.song_pat[SLOTS - 1] = 0; G.song_rep[SLOTS - 1] = 0;
                    if (song_slot >= song_len()) song_slot = 0;
                    edited_global();
                }
                song_sel = c > song_len() ? song_len() : c;   /* no gaps: at most one past the end */
                if (song_sel >= SLOTS) song_sel = SLOTS - 1;
            } else if (r == 1 || r == 2) {          /* pattern 1-32 for the selected slot */
                G.song_pat[song_sel] = (uint8_t)((r - 1) * 16 + c + 1);
                edited_global();
            } else if (c < 8 && G.song_pat[song_sel]) {   /* bottom row: bars for the selected slot */
                G.song_rep[song_sel] = (uint8_t)(c + 1);
                edited_global();
            }
            dirty = true;
            continue;
        }
        if (pad >= 0) {
            int r = pad / 16, c = pad % 16, t = vtrack(view_top + r), s = page * PAGE + c;
            uint8_t st = steps_get(CURPAT, t, s);
            if (!down) {                            /* release: a lit step tapped (not edited) turns off */
                check_hold();
                if (pad == held_pad && held_t >= 0) {
                    if (held_pad_was_lit && !held_pad_turned) { steps_set(CURPAT, held_t, held_s, 0, 0); edited_pattern(CURPAT); }
                    held_t = held_s = -1; held_pad = -1;
                    dirty = true;
                }
                continue;
            }
            undo_begin(CURPAT, false);              /* a press starts an edit (it only becomes the undo point if it changes something) */
            if (fire_held(IN_ALT)) {                /* ALT + pad: track length */
                steps_len[CURPAT][t] = (uint8_t)(s + 1);
                char *o = cat(status_line, "len "); cat_int(o, s + 1);
            } else if (s >= tlen(CURPAT, t)) {
                continue;                           /* past the track's end */
            } else if (fire_held(IN_SHIFT)) {       /* SHIFT + pad: velocity tier, lit steps only */
                int lv = st & 3;
                if (lv == LV_OFF) continue;
                steps_set_byte(CURPAT, t, s, (uint8_t)((st & ~3) | (lv == LV_ACCENT ? LV_SOFT : lv == LV_SOFT ? LV_NORMAL : LV_ACCENT)));
            } else {                                /* plain: unlit -> on now; lit -> off on release (unless edited) */
                check_hold();                       /* a hold from another view / page is already dropped */
                if (held_t >= 0 && ctl_step_displace(held_pad, held_pad_was_lit, held_pad_turned)) {
                    steps_set(CURPAT, held_t, held_s, 0, 0);   /* another lit step is still down, untouched: its tap counts now (only the newest pad is remembered) */
                    edited_pattern(CURPAT);
                }
                held_t = t; held_s = s; held_pad = pad; held_pad_turned = false;
                hold_view = view_top; hold_page = page; hold_pat = CURPAT; hold_songed = song_edit;
                held_pad_was_lit = (st & 3) != LV_OFF;
                if (!held_pad_was_lit) {
                    if (!steps_set(CURPAT, t, s, LV_NORMAL, is_mel(t) ? m_cur[t - DTRACKS] : 0)) {
                        held_t = held_s = -1; held_pad = -1; pool_full(); continue;
                    }
                }
                else { dirty = true; continue; }
            }
            dirty = true; edited_pattern(CURPAT);
            continue;
        }
        int row = -1;
        for (int r = 0; r < VIEW_ROWS; r++) if (idx == IN_ROWBTN[r]) row = r;
        if (row >= 0 && mode == MODE_UNI) continue; /* v0.31: rows do nothing in UNIVERSAL (a mute you could not see) */
        if (row >= 0 && mode == MODE_DAW) {         /* v0.31d: the host owns the DAW rows: no add / remove / set-up here */
            if (!down && held_row == row) held_row = -1;   /* (a FADERS row held across PERFORM taps lets go cleanly) */
            continue;
        }
        if (row >= 0 && mode == MODE_FADER) {       /* hold = set it up (knobs), tap = add (empty row), SHIFT + tap = remove */
            if (down) { held_row = row; held_row_t = fad_top + row; held_row_turned = false; }
            else if (held_row == row) {
                int f = held_row_t;
                if (!held_row_turned) {
                    if (fire_held(IN_SHIFT)) fader_remove(f);
                    else if (f == G.fad_n) { fader_add(); scroll_faders(1); }   /* keep the next "+" in view */
                    else if (f < G.fad_n) fader_send(f);   /* tap: send its level again */
                }
                held_row = -1;
            }
            dirty = true;
            continue;
        }
        if (row == 0 && mode == MODE_DRUM && !mpcrem) {   /* v0.29b: DRUM mode's top row button = 16 levels on / off */
            if (down) lv16_toggle();
            continue;
        }
        if (row >= 0) {                             /* hold = track settings, tap = mute; rows can be pressed together */
            int turned = held_row_turned;
            if (down) {
                int m = ctl_row_press(&held_row, &turned, &row_consumed, row);
                held_row_turned = turned;
                if (m >= 0) { toggle_mute(row_trk[m]); touched_global(); }   /* a row another one landed on: its tap counts now */
                held_row_t = vtrack(view_top + row); row_trk[row] = held_row_t;
            } else if (ctl_row_release(&held_row, &turned, &row_consumed, row)) {
                toggle_mute(row_trk[row]); touched_global();
            }
            dirty = true;
            continue;
        }
        if (!down && idx == pat_btn) {              /* PATTERN let go: if the picker wasn't used, it was a plain up / down */
            pat_btn = -1;
            if (!picker_used) pattern_step(idx == IN_PAT_UP ? 1 : -1, false);
            dirty = true;
            continue;
        }
        if (!down && idx == IN_KNOBMODE && roll_key) {   /* KNOB MODE let go: a plain tap (no roller pad) = the next knob bank */
            roll_key = false;
            if (roll_key_used) {}
            else if (mode == MODE_UNI && uni_ed == 1) uni_ed_kpage ^= 1;   /* v0.31: editing a pad: its knob page 1 / 2 */
            else if (mode == MODE_UNI) { G.uni_bank = (uint8_t)((G.uni_bank + 1) & 3); touched_global(); char *ko = cat(status_line, "knobs "); cat_int(ko, G.uni_bank + 1); }
            else { uint8_t *kb = kc_bank(kctx()); *kb = (uint8_t)((*kb + 1) & 3); touched_global(); char *ko = cat(status_line, "knobs "); cat_int(ko, *kb + 1); }
            dirty = true;
            continue;
        }
        if (!down && idx == IN_PERFORM && ms_ticks - perform_down_ms < 300) {   /* a tap: FADERS, again the DAW page (v0.38: if the host defined one), again back */
            int to = uni_perform_next(mode, mode_before_faders, MODE_FADER, MODE_DAW, daw.n > 0);
            if (to == MODE_FADER) mode_before_faders = mode;
            set_mode(to);
        }
        if (!down && idx == UNI_EDIT_BTN && mode == MODE_UNI) uni_edit_end();   /* v0.31: ALT let go = the edit is done */
        if (!down) { if (idx == IN_SHIFT || idx == IN_ALT || idx == IN_PERFORM) dirty = true; continue; }
        switch (idx) {
        case IN_PLAY:
            if (fire_held(IN_SHIFT)) {
                clock_out_toggle();
                break;
            }
            if (ext_clock) break;
            if (playing) stop(false); else start(now);
            break;
        case IN_STOP: stop(true); break;
        case IN_PATSONG:
            if (!fire_held(IN_SHIFT)) { set_mode(MODE_STEP); song_edit = !song_edit; if (song_edit) trkvol = false; }   /* PATTERN/SONG: the song editor (2026-10-19: was SHIFT +) */
            else {                                  /* SHIFT + PATTERN/SONG: song mode on / off (was a plain tap) */
                G.song_on = !G.song_on;
                queued_pat = -1;                    /* a queued slot / pattern means something else now */
                if (G.song_on && song_len()) {
                    if (playing || ext_playing) queued_pat = 0;  /* join the song at the next bar */
                    else song_goto(0);
                }
                cat(status_line, G.song_on ? "song on" : "song off");
                touched_global();
            }
            dirty = true;
            break;
        case IN_STEP:                               /* v0.23: pressed again while on the steps = the track volume faders, and back */
            if (fire_held(IN_ALT)) { tap_tempo(); break; }   /* v0.24: ALT + STEP = tap tempo (a plain STEP is unchanged) */
            if (mode == MODE_STEP && !song_edit && !picker_on()) { trkvol = !trkvol; held_t = held_s = -1; held_pad = -1; dirty = true; }   /* a held step means nothing in the other view */
            else set_mode(MODE_STEP);
            break;
        case IN_NOTE:                               /* v0.38: pressed again while on the keyboard = UNIVERSAL, and back (was PERFORM x2) */
            set_mode(picker_on() ? MODE_NOTE : uni_note_next(mode, MODE_NOTE, MODE_UNI));
            break;
        case IN_DRUM:                               /* v0.28: pressed again while on the groups = the MPC remote, and back */
            if (mode == MODE_DRUM && !picker_on()) { mpcrem = !mpcrem; dirty = true; }
            else set_mode(MODE_DRUM);
            break;
        case IN_REC:
            if (fire_held(IN_SHIFT)) { undo_do(); break; }   /* v0.29: SHIFT + REC = undo / redo */
            rec_arm = !rec_arm; dirty = true; break;
        case IN_GRID_L:
        case IN_GRID_R:
            if (mode == MODE_NOTE) {                /* octave (SHIFT: semitone) */
                int b = kb_base() + (idx == IN_GRID_R ? 1 : -1) * (fire_held(IN_SHIFT) ? 1 : 12);
                G.kb_base = (uint8_t)(b < 1 ? 1 : b > 96 ? 96 : b);
                touched_global();
            } else if (mode == MODE_UNI) {           /* v0.31: page 1-8 (an edit under way ends first) */
                int np = G.uni_page + (idx == IN_GRID_R ? 1 : -1);
                if (np >= 0 && np < UNI_PAGES) { if (uni_ed || uni_copy_src >= 0) uni_edit_end(); uni_latch_off(-1); G.uni_page = (uint8_t)np; touched_global(); }
            } else if (mode == MODE_STEP) {
                if (idx == IN_GRID_L && page > 0) page--;
                if (idx == IN_GRID_R && page < PAGES - 1) page++;
            }
            dirty = true;
            break;
        case IN_PAT_UP:
        case IN_PAT_DN:
            if (fire_held(IN_SHIFT)) {              /* SHIFT + PATTERN: scroll tracks a page */
                scroll_tracks(idx == IN_PAT_UP ? -VIEW_ROWS : VIEW_ROWS);
                break;
            }
            if (fire_held(IN_ALT)) { pattern_step(idx == IN_PAT_UP ? 1 : -1, true); break; }   /* ALT: copy, at once */
            pat_btn = idx; picker_used = false; pat_down_ms = ms_ticks;   /* hold = the picker; a quick press steps on release */
            picker_sel = song_active() && G.song_pat[song_slot] ? G.song_pat[song_slot] - 1 : CURPAT;
            dirty = true;
            break;
        case IN_PERFORM: perform_down_ms = ms_ticks; dirty = true; break;
        case IN_SHIFT: case IN_ALT: dirty = true; break;
        case IN_KNOBMODE:                           /* a step held: its knob page 1 / 2; else held = the roller, a tap = the next knob bank (on release) */
            if (held_t >= 0 && held_row < 0) { knob_page ^= 1; held_pad_turned = true; }   /* flipping the page is an edit: releasing must not erase the step */
            else { roll_key = true; roll_key_used = false; roll_key_ms = ms_ticks; }
            dirty = true; break;
        default: break;
        }
    }
    int d = fire_encoder_take();
    if (d) {
        if (menu_on) menu_turn(d);                  /* the menu: SELECT scrolls / changes the value */
        else if (fire_held(IN_SHIFT)) {             /* SHIFT + SELECT: tempo (with ALT: x10) */
            int b = BPM + d * (fire_held(IN_ALT) ? 10 : 1);
            BPM = (uint16_t)(b < 40 ? 40 : b > 300 ? 300 : b);
            touched_global(); pop_kind = 1; pop_ms = ms_ticks;
        } else if (mode == MODE_NOTE && fire_held(IN_ALT)) {   /* NOTE mode: ALT + SELECT = that track's channel */
            int t = kb_track(), c = tchan(t) + d;
            G.m_chan[t - DTRACKS] = (uint8_t)(c < 1 ? 1 : c > 16 ? 16 : c);
            edited_global();
        } else if (fader_mode()) {
            scroll_faders(d);                       /* SELECT: scroll faders (the DAW page: past its ends = 1E, another bank) */
        } else if (mode == MODE_NOTE) {             /* NOTE mode: SELECT = which melodic track it plays */
            int k = G.kb_track % MTRACKS + d;
            G.kb_track = (uint8_t)(k < 0 ? 0 : k > MTRACKS - 1 ? MTRACKS - 1 : k);
            touched_global();
        } else {
            scroll_tracks(d);                       /* SELECT: scroll tracks */
        }
        dirty = true;
    }
    check_hold();
    for (int k = 0; k < 4; k++) {
        int v = fire_knob_take(k);
        if (!v) continue;
        if (menu_on) continue;                      /* the menu uses SELECT only */
        if (picker_on()) {                          /* picker: knob 1 = the picked pattern's colour */
            if (k == 0) { pat_recolour(picker_sel, v); picker_used = true; }
            continue;
        }
        if (song_edit && k == 0) {                  /* song editor: knob 1 = the selected slot's pattern colour */
            if (G.song_pat[song_sel]) pat_recolour(G.song_pat[song_sel] - 1, v);
            continue;
        }
        if (mode == MODE_STEP && trkvol && held_row < 0) {   /* knob k = row k's track volume, fine */
            trk_level_set(vtrack(view_top + k), trk_level(vtrack(view_top + k)) + v);
            continue;
        }
        if (fader_mode()) {
            fset_t s = fset();
            if (!s.daw && held_row >= 0) {          /* setting up the held fader: CC, channel, colour (FADERS only) */
                int f = held_row_t;
                if (f >= G.fad_n || k == 3) continue;
                held_row_turned = true;
                if (k == 0) { int c = G.fad_cc[f] + v; G.fad_cc[f] = (uint8_t)(c < 0 ? 0 : c > 127 ? 127 : c); }
                else if (k == 1) { int c = G.fad_ch[f] + v; G.fad_ch[f] = (uint8_t)(c < 1 ? 1 : c > 16 ? 16 : c); }
                else { int c = G.fad_col[f] + v; G.fad_col[f] = (uint8_t)(c < 0 ? 0 : c > 15 ? 15 : c); }
                dirty = true; edited_global();
            } else if (*s.top + k < s.n) {          /* knob k = row k's fader, fine */
                if (s.daw) daw_knob(k, *s.top + k, v);   /* v0.31f: the DAW page's full 14-bit resolution */
                else fader_set(*s.top + k, s.val[*s.top + k] + v);
            }
            continue;
        }
        if (mode == MODE_UNI) {                     /* v0.31 UNIVERSAL */
            int pg = G.uni_page;
            if (uni_ed == 1) {                      /* editing a pad: knob k = one of its settings (uni_edit_pad) */
                int key = uni_pad_key(uni_ed_idx, uni_ed_layer);
                const uint8_t *e = uni_entry(pg, key);
                uni_pad_t p = e ? uni_pad_of(e) : uni_pad_blank();
                uni_edit_pad(&p, uni_ed_kpage, k, v, uni_ed_layer);
                uint8_t ne[UNI_ENT];
                uni_pad_enc(ne, uni_ed_idx, uni_ed_layer, p);
                if (p.type != UT_OFF) uni_set(pg, key, ne, false);
                else if (e) uni_set(pg, key, ne, true);   /* type "off" = the pad is cleared */
                dirty = true;
                continue;
            }
            if (uni_ed == 2 || fire_held(UNI_EDIT_BTN)) {   /* ALT + turn a knob: edit it (that first turn only picks it) */
                if (uni_ed != 2) { uni_ed = 2; uni_ed_idx = (G.uni_bank & 3) * 4 + k; dirty = true; continue; }
                int key = uni_knob_key(uni_ed_idx);
                const uint8_t *e = uni_entry(pg, key);
                uni_knob_t kn = e ? uni_knob_of(e) : uni_knob_blank();
                uni_edit_knob(&kn, k, v);
                uint8_t ne[UNI_ENT];
                uni_knob_enc(ne, uni_ed_idx, kn);
                if (kn.type != UK_NONE) uni_set(pg, key, ne, false);
                else if (e) uni_set(pg, key, ne, true);
                dirty = true;
                continue;
            }
            int slot = (G.uni_bank & 3) * 4 + k;    /* playing: the bank's knob sends its CC / pitch bend */
            uni_knob_t kn;
            if (!uni_knob_get(pg, slot, &kn)) continue;
            uint8_t m[3];
            midi_sendn(m, uni_knob_turn(kn, &uni_kv[pg][slot], v, m));
            uni_last_knob = slot; dirty = true;
            continue;
        }
        if (mode == MODE_DRUM && !mpcrem && fire_held(IN_ALT) && k < 2) {   /* editing a group: channel, pad 1's note */
            int g = grid_sel;
            if (k == 0) { int c = grid_chan(g) + v; G.grid_chan[g] = (uint8_t)(c < 1 ? 1 : c > 16 ? 16 : c); }
            else { int b = grid_base(g) + v; G.grid_base[g] = (uint8_t)(b < 1 ? 1 : b > 112 ? 112 : b); }
            dirty = true; edited_global();
            continue;
        }
        if (held_t >= 0 && held_row < 0) {
            /* holding a step. Page 1: knob 1 ratchet, 2 chance, 3 note (melodic), 4 gate / TIE / SLIDE.
               Page 2: knob 1 condition, 2 lock value, 3 chord (melodic). */
            step_t sv;
            if (!steps_read(CURPAT, held_t, held_s, &sv)) continue;
            if (k == 2 && !is_mel(held_t)) continue;   /* drum steps have no note or chord of their own */
            if (knob_page == 1 && k == 3) continue;
            held_pad_turned = true;
            uint8_t st = sv.b;
            if (knob_page == 0) {
                if (k == 0) { int r = step_ratchet(st) + v; r = r < 1 ? 1 : r > 4 ? 4 : r; sv.b = (uint8_t)((st & ~0x30) | ((r - 1) << 4)); }
                else if (k == 1) { int pr = step_prob(st) - v; pr = pr < 0 ? 0 : pr > 3 ? 3 : pr; sv.b = (uint8_t)((st & ~0x0C) | (pr << 2)); }  /* clockwise = more likely */
                else if (k == 2) {
                    int n = step_note(CURPAT, held_t, held_s) + v; n = n < 1 ? 1 : n > 127 ? 127 : n;
                    sv.note = (uint8_t)n; m_cur[held_t - DTRACKS] = (uint8_t)n;
                } else {                            /* gate 25 / 50 / 75 / 95 %, then TIE, SLIDE */
                    int hm = hold_of(sv.cond), pos = hm == H_TIE ? 4 : hm == H_SLIDE ? 5 : GATE_RANK[st >> 6 & 3];
                    pos += v; pos = pos < 0 ? 0 : pos > 5 ? 5 : pos;
                    if (pos <= 3) { sv.b = (uint8_t)((st & ~0xC0) | (GATE_CODE[pos] << 6)); sv.cond &= 0x3F; }
                    else sv.cond = (uint8_t)((sv.cond & 0x3F) | ((pos == 4 ? H_TIE : H_SLIDE) << 6));
                }
            } else {
                if (k == 0) { int c = cond_of(sv.cond) + v; c = c < 0 ? 0 : c > C_COUNT - 1 ? C_COUNT - 1 : c; sv.cond = (uint8_t)((sv.cond & 0xC0) | c); }
                else if (k == 1) {                  /* lock value: off <-> 0..127 (first turn up lands on 64) */
                    if (!*lock_cc_p(held_t) && !is_cc(held_t)) continue;   /* the track has no lock CC yet (hold its row + knob 3); a CC track's value needs none */
                    int l = sv.lock;
                    if (!l && v > 0) l = 65 + (v - 1); else l += v;
                    sv.lock = (uint8_t)(l < 0 ? 0 : l > 128 ? 128 : l);
                } else {                            /* chord shape */
                    int ci = chord_index(sv.ch2, sv.ch3);
                    if (ci < 0) ci = 0;
                    ci += v; ci = ci < 0 ? 0 : ci > NCHORDS - 1 ? NCHORDS - 1 : ci;
                    sv.ch2 = CHORDS[ci].a; sv.ch3 = CHORDS[ci].b;
                }
            }
            steps_write(CURPAT, held_t, held_s, &sv);   /* a lit step changes in place: never needs room */
            dirty = true; edited_pattern(CURPAT);
            continue;
        }
        if (held_row >= 0) {
            int t = held_row_t;
            if (k == 3) {                           /* v0.29: knob 4 = a note track (back) or a CC track (clockwise); drum tracks only */
                if (is_mel(t) || (v > 0) == is_cc(t)) continue;
                held_row_turned = true;
                if (held_notes[t].n) { play_release(&held_notes[t], &OUT); batch_flush(); }   /* a tied note must not hang */
                G.cc_trk[t >> 3] ^= (uint8_t)(1u << (t & 7));
                dirty = true; edited_global();
                continue;
            }
            held_row_turned = true;
            if (k == 2) {                           /* the CC this track's step locks send */
                if (is_cc(t)) continue;             /* (a CC track's steps carry their value: no lock CC) */
                uint8_t *lc = lock_cc_p(t);
                int c = *lc + v;
                *lc = (uint8_t)(c < 0 ? 0 : c > 127 ? 127 : c);
                dirty = true; edited_global();
                continue;
            }
            if (k == 1 && is_mel(t)) { int n = m_cur[t - DTRACKS] + v; m_cur[t - DTRACKS] = (uint8_t)(n < 1 ? 1 : n > 127 ? 127 : n); dirty = true; continue; }
            if (k == 1) { int n = tnote(t) + v; set_tnote(t, (uint8_t)(n < 1 ? 1 : n > 127 ? 127 : n)); }   /* (a CC track: CC 1-127; 0 = "default" in the record) */
            else { int c = tchan(t) + v; c = c < 1 ? 1 : c > 16 ? 16 : c; set_tchan(t, (uint8_t)c); }
            dirty = true; edited_global();
            continue;
        }
        int kc = kctx(), b = *kc_bank(kc) & 3, f = kb_fn(kc, b, k);   /* nothing held: the knob does what its section's bank says */
        if (f == KB_SWING) {
            int s = G.swing + v;
            G.swing = (uint8_t)(s < 50 ? 50 : s > 75 ? 75 : s);
            dirty = true; touched_global(); pop_kind = 2; pop_ms = ms_ticks;
            continue;
        }
        if (f == KB_TEMPO) {
            int t = (int)BPM + v;
            BPM = (uint16_t)(t < 40 ? 40 : t > 300 ? 300 : t);
            dirty = true; touched_global(); pop_kind = 1; pop_ms = ms_ticks;
            continue;
        }
        uint8_t ch = (uint8_t)(kb_chan(kc, b, k) - 1);
        char *o;
        if (*kc_abs(kc, b, k)) {                    /* absolute: the knob moves a value 0-127 and sends it */
            uint8_t *kv = &kb_val[kc][b][k];
            int x = *kv + v;
            *kv = (uint8_t)(x < 0 ? 0 : x > 127 ? 127 : x);
            midi_send3((uint8_t)(0xB0 | ch), (uint8_t)f, *kv);
            o = cat(status_line, "CC"); o = cat_int(o, f); o = cat(o, "="); cat_int(o, *kv);
        } else {                                    /* relative: 64 +- steps (65 = one step clockwise) */
            if (v > 63) v = 63;
            if (v < -63) v = -63;
            midi_send3((uint8_t)(0xB0 | ch), (uint8_t)f, (uint8_t)(64 + v));
            o = cat(status_line, "CC"); o = cat_int(o, f);
            o = cat(o, v > 0 ? "+" : "-"); cat_int(o, v > 0 ? v : -v);
        }
        dirty = true;
    }
    /* an edit gesture ends when no step pad is held any more and no REC take (a hit recorded) is running */
    if (undo_open && held_pad < 0 && !(undo_rec && rec_arm && (playing || ext_playing))) undo_open = false;
}

/* ---------- MIDI in ---------- */

static crash_info_t crash_boot;                     /* what the backup registers held at boot (the last crash, if any) */
/* v0.39 the last crash also lives in FLASH (store2 kind 61). The backup registers survive a reset but not a power-off
   (the Fire has no backup battery), so a crash on a computer, with no Pi to save it at once, vanished when the Fire was
   unplugged. Record: a magic word, then crash_info_t's first 15 words (valid .. stack_min). At boot a crash in the
   registers that flash does not hold yet is copied there (alone, at a quiet moment); no crash in the registers but one
   in flash = the flash copy is the crash on record (About, 06). 06 01 clears both. */
#define CRASH_REC_MAGIC 0x31465243u                 /* "CRF1" */
#define CRASH_REC_WORDS 15
typedef struct { uint32_t magic; uint32_t w[CRASH_REC_WORDS]; } crash_rec_t;
_Static_assert(sizeof(crash_rec_t) == CP_CRASH_LEN && KIND_CRASH == CP_KIND_CRASH && KIND_CRASH >= KIND_PAINT(PAINT_SLOTS)
               && KIND_CRASH < S2_KINDS, "the crash record's size and kind");
_Static_assert(offsetof(crash_info_t, stack_min) == offsetof(crash_info_t, valid) + 4 * (CRASH_REC_WORDS - 1),
               "valid .. stack_min are the 15 words the record keeps");
static crash_rec_t crash_rec;                       /* what flash holds (or is about to) */
static bool crash_pend;                             /* crash_rec waits to be written */
static void crash_load(crash_info_t *ci)
{
    memset(ci, 0, sizeof *ci);
    if (!bk_valid()) return;
    ci->valid = 1; ci->cause = RTC->BKP1R & 0xFFFFu; ci->stage = RTC->BKP13R;
    ci->pc = RTC->BKP3R; ci->lr = RTC->BKP4R; ci->psr = RTC->BKP5R; ci->cfsr = RTC->BKP6R; ci->hfsr = RTC->BKP7R; ci->addr = RTC->BKP8R;
    ci->up_ms = RTC->BKP9R; ci->count = RTC->BKP10R; ci->total = RTC->BKP11R; ci->build = RTC->BKP14R;
    ci->sp = RTC->BKP15R; ci->stack_min = RTC->BKP16R;
}

static uint8_t sx[40];
static unsigned sx_len;
static bool sx_on;

/* Host protocol (the Pi -> the Fire). SysEx, the non-commercial id 7D, then "ORC" and a command:
     F0 7D 4F 52 43 <cmd> <data...> F7
     00                hello: the Fire answers F0 7D 4F 52 43 00 <version> F7 (so a host can find it)
     01 / 02           (the PI VU bars' levels / on-off until v0.35: accepted and ignored since v0.36)
     03 / 04 / 05      (light speed / a show / scrolling text until v0.31: ignored like any unknown command)
     06 [1]            the last crash on record (cause, PC, LR, stage ...: crash.h); 06 01 also clears it
     0D <0-31>         queue a pattern (song mode: a slot) for the next bar, like PATTERN up / down (v0.30, ctl.h)
     0E <0|1>          bar messages (0F, Fire -> host) off / on; off at power-on (v0.30, ctl.h)
     10-15             UNIVERSAL pages (v0.31): write (begin / chunk / commit), read, list, clear (ctl.h, uni.h, USER_GUIDE)
     1A <n 0-9>        the DAW page (v0.31d) has n rows, 0 = none; rows from n on are cleared (RAM only, never saved)
     1B <row> <R> <G> <B> <level> <name 0-8>   one DAW row in full;   1C <row> <level>   a DAW row's level only
                       (Fire -> host: 1D <row> <level> = the user moved a DAW row; 1E <0|1> = scrolled past the first / last row)
   Anything else is ignored. 07-0C: USER_GUIDE "The host protocol". Version 2 = v0.30 (0D / 0E / 0F), 3 = v0.31 (10-15),
   5 = v0.31d (1A-1E; 4 was the rolled-back v0.32's 18 / 19 and is never used). */
#define HOST_VERSION 9                              /* 9 = v0.34: the short LED slot (about 200 brightness steps), + 25; 8 = v0.33: + 21-24 ORC mode (hostm.h); 7 = v0.31r: + 20 remote input; 03 / 04 / 05 (light shows) gone */
#ifdef CRASH_TEST
static void __attribute__((noinline)) bench_smash(void)   /* overruns its own buffer: the canary must catch it */
{
    volatile char b[8];
    for (int i = 0; i < 40; i++) ((volatile char *)b)[i] = '0';
}
#endif

static void __attribute__((noinline)) host_cmd(const uint8_t *p, unsigned n)
{
#ifdef CRASH_TEST                                   /* BENCH BUILDS ONLY (never in the shipped image): 7E 00 = bus fault, 7E 01 = hang, 7E 02 = undefined instruction, 7E 03 = stack smash */
    if (n == 2 && p[0] == 0x7E) {
        if (p[1] == 0) { volatile uint32_t *bad = (volatile uint32_t *)0xFFFFFFF0u; (void)*bad; }
        if (p[1] == 1) { for (;;) {} }
        if (p[1] == 2) { __asm volatile("udf #0"); }
        if (p[1] == 3) bench_smash();
        if (p[1] == 5) {                            /* a CPU write to a watched word: DebugMon must record it */
            NVIC_DisableIRQ(TIM4_IRQn);             /* (TIM4 re-arms + disarms the same comparator) */
            volatile uint32_t x = 0;
            DWT->COMP0 = (uint32_t)&x; DWT->MASK0 = 2; DWT->FUNCTION0 = 6;
            x = 0x5A5A5A5Au;
            for (volatile int i = 0; i < 100; i++) {}
            DWT->FUNCTION0 = 0;
            NVIC_EnableIRQ(TIM4_IRQn);
        }
        if (p[1] == 6) tim4_bench = 3;              /* TIM4's SP moved by 8 (kind 3): its stack check must record it */
        if (p[1] == 7) tim4_bench = 5;              /* TIM4's saved return overwritten (kind 5) */
        if (p[1] == 8) tim4_bench = 1;              /* v0.33b: TIM4 handed 0xFFFFFFFF as its entry address: noted, NO reset */
        if (p[1] == 4) {                            /* jump into RAM (two NOPs on the stack): the MPU must stop it at once */
            volatile uint16_t code[4] = {0xBF00, 0xBF00, 0x4770, 0xBF00};   /* nop, nop, bx lr */
            ((void (*)(void))((uint32_t)code | 1u))();
        }
        return;
    }
#endif
    ctl_host_t h = ctl_host_parse(p, n);
    switch (h.cmd) {
    case HC_HELLO:
        if (tud_midi_mounted()) {
            static const uint8_t R[] = {0xF0, 0x7D, 0x4F, 0x52, 0x43, 0x00, HOST_VERSION, 0xF7};
            tud_midi_stream_write(0, R, sizeof R);
        }
        break;
    case HC_LEVELS:                                 /* 01 / 02: the PI VU bars (gone in v0.36): ignored */
    case HC_LIGHTS:
        break;
    /* 03 / 04 / 05 (light speed, a show, scrolling text) are gone since v0.31r: ignored like any unknown command */
    case HC_CRASH:                                  /* the last crash on record (06), and clear it (06 01) */
        if (tud_midi_mounted()) {
            uint8_t msg[CRASH_REPLY_LEN + 2];
            msg[0] = 0xF0;
            bool tn = t4_note.magic == T4_NOTE_MAGIC;   /* the TIM4 check note is LIVE (this run + any before the reset) */
            crash_boot.t4_count = tn ? t4_note.count : 0; crash_boot.t4_ms = tn ? t4_note.ms : 0;
            crash_boot.t4_kind = tn ? t4_note.kind : 0; crash_boot.t4_seen = tn ? t4_note.seen : 0;
            crash_boot.t4_want = tn ? t4_note.want : 0; crash_boot.t4_exc = tn ? t4_note.exc : 0;
            crash_reply(&crash_boot, msg + 1);
            msg[CRASH_REPLY_LEN + 1] = 0xF7;
            tud_midi_stream_write(0, msg, sizeof msg);
        }
        if (h.arg == 1) {
            RTC->BKP1R = 0; RTC->BKP11R = 0; memset(&crash_boot, 0, sizeof crash_boot); crash_snap.magic = 0; t4_note.magic = 0;
            if (crash_rec.w[0]) {                   /* v0.39: flash's copy is cleared too (a cleared record: valid 0) */
                if (saving_kind == KIND_CRASH && store2_busy_kind() == KIND_CRASH) { store2_cancel(); saving_kind = -1; }
                memset(crash_rec.w, 0, sizeof crash_rec.w); crash_rec.magic = CRASH_REC_MAGIC; crash_pend = true;
                if (!save_run) { saver_start(); save_paint_only = G.autosave_off && !save_forced_run; }
            }
        }
        break;
    case HC_PEEK:                                   /* read one word (09): RAM, flash, peripheral / core registers */
        if (tud_midi_mounted()) {
            uint8_t msg[18] = {0xF0, 0x7D, 0x4F, 0x52, 0x43, 0x09};
            memcpy(msg + 6, h.data, 5);             /* the address, as asked (so a reply can't be paired with the wrong ask) */
            crash_pack32(*(volatile const uint32_t *)(uint32_t)h.arg, msg + 11);
            msg[16] = 0; msg[17] = 0xF7;            /* (spare byte, 0) */
            tud_midi_stream_write(0, msg, sizeof msg);
        }
        break;
    case HC_COLORS:                                 /* 0A / 0B: the ORC pads page is gone (v0.36): ignored */
    case HC_SURFACE:
        break;
    case HC_STACK:                                  /* the stack around the last fault (08) */
        if (tud_midi_mounted()) {
            static uint8_t msg[CRASH_STACK_REPLY_LEN + 2];
            int ok = crash_boot.valid && crash_snap.magic == SNAP_MAGIC;
            msg[0] = 0xF0;
            crash_stack_reply(ok, crash_snap.base, crash_snap.w, msg + 1);
            msg[CRASH_STACK_REPLY_LEN + 1] = 0xF7;
            tud_midi_stream_write(0, msg, sizeof msg);
        }
        break;
    case HC_STATUS:                                 /* read only: what the pads / saver are doing (host tests, debugging) */
        if (tud_midi_mounted()) {
            uint32_t lmax = loop_max_us > 16383 ? 16383 : loop_max_us;
            if (h.arg == 1) { loop_max_us = 0; fire_vdd_reset(); }   /* 07 01: measure from now on (v0.33b: the supply too) */
            uint32_t stack_depth = (uint32_t)&_estack - stack_low();
            if (stack_depth > 16383) stack_depth = 16383;
            const uint8_t R[] = {0xF0, 0x7D, 0x4F, 0x52, 0x43, 0x07,
                0, 127, 0, 0, 0,                    /* (the PI VU bars until v0.35: gone; 127 = no light mode) */
                unsaved(), save_lazy_due != 0, saves_global,
                playing, ext_playing, (uint8_t)(G.lt_mode & 127), (uint8_t)(G.lt_speed & 127), menu_on,
                0,                                  /* (PI VU frames drawn until v0.35) */
                (uint8_t)(stack_depth >> 7 & 127), (uint8_t)(stack_depth & 127),   /* deepest stack use since boot, bytes */
                0, 0,                               /* (the ORC surface + its channel until v0.35: gone) */
                (uint8_t)(lmax >> 7 & 0x7F), (uint8_t)(lmax & 0x7F),   /* v0.31: the longest main-loop pass since boot / 07 01, us (16383 = more) */
                0xF7};
            tud_midi_stream_write(0, R, sizeof R);
        }
        break;
    case HC_QUEUE: {                                /* 0D: the PATTERN buttons' rule (pattern_step / the picker), from the host */
        bool song = song_active();
        int q = queued_pat;
        int r = ctl_queue_pat(h.arg, song, song_len(), PATTERNS, playing || ext_playing, CURPAT, &q);
        if (r == CQ_QUEUE) queued_pat = q;
        else if (r == CQ_NOW) {
            if (song) song_goto(h.arg);
            else { CURPAT = (uint8_t)h.arg; queued_pat = -1; }
        }
        if (r != CQ_REFUSE) { dirty = true; if (!song) touched_global(); }
        if (tud_midi_mounted()) {
            const uint8_t R[] = {0xF0, 0x7D, 0x4F, 0x52, 0x43, 0x0D, (uint8_t)(r != CQ_REFUSE), (uint8_t)(CURPAT & 0x7F),
                                 (uint8_t)(queued_pat >= 0 ? queued_pat & 0x7F : CTL_NO_SLOT), (uint8_t)song, 0xF7};
            tud_midi_stream_write(0, R, sizeof R);
        }
        break;
    }
    case HC_BARS:                                   /* 0E: bar messages (0F) off / on */
        bar_msgs = h.arg == 1;
        break;
    case HC_UBEGIN: uni_host_begin(h.arg, h.len); break;   /* v0.31 UNIVERSAL pages (uni.h, USER_GUIDE) */
    case HC_UCHUNK: uni_host_chunk((uint32_t)h.arg, h.data, h.len); break;
    case HC_UCOMMIT: uni_host_commit((uint32_t)h.arg); break;
    case HC_UCLEAR: uni_host_clear(h.arg); break;
    case HC_DAWN:                                   /* 1A (v0.31d): the DAW page has n rows; 0 = gone */
        if (dawm) { ctl_daw_count(&pi_page, h.arg); break; }   /* v0.37c: DAW mode owns the rows; the Pi's page waits aside, kept current */
        ctl_daw_count(&daw, h.arg);
        if (daw_top > ctl_fset_max_top(daw.n, 0, CTL_DAW_ROWS, VIEW_ROWS)) daw_top = ctl_fset_max_top(daw.n, 0, CTL_DAW_ROWS, VIEW_ROWS);
        if (mode == MODE_DAW) {
            if (!daw.n) {                           /* removed while on screen: back to where you were before FADERS */
                set_mode(mode_before_faders);
            }
            dirty = true;
        }
        break;
    case HC_DAWROW:                                 /* 1B: a row in full (no echo); v0.37c: in DAW mode, to the page waiting aside */
        if (dawm) ctl_daw_row(&pi_page, h.arg, h.data, h.len);
        else { ctl_daw_row(&daw, h.arg, h.data, h.len); daw_dirty(h.arg); }
        break;
    case HC_DAWLVL:                                 /* 1C: a level from the host: never sent back; v0.37c: in DAW mode, aside */
        if (dawm) ctl_daw_level(&pi_page, h.arg, (int)h.len);
        else if (ctl_daw_level(&daw, h.arg, (int)h.len)) daw_dirty(h.arg);
        break;
    case HC_INPUT: {                                /* 20: remote input (v0.31r, remote.h): answered 20 <sub> <result> <armed> */
        int res = rin_cmd(&rin, h.arg, h.data, h.len, ms_ticks);
        if (tud_midi_mounted()) {
            uint8_t msg[10] = {0xF0, 0x7D, 0x4F, 0x52, 0x43, 0x20, (uint8_t)(h.arg & 0x7F), (uint8_t)res, rin.armed, 0xF7};
            tud_midi_stream_write(0, msg, sizeof msg);
        }
        break;
    }
    case HC_HOSTM:                                  /* 21 (v0.33): ORC mode off / on / "still here"; always answered 21 <state> */
        if (h.arg == 1 && !hostm) { hostm_enter(); break; }       /* (enter and leave say the new state themselves) */
        if (h.arg == 0 && hostm) { hostm_leave("ORC off"); break; }
        if (hostm) hostm_seen = ms_ticks;
        hostm_say();
        break;
    case HC_GAMMA: hm_set_gamma(h.arg); break;      /* 24 (v0.33): the pad colour curve x 10; the host repaints */
    case HC_FINEW:                                  /* 25 (v0.34): the short LED slot. 25 <12-64> sets the share ORC mode's colours
                                                       are split by; 25 00 only asks. Answered 25 <share> <measured slot us> <interrupt us> */
        if (h.arg) hm_set_weight(h.arg);
        if (tud_midi_mounted()) {
            uint8_t m[10] = {0xF0, 0x7D, 0x4F, 0x52, 0x43, 0x25, (uint8_t)hm_weight(), (uint8_t)(t4_gap_us & 0x7F), (uint8_t)(t4_len_us > 127 ? 127 : t4_len_us), 0xF7};
            tud_midi_stream_write(0, m, sizeof m);
        }
        break;
    case HC_UREAD:                                  /* 13: a page as the Fire plays it now (its edits not saved yet included) */
        if (tud_midi_mounted()) {
            uint32_t bl, off = h.len;
            const uint8_t *b = uni_base(h.arg, &bl);
            uint32_t len = uni_patched_len(b, bl, uni_ps, UNI_SLOTS, h.arg, UNI_ALL);
            uint32_t n = off > len ? 0 : (len - off > UNI_CHUNK ? UNI_CHUNK : len - off);
            uint8_t m[12 + UNI_CHUNK + 1] = {0xF0, 0x7D, 0x4F, 0x52, 0x43, 0x13, (uint8_t)(off > len ? UR_BAD : UR_OK), (uint8_t)h.arg,
                                             (uint8_t)(len >> 7 & 0x7F), (uint8_t)(len & 0x7F), (uint8_t)(off >> 7 & 0x7F), (uint8_t)(off & 0x7F)};
            if (n) uni_patched_read(b, bl, uni_ps, UNI_SLOTS, h.arg, UNI_ALL, off, m + 12, n);
            m[12 + n] = 0xF7;
            tud_midi_stream_write(0, m, 13 + n);
        }
        break;
    case HC_ULIST:                                  /* 14: every page's length, checksum and name, the budget */
        if (tud_midi_mounted()) {
            uint8_t m[14 + UNI_PAGES * 12 + 1];
            uint32_t used = 0;
            for (int p = 0; p < UNI_PAGES; p++) used += uni_size(p);
            const uint8_t hd[14] = {0xF0, 0x7D, 0x4F, 0x52, 0x43, 0x14, UNI_PAGES, (uint8_t)(UNI_BUDGET >> 7 & 0x7F), (uint8_t)(UNI_BUDGET & 0x7F),
                                    (uint8_t)(used >> 7 & 0x7F), (uint8_t)(used & 0x7F), (uint8_t)(uw.state ? uw.page : 127),
                                    G.uni_page, mode == MODE_UNI};
            memcpy(m, hd, sizeof hd);
            for (int p = 0; p < UNI_PAGES; p++) {
                uint8_t *q = m + 14 + 12 * p;
                uint32_t bl;
                const uint8_t *b = uni_base(p, &bl);
                uni_cached(p);                      /* (length + sum cached per page change: no walk here) */
                uint32_t len = uni_cache[p].len, sum = uni_cache[p].sum;
                for (int i = 0; i < UNI_NAME; i++) q[4 + i] = b ? b[1 + i] : ' ';   /* (edits never change the name) */
                q[0] = (uint8_t)(len >> 7 & 0x7F); q[1] = (uint8_t)(len & 0x7F); q[2] = (uint8_t)(sum >> 7 & 0x7F); q[3] = (uint8_t)(sum & 0x7F);
            }
            m[sizeof m - 1] = 0xF7;
            tud_midi_stream_write(0, m, sizeof m);
        }
        break;
    default: break;
    }
}

static void __attribute__((noinline)) midi_in_byte(uint8_t b, uint32_t now)
{
    if (b == 0xF8) {
        ext_clock = true; ext_last_ms = ms_ticks;
        if (ext_playing) {
            /* 12 ticks per pair of steps: the even step on tick 0, the odd (swung) one once its tick
               is reached. Once per pair, so turning swing mid-pair can't skip or double a step. */
            int pos = (int)(ext_ticks % 12);
            if (ext_tick_plays(pos, ext_late(), &ext_odd_due)) {
                if (pos == 0) { if (ext_even_at) ext_step_len = (now - ext_even_at) / 2; ext_even_at = now; }
                ext_step_at = now;
                play_step(gstep, now); gstep++;
            }
            ext_ticks++;
        }
        return;
    }
    if (b == 0xFA) {
        all_notes_off();                            /* a start without a stop first: clean slate */
        ext_clock = true; ext_last_ms = ms_ticks; playing = false; ext_playing = true; ext_ticks = 0; gstep = 0; memset(roll_at, 0, sizeof roll_at);
        ext_step_at = ext_even_at = ext_step_len = 0; ext_odd_due = false; rec_chord_t = -1;
        if (song_active()) song_goto(0);
        dirty = true; return;
    }
    if (b == 0xFB) { ext_playing = true; dirty = true; return; }
    if (b == 0xFC) { stop(false); return; }
    if (hostm && (hm_byte(b) & HM_OLED_DONE)) hostm_flush = true;   /* v0.33: Akai-format colours / bitmaps, of any length */
    if (b == 0xF0) { sx_on = true; sx_len = 0; }
    if (sx_on) {
        if (sx_len < sizeof sx) sx[sx_len++] = b;
        if (b == 0xF7) {
            static const uint8_t DFU[] = {0xF0, 0x47, 0x7F, 0x43, 0x72, 0x00, 0x00, 0xF7};
            const uint8_t *hp; unsigned hn;
            if (sx_len == sizeof DFU && !memcmp(sx, DFU, sizeof DFU)) dfu_requested = true;
            else if (ctl_sysex_payload(sx, sx_len, sizeof sx, &hp, &hn))
                host_cmd(hp, hn);                   /* F0 7D 'O' 'R' 'C' <cmd + data> F7 (a message longer than the buffer is dropped) */
            sx_on = false;
        }
    }
}

/* ---------- the DAW port (v0.36, USB cable 1) ----------
   Everything for the DAW goes out as ready-made USB-MIDI packets with cable 1 in them (tud_midi_packet_write), never
   through tud_midi_stream_write: TinyUSB keeps ONE stream state per interface, so a cable-1 message written while a
   cable-0 SysEx is half sent would land inside it. daw_send packs one complete MIDI message (or SysEx) itself. */
#define DAW_CABLE 1u
static uint32_t daw_rx_packets;                     /* packets seen on the DAW port (the spike: proves the port works) */
static void dawm_event(const daw_ev_t *ev);
static void dawm_relock(void);
static void daw_in_packet(const uint8_t pkt[4])
{
    static const uint8_t len[16] = {0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1};
    daw_rx_packets++;                               /* v0.36b: decoded even outside DAW mode, so going back in shows the DAW's state now
                                                       (a running DAW only sends changes; v0.36a waited for one that never came) */
    int n = len[pkt[0] & 0xF];
    for (int i = 0; i < n; i++) {
        daw_ev_t ev;
        if (daw_byte(pkt[1 + i], &ev)) dawm_event(&ev);
        if (daw_proto() != dawm_proto_seen) dawm_relock();   /* v0.38b: locked (or re-locked) by any message */
    }
}
static void daw_send(const uint8_t *m, int n)
{
    if (n <= 0 || !tud_midi_mounted()) return;
    uint8_t pkt[4];
    if (m[0] != 0xF0) {                             /* a channel message: 2 or 3 bytes, CIN = the status' high nibble */
        pkt[0] = (uint8_t)(DAW_CABLE << 4 | m[0] >> 4);
        pkt[1] = m[0]; pkt[2] = n > 1 ? m[1] : 0; pkt[3] = n > 2 ? m[2] : 0;
        tud_midi_packet_write(pkt);
        return;
    }
    for (int i = 0; i < n; i += 3) {                /* SysEx: CIN 4 = 3 bytes going on, 5 / 6 / 7 = ends with 1 / 2 / 3 */
        int left = n - i, k = left > 3 ? 3 : left;
        bool end = left <= 3;
        pkt[0] = (uint8_t)(DAW_CABLE << 4 | (end ? 4 + k : 4));
        pkt[1] = m[i]; pkt[2] = k > 1 ? m[i + 1] : 0; pkt[3] = k > 2 ? m[i + 2] : 0;
        tud_midi_packet_write(pkt);
    }
}
void daw_out(const uint8_t *m, int n) { daw_send(m, n); }   /* daw.c's one way out: the DAW port */

/* the rows' fixed colours (MCU / HUI carry none): eight hues, the master white */
static const uint8_t DAWM_RGB[9][3] = {{7, 0, 0}, {7, 3, 0}, {6, 6, 0}, {0, 7, 0}, {0, 6, 6}, {0, 2, 7}, {4, 0, 7}, {7, 0, 5}, {5, 5, 5}};
static void dawm_rows(int n)                        /* n rows (MCU 8 + master, HUI 8), coloured, named "1".."8" until the DAW names them */
{
    ctl_daw_count(&daw, 0);
    ctl_daw_count(&daw, n);
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < 3; j++) daw.rgb[i][j] = DAWM_RGB[i][j];
        if (i == 8) memcpy(daw.name[i], "Master", 7);
        else { daw.name[i][0] = (char)('1' + i); daw.name[i][1] = 0; }
    }
    if (daw_top > ctl_fset_max_top(daw.n, 0, CTL_DAW_ROWS, VIEW_ROWS)) daw_top = 0;
}
static void dawm_names(void)                        /* the DAW's text -> the rows' names (MCU: the LCD's top line, 7 a strip; HUI: the 4-character strips) */
{
    const char *t = daw_text();
    bool hui = daw_proto() == DAW_HUI;
    for (int i = 0; i < 8 && i < daw.n; i++) {
        const char *src = hui ? t + DAW_HUI_STRIP + 4 * i : t + 7 * i;
        int len = hui ? 4 : 7, a = 0, b = len;
        while (a < b && src[a] == ' ') a++;
        while (b > a && src[b - 1] == ' ') b--;
        if (a == b) continue;                       /* blank: keep the number */
        int n = b - a > CTL_DAW_NAME ? CTL_DAW_NAME : b - a;
        memcpy(daw.name[i], src + a, (size_t)n); daw.name[i][n] = 0;
    }
}
static void dawm_event(const daw_ev_t *ev)
{
    dawm_seen = ms_ticks ? ms_ticks : 1;
    if (ev->type == DE_LEVEL && ev->idx < DAW_STRIPS) dawm_lvl[ev->idx] = ev->val;
    if (!dawm) return;                              /* the rows (shared with the Pi's DAW page) are only touched in DAW mode */
    switch (ev->type) {
    case DE_LEVEL: if (ev->idx < daw.n && ctl_daw_level(&daw, ev->idx, ev->val)) daw_dirty(ev->idx); break;
    case DE_TEXT:  dawm_names(); dirty = true; break;
    case DE_LED: case DE_DIGIT: case DE_RING: case DE_HELLO: case DE_PING: dirty = true; break;
    default: break;
    }
}
/* v0.38b: the protocol changed: rows for it (HUI has no master) */
static void dawm_relock(void)
{
    if (dawm_proto_seen != DAW_AUTO) memset(dawm_lvl, 0, sizeof dawm_lvl);   /* the old protocol's levels mean nothing now */
    dawm_proto_seen = (uint8_t)daw_proto();
    if (!dawm) return;
    dawm_rows(daw_proto() == DAW_HUI ? 8 : 9);
    dawm_names();
    for (int i = 0; i < daw.n; i++) ctl_daw_level(&daw, i, dawm_lvl[i]);
    dirty = true;
}
static void dawm_press(int idx, int id) { if (daw_button(id, 1)) dawm_held[idx] = (uint8_t)(id + 1); }
static void dawm_release(int idx) { if (dawm_held[idx]) { daw_button(dawm_held[idx] - 1, 0); dawm_held[idx] = 0; } }
static void dawm_enter(void)
{
    if (dawm || hostm || paintm) return;
    if (!tud_midi_mounted()) { menu_toast_set("no computer"); return; }
    menu_on = menu_edit = false; menu_grp = 0; menu_sel = 0;
    for (int pad = 0; pad < 64; pad++) {            /* let go of everything the sequencer's own controls were holding */
        if (roll_pads >> pad & 1) roll_release(pad);
        live_release(pad);
    }
    up_wait = 0;
    if (uni_ed || uni_copy_src >= 0 || uni_unsaved()) uni_edit_end();
    uni_held = 0; uni_latch_off(-1);
    song_edit = false; trkvol = false;
    held_t = held_s = -1; held_pad = -1; held_row = -1; held_row_t = -1; row_consumed = 0;
    if (mode != MODE_FADER && mode != MODE_DAW) mode_before_faders = mode;   /* v0.38: UNIVERSAL is a place of its own (NOTE x2) */
    pi_page = daw; pi_top = daw_top;                /* v0.37c: the Pi's DAW page waits aside (1A-1C keep it current), back on leaving */
    dawm_rows(daw_proto() == DAW_HUI ? 8 : 9);      /* v0.36b: no daw_init here: what the DAW already said stays */
    dawm_names();
    for (int i = 0; i < daw.n; i++) ctl_daw_level(&daw, i, dawm_lvl[i]);
    daw_top = 0; dawm_view = 0; dawm_half = 0;
    dawm = true;
    set_mode(MODE_DAW);
    daw_announce();                                 /* MCU: the surface introduces itself; Auto / HUI: wait for the DAW */
    dirty = true;
}
static void dawm_leave(void)
{
    if (!dawm) return;
    for (int i = 0; i < IN_COUNT; i++) dawm_release(i);   /* v0.38b: every held DAW button let go too */
    daw_release_all();                              /* every touched fader let go */
    dawm = false;
    daw = pi_page;                                  /* v0.37c: the Pi's DAW page back as it is now (it was wiped, and the Pi never resent it) */
    daw_top = pi_top > ctl_fset_max_top(daw.n, 0, CTL_DAW_ROWS, VIEW_ROWS) ? ctl_fset_max_top(daw.n, 0, CTL_DAW_ROWS, VIEW_ROWS) : pi_top;
    set_mode(mode_before_faders);
    status_brief("DAW off");                        /* v0.37d: gone again after 2 s (it stayed, hiding UNIVERSAL's page name) */
    dirty = true;
}
static void menu_dawm_now(void) { dawm_enter(); }

static void __attribute__((noinline)) dawm_events(void)
{
    static const uint8_t GRP[4] = {DB_REC, DB_SOLO, DB_MUTE, DB_SELECT};
    rin_tick(&rin, ms_ticks);                       /* the host's remote input (20) works here too (the bench tests) */
    int e;
    while ((e = fire_event()) >= 0) {
        if (hm_exit(e, fire_held(IN_SHIFT))) { dawm_leave(); return; }   /* SHIFT + BROWSER: out (as ORC mode) */
        int idx = e & 0x7F, pad = pad_of_input(idx);
        bool down = e & 0x80;
        if (idx >= IN_COUNT) continue;
        if (!down) {                                /* v0.38b: a release goes to what its press sent, whatever shows now */
            dawm_release(idx);
            if (idx == IN_SHIFT || idx == IN_ALT) dirty = true;
            continue;
        }
        if (pad >= 0) {
            if (dawm_view) dawm_press(idx, GRP[pad / 16] + (pad % 16) / 2);   /* 8 strips, 2 pads each */
            else {                        /* a tap: that row's level (the DAW page's own rule) */
                int f = daw_top + pad / 16;
                if (f < daw.n) fader_set(f, ((pad % 16) * 127 + 7) / 15);
            }
            continue;
        }
        switch (idx) {
        case IN_PLAY:   dawm_press(idx, DB_PLAY); break;
        case IN_STOP:   dawm_press(idx, DB_STOP); break;
        case IN_REC:    dawm_press(idx, DB_RECORD); break;
        case IN_PAT_UP: if (down) daw_bank(-1); break;
        case IN_PAT_DN: if (down) daw_bank(1); break;
        case IN_GRID_L: if (down) daw_channel(-1); break;
        case IN_GRID_R: if (down) daw_channel(1); break;
        case IN_STEP:   if (down) { dawm_view = 1; dirty = true; } break;
        case IN_PERFORM: if (down) { dawm_view = 0; dirty = true; } break;
        case IN_SELECT: if (down && dawm_view) { dawm_half ^= 1; dirty = true; } break;
        case IN_ROW1: case IN_ROW2: case IN_ROW3: case IN_ROW4: {   /* fader view: the row's MUTE */
            int r = idx == IN_ROW1 ? 0 : idx == IN_ROW2 ? 1 : idx == IN_ROW3 ? 2 : 3, f = daw_top + r;
            if (!dawm_view && f < 8 && f < daw.n) dawm_press(idx, DB_MUTE + f);
            break;
        }
        default: break;
        }
        if (idx == IN_SHIFT || idx == IN_ALT) dirty = true;
    }
    int d = fire_encoder_take();
    if (d) { if (dawm_view) daw_jog(d); else scroll_faders(d); }
    for (int k = 0; k < 4; k++) {
        int v = fire_knob_take(k);
        if (!v) continue;
        if (dawm_view) daw_vpot(dawm_half * 4 + k, v);
        else if (daw_top + k < daw.n) daw_knob(k, daw_top + k, v);
    }
    daw_tick(ms_ticks);                             /* faders left alone let go (touch off) */
    if (dirty) return;
}

static void __attribute__((noinline)) paint_dawm(void)   /* the button view: lit = on in the DAW, dim = off */
{
    static const uint8_t GRP[4] = {DB_REC, DB_SOLO, DB_MUTE, DB_SELECT};
    static const uint8_t COL[4][3] = {{7, 0, 0}, {7, 6, 0}, {7, 2, 0}, {6, 6, 6}};   /* REC red, SOLO yellow, MUTE orange, SELECT white */
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 16; c++) {
            bool on = daw_led(GRP[r] + c / 2) != 0;
            for (int j = 0; j < 3; j++) led_set(led_pad(r, c, j), on ? COL[r][j] : (COL[r][j] ? 1 : 0));
        }
    for (int r = 0; r < 4; r++) { led_set(LED_ROW_G[r], 0); led_set(LED_ROWSIDE_R[r], 0); led_set(LED_ROWSIDE_G[r], 0); }
}
static void __attribute__((noinline)) draw_dawm_screen(void)
{
    char buf[24], *o;
    o = cat(buf, "DAW "); o = cat(o, NM_DAWP[daw_proto() ? daw_proto() : G.daw_proto % 3]);
    if (!dawm_seen) cat(o, " waiting");
    draw_text(0, 0, buf, 1);
    for (int row = 0; row < 2; row++) {             /* the 8 strips' names, 4 a line, 5 characters each */
        o = buf;
        for (int i = row * 4; i < row * 4 + 4; i++) {
            int n = 0;
            for (; n < 5 && i < daw.n && daw.name[i][n]; n++) *o++ = daw.name[i][n];
            for (; n < 5; n++) *o++ = ' ';
        }
        *o = 0;
        draw_text(0, 14 + 10 * row, buf, 1);
    }
    o = buf;                                        /* the DAW's time display, left to right (digit 0 is the rightmost) */
    for (int i = DAW_DIGITS - 3; i >= 0; i--) { uint8_t ch = daw_st.digit[i] & 0x7F; *o++ = (char)(ch >= 32 && ch < 127 ? ch : ' '); }
    *o = 0;
    draw_text(0, 36, buf, 1);
    draw_text(0, 55, dawm_half ? "knobs 5-8  SH+BRW=out" : "knobs 1-4  SH+BRW=out", 1);
}


static void __attribute__((noinline)) midi_task(uint32_t now)
{
    if (!tud_midi_mounted()) return;
    uint8_t pkt[4];
    static const uint8_t len[16] = {0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1};
    while (tud_midi_packet_read(pkt)) {
        /* v0.36: cable 1 = the DAW port (Mackie Control / HUI). Nothing on it reaches the sequencer: no clock, no
           host SysEx, no fader_follow (a DAW sends clock and CCs to every port it is told to). */
        if (pkt[0] >> 4) { daw_in_packet(pkt); continue; }
        uint8_t n = len[pkt[0] & 0xF];
        if ((pkt[0] & 0xF) == 0xB) fader_follow(pkt[1], pkt[2], pkt[3]);   /* v0.31c: a CC (USB-MIDI packets always carry its status) */
        for (uint8_t i = 0; i < n; i++) midi_in_byte(pkt[1 + i], now);
    }
}

/* ---------- save / load ---------- */

static const int PAT_ID[PATTERNS] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                                      16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31};
static void pat_reader(const void *ctx, uint32_t off, uint8_t *dst, uint32_t n) { steps_record_read(*(const int *)ctx, off, dst, n); }

/* Erase the area and write the current state: the global record + one sparse record per pattern
   (old full-size records are dropped here). */
/* v0.31: the clean-up itself is compact.c (compact_run, tested against a real save area by compact_test.c). The
   UNIVERSAL pages live only in flash, so before the erase they are copied into RAM nothing else needs while stopped:
   the undo buffers (undo is dropped) + the MIDI batch buffer (empty between passes). UNI_BUDGET keeps them small enough.
   Edits not saved yet stay as patches (uni_ps) and are saved afterwards as usual. */
#define UNI_SCRATCH (sizeof undo_buf + sizeof batch)
_Static_assert(UNI_BUDGET <= UNI_SCRATCH, "the pages must fit the compaction scratch");
_Static_assert(CP_KIND_PAT == KIND_PAT(0) && CP_KIND_UNI == KIND_UNI(0) && CP_KIND_PAINT == KIND_PAINT(0), "compact.h kinds");
_Static_assert(CP_PAINTS == PAINT_SLOTS && CP_PAINT_LEN == PAINT_LEN, "compact.h paintings");
static bool pat_keep_cb(int p)                      /* an empty pattern with default lengths needs no record */
{
    if (steps_pattern_size(p)) return true;
    for (int t = 0; t < TRACKS; t++) if (steps_len[p][t]) return true;
    return false;
}
static uint32_t pat_len_cb(int p) { return steps_record_len(p); }
static bool compact_all(void)
{
    if (pat_stuck) return false;                    /* a pattern we couldn't load would be written empty: never */
    saving_kind = -1;
    batch_flush();                                  /* (the batch buffer is part of the page scratch) */
    for (int i = 0; i < UNI_SLOTS; i++) if (uni_ps[i].state == US_SAVE) uni_ps[i].state = US_PEND;
    G_snap = G;
    const compact_t c = {&G_snap, sizeof G_snap, PATTERNS, pat_keep_cb, pat_len_cb, pat_reader, PAT_ID,
                         &undo_buf[0][0], sizeof undo_buf, sizeof batch, batch, &paint_saved[0][0], paint_has,
                         crash_rec.w[0] ? (const void *)&crash_rec : 0};   /* v0.39: a cleared record is not kept */
    uint32_t kept = 0;
    int r = compact_run(&c, &kept);
    if (r == CP_REFUSED) return false;              /* nothing was touched */
    if (kept) { undo_pat = -1; undo_len[0] = undo_len[1] = 0; undo_open = false; }   /* the undo buffers held the pages */
    uni_gen++;
    for (int p = 0; p < UNI_PAGES; p++) uni_touch(p);
    bool ok = r == CP_OK;
    if (ok) for (int p = 0; p < PATTERNS; p++) steps_saved[p] = (uint16_t)steps_pattern_size(p);
    if (ok) { pat_dirty = 0; global_dirty = false; G_saved = G_snap; paint_pend = 0; crash_pend = false; }   /* (paintings + crash from RAM) */
    if (ok && uni_dirty()) save_soon();             /* v0.31: page edits still waiting are saved after it */
    return ok;
}

/* Background save: one record at a time, a few flash words per main-loop pass (see saver_poll). */

static void saver_start(void) { save_run = true; save_due = 0; save_forced_run = save_force; save_force = false; save_paint_only = false; dirty = true; }

/* v0.31: the stored page + its edits, through the cursor set up when the save began: O(1) per flash word (review
   2026-10-27: re-walking the page per word took 7-10 us a word = 1-2 ms per quiet pass) */
static void uni_save_reader(const void *ctx, uint32_t off, uint8_t *dst, uint32_t n) { (void)ctx; uni_cur_read(&uni_wcur, off, dst, n); }
static void saver_poll(bool quiet_moment)
{
    if (uw.state) return;                           /* v0.31: a host page write owns the writer (uni_poll steps it) */
    if (store2_busy()) {
        if (quiet_moment) store2_step(8);           /* ~8 x 16 us of flash stall per pass */
        if (!store2_busy() && store2_last_failed()) {   /* write failed: mark it unsaved again, retry later */
            if (saving_kind == S2_KIND_GLOBAL) global_dirty = true;
            else if (IS_KIND_PAT(saving_kind)) pat_dirty |= (uint32_t)(1u << (saving_kind - KIND_PAT(0)));
            else if (IS_KIND_UNI(saving_kind)) uni_slots_state(saving_kind - KIND_UNI(0), US_SAVE, US_PEND);
            else if (IS_KIND_PAINT(saving_kind)) paint_pend |= (uint8_t)(1u << (saving_kind - KIND_PAINT(0)));
            else if (saving_kind == KIND_CRASH) crash_pend = true;   /* v0.39 */
            save_run = false; save_due = ms_ticks + 10000; if (!save_due) save_due = 1;
            if (save_forced_run) save_force = true;     /* a SAVE NOW that failed retries even with AUTOSAVE off */
        } else if (!store2_busy() && saving_kind == S2_KIND_GLOBAL) { G_saved = G_snap; saves_global = (saves_global + 1) & 127; }   /* what was written */
        else if (!store2_busy() && IS_KIND_PAT(saving_kind)) steps_saved[saving_kind - KIND_PAT(0)] = (uint16_t)saving_size;
        else if (!store2_busy() && IS_KIND_UNI(saving_kind)) { uni_slots_state(saving_kind - KIND_UNI(0), US_SAVE, US_FREE); uni_gen++; dirty = true; }   /* now in flash */
        if (!store2_busy()) saving_kind = -1;
        return;
    }
    if (!save_run) return;
    if (global_dirty && !memcmp(&G, &G_saved, sizeof G)) global_dirty = false;   /* e.g. pattern 1 -> 2 -> 1: nothing new */
    int kind = -1, p = -1;
    if (save_paint_only) p = PATTERNS;              /* v0.38b: paintings only: skip settings, patterns, pages */
    else if (global_dirty) kind = S2_KIND_GLOBAL;
    else for (p = 0; p < PATTERNS; p++) if ((pat_dirty & ~pat_stuck) >> p & 1) { kind = KIND_PAT(p); break; }
    uint32_t ulen = 0;
    int udrop = -1;
    if (kind < 0 && !save_paint_only)               /* v0.31: a UNIVERSAL page with edits, if it fits the budget with the others as STORED */
        for (p = 0; p < UNI_PAGES && kind < 0; p++) {
            if (!uni_has_slots(p, 1u << US_PEND)) continue;
            uint32_t bl, others = 0;
            const uint8_t *b = uni_base(p, &bl);
            ulen = uni_patched_len(b, bl, uni_ps, UNI_SLOTS, p, (1u << US_PEND) | (1u << US_SAVE));   /* (one pass, once per save) */
            for (int q = 0; q < UNI_PAGES; q++) if (q != p) { uint32_t ql; if (uni_base(q, &ql)) others += ql; }
            if (others + ulen <= UNI_BUDGET) { kind = KIND_UNI(p); break; }
            if (udrop < 0) udrop = p;
        }
    if (kind < 0 && udrop >= 0) {                   /* no page's edits can ever fit (pages grew meanwhile): dropped, never stuck (USER_GUIDE) */
        uni_drop_edits(udrop, "edits dropped");
        return;                                     /* (the run goes on next pass) */
    }
    if (kind < 0)                                   /* v0.37 a painting saved by hand (from its RAM copy) */
        for (p = 0; p < PAINT_SLOTS; p++) if (paint_pend >> p & 1) { kind = KIND_PAINT(p); break; }
    if (kind < 0 && crash_pend) kind = KIND_CRASH;  /* v0.39 the last crash (from crash_rec) */
    if (kind < 0) { save_run = false; save_waiting_compact = false; dirty = true; return; }
    if (kind == S2_KIND_GLOBAL) G_snap = G;
    else if (IS_KIND_PAT(kind)) saving_size = steps_pattern_size(p);
    else if (IS_KIND_UNI(kind)) {                                          /* these edits go into this record (an edit to one of them cancels it) */
        uint32_t bl;
        const uint8_t *b = uni_base(p, &bl);       /* (the page being written is not the newest until its commit word) */
        uni_slots_state(p, US_PEND, US_SAVE);
        uni_cur_init(&uni_wcur, b, bl, uni_ps, UNI_SLOTS, p, 1u << US_SAVE);
    }
    bool begun = kind == S2_KIND_GLOBAL ? store2_begin(kind, &G_snap, sizeof G_snap)   /* from a snapshot */
               : IS_KIND_UNI(kind) ? store2_begin_src(kind, ulen, uni_save_reader, &UNI_ID[p])
               : IS_KIND_PAINT(kind) ? store2_begin(kind, paint_saved[p], PAINT_LEN)   /* (a new save of it cancels it first) */
               : kind == KIND_CRASH ? store2_begin(kind, &crash_rec, sizeof crash_rec)   /* (a clear cancels it first) */
                                        : store2_begin_src(kind, steps_record_len(p), pat_reader, &PAT_ID[p]);   /* live: edits cancel it */
    if (begun) {
        if (kind == S2_KIND_GLOBAL) global_dirty = false; else if (IS_KIND_PAT(kind)) pat_dirty &= (uint32_t)~(1u << p);
        else if (IS_KIND_PAINT(kind)) paint_pend &= (uint8_t)~(1u << p);
        else if (kind == KIND_CRASH) crash_pend = false;
        saving_kind = kind;
        return;
    }
    if (IS_KIND_UNI(kind)) uni_slots_state(p, US_SAVE, US_PEND);   /* (not begun: they wait) */
    /* no room: erase + rewrite everything, but only while stopped (it stalls ~1-2 s) */
    if (!playing && !ext_playing) { if (!compact_all()) memcpy(status_line, "full", 5); save_run = false; save_waiting_compact = false; }
    else save_waiting_compact = true;
    dirty = true;
}

/* The starting fader bank: 16 volume faders (CC 7) on channels 1-16, rainbow colours, level 100. */
static void faders_default(void)
{
    G.fad_init = 1; G.fad_n = 16;
    for (int i = 0; i < MAXFADERS; i++) { G.fad_cc[i] = 7; G.fad_ch[i] = (uint8_t)(i % 16 + 1); G.fad_col[i] = (uint8_t)(i % 16); G.fad_val[i] = 100; }
}

static void set_defaults(void)
{
    memset(&G, 0, sizeof G);
    steps_clear_all();
    G.bpm = 120; G.swing = 50;
    faders_default();
    for (int s = 0; s < 16; s += 4) steps_set(0, 0, s, LV_NORMAL, 0);
    steps_set(0, 1, 4, LV_NORMAL, 0); steps_set(0, 1, 12, LV_NORMAL, 0);
    for (int s = 2; s < 16; s += 4) steps_set(0, 2, s, LV_NORMAL, 0);
}

static bool area_blank(void)
{
    const uint32_t *w = (const uint32_t *)store_area;
    for (uint32_t i = 0; i < S2_AREA_SIZE / 4; i++) if (w[i] != 0xFFFFFFFFu) return false;
    return true;
}

static void load_state(void)
{
    set_defaults();
    store2_scan();
    if (store2_has_any()) {
        store2_read(S2_KIND_GLOBAL, &G, sizeof G);
        G_saved = G;                                /* as stored: if a clamp below changes G, the next save writes it */
        for (int ps = 0; ps < PAINT_SLOTS; ps++) {  /* v0.37 the saved paintings (a record of another length is not one) */
            uint32_t len = 0;
            const uint8_t *r = store2_ptr(KIND_PAINT(ps), &len);
            if (r && len == PAINT_LEN) { memcpy(paint_saved[ps], r, PAINT_LEN); paint_has |= (uint8_t)(1u << ps); }
        }
        bool converted = false;
        for (int p = 0; p < PATTERNS; p++) {
            uint32_t len = 0;
            const uint8_t *r = store2_ptr(KIND_PAT(p), &len);
            if (r) {                                /* the current record (v0.11 on): never fall back past it */
                if (steps_record_load(p, r, len)) steps_saved[p] = (uint16_t)steps_pattern_size(p);
                else { pat_stuck |= (uint32_t)(1u << p); cat(status_line, "FULL"); }
                continue;
            }
            if (p >= 8) continue;                   /* older formats only ever had patterns 1-8 */
            r = store2_ptr(KIND_PAT_V1(p), &len);   /* v0.10's sparse record */
            if (r) {
                if (steps_record_load_v1(p, r, len)) { steps_saved[p] = (uint16_t)steps_pattern_size(p); pat_dirty |= (uint32_t)(1u << p); converted = true; }
                else { pat_stuck |= (uint32_t)(1u << p); cat(status_line, "FULL"); }
                continue;
            }
            r = store2_ptr(1 + p, &len);               /* else an old full-size one: convert it */
            if (!r) continue;
            if (!steps_convert_full(p, r, len)) { pat_stuck |= (uint32_t)(1u << p); cat(status_line, "FULL"); continue; }
            steps_saved[p] = (uint16_t)steps_pattern_size(p);
            pat_dirty |= (uint32_t)(1u << p);
            converted = true;
        }
        if (converted) { save_due = ms_ticks + 3000; save_force = true; }   /* write them in the new format soon (AUTOSAVE or not) */
        if (G.bpm < 40 || G.bpm > 300) G.bpm = 120;
        if (G.swing < 50 || G.swing > 75) G.swing = 50;
        G.cur_pat %= PATTERNS;
        for (int t = 0; t < TRACKS; t++) if (*trk_lvl_p(t) > 127) *trk_lvl_p(t) = 0;   /* v0.23: a junk byte plays at full volume */
        for (int i = 0; i < 4; i++) G.cc_trk[i] &= (uint8_t)(i == 2 ? 0x00 : 0xFF);  /* v0.29: tracks 16-23 (melodic) are never CC tracks */
        for (int g = 0; g < 4; g++) G.grid_layout[g] = 0;   /* v0.29b: no group maps any more (MPC pads -> the remote, 16 levels -> the row button) */
        if (!G.fad_init) faders_default();          /* a save from before v0.13 */
        G.autosave_off &= 1;
        if (G.fad_n > MAXFADERS) G.fad_n = MAXFADERS;
        for (int i = 0; i < MAXFADERS; i++) {
            if (G.fad_ch[i] < 1 || G.fad_ch[i] > 16) G.fad_ch[i] = 1;
            G.fad_cc[i] &= 127; G.fad_col[i] &= 15; G.fad_val[i] &= 127;
        }
        return;
    }
    if (area_blank()) return;                       /* first run: defaults, nothing written yet */
    /* older firmware's saves (4 tracks x 16 steps): convert, then rewrite the area in the new format */
    store_data_t old;
    if (store_load(&old)) {
        steps_clear_all();
        G.bpm = (old.bpm >= 40 && old.bpm <= 300) ? old.bpm : 120;
        G.cur_pat = old.cur_pattern % PATTERNS;
        G.swing = (old.swing >= 50 && old.swing <= 75) ? old.swing : 50;
        G.clock_out = old.clock_out == 1;
        G.mute_bits = old.mute_bits & 0x0F;
        for (int t = 0; t < STORE_TRACKS; t++) {
            if (old.track_note[t]) G.note[t] = old.track_note[t] & 127;
            if (old.track_chan[t] >= 1 && old.track_chan[t] <= 16) G.chan[t] = old.track_chan[t];
        }
        for (int p = 0; p < STORE_PATTERNS; p++)
            for (int t = 0; t < STORE_TRACKS; t++)
                for (int s = 0; s < 16; s++) {
                    if (!(old.steps[p][t] >> s & 1)) continue;
                    steps_set(p, t, s, (old.accent[p][t] >> s & 1) ? LV_ACCENT : (old.soft[p][t] >> s & 1) ? LV_SOFT : LV_NORMAL, 0);
                }
        cat(status_line, "convrtd");
    }
    compact_all();                                  /* boot = stopped, so the erase is fine */
    for (int p = 0; p < PATTERNS; p++) steps_saved[p] = (uint16_t)steps_pattern_size(p);
}

int main(void)
{
    SCB->VTOR = 0x08008000u;
    bk_enable();
    uint32_t stage_before = RTC->BKP2R;             /* the stage the previous run was in (read before anything overwrites it) */
    bool wdog = (RCC->CSR & RCC_CSR_IWDGRSTF) != 0;
    RCC->CSR |= RCC_CSR_RMVF;
    if (wdog) {                                     /* a HANG: the watchdog reset us. Record it; we ARE the restarted app, so just run on. */
        bool dfu;
        crash_record(CRASH_WDOG, 0, 0, 0, 0, 0, 0, RTC->BKP12R, stage_before, &dfu);
        RTC->BKP15R = 0; RTC->BKP16R = stack_low();  /* the previous run's paint is still in RAM: its low point */
        crash_snap.magic = 0;                       /* a hang has no fault frame: an older snapshot must not pose as this one's */
        if (dfu) reboot_to_dfu();                   /* 3 early crashes in a row: update mode, so a bad image can be replaced */
    }
    RTC->BKP0R = 0u;
    mpu_ram_xn();                                   /* RAM is never code: a jump into it faults where it happens */
    dbgmon_init();                                  /* TIM4's canary watchpoint reports here */
    stack_paint();                                /* (after the watchdog record read the previous run's mark) */
    (void)*(volatile const char *)BUILD_TAG;        /* referenced, so --gc-sections keeps the tag in the image */
    crash_stage(ST_BOOT);
    IWDG->KR = 0x5555; IWDG->PR = 4; IWDG->RLR = 1000; IWDG->KR = 0xCCCC; IWDG->KR = 0xAAAA;

    clock_init();
    SysTick_Config(84000u);
    us_clock_init();
    usb_pins_init();
    NVIC_SetPriority(OTG_FS_IRQn, 2);
    tusb_rhport_init_t dev_init = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_AUTO};
    tusb_init(0, &dev_init);
    oled_init();
    fire_hw_init();
    hm_init(fb, led_level, led_fine, LED_FRAMES);   /* v0.33 ORC mode's decoders (nothing shows until it is on) */
    load_state();
    daw_init(G.daw_proto % 3);                      /* v0.36b: listens on the DAW port from boot (DAW mode or not) */
    if (!G.pad_hold_v28) { G.pad_hold = PAD_HOLD_DEFAULT; G.pad_hold_v28 = 1; }   /* v0.28: a save from before the hold-off (or none) gets 20 ms */
    G.pad_hold %= 6;
    G.uni_page %= UNI_PAGES; G.uni_bank &= 3;       /* v0.31 */
    uni_latch_init(uni_lat);
    for (int p = 0; p < UNI_PAGES; p++) uni_values_reset(p);   /* knobs at their start values, toggles off */
    kc_init_copies();                               /* v0.29: the per-section knob banks start as the STEP ones */
    memset(kb_val, 64, sizeof kb_val);
    crash_load(&crash_boot);
    if (crash_boot.valid) cat(status_line, crash_boot.cause == CRASH_WDOG ? "WDOG RST" : "FAULT RST");   /* the header's status slot */
    {   /* v0.39: the crash record in flash (see crash_rec) */
        uint32_t len = 0;
        const uint8_t *r = store2_ptr(KIND_CRASH, &len);
        crash_rec_t f;
        bool have = r && len == sizeof f;
        if (have) memcpy(&f, r, sizeof f);
        have = have && f.magic == CRASH_REC_MAGIC;
        if (have) crash_rec = f;
        if (crash_boot.valid) {                     /* a crash in the registers: flash gets it unless it holds it already */
            crash_rec_t now = {CRASH_REC_MAGIC, {0}};
            memcpy(now.w, &crash_boot.valid, sizeof now.w);
            if (!have || memcmp(&f, &now, sizeof now)) { crash_rec = now; crash_pend = true; saver_start(); save_paint_only = true; }
        } else if (have && f.w[0] == 1) memcpy(&crash_boot.valid, f.w, sizeof f.w);   /* the registers lost it: flash's copy */
    }

    uint32_t next_beat = 0;
    uint32_t next_knobs = 0, next_draw = 0, dfu_at = 0, next_anim = 0;
    for (;;) {
        IWDG->KR = 0xAAAA;
        uint32_t now = us_now();
        if (loop_last_us) { uint32_t dl = now - loop_last_us; if (dl > loop_max_us) loop_max_us = dl; }   /* v0.31: the longest pass (07) */
        loop_last_us = now;
        if ((int32_t)(ms_ticks - next_beat) >= 0) { next_beat = ms_ticks + 1000; RTC->BKP12R = ms_ticks; }   /* uptime, for a watchdog record */
        crash_stage(ST_USB);
        tud_task();
        midi_task(now);

        if (ext_clock && ms_ticks - ext_last_ms > 500) { ext_clock = false; if (ext_playing) stop(false); dirty = true; }

        crash_stage(ST_SEQ);
        if (playing) {
            /* step boundaries on the straight grid; swing delays only odd steps' notes;
               6 clock ticks per step anchored to each boundary (no drift) */
            if ((int32_t)(now - grid_us) >= 0) {
                if (pending >= 0) { play_step((uint32_t)pending, now); }
                step_start_us = grid_us;
                grid_us += step_us();
                if ((int32_t)(now - grid_us) > 0) { step_start_us = now; grid_us = now + step_us(); }
                pending = (int)gstep;
                gstep++;
                clock_k = 0;
            }
            if (pending >= 0 && (int32_t)(now - (step_start_us + ((pending & 1) ? swing_us() : 0))) >= 0) {
                play_step((uint32_t)pending, now);
                pending = -1;
            }
            if (G.clock_out && clock_k < 6 && (int32_t)(now - (step_start_us + (uint32_t)clock_k * step_us() / 6)) >= 0) {
                midi_send1(0xF8);
                clock_k++;
            }
        }
        crash_stage(ST_QUEUES);
        for (int i = 0; i < NOFFS; i++)
            if (offs[i].live && (int32_t)(now - offs[i].at_us) >= 0) { batch_add((uint8_t)(0x80 | offs[i].chan), offs[i].note, 0); offs[i].live = false; }
        for (int i = 0; i < NRATQ; i++)
            if (ratq[i].live && (int32_t)(now - ratq[i].at_us) >= 0) {
                seq_note_on(ratq[i].chan, ratq[i].note, ratq[i].vel);
                add_off(ratq[i].note, ratq[i].chan, now + ratq[i].gate_us);
                ratq[i].live = false;
            }
        batch_flush();

        crash_stage(ST_EVENTS);
        if (hostm) hostm_events(); else if (dawm) dawm_events(); else if (paintm) paintm_events(); else handle_events(now);   /* v0.33 ORC mode / v0.36 DAW mode own every press */

        crash_stage(ST_GATES);
        /* (v0.31r: the idle light start that lived here is gone with the shows; nothing lights the pads by itself) */

        if (ctl_lazy_due(save_lazy_due, ms_ticks) && !playing && !ext_playing) {   /* settings changed while stopped */
            save_lazy_due = 0;
            if ((global_dirty || (pat_dirty & ~pat_stuck)) && !save_due && !save_run) save_due = ms_ticks ? ms_ticks : 1;
        }
        if (save_due && (int32_t)(ms_ticks - save_due) >= 0 && G.autosave_off && !save_force) {
            save_due = 0;                           /* AUTOSAVE off: the change stays marked (*) until SAVE NOW */
            if ((paint_pend || crash_pend) && !save_run) { saver_start(); save_paint_only = true; }   /* v0.38b / v0.39: a failed painting
                                                       or crash record retries alone */
        }
        if ((save_due && (int32_t)(ms_ticks - save_due) >= 0) || (save_waiting_compact && !playing && !ext_playing && !save_run))
            saver_start();
        {
            /* flash writes stall the CPU briefly (and a lights slice takes time): never right before a step or a swung note is due */
            bool quiet = true;
            if (playing) {
                uint32_t nxt = grid_us;
                if (pending >= 0) { uint32_t d2 = step_start_us + ((pending & 1) ? swing_us() : 0); if ((int32_t)(d2 - nxt) < 0) nxt = d2; }
                quiet = (int32_t)(nxt - now) > 1500;
            } else if (ext_playing && ext_step_len) {   /* external clock: the odd step comes at (swing: after) half a pair, the next even one a pair on */
                uint32_t nxt = ext_even_at + (ext_odd_due ? ext_step_len : 2 * ext_step_len);
                quiet = (int32_t)(nxt - now) > 1500;    /* (an odd step that is due but swung late: not quiet until it plays) */
            }
            crash_stage(ST_SAVER);
            saver_poll(quiet);
            uni_poll(quiet);                        /* v0.31: a host page write, streamed into flash */
        }
        crash_stage(ST_KNOBS);
        if ((int32_t)(ms_ticks - next_knobs) >= 0) { next_knobs = ms_ticks + 5; fire_knobs_poll(); }

        if ((int32_t)(ms_ticks - next_anim) >= 0) { next_anim = ms_ticks + (mode == MODE_DRUM ? 40 : 150); dirty = true; }   /* pulses + blinks */
        status_brief_tick();                        /* v0.37d: "DAW off" / "paint off" go after 2 s */
        crash_stage(ST_PAINT);
        if (dirty && !hostm) paint();               /* v0.33: in ORC mode the ORC paints */
        crash_stage(ST_RENDER);
        oled_busy();
        if (hostm) { if (hostm_flush && oled_flush()) hostm_flush = false; }   /* the ORC's bitmap, as it arrived */
        else if (dirty && (int32_t)(ms_ticks - next_draw) >= 0) {
            render();
            if (oled_flush()) { dirty = false; next_draw = ms_ticks + 33; }
        }

        crash_stage(ST_TAIL);
        if (dfu_requested && !dfu_at) dfu_at = ms_ticks + 50;
        if (dfu_at && (int32_t)(ms_ticks - dfu_at) >= 0) reboot_to_dfu();
    }
}
