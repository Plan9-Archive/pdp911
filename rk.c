#include "pdp11.h"

/*
 * RK11 (RK05) DECpack cartridge disk
 */

#define DRY     (1 << 7)
#define ARDY    (1 << 6)

#define ERR     (1 << 15)
#define HARDERR (1 << 14)
#define SSE     (1 << 8)
#define RDY     (1 << 7)
#define IDE     (1 << 6)
#define MX	(3 << 4)

static word rkds, rker, rkcs, rkwc, rkba, rkda, rkdb;
static int fd[8];
static word buf[256];

static Interrupt rkintr;

static word bootrom[] = {
	0042113,
	0012706,
	0002000,
	0012700,
	0000000,	/* boot unit */
	0010003,
	0000303,
	0006303,
	0006303,
	0006303,
	0006303,
	0006303,
	0012701,
	0177412,
	0010311,
	0005041,
	0012741,
	0177000,
	0012741,
	0000005,
	0005002,
	0005003,
	0012704,
	0002020,
	0005005,
	0105711,
	0100376,
	0105011,
	0005007
};

static word
rkread(u32int addr, int type)
{
	switch(addr&~01){
	case 0777400:
		return GETVAL(rkds, addr, type);
	case 0777402:
		return GETVAL(rker, addr, type);
	case 0777404:
		return GETVAL(rkcs, addr, type);
	case 0777406:
		return GETVAL(rkwc, addr, type);
	case 0777410:
		return GETVAL(rkba, addr, type);
	case 0777412:
		return GETVAL(rkda, addr, type);
	case 0777416:
		return GETVAL(rkdb, addr, type);
	default:
		/* can't happen */
		fprint(2, "can't read from %uo\n", addr);
		return 0;
	}
}

static void rkgo(void);

static void
rkwrite(u32int addr, word val, int type)
{
	switch(addr&~01){
	case 0777400: break;
	case 0777402: break;
	case 0777404:
		val = SETVAL(rkcs, val, addr, type);
		val &= 06577;
		rkcs &= ~06577;
		rkcs |= val;
		if(rkcs & 1)
			rkgo();
		break;
	case 0777406:
		rkwc = SETVAL(rkwc, val, addr, type);
		break;
	case 0777410:
		rkba = SETVAL(rkba, val, addr, type);
		break;
	case 0777412:
		rkda = SETVAL(rkda, val, addr, type);
		break;
	case 0777416:
		rkdb = SETVAL(rkdb, val, addr, type);
		break;
	default:
		/* can't happen */
		fprint(2, "can't write to %uo\n", addr);
	}
}

static void
rkerror(int bit)
{
	rker |= bit;
	if(rker)
		rkcs |= ERR;
	/* hard error */
	if(rker & 0177740)
		rkcs |= HARDERR;
	rkds |= ARDY;
	rkcs |= RDY;
	if(rkcs & IDE)
		if((rkcs & HARDERR) ||
                   (rkcs & ERR) && (rkcs & SSE))
			reqintr(&rkintr);
}

/* TODO: make asynchronous */
static void
rkrw(int action)
{
	int drv, cyl, surf, sec;
	char (*geo)[203][2][12][512] = nil;
	int da, ba;
	int n;

	rkds &= ~ARDY;
	rkcs &= ~RDY;
	drv = (rkda >> 13) & 07;
	cyl = (rkda >> 5) & 0377;
	surf = (rkda >> 4) & 01;
	sec = rkda & 017;
	if(fd[drv] < 0){
		rkerror(1 << 7);
		return;
	}
	if(cyl > 0312){
		rkerror(1 << 6);
		return;
	}
	if(sec > 013){
		rkerror(1 << 5);
		return;
	}
	da = (int)&(*geo)[cyl][surf][sec][0];
	ba = rkba | (((rkcs >> 4) & 03) << 16);
	seek(fd[drv], da, 0);
	while(rkwc){
		memset(buf, 0, 512);
		if(action == 1)
			read(fd[drv], buf, 512);
		for(n = 0; n < 256 && rkwc; n++, rkwc++){
			if(ba >= MEMSIZE*2){
				print("error\n");
				rkerror(1 << 10);
				return;
			}
			if(action == 0)
				buf[n] = coremem[ba>>1];
			else
				coremem[ba>>1] = buf[n];
			if((rkcs & (1 << 10)) == 0){
				ba += 2;
				rkba += 2;
				/* rkba overflow */
				if(rkba == 0)
					rkcs = (rkcs & ~MX) |
					       ((((rkcs & MX) >> 4) + 1) & 3) << 4;
			}
		}
		if(action == 0)
			write(fd[drv], buf, n*2);
		sec++;
		if(sec >= 12){
			sec = 0;
			surf++;
			if(surf >= 2){
				surf = 0;
				cyl++;
				if(cyl >= 203){
					rkerror(1 << 14);
					return;
				}
			}
		}
		rkda = (drv << 13) | (cyl << 5) | (surf << 4) | sec;
	}
	rkds |= ARDY;
	rkcs |= RDY;
	if(rkcs & IDE)
		reqintr(&rkintr);
}

static void
rkgo(void)
{
	rkcs &= ~((1 << 13) | 1);
	switch((rkcs>>1) & 07){
	case 0:		/* reset */
		rkreset();
		break;
	case 1:		/* write */
		rkrw(0);
		break;
	case 2:		/* read */
		rkrw(1);
		break;
	default:
		fprint(2, "unsupported rk function %o, trapping\n", (rkcs>>1) & 07);
		reqintr(&rkintr);
	}
}

void
rkattach(int n, char *file)
{
	if(n < 0 || n > 7){
		fprint(2, "Drive no. must be between 0 and 7\n");
		return;
	}
	if(fd[n] >= 0)
		rkdetach(n);
	fd[n] = open(file, ORDWR);
	if(fd[n] < 0){
		fd[n] = create(file, ORDWR, 0666);
		if(fd[n] < 0){
			fprint(2, "Can't create file: %r\n");
			return;
		}
		fprint(2, "Creating %s\n", file);
	}
}

void
rkdetach(int n)
{
	close(fd[n]);
	fd[n] = -1;
}

void
rkreset(void)
{
	rkds = (01 << 11) | DRY | ARDY;
	rker = 0;
	rkcs = RDY;
	rkwc = 0;
	rkba = 0;
	rkda = 0;
	rkdb = 0;
}

void
rkinit(void)
{
	int i;

	for(i = 0; i < 8; i++)
		fd[i] = -1;
	rkintr = (Interrupt){ 5, 0220, 0, nil };
	for(i = 0; i < 6; i++){
		ioreadtbl[ADDRTOIDX(0777400) + i] = rkread;
		iowritetbl[ADDRTOIDX(0777400) + i] = rkwrite;
	}
	ioreadtbl[ADDRTOIDX(0777416)] = rkread;
	iowritetbl[ADDRTOIDX(0777416)] = rkwrite;
	rkreset();
}

void
rkboot(int unit)
{
	int i;
	for(i = 0; i < sizeof bootrom / sizeof(word); i++)
		coremem[(02000>>1)+i] = bootrom[i];
	coremem[02010>>1] = unit&7;
	r[PC] = 02000;
	run();
}
