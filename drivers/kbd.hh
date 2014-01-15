/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef DRIVERS_KBD_HH
#define DRIVERS_KBD_HH

#include "console.hh"
#include "sched.hh"
#include "interrupt.hh"
#include <termios.h>

#define SHIFT           (1<<0)
#define CTL             (1<<1)
#define ALT             (1<<2)
#define CAPSLOCK        (1<<3)
#define NUMLOCK         (1<<4)
#define SCROLLLOCK      (1<<5)
#define E0ESC           (1<<6)

class Keyboard {
public:
    explicit Keyboard(sched::thread* consumer);
    virtual bool input_ready();
    virtual uint32_t readkey();
    unsigned int shift;
private:
    gsi_edge_interrupt _irq;
};

#endif
