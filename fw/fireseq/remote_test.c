/* Host test for remote.c (v0.31r remote input), on the Pi:
 *   gcc -O1 -Wall -Wextra -fsanitize=bounds,undefined -fsanitize-undefined-trap-on-error -o remote_test remote_test.c remote.c && ./remote_test */
#include <stdio.h>
#include <string.h>
#include "remote.h"

#define EXPECTED 47
static int fails, checks;
#define CHECK(c, msg) do { checks++; if (!(c)) { printf("FAIL: %s\n", msg); fails++; } } while (0)

static int cmd(rin_t *r, int sub, uint32_t now, int n, int a, int b, int c)
{
    uint8_t d[3] = {(uint8_t)a, (uint8_t)b, (uint8_t)c};
    return rin_cmd(r, sub, d, (unsigned)n, now);
}
static int arm(rin_t *r, int on, uint32_t now) { return cmd(r, RIN_ARM, now, 1, on, 0, 0); }
static int press(rin_t *r, int idx, int down, uint32_t now) { return cmd(r, RIN_PRESS, now, 2, idx, down, 0); }
static int turn(rin_t *r, int k, int dir, int count, uint32_t now) { return cmd(r, RIN_TURN, now, 3, k, dir, count); }
static int drain(rin_t *r) { int n = 0; while (rin_event(r) >= 0) n++; return n; }

