#include "pdp11.h"

word (*ioreadtbl[4096])(u32int addr, int type);
void (*iowritetbl[4096])(u32int addr, word val, int type);
u32int (*tophys)(word addr, int access, int mode);

word mmr2;
static word mmr0, mmr1, mmr3;

/* Only user and kernel i-space used for now */
static word uisd[8];
static word udsd[8];
static word uisa[8];
static word udsa[8];
static word sisd[8];
static word sdsd[8];
static word sisa[8];
static word sdsa[8];
static word kisd[8];
static word kdsd[8];
static word kisa[8];
static word kdsa[8];

/* TODO: W bit 0100 in pdr */

static u32int
tophysnommu(word addr, int, int)
{
	return ((addr&0160000) == 0160000) ? addr | 0600000 : addr;
}

/* TODO: Error-setting is probably too complicated and wrong. */
static u32int
tophysmmu(word addr, int access, int mode)
{
	word par, *pars, pdr, *pdrs;
	u32int phys;
	int n;
	int plf, bn;
	int doabort;

	/* TODO: i and d space */
	if(mode == KERNEL){
		pars = kisa;
		pdrs = kisd;
	}else if(mode == USER){
		pars = uisa;
		pdrs = uisd;
	}else{
		pars = pdrs = nil;	/* compiler complains */
		fprint(2, "error: invalid processor mode %d\n", mode);
		threadexitsall(nil);
	}
	n = (addr >> 13) & 07;
	par = pars[n];
	pdr = pdrs[n];
	doabort = 0;
	switch(pdr&07){
	case 06:	/* RW */
		break;

	case 02:	/* RO */
		if(access & WO){
			mmr0 &= ~0160156;
			mmr0 |= 0020000;
			goto abort;
		}
		break;

	case 00:	/* non-resident */
	case 04:	/* unused */
		mmr0 &= ~0160156;
		mmr0 |= 0100000;
		/* not sure if this is correct */
		doabort = 1;
		break;
		
	default:
		fprint(2, "unsupported access mode %uo\n", pdr&07);
		mmr0 &= ~0160156;
		mmr0 |= 0100000;
		goto abort;
	}
	addr &= 017777;
	bn = (addr>>6)&0177;
	plf = (pdr>>8)&0177;
	if((pdr & 010) ? (bn < plf) : (bn > plf)){
		if(!doabort)
			mmr0 &= ~0160156;
		mmr0 |= 0040000;
		goto abort;
	}
	if(doabort)
		goto abort;
	phys = par + bn;
	phys <<= 6;
	phys += addr&077;
	return phys;

abort:
	mmr0 |= n << 1;
	mmr0 |= (psw&PSW_CM) >> 9;
	trap(TRAP_SEG);
	return 0;	/* for the compiler */
}

static word
readmmu(u32int addr, int type)
{
	switch(addr&~01){
	case 0777572:
		return GETVAL(mmr0, addr, type);
	case 0777574:
		return GETVAL(mmr1, addr, type);
	case 0777576:
		return GETVAL(mmr2, addr, type);
	case 0777516:
		return GETVAL(mmr3, addr, type);
	}

	/* ugly, ugly, ugly */
	if(addr >= 0777600 && addr < 0777620)
		return GETVAL(uisd[(addr-0777600)>>1], addr, type);
	else if(addr >= 0777620 && addr < 0777640)
		return GETVAL(udsd[(addr-0777620)>>1], addr, type);
	else if(addr >= 0777640 && addr < 0777660)
		return GETVAL(uisa[(addr-0777640)>>1], addr, type);
	else if(addr >= 0777660 && addr < 0777700)
		return GETVAL(udsa[(addr-0777660)>>1], addr, type);
	else if(addr >= 0772200 && addr < 0772220)
		return GETVAL(sisd[(addr-0772200)>>1], addr, type);
	else if(addr >= 0772220 && addr < 0772240)
		return GETVAL(sdsd[(addr-0772220)>>1], addr, type);
	else if(addr >= 0772240 && addr < 0772260)
		return GETVAL(sisa[(addr-0772240)>>1], addr, type);
	else if(addr >= 0772260 && addr < 0772300)
		return GETVAL(sdsa[(addr-0772260)>>1], addr, type);
	else if(addr >= 0772300 && addr < 0772320)
		return GETVAL(kisd[(addr-0772300)>>1], addr, type);
	else if(addr >= 0772320 && addr < 0772340)
		return GETVAL(kdsd[(addr-0772320)>>1], addr, type);
	else if(addr >= 0772340 && addr < 0772360)
		return GETVAL(kisa[(addr-0772340)>>1], addr, type);
	else if(addr >= 0772360 && addr < 0772400)
		return GETVAL(kdsa[(addr-0772360)>>1], addr, type);
	else{
		fprint(2, "invalid read from %06uo\n", addr);
		return 0;
	}
}

