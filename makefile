CFLAGS=
LIBS= -lpthread -lSDL_mixer `sdl-config --cflags --libs`

ifeq (${SIMULATE_LCD},1)
CFLAGS+=-DSIMULATE_LCD=1
else
LIBS+=-lbcm2835
endif

ifeq (${SIMULATE_BUTTONS},1)
CFLAGS+=-DSIMULATE_BUTTONS=1
endif

all:	rpilcd_test play

rpilcd_test:	rpilcd_test.c rpilcd.o
	${CC} -o rpilcd_test rpilcd_test.c rpilcd.o ${CFLAGS} ${LIBS}

rpilcd.o:	rpilcd.c
	${CC} -ggdb -static -o rpilcd.o -c rpilcd.c ${CFLAGS} ${LIBS}

play:	play.c rpilcd.o
	${CC} -ggdb -o play play.c rpilcd.o ${CFLAGS} ${LIBS}

clean:
	rm -f play rpilcd.o rpilcd_test
