/* Host test: render the firmware's screen into a PBM image (run on the Pi with plain gcc).
 *   gcc -O2 -o draw_test draw_test.c draw.c screen.c && ./draw_test > screen.pbm */
#include <stdio.h>
#include <string.h>
#include "draw.h"
#include "screen.h"

int main(void)
{
    strcpy(screen_lines[0], "Hello from the Pi!");
    strcpy(screen_lines[1], "abcdefg xyz 0123:;");
    screen_render(1234, "90 3C 64");
    printf("P1\n%d %d\n", FB_W, FB_H);
    for (int y = 0; y < FB_H; y++) {
        for (int x = 0; x < FB_W; x++) putchar(fb[(y >> 3) * FB_W + x] >> (y & 7) & 1 ? '1' : '0');
        putchar('\n');
    }
    return 0;
}
