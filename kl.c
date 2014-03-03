#include "pdp11.h"

/*
 * KL11 console
 * (actually something between KL11 and DL11, needs more work)
 */

#define INTR  (1 << 6)
#define RDONE (1 << 7)
#define RENB  1

#define XRDY (1 << 7)

static word rsr, rbr;
static word xsr, xbr;

static Interrupt rintr, xintr;

static word
klread(u32int addr, int type)
{
	switch(addr&~01){
	case 0777560:
		return GETVAL(rsr, addr, type);
	case 0777562:
		rsr &= ~RDONE;
		return GETVAL(rbr, addr, type);
	case 0777564:
		return GETVAL(xsr, addr, type);
	case 0777566:
		/* write only */
		return 0;
	default:
		/* can't happen */
		print("can't read from %uo\n", addr);
		return 0;
	}
}

static void
klwrite(u32int addr, word val, int type)
{
	switch(addr&~01){
	case 0777560:
		val = SETVAL(rsr, val, addr, type);
		val &= 0157;
		rsr = val;
		if(rsr&RENB)
			rsr &= ~RDONE;
		rsr &= ~RENB;
		break;
	case 0777562:
		rsr &= ~RDONE;
		break;
	case 0777564:
		val = SETVAL(xsr, val, addr, type);
		val &= 0105;
		xsr = val;
		/* always ready */
		xsr |= XRDY;
		break;
	case 0777566:
		val &= 0177;
		if(xsr&XRDY)
			write(cons, &val, 1);
		if(xsr&INTR)
			reqintr(&xintr);
		break;
	default:
		/* can't happen */
		print("can't write to %uo\n", addr);
	}
}

static void
inthread(void *)
{
	char c;
	Ioproc *io;

	io = ioproc();
	while(running){
		ioread(io, cons, &c, 1);
		c &= 0177;
		rbr = c;
		rsr |= RDONE;
		if(rsr&INTR)
			reqintr(&rintr);
		yield();
	}
	closeioproc(io);
	threadexits(nil);
}

void
klreset(void)
{
	rsr = 0;
	xsr = XRDY;
}

void
klinit(void)
{
	int i;

	rintr = (Interrupt){ 4, 060, 0, nil };
	xintr = (Interrupt){ 4, 064, 0, nil };
	for(i = 0; i < 4; i++){
		ioreadtbl[ADDRTOIDX(0777560) + i] = klread;
		iowritetbl[ADDRTOIDX(0777560) + i] = klwrite;
	}
	threadcreate(inthread, nil, mainstacksize);

	klreset();
}
