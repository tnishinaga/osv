# $FreeBSD$

PROG=	bhyveosvload
SRCS=	bhyveosvload.c osv.c

DPADD+=	${LIBVMMAPI} ${LIBUTIL}
LDADD+=	-lvmmapi -lutil

WARNS?=	3

CFLAGS+=-I/usr/src/sys/boot/userboot

.include <bsd.prog.mk>
