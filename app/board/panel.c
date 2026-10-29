/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Copyright (c) 2026 珠海笙科智能科技有限公司 (M-VAVE).
 * Connect the FM1 control API to the board scanner.
 * Modified 2026-10 for Hindsight: the soft key's download mode, and the
 * Pi's lights while it has the screen (hindsight.h), and a longer reset
 * before the pads' chain. */
#include "smk37_hw.h"
#include "fm1_lcd_hw.h"
#include "lcd.inc"
#include "hindsight.h"

static uint32_t virtual_leds;
static uint16_t button_leds;
/* The pads' chain: RGB_RESET bytes held low, then 24 a pad. Open held it low
 * for 36 (about 45 us), under the reset these LEDs need (50-280 us by part),
 * so a glitch as SPI1 switched over from the LCD came through as a stray bit
 * at the chain's front: pad 1 wrong and every pad after it shifted, until
 * the next transfer (on the unit, 2026-10-29). */
enum { RGB_RESET = 256 };
static uint8_t rgb_buffer[RGB_RESET + 24 * 16] __attribute__((aligned(4)));
static uint32_t rgb_colors[16];
static unsigned rgb_dirty=0xffff;
static unsigned led_modes=~0U;
/* The buttons whose lights the Pi set at the last read (hindsight.h). */
static uint32_t pi_buttons;
/* When the pads' chain was last sent whole while the Pi has the screen. */
static uint32_t pi_pads_ms;
static const uint8_t rgb_map[16]={0,15,14,13,12,11,10,9,1,2,3,4,5,6,7,8};
/* PH8 is an encoder input on both boards, with no fitted TRS MIDI RX. */
uint8_t fm1_io_trs_midi_enabled(void) {return 0;}
void fm1_controls_init(void) {smk37_scan_init();led_modes=~0U;}
void fm1_controls_read(struct fm1_controls *state)
{
    smk37_scan_read();smk37_controls_snapshot(state);
    state->battery=smk37_battery_adc();
    /* The Pi's lights, while it has them. A light it lets go of is put back
     * as the keyboard has it: off, or Open's own, below and on the next read. */
    uint32_t pi=0;
    for(unsigned id=0;id<HS_BUTTONS;++id) {
        int on=hindsight_button(id);
        if(on>=0){smk37_led(id,(unsigned)on);pi|=1U<<id;}
        else if(pi_buttons&(1U<<id))smk37_led(id,0);
    }
    if(pi_buttons&~pi) {
        for(unsigned key=0;key<14;++key)fm1_led_set(key,(button_leds>>key)&1U);
        led_modes=~0U;
    }
    pi_buttons=pi;
    unsigned bank=smk37_fader_bank(),pad=smk37_pad_steps()||smk37_pad_focus();
    unsigned modes=bank|(pad<<1),changed=led_modes==~0U?3:modes^led_modes;
    if(changed&1)smk37_led(1,bank);
    if(changed&2)smk37_led(7,pad);
    led_modes=modes;
    /* The chain shares SPI1 with the LCD, and a pad sent wrong stays wrong
     * until it changes: while the Pi has the pads, send them every 500 ms. */
    if(hindsight_active() && fm1_millis()-pi_pads_ms>=500U){pi_pads_ms=fm1_millis();rgb_dirty=0xffff;}
    for(unsigned i=0;i<16;++i) {
        unsigned key=smk37_screen_pad_key(i);
        unsigned on=key<27 && (virtual_leds&(1U<<key));
        smk37_rgb_set(i,hindsight_pad(i,smk37_pad_color(i,on)));
    }
    smk37_rgb_service();
}
void fm1_led_set(unsigned key,int on)
{
    static const uint8_t physical[14]={4,5,14,8,9,9,13,10,12,3,6,15,16,17};
    if(key<14) {
        if(on)button_leds|=1U<<key;else button_leds&=~(1U<<key);
        /* ENV and LFO share one physical indicator. */
        smk37_led(physical[key],(key==4||key==5)?(button_leds&0x30)!=0:on);
    }
    else if(key<41) {
        unsigned bit=1U<<(key-14);
        if(on)virtual_leds|=bit;else virtual_leds&=~bit;
    }
}
void smk37_rgb_set(unsigned id,unsigned rgb)
{
    if(rgb_colors[id]!=rgb){rgb_colors[id]=rgb;rgb_dirty|=1U<<id;}
}
void smk37_rgb_service(void)
{
    if(!rgb_dirty || fm1_screen_pending())return;
    static const uint32_t nibble[16]={
        0xc0c0c0c0,0xf0c0c0c0,0xc0f0c0c0,0xf0f0c0c0,
        0xc0c0f0c0,0xf0c0f0c0,0xc0f0f0c0,0xf0f0f0c0,
        0xc0c0c0f0,0xf0c0c0f0,0xc0f0c0f0,0xf0f0c0f0,
        0xc0c0f0f0,0xf0c0f0f0,0xc0f0f0f0,0xf0f0f0f0};
    for(unsigned i=0;i<16;++i) {
        if(!(rgb_dirty&(1U<<i)))continue;
        unsigned rgb=rgb_colors[i],grb=((rgb&0xff00)<<8)|((rgb>>8)&0xff00)|(rgb&255);
        for(unsigned n=0;n<6;++n) {
            uint32_t word=nibble[(grb>>(20-4*n))&15];
            memcpy(rgb_buffer+RGB_RESET+24*rgb_map[i]+4*n,&word,4);
        }
    }
    /* One task owns SPI1: finish LCD, keep CS high during RGB DMA, restore. */
    fm1_lcd_deselect();JL_IOMAP->CON1|=BIT(4);JL_SPI1->BAUD=8;
    JL_SPI1->CON|=BIT(14);fm1_device_fence();
    JL_SPI1->ADR=(uint32_t)(uintptr_t)rgb_buffer;JL_SPI1->CNT=sizeof rgb_buffer;
    uint32_t errors=fm1_lcd_timeouts;fm1_lcd_wait();
    JL_IOMAP->CON1&=~BIT(4);JL_SPI1->BAUD=4;
    if(errors==fm1_lcd_timeouts)rgb_dirty=0;
}
/* hindsight.h. The ROM looks for this marker in its retained RAM at a reset,
 * whoever wrote it, and comes up as its download disk: m-bootloader's
 * rom_download (MIT; docs/SPEC.md §4.2 there). */
void hindsight_download_mode(void)
{
    (void)smk_irq_save();
    memcpy((void *)(uintptr_t)0x01c7fd80U, "usb_update_mode", 16);
    __asm__ volatile("csync" ::: "memory");
    SMK_REG(0x10000U) |= BIT(4);
    __asm__ volatile("csync" ::: "memory");
    for (;;) __asm__ volatile("" ::: "memory");
}
