/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Renders the keyboard's screen on the host: this firmware's app/ code on
 * Open's simulator (web/engine), driven by a script. tools/screen/run.sh
 * builds and runs it; each `shot` becomes a PNG.
 *
 *   boot                          start the simulator, Open's home page
 *   wait <ms>                     run the instrument for a while
 *   sysex <hex bytes...>          send a SysEx frame on USB
 *   version <1|2>                 the version the commands below send (1 at first)
 *   clear <colour>                send Hindsight's CLEAR
 *   text <x> <y> <scale> <colour> [<back>, version 2] <words...>   its TEXT
 *   box <x> <y> <w> <h> <colour>  its BOX, version 2
 *   bar <x> <y> <w> <h> <value> <colour> <back>   its BAR, version 2
 *   led pad <n> <red> <green> <blue> / led button <id> <0|1|2>   its LED
 *   batch ... end                 the clears, texts, boxes, bars and leds between
 *                                 go in one BATCH, sent at `end`
 *   strict                        from here, an ack of Hindsight's that isn't OK fails
 *   pixel <x> <y> <rgb565 hex>    the screen's pixel at (x, y) is this colour
 *   light pad <n> <rrggbb hex>|open / light button <id> <0|1|open>
 *                                 the Pi's light on the panel is this, or the keyboard's
 *   press <button> / release <button>   an SMK-37 button by physical id (HOME is 2)
 *   button|key|pad|pressure|knob|fader <id> <value>   any panel event
 *   expect <hex bytes...>         the keyboard sent this since the last expect
 *   unsent <hex bytes...>         the keyboard didn't send this since the last expect
 *   downloads <n>                 the keyboard has asked for download mode n times
 *   bands <hex mask>              the LCD's 16-row bands written since the last
 *                                 `bands` were exactly these (bit b: rows 16b-16b+15)
 *   shot <name>                   save the screen as <out>/<name>.rgb565
 *
 * Everything the keyboard sends is printed as it arrives. A failed expect
 * makes the run exit 1. */
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void sim_boot(unsigned clear);
const float *sim_render(void);
const uint16_t *sim_pixels(void);
uint8_t *sim_file(void);
int sim_midi(unsigned bytes);
void sim_midi_capture(unsigned enable);
unsigned sim_midi_output(void);
int sim_smk37(unsigned kind, unsigned id, int value);
unsigned sim_download_requests(void);
unsigned sim_frame_changes(void);
/* app/hindsight.h's lights: the panel's board code isn't in the simulator. */
uint32_t hindsight_pad(unsigned pad, uint32_t open);
int hindsight_button(unsigned id);
/* smk37_controls.h's kinds. */
enum { SMK37_BUTTON, SMK37_KEY, SMK37_PRESSURE, SMK37_PAD, SMK37_ENCODER, SMK37_FADER = 7 };

/* What the keyboard sent since the last expect: the latest 128, without the
 * clock and active sensing it sends all along. */
static unsigned seq, failures, seen_count, strict;
static char seen[128][64];
static void acks(void)
{
    for (unsigned r; (r = sim_midi_output());) {
        unsigned n = r & 255U, at = 0;
        const uint8_t *b = sim_file();
        if (n == 1 && b[0] >= 0xf8) continue;
        char hex[64] = "";
        for (unsigned i = 0; i < n && at + 4 < sizeof hex; ++i) at += (unsigned)snprintf(hex + at, sizeof hex - at, i ? " %02X" : "%02X", b[i]);
        printf("  from the keyboard (port %u): %s\n", r >> 8, hex);
        if (strict && n >= 9 && b[0] == 0xf0 && b[1] == 0x7d && b[2] == 'H' && b[3] == 'S' && b[6] == 0x7f && b[7]) {
            printf("  NOT OK: status %u\n", b[7]);
            ++failures;
        }
        strcpy(seen[seen_count++ % 128], hex);
    }
}
static void run(unsigned ms)
{
    for (unsigned i = 0; i <= ms * 441U / 1280U; ++i) { (void)sim_render(); acks(); }
}
static void send(const uint8_t *m, unsigned n)
{
    memcpy(sim_file(), m, n);
    if (sim_midi(n)) printf("  refused by the simulator\n");
    acks();
}

