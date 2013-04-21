#include "osv/types.h"
#include <xen/xen.h>

struct start_info* xen_start_info;
extern void* xen_bootstrap_end;

extern "C"
void xen_init(struct start_info* si)
{
    xen_start_info = si;
}
