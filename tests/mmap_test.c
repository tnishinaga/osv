/*
 * mmap(2) tester
 *
 * Written by Marcin Cieslak <saper@saper.info>
 * Updated by Jung-uk Kim <jkim@FreeBSD.org>
 *
 * This file is placed in the public domain.
 */

#ifdef __FreeBSD__
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#endif

#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/mman.h>

struct intdesc {
	const int	val;
	const char	*desc;
};

static char nofile[] = "N/A";
static char testfile[] = "/tmp/mmap_test.XXXXXX";

static sigjmp_buf env;
static int sigsegv = 0;
static int buserr = 0;
static int othersig = 0;

#if defined(__amd64__) || defined(__x86_64__)
static char testfunc[] = {
	0x55,			/* push %rbp */
	0x48, 0x89, 0xe5,	/* mov %rsp,%rbp */
	0xc7, 0x45, 0xfc,
	0x41, 0x00, 0x00, 0x00,	/* movl $0x41,-4(%rbp) */
	0xc9,			/* leaveq */
	0xc3			/* retq */
};
#define	TOFFSET		7
#elif defined(__i386__)
static char testfunc[] = {
	0x55,			/* push %ebp */
	0x89, 0xe5,		/* mov %esp,%ebp */
	0x83, 0xec, 0x04,	/* sub $0x4,%esp */
	0xc7, 0x45, 0xfc,
	0x41, 0x00, 0x00, 0x00,	/* movl $0x41,-4(%ebp) */
	0xc9,			/* leave */
	0xc3			/* ret */
};
#define	TOFFSET		9
#else
#warn	"Sorry, this platform is not supported."
#endif

static char *
mmap_test(int map_prot, int map_mode, int fd)
{
	char *qp;

	qp = mmap(0, 1024, map_prot, map_mode, fd, 0);
	if (qp != MAP_FAILED) {
		printf("mmap: OK, ");
		return (qp);
	} else {
		printf("mmap: %s (%d)\n", strerror(errno), errno);
		return (NULL);
	}
}

static void
unmap_test(void *ptr)
{

	if (ptr != NULL)
		munmap(ptr, 1024);
}

static void
handle_sig(int sig)
{

	switch(sig) {
	case SIGSEGV:
		sigsegv++;
		break;
	case SIGBUS:
		buserr++;
		break;
	default:
		othersig = sig;
	}
	siglongjmp(env, 1);
}

#define	PRINT_SIGNAL()	do {			\
	if (sigsegv)				\
		printf("SIGSEGV");		\
	if (buserr)				\
		printf("SIGBUS");		\
	if (othersig)				\
		printf("SIG#%02d", othersig);	\
} while (0)

static void
access_test(void *ptr)
{
	struct sigaction newsig = {
		.sa_handler = &handle_sig
	};
	struct sigaction oldsegv;
	struct sigaction oldbus;
	char *qp = (char *)ptr;

	sigaction(SIGSEGV, &newsig, &oldsegv);
	sigaction(SIGBUS, &newsig, &oldbus);

	/* read test */
	sigsegv = buserr = othersig = 0;
	printf("read: ");
	if (sigsetjmp(env, 1) == 0)
		printf("0x%02x, ", qp[TOFFSET]);
	else {
		PRINT_SIGNAL();
		printf(", ");
	}

	/* write test */
	sigsegv = buserr = othersig = 0;
	printf("write: ");
	if (sigsetjmp(env, 1) == 0) {
		memcpy(qp, testfunc, sizeof(testfunc));
		qp[TOFFSET] = 0x42;
		printf("OK, ");
	} else {
		PRINT_SIGNAL();
		printf(", ");
	}

	/* exec test */
	sigsegv = buserr = othersig = 0;
	printf("exec: ");
	if (sigsetjmp(env, 1) == 0) {
		((void (*)())qp)();
		printf("OK\n");
	} else {
		PRINT_SIGNAL();
		printf("\n");
	}

	sigaction(SIGSEGV, &oldsegv, NULL);
	sigaction(SIGBUS, &oldbus, NULL);
}
#undef PRINT_SIGNAL

#define	FOR_EACH(list)	\
	for ((list) = (list ## s); (list)->desc != NULL; (list)++)

static void
run_cases(struct intdesc filemodes[], struct intdesc mapmodes[],
	struct intdesc mapprots[], char *(*mapfunc)(int, int, int),
	void (*accessfunc)(void *), void (*unmapfunc)(void *))
{
	struct intdesc *filemode, *mapmode, *mapprot;
	int fd, caseid, anon;
	void *region;

	caseid = 1;
	FOR_EACH(filemode) {
		FOR_EACH(mapmode) {
			FOR_EACH(mapprot) {
				if (filemode->desc != nofile) {
					if ((fd = open(testfile,
					    filemode->val, 0600)) < 0 ) {
						perror("open testfile");
						return;
					}
					anon = 0;
				} else {
					fd = -1;
					anon = MAP_ANON;
				}

				printf("#%02d: mmap(0, 1024, %s, %s%s, ...)\n"
				    " for filemode %s:\t",
				    caseid, mapprot->desc,
				    anon ? "MAP_ANON|" : "",
				    mapmode->desc,
				    filemode->desc);

				region = (*mapfunc)(mapprot->val,
				    anon | mapmode->val, fd);
				if (region) {
					(*accessfunc)(region);
					(*unmapfunc)(region);
				}
				caseid++;
				if (fd >= 0)
					close(fd);
			}
		}
	}
}
#undef FOR_EACH

int
main(void)
{
	struct intdesc filemodes[] = {
		{	O_RDONLY,	"O_RDONLY"	},
		{	O_WRONLY,	"O_WRONLY"	},
		{	O_RDWR,		"O_RDWR"	},
		{	-1,		nofile		},
		{	-1,		NULL		}
	};
	struct intdesc mapmodes[] = {
#if 0
		{	0,		"none"		},
#endif
		{	MAP_SHARED,	"MAP_SHARED"	},
		{	MAP_PRIVATE,	"MAP_PRIVATE"	},
		{	-1,		NULL		}
	};
	struct intdesc mapprots[] = {
		{	PROT_NONE,	"PROTO_NONE"	},
		{	PROT_READ,	"PROTO_READ"	},
		{	PROT_WRITE,	"PROTO_WRITE"	},
		{	PROT_EXEC,	"PROTO_EXEC"	},
		{	PROT_READ | PROT_WRITE,
			"PROTO_READ|PROTO_WRITE"	},
		{	PROT_READ | PROT_EXEC,
			"PROTO_READ|PROTO_EXEC"		},
		{	PROT_WRITE | PROT_EXEC,
			"PROTO_WRITE|PROTO_EXEC"	},
		{	PROT_READ | PROT_WRITE |
			PROT_EXEC,
			"PROTO_READ|PROTO_WRITE|"
			"PROTO_EXEC"			},
		{	-1,		NULL		}
	};
	int fd;

	if ((fd = mkstemp(testfile)) < 0) {
		perror("open testfile");
		return (-1);
	}
	if (write(fd, testfunc, sizeof(testfunc)) != sizeof(testfunc)) {
		perror("write testfile");
		return (-1);
	}
	close(fd);

	run_cases(filemodes, mapmodes, mapprots, &mmap_test,
	    &access_test, &unmap_test);

	unlink(testfile);

	return (0);
}
