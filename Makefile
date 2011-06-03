CC=gcc

CFLAGS=-g -O2 -DDEBUG
CPPFLAGS=
LIBS=-lm
LDFLAGS=

SRC= locod.c locod.h \
loco.c loco.h \
common.c common.h \
debug.c debug.h

SOBJS=   locod.o debug.o common.o
ROBJS=   loco.o debug.o common.o
OBJS=    $(SOBJS) $(ROBJS)

TARGETS=locod loco

all:${TARGETS}

locod: $(SOBJS)
	 $(CC) $(SOBJS) -o locod $(LIBS) $(LDFLAGS)

loco: $(ROBJS)
	 $(CC) $(ROBJS) -o loco $(LIBS) $(LDFLAGS)

clean: 
	 rm -f ${OBJS} ${TARGETS}

.c.o:
	$(CC) -c -Wall $(CPPFLAGS) $(DEFS) $(CFLAGS) $<

