/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Host test for unit_regions.h. Compiling it evaluates instrument.h's
 * asserts; running it prints the areas the app writes and checks that the
 * sample bank ends where the unit's own regions begin. Built by tests/run.sh. */
#include <stdio.h>
#include "instrument.h"

int main(void)
{
    const unsigned base = UNIT_USER_BASE;
    printf("projects, settings, staging 0x%05X-0x%05X\n", base, base + GROOVE_SAMPLE_OFFSET);
    printf("new sample banks            0x%05X-0x%05X\n", base + GROOVE_SAMPLE_OFFSET,
           base + GROOVE_SAMPLE_OFFSET + GROOVE_SAMPLE_LIMIT);
    printf("unit's own (never written)  0x%05X-0x%05X\n", UNIT_REGIONS_START, UNIT_REGIONS_END);
    printf("DX7 banks, tuning           0x%05X-0x%05X\n", base + GROOVE_LIBRARY_OFFSET, base + GROOVE_FLASH_BYTES);
    if (base + GROOVE_SAMPLE_OFFSET + GROOVE_SAMPLE_LIMIT != UNIT_REGIONS_START || UNIT_REGIONS_CLEAR(0x43FFF, 2) ||
        UNIT_REGIONS_CLEAR(0x45FFF, 1) || !UNIT_REGIONS_CLEAR(0x46000, 1) || UNIT_REGIONS_CLEAR(0x82FFF, 2)) {
        printf("regions: FAIL\n");
        return 1;
    }
    printf("regions: ok\n");
    return 0;
}
