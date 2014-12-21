PROG=		relayd-updateconf

SRCS=		parse.y
SRCS+=		updateconf.c

BINDIR=		/usr/local/bin

LDADD=		-lutil
DPADD=		${LIBUTIL}
CFLAGS+=	-Wall -I${.CURDIR}
CFLAGS+=	-Wstrict-prototypes -Wmissing-prototypes
CFLAGS+=	-Wmissing-declarations
CFLAGS+=	-Wshadow -Wpointer-arith
CFLAGS+=	-Wsign-compare

CLEANFILES+=	y.tab.h

.include <bsd.prog.mk>

run: .PHONY
	./relayd-updateconf -f ./relayd.conf

DISTSRCS=	${SRCS}
DISTSRCS+=	Makefile
DISTSRCS+=	relayd-updateconf.1
DISTSRCS+=	relayd.conf
DISTSRCS+=	updateconf.h

DISTNAME=	relayd-updateconf-5.6.2
dist: .PHONY ${DISTNAME}.tar.gz

${DISTNAME}.tar.gz:
	mkdir ${DISTNAME}
	cp ${DISTSRCS} ${DISTNAME}
	tar -zcvf $@ ${DISTNAME}

distclean: .PHONY
	rm -rf ${DISTNAME}

CLEANFILES+=	${DISTNAME}.tar.gz
