/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Renders the keyboard's screen on the host: this firmware's app/ code on
 * Open's simulator (web/engine), driven by a script. tools/screen/run.sh
 * builds and runs it; each `shot` becomes a PNG.
 *
 *   boot                          start the simulator, Open's home page
 *   wait <ms>                     run the instrument for a while
 *   sysex <hex bytes...>          send a SysEx frame on USB
 *   text <x> <y> <scale> <colour> <words...>   send Hindsight's TEXT
 *   clear <colour>                send Hindsight's CLEAR
 *   press <button> / release <button>   an SMK-37 button by physical id (HOME is 2)
 *   button|key|pad|pressure|knob|fader <id> <value>   any panel event
 *   expect <hex bytes...>         the keyboard sent this since the last check
 *   unsent <hex bytes...>         the keyboard didn't send this since the last check
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
/* smk37_controls.h's kinds. */
enum { SMK37_BUTTON, SMK37_KEY, SMK37_PRESSURE, SMK37_PAD, SMK37_ENCODER, SMK37_FADER = 7 };

/* What the keyboard sent since the last expect: the latest 128, without the
 * clock and active sensing it sends all along. */
static unsigned seq, failures, seen_count;
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

int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "usage: screen SCRIPT OUTDIR\n"); return 2; }
    FILE *script = fopen(argv[1], "r");
    if (!script) { perror(argv[1]); return 2; }
    char line[512];
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
            uint8_t m[256]; unsigned n = 0, byte; int at = 0;
            while (n < sizeof m && sscanf(rest, "%x %n", &byte, &at) == 1) { m[n++] = (uint8_t)byte; rest += at; }
            send(m, n);
        } else if (!strcmp(word, "clear")) {
            uint8_t m[] = {0xf0, 0x7d, 'H', 'S', 1, (uint8_t)(++seq & 127), 1, (uint8_t)atoi(rest), 0xf7};
            send(m, sizeof m);
        } else if (!strcmp(word, "text")) {
            unsigned x, y, scale, colour; int at = 0;
            if (sscanf(rest, "%u %u %u %u %n", &x, &y, &scale, &colour, &at) < 4) { printf("  bad text line\n"); continue; }
            uint8_t m[64] = {0xf0, 0x7d, 'H', 'S', 1, (uint8_t)(++seq & 127), 2,
                             (uint8_t)(x & 127), (uint8_t)(x >> 7), (uint8_t)(y & 127), (uint8_t)(y >> 7),
                             (uint8_t)scale, (uint8_t)colour};
            unsigned n = (unsigned)strlen(rest + at);
            if (n > 40) n = 40;
            memcpy(m + 13, rest + at, n);
            m[13 + n] = 0xf7;
            send(m, 14 + n);
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
            seen_count = 0;
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