int main(void)
{
    rin_t r;
    memset(&r, 0, sizeof r);

    /* off at power-on: nothing gets in */
    CHECK(press(&r, 79, 1, 0) == RIN_NOT_ARMED && rin_event(&r) == -1 && !rin_held(&r, 79), "not armed: a press is refused, no event, not held");
    CHECK(turn(&r, 0, 1, 3, 0) == RIN_NOT_ARMED && rin_turn_pending(&r, 0) == 0, "not armed: a turn is refused");
    CHECK(cmd(&r, RIN_RELEASE_ALL, 0, 0, 0, 0, 0) == RIN_NOT_ARMED, "not armed: release-all is refused");
    CHECK(cmd(&r, 4, 0, 0, 0, 0, 0) == RIN_BAD && cmd(&r, 127, 0, 0, 0, 0, 0) == RIN_BAD, "an unknown sub-command is BAD");
    CHECK(arm(&r, 2, 0) == RIN_BAD && !r.armed, "20 00 02 is BAD and does not arm");
    CHECK(cmd(&r, RIN_ARM, 0, 0, 0, 0, 0) == RIN_BAD && cmd(&r, RIN_ARM, 0, 2, 1, 0, 0) == RIN_BAD && !r.armed, "arm with the wrong length is BAD");
    CHECK(rin_tick(&r, 999999) == 0, "not armed: the timeout never fires");

    /* press / release */
    uint32_t t = 1000;
    CHECK(arm(&r, 1, t) == RIN_OK && r.armed, "20 00 01 arms");
    CHECK(press(&r, 79, 1, t) == RIN_OK && rin_held(&r, 79), "SHIFT pressed: held");
    CHECK(rin_event(&r) == (79 | 0x80) && rin_event(&r) == -1, "one press event, index | 0x80");
    CHECK(press(&r, 79, 1, t) == RIN_OK && rin_event(&r) == -1 && rin_held(&r, 79), "pressing a held input again: no second event");
    CHECK(press(&r, 79, 0, t) == RIN_OK && !rin_held(&r, 79) && rin_event(&r) == 79, "release: a release event, not held");
    CHECK(press(&r, 79, 0, t) == RIN_OK && rin_event(&r) == -1, "releasing an input not held: no event");
    CHECK(press(&r, 0, 1, t) == RIN_OK && rin_event(&r) == 0x80 && press(&r, 0, 0, t) == RIN_OK && rin_event(&r) == 0, "input 0 (a pad) works");
    CHECK(press(&r, 87, 1, t) == RIN_OK && rin_event(&r) == (87 | 0x80) && press(&r, 87, 0, t) == RIN_OK && rin_event(&r) == 87, "input 87 (ROW1) works");
    CHECK(press(&r, 68, 1, t) == RIN_OK && rin_event(&r) == (68 | 0x80), "input 68 (REC) works");
    press(&r, 68, 0, t); drain(&r);
    CHECK(press(&r, 69, 1, t) == RIN_BAD && press(&r, 70, 1, t) == RIN_BAD && press(&r, 71, 1, t) == RIN_BAD, "69-71 do not exist: BAD");
    CHECK(press(&r, 72, 1, t) == RIN_OK, "72 (ROW2) exists");
    press(&r, 72, 0, t); drain(&r);
    CHECK(press(&r, 88, 1, t) == RIN_BAD && press(&r, 127, 1, t) == RIN_BAD, "88+ is BAD");
    CHECK(press(&r, 5, 2, t) == RIN_BAD && !rin_held(&r, 5) && rin_event(&r) == -1, "a down byte of 2 is BAD, nothing happens");
    CHECK(cmd(&r, RIN_PRESS, t, 1, 5, 0, 0) == RIN_BAD && cmd(&r, RIN_PRESS, t, 3, 5, 1, 0) == RIN_BAD, "press with the wrong length is BAD");
    CHECK(rin_valid_input(0) && rin_valid_input(68) && !rin_valid_input(69) && !rin_valid_input(71) && rin_valid_input(72) && rin_valid_input(87)
          && !rin_valid_input(88) && !rin_valid_input(-1), "rin_valid_input: 0-68, 72-87");
    CHECK(!rin_held(&r, -1) && !rin_held(&r, 88) && !rin_held(&r, 1000), "rin_held out of range = 0");

    /* turns */
    CHECK(turn(&r, 2, 1, 5, t) == RIN_OK && turn(&r, 2, 0, 2, t) == RIN_OK && rin_turn_pending(&r, 2) == 3, "knob 3: +5 then -2 = 3 waiting");
    CHECK(rin_turn_take(&r, 2) == 3 && rin_turn_take(&r, 2) == 0, "take hands them out once");
    CHECK(turn(&r, RIN_SELECT, 0, 4, t) == RIN_OK && rin_turn_take(&r, RIN_SELECT) == -4, "SELECT anticlockwise 4 = -4");
    CHECK(turn(&r, 5, 1, 1, t) == RIN_BAD && turn(&r, 0, 2, 1, t) == RIN_BAD && turn(&r, 0, 1, 0, t) == RIN_BAD && turn(&r, 0, 1, 128, t) == RIN_BAD,
          "turner 5 / dir 2 / count 0 / count 128 are BAD");
    CHECK(rin_turn_pending(&r, 0) == 0 && rin_turn_take(&r, -1) == 0 && rin_turn_take(&r, 5) == 0 && rin_turn_pending(&r, 9) == 0, "a BAD turn adds nothing; out of range = 0");
    {
        int full = 0;
        for (int i = 0; i < 7; i++) full |= turn(&r, 1, 1, 127, t) != RIN_OK;
        CHECK(!full && rin_turn_pending(&r, 1) == 889 && turn(&r, 1, 1, 127, t) == RIN_FULL && rin_turn_pending(&r, 1) == 889,
              "at most 1000 detents waiting: the turn past it is FULL and adds nothing");
        rin_turn_take(&r, 1);
    }

    /* the quiet timeout: releases everything, disarms */
    t = 50000;
    arm(&r, 1, t);
    press(&r, 79, 1, t); press(&r, 64, 1, t); press(&r, 10, 1, t); drain(&r);
    turn(&r, 0, 1, 9, t);
    CHECK(rin_tick(&r, t + RIN_QUIET_MS - 1) == 0 && r.armed && rin_held(&r, 79), "quiet for 4.999 s: still armed, still held");
    CHECK(rin_tick(&r, t + RIN_QUIET_MS) == 1 && !r.armed, "quiet for 5 s: disarmed");
    CHECK(!rin_held(&r, 79) && !rin_held(&r, 64) && !rin_held(&r, 10), "... and nothing held");
    {
        int e1 = rin_event(&r), e2 = rin_event(&r), e3 = rin_event(&r), e4 = rin_event(&r);
        CHECK(e1 == 10 && e2 == 64 && e3 == 79 && e4 == -1, "... with a release event for each held input");
    }
    CHECK(rin_turn_pending(&r, 0) == 0, "... and the waiting turns dropped");
    CHECK(press(&r, 79, 1, t + 6000) == RIN_NOT_ARMED, "after the timeout a press is refused until armed again");

    /* any 20 command keeps it alive */
    t = 100000;
    arm(&r, 1, t);
    arm(&r, 1, t + 4000);
    CHECK(rin_tick(&r, t + 6000) == 0 && r.armed, "re-arming (the keepalive) restarts the quiet time");
    press(&r, 3, 1, t + 8000); drain(&r);
    CHECK(rin_tick(&r, t + 12000) == 0 && rin_tick(&r, t + 13000) == 1, "a press restarts it too");
    drain(&r);
    r.armed = 1; r.last_ms = 0xFFFFF000u;
    CHECK(rin_tick(&r, 0x00000100u) == 0 && rin_tick(&r, 0xFFFFF000u + RIN_QUIET_MS) == 1, "the timeout is right across the ms wrap");

    /* 20 00 00 and release-all */
    arm(&r, 1, t);
    press(&r, 82, 1, t); press(&r, 86, 1, t); drain(&r);
    CHECK(cmd(&r, RIN_RELEASE_ALL, t, 1, 0, 0, 0) == RIN_BAD, "release-all with data is BAD");
    CHECK(cmd(&r, RIN_RELEASE_ALL, t, 0, 0, 0, 0) == RIN_OK && r.armed && !rin_held(&r, 82) && !rin_held(&r, 86) && drain(&r) == 2,
          "release-all: both released (2 events), stays armed");
    press(&r, 82, 1, t); drain(&r);
    CHECK(arm(&r, 0, t) == RIN_OK && !r.armed && !rin_held(&r, 82) && rin_event(&r) == 82, "20 00 00: disarmed, its held input released");
    CHECK(arm(&r, 0, t) == RIN_OK && rin_event(&r) == -1, "disarming twice: nothing more");

    /* the queue always has room for releasing everything held */
    arm(&r, 1, t);
    {
        int taken = 0, i;
        for (i = 0; i < RIN_INPUTS; i++) {
            if (!rin_valid_input(i)) continue;
            int res = press(&r, i, 1, t);
            if (res == RIN_FULL) break;
            if (res == RIN_OK) taken++;
        }
        CHECK(i < RIN_INPUTS && taken == (RIN_QUEUE - 2) / 2, "undrained presses fill up: FULL before the queue overflows (7 held with 16)");
        CHECK(!rin_held(&r, i), "the refused press is not held");
        int presses = drain(&r);
        CHECK(presses == taken, "every accepted press has its event");
        CHECK(arm(&r, 0, t) == RIN_OK && drain(&r) == taken, "release-all after a full queue: a release event for every held input");
        /* the same, but releases racing presses with nothing drained */
        arm(&r, 1, t);
        for (int k = 0; k < 40; k++) { press(&r, k, 1, t); if (k % 3 == 0) press(&r, k, 0, t); }
        int held = 0; for (int k = 0; k < RIN_INPUTS; k++) held += rin_held(&r, k);
        int before = drain(&r);
        arm(&r, 0, t);
        CHECK(drain(&r) == held && before > 0, "mixed presses / releases: still a release for each held input");
    }

    printf("remote_test: %s (%d checks%s)\n", fails ? "FAILED" : "all passed", checks, checks == EXPECTED ? "" : " -- EXPECTED differs!");
    return (fails || checks != EXPECTED) ? 1 : 0;
}
