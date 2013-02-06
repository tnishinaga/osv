#include "stdio_impl.h"

void funlockfile(FILE *f)
{
#if 0
	if (!--f->lockcount) __unlockfile(f);
#endif
}
