/*
 * Demo: ffplay Clock logic (set_clock / get_clock)
 *
 * Build:  cc -o clock_demo clock_demo.c -lm
 * Run:    ./clock_demo
 */

#include <stdio.h>
#include <stdint.h>
#include <math.h>

typedef struct Clock {
    double pts;
    double pts_drift;
    double last_updated;
    double speed;
    int serial;
    int paused;
    int queue_serial;
} Clock;

/* Simulated monotonic clock (microseconds internally, like FFmpeg) */
static int64_t fake_time_us = 100000000LL; /* start at 100 seconds */

static double now_sec(void)
{
    return fake_time_us / 1000000.0;
}

static void advance_us(int64_t us)
{
    fake_time_us += us;
}

static void set_clock_at(Clock *c, double pts, int serial, double time)
{
    c->pts = pts;
    c->last_updated = time;
    c->pts_drift = c->pts - time;
    c->serial = serial;
}

static void set_clock(Clock *c, double pts, int serial)
{
    set_clock_at(c, pts, serial, now_sec());
}

static void init_clock(Clock *c)
{
    c->speed = 1.0;
    c->paused = 0;
    c->queue_serial = 0;
    set_clock(c, NAN, -1);
}

static double get_clock(Clock *c)
{
    if (c->queue_serial != c->serial)
        return NAN;
    if (c->paused) {
        return c->pts;
    } else {
        double time = now_sec();
        return c->pts_drift + time - (time - c->last_updated) * (1.0 - c->speed);
    }
}

static void print_clock(const char *label, Clock *c)
{
    double clk = get_clock(c);
    printf("%-28s | pts=%.1f last_upd=%.1f drift=%.1f speed=%.1f | get_clock=",
           label, c->pts, c->last_updated, c->pts_drift, c->speed);
    if (isnan(clk))
        printf("NAN\n");
    else
        printf("%.3f\n", clk);
}

static void simulate_seek(Clock *c, int new_serial)
{
    printf("\n--- SEEK: queue serial %d -> %d (clock serial still %d) ---\n",
           c->queue_serial, new_serial, c->serial);
    c->queue_serial = new_serial;
}

int main(void)
{
    Clock vidclk;

    printf("=== ffplay Clock Demo ===\n\n");

    /* --- Part 1: normal playback at speed 1.0 --- */
    printf("========== Part 1: Normal play (speed=1.0) ==========\n");
    init_clock(&vidclk);
    vidclk.queue_serial = 3;
    set_clock(&vidclk, 10.0, 3);

    print_clock("Just updated (frame @ 10s)", &vidclk);

    advance_us(100000); /* +0.1s */
    print_clock("After +0.1s real time", &vidclk);

    advance_us(400000); /* +0.4s, total +0.5s */
    print_clock("After +0.5s real time", &vidclk);

    advance_us(1500000); /* +1.5s, total +2.0s */
    print_clock("After +2.0s real time", &vidclk);

    /* --- Part 2: pause --- */
    printf("\n========== Part 2: Pause ==========\n");
    vidclk.paused = 1;
    print_clock("Paused immediately", &vidclk);

    advance_us(3000000); /* +3s while paused */
    print_clock("Still paused after +3s", &vidclk);

    vidclk.paused = 0;
    print_clock("Resumed (no new set_clock)", &vidclk);

    /* --- Part 3: slow motion speed=0.5 --- */
    printf("\n========== Part 3: Slow motion (speed=0.5) ==========\n");
    init_clock(&vidclk);
    vidclk.queue_serial = 3;
    vidclk.speed = 0.5;
    set_clock(&vidclk, 20.0, 3);

    print_clock("Start @ 20s, speed=0.5", &vidclk);

    advance_us(4000000); /* +4s real */
    print_clock("After +4s real time", &vidclk);
    printf("  (real 4s x speed 0.5 => playback +2s => expect ~22.0)\n");

    /* --- Part 4: seek makes clock obsolete --- */
    printf("\n========== Part 4: Seek (clock obsolete) ==========\n");
    simulate_seek(&vidclk, 4);
    print_clock("After seek, old clock", &vidclk);

    set_clock(&vidclk, 60.0, 4);
    print_clock("New frame @ 60s, serial=4", &vidclk);

    advance_us(500000);
    print_clock("After +0.5s at new position", &vidclk);

    printf("\nDone.\n");
    return 0;
}
