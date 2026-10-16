/* Akai's bootloader only starts the app if this sits at 0x0801FFF0 (see fire_app.ld). */
__attribute__((section(".token"), used))
const char fire_boot_token[16] = "EndOfFlashToken";
