#include "osv/types.h"
#include <xen/xen.h>

struct start_info* xen_start_info;

extern "C"
void xen_init(struct start_info* si)
{
    xen_start_info = si;
}
