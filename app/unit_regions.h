/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Added 2026-10-26 for Hindsight.
 *
 * The SDK's user partition is [0x7C000, 0xFF000) (sdk/Package.cs: FlashLimit
 * minus UserSize). It lies over flash that is the unit's own and that no
 * download can put back: the stock Bluetooth pairing at 0xC0000 and the
 * factory key and pad calibration at 0xC1000. The chip key and MAC follow
 * the partition at 0xFF000. Every area the app writes asserts that it keeps
 * clear of them (instrument.h, project.c, fm_library.c); tests/run.sh fails
 * on a Flash write anywhere else. The check is at compile time because the
 * Elite's image has 29 bytes to spare. */
#ifndef UNIT_REGIONS_H
#define UNIT_REGIONS_H

#define UNIT_USER_BASE 0x7C000U     /* physical address of user offset 0 */
#define UNIT_REGIONS_START 0xC0000U /* Bluetooth pairing, then calibration */
#define UNIT_REGIONS_END 0xC2000U
#define UNIT_KEY_START 0xFF000U     /* chip key and MAC */

/* Whether user-partition bytes [offset, offset + bytes) keep clear of them. */
#define UNIT_REGIONS_CLEAR(offset, bytes)                                   \
    (UNIT_USER_BASE + (offset) + (bytes) <= UNIT_REGIONS_START ||           \
     (UNIT_USER_BASE + (offset) >= UNIT_REGIONS_END &&                      \
      UNIT_USER_BASE + (offset) + (bytes) <= UNIT_KEY_START))

#endif
