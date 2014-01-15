/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef DRIVERS_VGA_HH
#define DRIVERS_VGA_HH

#include "console.hh"
#include "sched.hh"
#include "kbd.hh"
#include "libtsm/libtsm.h"
#include <termios.h>
#include <queue>

class VGAConsole : public Console {
public:
    explicit VGAConsole(sched::thread* consumer, const termios *tio);
    virtual void write(const char *str, size_t len);
    virtual bool input_ready();
    virtual char readch();
    virtual void draw(const uint32_t c, const struct tsm_screen_attr *attr, unsigned int x, unsigned int y);
    struct tsm_screen *tsm_screen;
    struct tsm_vte *tsm_vte;
    std::queue<char> read_queue;
    tsm_age_t tsm_age;
private:
    unsigned _col = 0;
    static const unsigned ncols = 80, nrows = 25;
    static volatile unsigned short * const buffer;
    const termios *_tio;
    Keyboard kbd;
};

#endif
