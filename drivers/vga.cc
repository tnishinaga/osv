/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "vga.hh"
#include "mmu.hh"
#include "libtsm/external/xkbcommon-keysyms.h"

volatile unsigned short * const VGAConsole::buffer
= reinterpret_cast<volatile unsigned short *>(mmu::phys_mem + 0xb8000);

static void tsm_log_cb(void *data, const char *file, int line, const char *func,
    const char *subs, unsigned int sev, const char *format, va_list args)
{
    printf("%s:%d:%s subs:%s sev:%d ", file, line, func, subs, sev);
    vprintf(format, args);
    printf("\n");
}

static void tsm_write_cb(struct tsm_vte *vte, const char *u8, size_t len,
    void *data)
{
    VGAConsole *vga = reinterpret_cast<VGAConsole *>(data);
    for (size_t i = 0; i < len; i++)
        vga->read_queue.push(u8[i]);
}

static int tsm_draw_cb(struct tsm_screen *screen, uint32_t id,
    const uint32_t *ch, size_t len, unsigned int cwidth, unsigned int posx,
    unsigned int posy, const struct tsm_screen_attr *attr, tsm_age_t age,
    void *data)
{
    VGAConsole *vga = reinterpret_cast<VGAConsole *>(data);

    if (len)
        vga->draw(*ch, attr, posx, posy);
    else
        vga->draw(' ', attr, posx, posy);
    return 0;
}

VGAConsole::VGAConsole(sched::thread* poll_thread, const termios *tio)
    : tsm_age(0), _tio(tio), kbd(poll_thread)
{
    tsm_screen_new(&tsm_screen, tsm_log_cb, this);
    tsm_screen_resize(tsm_screen, ncols, nrows);
    tsm_screen_set_max_sb(tsm_screen, 1024);
    tsm_vte_new(&tsm_vte, tsm_screen, tsm_write_cb, this, tsm_log_cb, this);
}

void VGAConsole::write(const char *str, size_t len)
{
    tsm_vte_input(tsm_vte, str, len);
    tsm_age = tsm_screen_draw(tsm_screen, tsm_draw_cb, this);
}

void VGAConsole::draw(const uint32_t c, const struct tsm_screen_attr *attr, 
    unsigned int x, unsigned int y)
{
    uint32_t colors = (attr->bccode << 4) | (attr->fccode & 0xf);
    buffer[y * ncols + x] = (colors << 8) | (c & 0xff);
}

bool VGAConsole::input_ready()
{
    return !read_queue.empty() || kbd.input_ready();
}

char VGAConsole::readch()
{
    uint32_t key;
    unsigned int mods = 0;
    char c;

    while(1) {
        if (!read_queue.empty()) {
            c = read_queue.front();
            read_queue.pop();
            return c;
        }

        key = kbd.readkey();

        if (kbd.shift & SHIFT)
            mods |= TSM_SHIFT_MASK;
        if (kbd.shift & CTL)
            mods |= TSM_CONTROL_MASK;
        if (kbd.shift & ALT)
            mods |= TSM_ALT_MASK;

        switch (key) {
        case XKB_KEY_Up:
            tsm_screen_sb_up(tsm_screen, 1);
            tsm_age = tsm_screen_draw(tsm_screen, tsm_draw_cb, this);
            break;
        case XKB_KEY_Down:
            tsm_screen_sb_down(tsm_screen, 1);
            tsm_age = tsm_screen_draw(tsm_screen, tsm_draw_cb, this);
            break;
        case XKB_KEY_Page_Up:
            tsm_screen_sb_page_up(tsm_screen, 1);
            tsm_age = tsm_screen_draw(tsm_screen, tsm_draw_cb, this);
            break;
        case XKB_KEY_Page_Down:
            tsm_screen_sb_page_down(tsm_screen, 1);
            tsm_age = tsm_screen_draw(tsm_screen, tsm_draw_cb, this);
            break;
        default:
            if (tsm_vte_handle_keyboard(tsm_vte, key, 0, mods, key))
                tsm_screen_sb_reset(tsm_screen);
        }
        while (read_queue.empty())
            ;
    }
}