/* Hindsight's commands: sent alone, or gathered between `batch` and `end`. */
static unsigned version = 1, batching, batch_length;
static uint8_t batch[1024];
static void command(unsigned cmd, const uint8_t *p, unsigned n)
{
    if (batching) {
        if (batch_length + 2 + n > sizeof batch - 8) { printf("  the batch is full\n"); ++failures; return; }
        batch[batch_length++] = (uint8_t)cmd;
        batch[batch_length++] = (uint8_t)n;
        memcpy(batch + batch_length, p, n);
        batch_length += n;
        return;
    }
    uint8_t m[1024] = {0xf0, 0x7d, 'H', 'S', (uint8_t)version, (uint8_t)(++seq & 127), (uint8_t)cmd};
    memcpy(m + 7, p, n);
    m[7 + n] = 0xf7;
    send(m, 8 + n);
}
/* Fourteen-bit numbers, as the protocol carries them. */
static unsigned put14(uint8_t *p, const unsigned *v, unsigned n)
{
    for (unsigned i = 0; i < n; ++i) { p[2 * i] = (uint8_t)(v[i] & 127); p[2 * i + 1] = (uint8_t)(v[i] >> 7); }
    return 2 * n;
}

int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "usage: screen SCRIPT OUTDIR\n"); return 2; }
    FILE *script = fopen(argv[1], "r");
    if (!script) { perror(argv[1]); return 2; }
    char line[4096];
    while (fgets(line, sizeof line, script)) {
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        char word[16] = "";
        int used = 0;
        if (sscanf(line, "%15s %n", word, &used) < 1) continue;
        char *rest = line + used;
        rest[strcspn(rest, "\r\n")] = 0;
        for (size_t end = strlen(rest); end && (rest[end - 1] == ' ' || rest[end - 1] == '\t');) rest[--end] = 0;
        printf("%s %s\n", word, rest);
        static const char *const kinds[] = {"button", "key", "pressure", "pad", "knob", "", "", "fader"};
        int kind = -1;
        for (int k = 0; k < 8; ++k) if (*kinds[k] && !strcmp(word, kinds[k])) kind = k;
        if (!strcmp(word, "boot")) { sim_boot(1); sim_midi_capture(1); seen_count = 0; }
        else if (!strcmp(word, "wait")) run((unsigned)atoi(rest));
        else if (!strcmp(word, "sysex")) {
            uint8_t m[1024]; unsigned n = 0, byte; int at = 0;
            while (n < sizeof m && sscanf(rest, "%x %n", &byte, &at) == 1) { m[n++] = (uint8_t)byte; rest += at; }
            send(m, n);
        } else if (!strcmp(word, "version")) version = (unsigned)atoi(rest);
        else if (!strcmp(word, "strict")) strict = 1;
        else if (!strcmp(word, "batch")) { batching = 1; batch_length = 0; }
        else if (!strcmp(word, "end")) {
            batching = 0;
            command(9, batch, batch_length);
        } else if (!strcmp(word, "clear")) {
            const uint8_t colour = (uint8_t)atoi(rest);
            command(1, &colour, 1);
        } else if (!strcmp(word, "text")) {
            unsigned v[2], scale, colour, back = 0; int at = 0;
            int got = version == 1 ? sscanf(rest, "%u %u %u %u %n", &v[0], &v[1], &scale, &colour, &at)
                                   : sscanf(rest, "%u %u %u %u %u %n", &v[0], &v[1], &scale, &colour, &back, &at);
            if (got < (version == 1 ? 4 : 5)) { printf("  bad text line\n"); continue; }
            uint8_t p[64];
            unsigned n = put14(p, v, 2), length = (unsigned)strlen(rest + at);
            p[n++] = (uint8_t)scale; p[n++] = (uint8_t)colour;
            if (version > 1) p[n++] = (uint8_t)back;
            /* Underscores are spaces, so a line can start or end with one. */
            for (unsigned i = 0; i < length && i < 40; ++i) p[n++] = rest[at + i] == '_' ? ' ' : (uint8_t)rest[at + i];
            command(2, p, n);
        } else if (!strcmp(word, "box") || !strcmp(word, "bar")) {
            unsigned v[4], value = 127, colour, back = 0, bar = word[1] == 'a';
            int got = bar ? sscanf(rest, "%u %u %u %u %u %u %u", &v[0], &v[1], &v[2], &v[3], &value, &colour, &back)
                          : sscanf(rest, "%u %u %u %u %u", &v[0], &v[1], &v[2], &v[3], &colour);
            if (got != (bar ? 7 : 5)) { printf("  bad %s line\n", word); continue; }
            uint8_t p[16];
            unsigned n = put14(p, v, 4);
            if (bar) p[n++] = (uint8_t)value;
            p[n++] = (uint8_t)colour;
            if (bar) p[n++] = (uint8_t)back;
            command(bar ? 7 : 6, p, n);
        } else if (!strcmp(word, "led")) {
            char what[8]; unsigned id, a, b = 0, c = 0;
            int got = sscanf(rest, "%7s %u %u %u %u", what, &id, &a, &b, &c);
            if (!strcmp(what, "pad") && got == 5) { const uint8_t p[] = {(uint8_t)id, (uint8_t)a, (uint8_t)b, (uint8_t)c}; command(8, p, 4); }
            else if (!strcmp(what, "button") && got == 3) { const uint8_t p[] = {(uint8_t)(16 + id), (uint8_t)a}; command(8, p, 2); }
            else printf("  bad led line\n");
        } else if (!strcmp(word, "pixel")) {
            unsigned x, y, want;
            if (sscanf(rest, "%u %u %x", &x, &y, &want) != 3 || x >= 240 || y >= 240) { printf("  bad pixel line\n"); continue; }
            unsigned got = sim_pixels()[y * 240 + x];
            if (got != want) { printf("  PIXEL (%u, %u) IS %04X, NOT %04X\n", x, y, got, want); ++failures; }
        } else if (!strcmp(word, "light")) {
            char what[8], want[16]; unsigned id;
            if (sscanf(rest, "%7s %u %15s", what, &id, want) != 3) { printf("  bad light line\n"); continue; }
            long got = !strcmp(what, "pad") ? (long)hindsight_pad(id, 0xffffffffU) : hindsight_button(id);
            long expected = !strcmp(want, "open") ? (!strcmp(what, "pad") ? 0xffffffffL : -1L) : strtol(want, NULL, 16);
            if (got != expected) { printf("  LIGHT %s %u IS %lX, NOT %s\n", what, id, (unsigned long)got, want); ++failures; }
        } else if (!strcmp(word, "press") || !strcmp(word, "release") || kind >= 0) {
            unsigned id = 0; int value = 0;
            if (kind < 0) { kind = SMK37_BUTTON; id = (unsigned)atoi(rest); value = word[0] == 'p'; }
            else if (sscanf(rest, "%u %d", &id, &value) != 2) { printf("  bad control line\n"); continue; }
            if (sim_smk37((unsigned)kind, id, value)) printf("  refused by the simulator\n");
            acks();
        } else if (!strcmp(word, "expect") || !strcmp(word, "unsent")) {
            char want[64] = ""; unsigned at = 0, byte; int used_hex = 0;
            for (char *p = rest; at + 4 < sizeof want && sscanf(p, "%x %n", &byte, &used_hex) == 1; p += used_hex)
                at += (unsigned)snprintf(want + at, sizeof want - at, at ? " %02X" : "%02X", byte);
            unsigned found = 0;
            for (unsigned i = 0; i < seen_count && i < 128 && !found; ++i) found = !strcmp(seen[i], want);
            if (word[0] == 'e' && !found) { printf("  EXPECTED, NOT SENT: %s\n", want); ++failures; }
            if (word[0] == 'u' && found) { printf("  SENT, NOT EXPECTED: %s\n", want); ++failures; }
            if (word[0] == 'e') seen_count = 0;
        } else if (!strcmp(word, "downloads")) {
            unsigned want = (unsigned)atoi(rest), got = sim_download_requests();
            if (got != want) { printf("  DOWNLOAD MODE ASKED FOR %u TIMES, NOT %u\n", got, want); ++failures; }
        } else if (!strcmp(word, "bands")) {
            unsigned want = (unsigned)strtoul(rest, NULL, 16), got = sim_frame_changes();
            if (got != want) { printf("  BANDS WRITTEN %04X, NOT %04X\n", got, want); ++failures; }
        } else if (!strcmp(word, "shot")) {
            char path[1024];
            snprintf(path, sizeof path, "%s/%s.rgb565", argv[2], rest);
            FILE *out = fopen(path, "wb");
            if (!out || fwrite(sim_pixels(), 2, 240 * 240, out) != 240 * 240) { perror(path); return 1; }
            fclose(out);
        } else printf("  unknown command\n");
    }
    fclose(script);
    if (failures) printf("%u expectations failed\n", failures);
    return failures ? 1 : 0;
}
