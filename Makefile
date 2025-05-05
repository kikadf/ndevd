CC=			gcc
SED=		sed
INSTALL=	install
RM=			rm -f

PROG=		ndevd
SRC=		devpubd.c
HEADER=		ndevd.h
MAN=		devpubd.8
SCRIPT=		devpubd-run-hooks
HOOKS=		01-makedev 02-wedgenames
CLEANFILES=	$(SCRIPT)

PREFIX?=	/usr/pkg
BINDIR?=	${PREFIX}/sbin
INCLUDEDIR=	${PREFIX}/include
MANDIR=		${PREFIX}/man/man8
SCRIPTDIR=	${PREFIX}/libexec
HOOKSDIR=	${SCRIPTDIR}/ndevd-hooks

CFLAGS+=	-DDEVPUBD_RUN_HOOKS=\"${PREFIX}/libexec/ndevd-run-hooks\"
LIBS+=		-lprop -pthread

all: $(SCRIPT) prog

prog:
	$(CC) -o $(PROG) $(SRC) $(CFLAGS) $(LDFLAGS) $< $(LIBS)

$(SCRIPT): $(SCRIPT:=.in)
	$(SED) -e 's,@HOOKSDIR@,$(HOOKSDIR),g' $@.in > $@

install:
	$(INSTALL) -d $(DESTDIR)$(BINDIR)
	$(INSTALL) -d $(DESTDIR)$(INCLUDEDIR)
	$(INSTALL) -d $(DESTDIR)$(HOOKSDIR)
	$(INSTALL) -d $(DESTDIR)$(MANDIR)
	$(INSTALL) -m755 $(PROG) $(DESTDIR)$(BINDIR)
	$(INSTALL) -m755 $(SCRIPT) $(DESTDIR)$(SCRIPTDIR)/ndevd-run-hooks
.	for hook in $(HOOKS)
		$(INSTALL) -m755 hooks/$(hook) $(DESTDIR)$(HOOKSDIR)
.	endfor
	$(INSTALL) -m644 $(MAN) $(DESTDIR)$(MANDIR)/ndevd.8
	$(INSTALL) -m644 $(HEADER) $(DESTDIR)$(INCLUDEDIR)

clean: 
.	for file in $(CLEANFILES)
		$(RM) $(file)
.	endfor
	$(RM) $(PROG)

.PHONY: prog install clean