static void
writemmu(u32int addr, word val, int type)
{
	int desc;
	word *p;

	switch(addr&~01){
	case 0777572:
		mmr0 = SETVAL(mmr0, val, addr, type);
		tophys = (mmr0 & 1) ? tophysmmu : tophysnommu;
		return;
	case 0777574:
		mmr1 = SETVAL(mmr1, val, addr, type);
		return;
	case 0777576:
		return;
	case 0777516:
		mmr3 = SETVAL(mmr3, val, addr, type);
		return;
	}

	desc = 0;
	/* ugly, ugly, ugly */
	if(addr >= 0777600 && addr < 0777620){
		desc = 1;
		p = &uisd[(addr-0777600)>>1];
	}else if(addr >= 0777620 && addr < 0777640){
		desc = 1;
		p = &udsd[(addr-0777620)>>1];
	}else if(addr >= 0777640 && addr < 0777660){
		p = &uisa[(addr-0777640)>>1];
	}else if(addr >= 0777660 && addr < 0777700){
		p = &udsa[(addr-0777660)>>1];
	}else if(addr >= 0772200 && addr < 0772220){
		desc = 1;
		p = &sisd[(addr-0772200)>>1];
	}else if(addr >= 0772220 && addr < 0772240){
		desc = 1;
		p = &sdsd[(addr-0772220)>>1];
	}else if(addr >= 0772240 && addr < 0772260){
		p = &sisa[(addr-0772240)>>1];
	}else if(addr >= 0772260 && addr < 0772300){
		p = &sdsa[(addr-0772260)>>1];
	}else if(addr >= 0772300 && addr < 0772320){
		desc = 1;
		p = &kisd[(addr-0772300)>>1];
	}else if(addr >= 0772320 && addr < 0772340){
		desc = 1;
		p = &kdsd[(addr-0772320)>>1];
	}else if(addr >= 0772340 && addr < 0772360){
		p = &kisa[(addr-0772340)>>1];
	}else if(addr >= 0772360 && addr < 0772400){
		p = &kdsa[(addr-0772360)>>1];
	}else{
		p = nil;
		fprint(2, "invalid write to %06uo\n", addr);
	}
	*p = SETVAL(*p, val, addr, type);
	if(desc)
		*p &= 077417;
}


static word
readundef(u32int addr, int)
{
	fprint(2, "invalid read from device %uo\n", addr);
	trap(TRAP_BUS);
	running = 0;
	return 0;
}

static void
writeundef(u32int addr, word val, int)
{
	fprint(2, "invalid write %uo to device %uo\n", val, addr);
	trap(TRAP_BUS);
	running = 0;
}


static word
readcore(u32int addr, int type)
{
	if(addr/2 >= MEMSIZE)
		trap(TRAP_BUS);
	if(type == WORD){
		return coremem[addr/2];
	}else
		return corememb[addr];
}

static void
writecore(u32int addr, word val, int type)
{
	if(addr/2 >= MEMSIZE)
		trap(TRAP_BUS);
	if(type == WORD)
		coremem[addr/2] = val;
	else
		corememb[addr] = val;
}


void
initbus(void)
{
	int i;

	for(i = 0; i < 4096; i++){
		ioreadtbl[i] = readundef;
		iowritetbl[i] = writeundef;
	}
	ioreadtbl[ADDRTOIDX(0777572)] = readmmu;
	iowritetbl[ADDRTOIDX(0777572)] = writemmu;
	ioreadtbl[ADDRTOIDX(0777574)] = readmmu;
	iowritetbl[ADDRTOIDX(0777574)] = writemmu;
	ioreadtbl[ADDRTOIDX(0777576)] = readmmu;
	iowritetbl[ADDRTOIDX(0777576)] = writemmu;
	ioreadtbl[ADDRTOIDX(0772516)] = readmmu;
	iowritetbl[ADDRTOIDX(0772516)] = writemmu;
	for(i = 0777600; i < 0777700; i += 2){
		ioreadtbl[ADDRTOIDX(i)] = readmmu;
		iowritetbl[ADDRTOIDX(i)] = writemmu;
	}
	for(i = 0772200; i < 0772400; i += 2){
		ioreadtbl[ADDRTOIDX(i)] = readmmu;
		iowritetbl[ADDRTOIDX(i)] = writemmu;
	}
	tophys = tophysnommu;
}

void
checkaddr(word addr, int type)
{
	if(type == WORD && (addr & 01)){
		fprint(2, "error: odd address %06uo %06uo\n", addr, r[PC]);
		trap(TRAP_BUS);
	}
}

word
readmem(u32int addr, int type)
{
	u32int phys;

	if(addr >= REGADDR)
		return (type == WORD) ? r[addr&7]
		                      : r[addr&7]&0377;
	checkaddr(addr, type);
	phys = tophys(addr, RO, (psw & PSW_CM) >> 14);
	return readphys(phys, type);
}

void
writemem(u32int addr, word val, int type)
{
	u32int phys;
	int i;

	if(addr >= REGADDR){
		i = addr&7;
		if(type == WORD)
			r[i] = val;
		else
			r[i] = (r[i]&~0377) | (val&0377);
		return;
	}
	checkaddr(addr, type);
	phys = tophys(addr, WO, (psw & PSW_CM) >> 14);
	writephys(phys, val, type);
}

word
readphys(u32int addr, int type)
{
	return (addr < 0760000) ? readcore(addr, type)
	                        : ioreadtbl[ADDRTOIDX(addr)](addr, type);
}

void
writephys(u32int addr, word val, int type)
{
	if(addr < 0760000)
		writecore(addr, val, type);
	else
		iowritetbl[ADDRTOIDX(addr)](addr, val, type);
}
