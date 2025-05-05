CC=			gcc
SED=		sed
INSTALL=	install
RM=			rm -f

PROG=		ndevd
SRC=		devpubd.c
HEADER=		ndevd.h
MAN=		devpubd.8
SCRIPT=		devpubd-run-hooks
RCSCRIPT=	ndevd
HOOKS=		01-makedev 02-wedgenames
CLEANFILES=	$(SCRIPT) $(RCSCRIPT)

PREFIX?=	/usr/pkg
BINDIR?=	${PREFIX}/sbin
INCLUDEDIR=	${PREFIX}/include
MANDIR=		${PREFIX}/man/man8
SCRIPTDIR=	${PREFIX}/libexec
HOOKSDIR=	${SCRIPTDIR}/ndevd-hooks
RCDIR=		/etc/rc.d

CFLAGS+=	-DDEVPUBD_RUN_HOOKS=\"${PREFIX}/libexec/ndevd-run-hooks\"
LIBS+=		-lprop -pthread

all: $(SCRIPT) $(RCSCRIPT) prog

prog:
	$(CC) -o $(PROG) $(SRC) $(CFLAGS) $(LDFLAGS) $< $(LIBS)

$(SCRIPT): $(SCRIPT:=.in)
	$(SED) -e 's,@HOOKSDIR@,$(HOOKSDIR),g' $@.in > $@

$(RCSCRIPT): $(RCSCRIPT:=.in)
	$(SED) -e 's,@PREFIX@,$(PREFIX),g' $@.in > $@

install:
	$(INSTALL) -d $(DESTDIR)$(BINDIR)
	$(INSTALL) -d $(DESTDIR)$(INCLUDEDIR)
	$(INSTALL) -d $(DESTDIR)$(HOOKSDIR)
	$(INSTALL) -d $(DESTDIR)$(MANDIR)
	$(INSTALL) -m755 $(PROG) $(DESTDIR)$(BINDIR)
	$(INSTALL) -m755 $(SCRIPT) $(DESTDIR)$(SCRIPTDIR)/ndevd-run-hooks
	$(INSTALL) -m755 $(RCSCRIPT) $(DESTDIR)$(RCDIR)
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
