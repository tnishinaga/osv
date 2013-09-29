/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <string.h>
#include "mempool.hh"
#include "mmu.hh"

#include "virtio.hh"
#include "drivers/vmxnet3-queues.hh"
#include "debug.hh"

namespace vmware {

#define vmxnet3_tag "vmxnet3"
#define vmxnet3_d(...)   tprintf_d(vmxnet3_tag, __VA_ARGS__)
#define vmxnet3_i(...)   tprintf_i(vmxnet3_tag, __VA_ARGS__)
#define vmxnet3_w(...)   tprintf_w(vmxnet3_tag, __VA_ARGS__)
#define vmxnet3_e(...)   tprintf_e(vmxnet3_tag, __VA_ARGS__)

};
