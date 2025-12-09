CC?=		gcc
SED?=		sed
INSTALL?=	install
RM=		rm -f

PROG=		ndevd
SRC=		devpubd.c
HEADER=		ndevd.h
MAN=		devpubd.8
RCSCRIPT=	ndevd.sh
CLEANFILES=	$(RCSCRIPT)

PREFIX?=	/usr/pkg
BINDIR?=	${PREFIX}/sbin
INCLUDEDIR=	${PREFIX}/include
MANDIR?=	${PREFIX}/man/man8
RCDIR=		${PREFIX}/share/examples/rc.d

CFLAGS+=	-DDEVPUBD_RUN_HOOKS=\"/libexec/devpubd-run-hooks\"
LIBS+=		-lprop -pthread

all: $(RCSCRIPT) prog

prog:
	$(CC) -o $(PROG) $(SRC) $(CFLAGS) $(LDFLAGS) $< $(LIBS)

$(RCSCRIPT): $(RCSCRIPT:=.in)
	$(SED) -e 's,@PREFIX@,$(PREFIX),g' $@.in > $@

install:
	$(INSTALL) -d $(DESTDIR)$(BINDIR)
	$(INSTALL) -d $(DESTDIR)$(INCLUDEDIR)
	$(INSTALL) -d $(DESTDIR)$(MANDIR)
	$(INSTALL) -d $(DESTDIR)$(RCDIR)
	$(INSTALL) -m755 $(PROG) $(DESTDIR)$(BINDIR)
	$(INSTALL) -m755 $(RCSCRIPT) $(DESTDIR)$(RCDIR)/ndevd
	$(INSTALL) -m644 $(MAN) $(DESTDIR)$(MANDIR)/ndevd.8
	$(INSTALL) -m644 $(HEADER) $(DESTDIR)$(INCLUDEDIR)

clean: 
.	for file in $(CLEANFILES)
		$(RM) $(file)
.	endfor
	$(RM) $(PROG)

.PHONY: prog install clean
