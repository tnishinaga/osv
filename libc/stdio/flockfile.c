#include "stdio_impl.h"

void flockfile(FILE *f)
{
#if 0
	while (ftrylockfile(f)) {
		int owner = f->lock;
		if (owner) __wait(&f->lock, &f->waiters, owner, 1);
	}
#endif
}
