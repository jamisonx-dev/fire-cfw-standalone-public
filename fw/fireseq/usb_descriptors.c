/* USB descriptors for the "hello" Fire firmware: one MIDI port.
 * VID/PID 1209:0001 = pid.codes' ID reserved for testing, so the ORC's brain
 * never mistakes this for a stock Fire (09e8:0043). */
#include <string.h>
#include "tusb.h"

static tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = 0x1209,
    .idProduct          = 0x0001,
    .bcdDevice          = 0x0001,
    .iManufacturer      = 1,
    .iProduct           = 2,
    .iSerialNumber      = 3,
    .bNumConfigurations = 1,
};

uint8_t const *tud_descriptor_device_cb(void) { return (uint8_t const *)&desc_device; }

/* v0.36 (DAW mode): TWO MIDI ports on one interface. Cable 0 = the sequencer (exactly what the single port was:
   its notes, clock, the ORC's host SysEx); cable 1 = DAW control (Mackie Control / HUI), so a DAW never reads the
   sequencer's notes as button presses. Each jack carries its own name (string 4 / 5). */
enum { ITF_NUM_MIDI = 0, ITF_NUM_MIDI_STREAMING, ITF_NUM_TOTAL };
#define MIDI_CABLES 2
#define MIDI_DESC_LEN_N (TUD_MIDI_DESC_HEAD_LEN + MIDI_CABLES * TUD_MIDI_DESC_JACK_LEN + 2 * TUD_MIDI_DESC_EP_LEN(MIDI_CABLES))
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + MIDI_DESC_LEN_N)
#define EPNUM_MIDI_OUT 0x01
#define EPNUM_MIDI_IN  0x81
#define STR_JACK_SEQ 4
#define STR_JACK_DAW 5

static uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 500),
    TUD_MIDI_DESC_HEAD(ITF_NUM_MIDI, 0, MIDI_CABLES),
    TUD_MIDI_DESC_JACK_DESC(1, STR_JACK_SEQ),
    TUD_MIDI_DESC_JACK_DESC(2, STR_JACK_DAW),
    TUD_MIDI_DESC_EP(EPNUM_MIDI_OUT, 64, MIDI_CABLES), TUD_MIDI_JACKID_IN_EMB(1), TUD_MIDI_JACKID_IN_EMB(2),
    TUD_MIDI_DESC_EP(EPNUM_MIDI_IN, 64, MIDI_CABLES), TUD_MIDI_JACKID_OUT_EMB(1), TUD_MIDI_JACKID_OUT_EMB(2),
};
_Static_assert(sizeof desc_configuration == CONFIG_TOTAL_LEN, "MIDI descriptor length");

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) { (void)index; return desc_configuration; }

static char const *string_desc_arr[] = {
    (const char[]){0x09, 0x04},   /* English */
    "FLAME",                      /* maker. v0.36: the firmware is "Akai Fire FLAME custom firmware" (was "ORC-1") */
    "FLAME",                      /* the device. v0.36: was "ORC FW seq"; now the PORTS carry seq / DAW. Linux names a
                                     port "<device> <jack>" unless the jack name starts with the device name, so with
                                     "FLAME" the ports should be exactly "FLAME seq" + "FLAME DAW" (measured v0.36p:
                                     with the device still "ORC FW seq", port 1 was "ORC FW seq ORC FW DAW", which every
                                     seq-port matcher on the ORC also caught) */
    "",                           /* 3 = the serial number (serial_from_uid) */
    "FLAME seq",                  /* 4 = cable 0's jacks: the sequencer */
    "FLAME DAW",                  /* 5 = cable 1's jacks: Mackie Control / HUI */
};

static uint16_t _desc_str[33];

/* Serial number = the chip's 96-bit unique ID (0x1FFF7A10) in hex. */
static size_t serial_from_uid(uint16_t *out, size_t max)
{
    const uint8_t *uid = (const uint8_t *)0x1FFF7A10;
    static const char hex[] = "0123456789ABCDEF";
    size_t n = 0;
    for (int i = 0; i < 12 && n + 2 <= max; i++) {
        out[n++] = hex[uid[i] >> 4];
        out[n++] = hex[uid[i] & 15];
    }
    return n;
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    size_t n;
    if (index == 0) {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        n = 1;
    } else if (index == 3) {
        n = serial_from_uid(_desc_str + 1, 32);
    } else {
        if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) return NULL;
        const char *s = string_desc_arr[index];
        n = strlen(s);
        if (n > 32) n = 32;
        for (size_t i = 0; i < n; i++) _desc_str[1 + i] = s[i];
    }
    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * n + 2));
    return _desc_str;
}
