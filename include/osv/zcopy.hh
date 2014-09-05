#ifndef _ZCOPY_HH
#define _ZCOPY_HH

#include <osv/zcopy.h>

struct ztx_handle {
    std::atomic<size_t> zh_remained;
};

#endif
