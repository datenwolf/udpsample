#
# makefile for vdm.
#
# gcc does something strange, so check the various opt. levels for
# best performance (or write addmul1 in assembly code).
#
# Standard compilation with -O9 works well for PentiumPro and Pentium2
# machines.
#

CC=gcc
COPT= -O3
CFLAGS=$(COPT) -Wall -D_NDEBUG # -DTEST
SRCS= fec.c Makefile test.c
DOCS= README fec.3
ALLSRCS= $(SRCS) $(DOCS) fec.h

fec: fec.o test.c
	$(CC) $(CFLAGS) -o fec fec.o test.c

fec.o: fec.h fec.c Makefile
	$(CC) $(CFLAGS) -c fec.c

clean:
	- rm *.core *.o fec.s fec

tgz: $(ALLSRCS)
	tar cvzf vdm`date +%y%m%d`.tgz $(ALLSRCS)
