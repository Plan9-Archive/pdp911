#include "pdp11.h"

/*
 * KW11-L line time clock
 */

static word lks;

static Interrupt cintr;

static word
kwlread(u32int addr, int type)
{
	switch(addr&~01){
	case 0777546:
		return GETVAL(lks, addr, type);
	default:
		print("can't read from %uo\n", addr);
		return 0;
	}
}

static void
kwlwrite(u32int addr, word val, int type)
{
	switch(addr&~01){
	case 0777546:
		lks = SETVAL(lks, val, addr, type);
		break;
	default:
		print("can't write to %uo\n", addr);
	}
}

static void
clockproc(void *a)
{
	int i;
	Channel *c;
	c = a;
	for(i = 0; ; i++){
		/* tick at 60Hz */
		if(i == 2){
			i = 0;
			sleep(17);
		}else
			sleep(16);
		lks |= 0200;
		sendul(c, 1);
	}
}

static void
clockthread(void *)
{
	Channel *c;
	c = chancreate(sizeof(ulong), 5);
	proccreate(clockproc, c, mainstacksize);
	for(;;){
		recvul(c);
		if((lks & 0300) == 0300)
			reqintr(&cintr);
	}
}

void
clockreset(void)
{
	lks = 0200;
}

void
clockinit(void)
{
	cintr = (Interrupt){ 6, 0100, 0, nil };
	ioreadtbl[ADDRTOIDX(0777546)] = kwlread;
	iowritetbl[ADDRTOIDX(0777546)] = kwlwrite;
	threadcreate(clockthread, nil, mainstacksize);
	clockreset();
}
