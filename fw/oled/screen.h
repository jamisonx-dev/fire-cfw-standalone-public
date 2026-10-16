#ifndef SCREEN_H
#define SCREEN_H

#define SCREEN_TEXT_LINES 2
#define SCREEN_TEXT_COLS  20

extern char screen_lines[SCREEN_TEXT_LINES][SCREEN_TEXT_COLS + 1];
void screen_render(unsigned uptime_s, const char *last_midi);

#endif
