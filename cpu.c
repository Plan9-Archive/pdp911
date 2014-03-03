#include "pdp11.h"

jmp_buf nextinst;

word coremem[MEMSIZE];
byte *corememb = (byte*) coremem;
word r[8], ksp, usp;
word psw;
u32int sr, dr;
int running;
int waiting;
word trapvec;
Interrupt *intrq;

void setpsw(word npsw, int setprev);
void execute(word inst);

word
getintr(void)
{
	int pl;
	word vec;

	pl = (psw>>5)&07;
	if(intrq == nil || intrq->br <= pl)
		return 0;
	vec = intrq->vec;
	intrq->asserted = 0;
	intrq = intrq->next;
	return vec;
}

void
handleintr(word vec)
{
	word pc, sw;

	pc = r[PC];
	sw = psw;
	r[PC] = readphys(tophys(vec, RO, KERNEL), WORD);
	setpsw(readphys(tophys(vec+2, RO, KERNEL), WORD), 1);
	r[SP] -= 2;
	writew(r[SP], sw);
	r[SP] -= 2;
	writew(r[SP], pc);
//	print("trap through %o: %06uo %06uo (sp: %06uo), %06uo %06uo\n", vec, pc, sw, r[SP], r[PC], psw);
}

void
reqintr(Interrupt *intr)
{
	Interrupt **i;

	if(intr->asserted)
		return;
	intr->asserted++;

	for(i = &intrq; *i; i = &(*i)->next)
		if((*i)->br < intr->br)
			break;
		else if((*i)->br == intr->br)
			if((*i)->vec > intr->vec)
				break;

	intr->next = *i;
	*i = intr;
}

void
printintrq(Interrupt *intr)
{
	if(intr){
		print("[ %uo %uo ]->", intr->br, intr->vec);
		printintrq(intr->next);
	}else
		print("nil\n");
}

word
fetch(void)
{
	word inst;
	mmr2 = r[PC];
	inst = readw(r[PC]);
	r[PC] += 2;
	return inst;
}

void
trap(word vec)
{
	trapvec = vec;
	longjmp(nextinst, 0);
}

void
run(void)
{
	running = 1;
	waiting = 0;
	trapvec = 0;
	setjmp(nextinst);
	while(running){
		if(trapvec){
//			print("TRAP at %uo through %uo (psw: %06uo)\n", r[PC]-2, trapvec, psw);
			handleintr(trapvec);
			trapvec = 0;
			waiting = 0;
			continue;
		}
		if(!waiting)
			execute(fetch());
		trapvec = getintr();
		yield();
	}
}

void
savesp(void)
{
	if((psw&PSW_CM) == 0)
		ksp = r[SP];
	else
		usp = r[SP];
}

void
loadsp(void)
{
	if((psw&PSW_CM) == 0)
		r[SP] = ksp;
	else
		r[SP] = usp;
}

void
setpsw(word npsw, int setprev)
{
	savesp();
	if(setprev)
		psw = (npsw&~PSW_PM) | ((psw&PSW_CM) >> 2);
	else
		psw = npsw;
	psw &= ~07400;
	loadsp();
}

static word
readcpu(u32int addr, int type)
{
	switch(addr){
	case 0777570:
		return GETVAL(sr, addr, type);
	case 0777776:
		return GETVAL(psw, addr, type);
	default:
		print("can't read from %uo\n", addr);
		return 0;
	}
}

static void
writecpu(u32int addr, word val, int type)
{
	word npsw;
	switch(addr){
	case 0777570:
		dr = SETVAL(dr, val, addr, type);
		break;
	case 0777776:
		npsw = SETVAL(psw, val, addr, type);
		setpsw(npsw, 0);
		break;
	default:
		print("can't write to %uo\n", addr);
	}
}

void
initcpu(void)
{
	int i;

	psw = 0340;
	for(i = 0; i < 8; i++)
		r[i] = 0;
	ksp = usp = 0;
	intrq = nil;
	ioreadtbl[ADDRTOIDX(0777776)] = readcpu;
	ioreadtbl[ADDRTOIDX(0777570)] = readcpu;
	iowritetbl[ADDRTOIDX(0777776)] = writecpu;
	iowritetbl[ADDRTOIDX(0777570)] = writecpu;
}

u32int
getaddr(int am, int type)
{
	word i;
	word ret;
	int inc;

	ret = 0;
	i = am&07;
	inc = 2;
	if(type == BYTE && i != PC && i != SP)
		inc = 1;
	switch((am>>3)&07){
	case 0:
		return REGADDR + i;
	case 1:
		return r[i];
	case 2:
		ret = r[i];
		r[i] += inc;
		break;
	case 3:
		ret = readw(r[i]);
		r[i] += 2;
		break;
	case 4:
		r[i] -= inc;
		return r[i];
	case 5:
		r[i] -= 2;
		return readw(r[i]);
	case 6:
		ret = readw(r[PC]);
		r[PC] += 2;
		ret += r[i];
		break;
	case 7:
		ret = readw(r[PC]);
		r[PC] += 2;
		ret += r[i];
		ret = readw(ret);
		break;
	}
	return ret;
}

void
execute(word inst)
{
	int type;
	word sign, mask;
	int m1, m2, rn;
	u32int src, dst;
	word val, val2;
	sword sval, sval2;
	int res;
	sword broff;
	int prevmode;

	type = (inst & 0100000) ? BYTE : WORD;
	if(type == WORD){
		sign = 0100000;
		mask =  0177777;
	}else{
		sign = 0200;
		mask  = 0377;
	}
	rn = (inst>>6) & 07;
	m1 = (inst>>6) & 077;
	m2 = inst & 077;
	broff = (sbyte) inst;
	broff <<= 1;

	switch(inst & 0070000){
	case 0000000:
		break;

	/* MOV(B) */
	case 0010000:
		src = getaddr(m1, type);
		dst = getaddr(m2, type);
		val = readmem(src, type);
		if(type == BYTE && (m2 & 070) == 0){
			val = (sbyte) val;
			type = WORD;
			sign = 0100000;
		}
		psw &= ~(PSW_N | PSW_Z | PSW_V);
		if(val & sign) psw |= PSW_N;
		if(val == 0) psw |= PSW_Z;
		writemem(dst, val, type);
		return;

	/* CMP(B) */
	case 0020000:
		src = getaddr(m1, type);
		dst = getaddr(m2, type);
		val = readmem(src, type);
		val2 = readmem(dst, type);
		res = val + ((~val2 + 1) & mask);
		psw &= ~(PSW_N | PSW_Z | PSW_V | PSW_C);
		if(res & sign) psw |= PSW_N;
		if((res&mask) == 0) psw |= PSW_Z;
		if((res&sign) == (val2&sign) &&
		   (val&sign) != (val2&sign))
			psw |= PSW_V;
		if(val2 > val) psw |= PSW_C;
		return;

	/* BIT(B) */
	case 0030000:	
		src = getaddr(m1, type);
		dst = getaddr(m2, type);
		val = readmem(src, type);
		val2 = readmem(dst, type);
		val2 &= val;
		psw &= ~(PSW_N | PSW_Z | PSW_V);
		if(val2&sign) psw |= PSW_N;
		if(val2 == 0) psw |= PSW_Z;
		return;

	/* BIC(B) */
	case 0040000:	
		src = getaddr(m1, type);
		dst = getaddr(m2, type);
		val = readmem(src, type);
		val2 = readmem(dst, type);
		val2 &= ~val;
		psw &= ~(PSW_N | PSW_Z | PSW_V);
		if(val2&sign) psw |= PSW_N;
		if(val2 == 0) psw |= PSW_Z;
		writemem(dst, val2, type);
		return;

	/* BIS(B) */
	case 0050000:	
		src = getaddr(m1, type);
		dst = getaddr(m2, type);
		val = readmem(src, type);
		val2 = readmem(dst, type);
		val2 |= val;
		psw &= ~(PSW_N | PSW_Z | PSW_V);
		if(val2&sign) psw |= PSW_N;
		if(val2 == 0) psw |= PSW_Z;
		writemem(dst, val2, type);
		return;

	/* ADD/SUB */
	case 0060000:
		src = getaddr(m1, WORD);
		dst = getaddr(m2, WORD);
		val = readmem(src, WORD);
		val2 = readmem(dst, WORD);
		psw &= ~(PSW_N | PSW_Z | PSW_V | PSW_C);
		if(inst&0100000){
			res = val2 + ((~val + 1) & 0177777);
			if((res&0100000) == (val2&0100000) &&
			   (val&0100000) != (val2&0100000))
				psw |= PSW_V;
			if(val2 < val) psw |= PSW_C;
		}else{
			res  = val + val2;
			if((res&0100000) != (val2&0100000) &&
			   (val&0100000) == (val2&0100000))
				psw |= PSW_V;
			if(res & ~0177777) psw |= PSW_C;
		}
		if(res & 0100000) psw |= PSW_N;
		if((res&0177777) == 0) psw |= PSW_Z;
		writemem(dst, res, WORD);
		return;
	}

	switch(inst&0777000){
	/* MUL */
	case 0070000:
		src = getaddr(m2, WORD);
		sval = r[rn];
		sval2 = readmem(src, WORD);
		res = sval * sval2;
		if(rn%2 == 0) r[rn++] = res >> 16;
		r[rn] = res;
		psw &= ~(PSW_N | PSW_Z | PSW_V | PSW_C);
		if(res < 0) psw |= PSW_N;
		if(res == 0) psw |= PSW_Z;
		if(res < -0100000 || res >= 0100000) psw |= PSW_C;
		return;

	/* DIV */
	case 0071000:{
		int num, quot, rem;

		src = getaddr(m2, WORD);
		sval = readmem(src, WORD);
		psw &= ~(PSW_N | PSW_Z | PSW_V | PSW_C);
		if(sval == 0) goto zero;
		if(rn%2 != 0) goto undef;
		num = r[rn]<<16;
		num |= r[rn+1];
		quot = num / sval;
		rem = num % sval;
		if((quot&~0177777) != 0 && (quot&~0177777) != ~0177777) goto undef;
		if(quot < 0) psw |= PSW_N;
		if(quot == 0) psw |= PSW_Z;
		r[rn] = quot;
		r[rn+1] = rem;
		return;
	zero:
		psw |= PSW_C;
	undef:
		psw |= PSW_V;
		return;}

	/* ASH */
	case 0072000:
		src = getaddr(m2, WORD);
		sval2 = readmem(src, WORD) & 077;
		if(sval2 & 040) sval2 |= ~077;
		sval = r[rn];
		psw &= ~(PSW_N | PSW_Z | PSW_V | PSW_C);
		if(sval2 < 0){
			if((sval >> -sval2-1) & 01) psw |= PSW_C;
			sval >>= -sval2;
		}else if(sval2 > 0){
			mask = (sword)0100000 >> sval2;
			if((sval&mask) != 0 && (sval&mask) != mask) psw |= PSW_V;
			if((sval << sval2-1) & 0100000) psw |= PSW_C;
			sval <<= sval2;
		}
		if(sval < 0) psw |= PSW_N;
		if(sval == 0) psw |= PSW_Z;
		r[rn] = sval;
		return;

	/* ASHC */
	case 0073000:{
		int reg, mask;

		src = getaddr(m2, WORD);
		sval = readmem(src, WORD) & 077;
		if(sval & 040) sval |= ~077;
		reg = r[rn]<<16;
		reg |= r[rn+1];
		psw &= ~(PSW_N | PSW_Z | PSW_V | PSW_C);
		if(sval < 0){
			if((sval >> -sval-1) & 01) psw |= PSW_C;
			reg >>= -sval;
		}else if(sval > 0){
			mask = (sword)020000000000 >> sval;
			if((reg&mask) != 0 && (reg&mask) != mask) psw |= PSW_V;
			if((reg << sval-1) & 020000000000) psw |= PSW_C;
			reg <<= sval;
		}
		if(reg < 0) psw |= PSW_N;
		if(reg == 0) psw |= PSW_Z;
		r[rn++] = reg>>16;
		r[rn] = reg;
		return;}

	/* XOR */
	case 0074000:
		dst = getaddr(m2, WORD);
		val = r[rn];
		val2 = readmem(dst, WORD);
		val2 ^= val;
		psw &= ~(PSW_N | PSW_Z | PSW_V);
		if(val2&0100000) psw |= PSW_N;
		if(val2 == 0) psw |= PSW_Z;
		writemem(dst, val2, WORD);
		return;

	/* SOB */
	case 0077000:
		if(--r[rn] != 0)
			r[PC] -= m2 << 1;
		return;

	/* JSR */
	case 0004000:
		if((m2 & 070) == 0)
			trap(TRAP_INST);
		val2 = getaddr(m2, WORD);
		val = r[rn];
		dst = getaddr(046, WORD);
		writemem(dst, val, WORD);
		r[rn] = r[PC];
		r[PC] = val2;
		return;
	}

	switch(inst&0077700){
	/* CLR(B) */
	case 0005000:
		psw |= PSW_Z;
		dst = getaddr(m2, type);
		writemem(dst, 0, type);
		psw &= ~(PSW_N | PSW_Z | PSW_V | PSW_C);
		return;

	/* COM(B) */
	case 0005100:
		psw |= PSW_C;
		dst = getaddr(m2, type);
		val = ~readmem(dst, type);
		psw &= ~(PSW_N | PSW_Z | PSW_V);
		if(val&sign) psw |= PSW_N;
		if(val == 0) psw |= PSW_Z;
		writemem(dst, val, type);
		return;

	/* INC(B) */
	case 0005200:
		dst = getaddr(m2, type);
		val = readmem(dst, type) + 1;
		psw &= ~(PSW_N | PSW_Z | PSW_V);
		if(val&sign) psw |= PSW_N;
		if(val == 0) psw |= PSW_Z;
		if(val == sign) psw |= PSW_V;
		writemem(dst, val, type);
		return;

	/* DEC(B) */
	case 0005300:
		dst = getaddr(m2, type);
		val = readmem(dst, type) - 1;
		psw &= ~(PSW_N | PSW_Z | PSW_V);
		if(val&sign) psw |= PSW_N;
		if(val == 0) psw |= PSW_Z;
		if(val == mask-sign) psw |= PSW_V;
		writemem(dst, val, type);
		return;

	/* NEG(B) */
	case 0005400:
		dst = getaddr(m2, type);
		val = ~readmem(dst, type) + 1;
		psw &= ~(PSW_N | PSW_Z | PSW_V | PSW_C);
		if(val & sign) psw |= PSW_N;
		if(val == 0) psw |= PSW_Z;
		if(val == sign) psw |= PSW_V;
		if(val != 0) psw |= PSW_C;
		writemem(dst, val, type);
		return;

	/* ADC(B) */
	case 0005500:
		res = psw & PSW_C;
		dst = getaddr(m2, type);
		val = readmem(dst, type);
		psw &= ~(PSW_N | PSW_Z | PSW_V | PSW_C);
		if(res){
			if(val == mask-sign) psw |= PSW_V;
			if(val == mask) psw |= PSW_C;
			val++;
		}
		if(val & sign) psw |= PSW_N;
		if(val == 0) psw |= PSW_Z;
		writemem(dst, val, type);
		return;

	/* SBC(B) */
	case 0005600:
		dst = getaddr(m2, type);
		val = readmem(dst, type);
		res = psw & PSW_C;
		psw &= ~(PSW_N | PSW_Z | PSW_V);
		psw |= PSW_C;
		if(res){
			if(val == sign) psw |= PSW_V;
			if(val == 0) psw &= ~PSW_C;
			val--;
		}
		if(val & sign) psw |= PSW_N;
		if(val == 0) psw |= PSW_Z;
		writemem(dst, val, type);
		return;

	/* TST(B) */
	case 0005700:
		dst = getaddr(m2, type);
		val = readmem(dst, type);
		psw &= ~(PSW_N | PSW_Z | PSW_V | PSW_C);
		if(val & sign) psw |= PSW_N;
		if(val == 0) psw |= PSW_Z;
		return;

	/* ROR(B) */
	case 0006000:
		res = psw & PSW_C;
		dst = getaddr(m2, type);
		sval = readmem(dst, type);
		psw &= ~(PSW_N | PSW_Z | PSW_V | PSW_C);
		if(sval&01) psw |= PSW_C;
		sval = (sval >> 1) & ~sign;
		if(res) sval |= sign;
		if(sval&sign) psw |= PSW_N;
		if(sval == 0) psw |= PSW_Z;
		if(ISSET(PSW_N) ^ ISSET(PSW_C))
			psw |= PSW_V;
		writemem(dst, sval, type);
		return;

	/* ROL(B) */
	case 0006100:
		res = psw & PSW_C;
		dst = getaddr(m2, type);
		sval = readmem(dst, type);
		psw &= ~(PSW_N | PSW_Z | PSW_V | PSW_C);
		if(sval&sign) psw |= PSW_C;
		sval <<= 1;
		if(res) sval |= 01;
		if(sval&sign) psw |= PSW_N;
		if(sval == 0) psw |= PSW_Z;
		if(ISSET(PSW_N) ^ ISSET(PSW_C))
			psw |= PSW_V;
		writemem(dst, sval, type);
		return;

	/* ASR(B) */
	case 0006200:
		dst = getaddr(m2, type);
		sval = readmem(dst, type);
		psw &= ~(PSW_N | PSW_Z | PSW_V | PSW_C);
		if(sval&01) psw |= PSW_C;
		sval >>= 1;
		if(sval&sign) psw |= PSW_N;
		if(sval == 0) psw |= PSW_Z;
		if(ISSET(PSW_N) ^ ISSET(PSW_C))
			psw |= PSW_V;
		writemem(dst, sval, type);
		return;

	/* ASL(B) */
	case 0006300:
		dst = getaddr(m2, type);
		sval = readmem(dst, type);
		psw &= ~(PSW_N | PSW_Z | PSW_V | PSW_C);
		if(sval&sign) psw |= PSW_C;
		sval <<= 1;
		if(sval&sign) psw |= PSW_N;
		if(sval == 0) psw |= PSW_Z;
		if(ISSET(PSW_N) ^ ISSET(PSW_C))
			psw |= PSW_V;
		writemem(dst, sval, type);
		return;

	/* MFPI */
	case 0006500:
		if(type == BYTE)
			break;
		prevmode = (psw&PSW_PM)>>12;
		if((m2&070) == 0){	/* register */
			if((m2&07) == SP){
				savesp();
				val = (prevmode == KERNEL) ? ksp : usp;
			}else
				val = r[m2&07];
		}else{
			src = getaddr(m2, WORD);
			checkaddr(src, WORD);
			src = tophys(src, RO, prevmode);
			val = readphys(src, WORD);
		}
		psw &= ~(PSW_N | PSW_Z | PSW_V);
		if(val & 0100000) psw |= PSW_N;
		if(val == 0) psw |= PSW_Z;
		dst = getaddr(046, WORD);
		writemem(dst, val, WORD);
		return;

	/* MTPI */
	case 0006600:
		if(type == BYTE)
			break;
		src = getaddr(026, WORD);
		val = readmem(src, WORD);
		psw &= ~(PSW_N | PSW_Z | PSW_V);
		if(val & 0100000) psw |= PSW_N;
		if(val == 0) psw |= PSW_Z;
		prevmode = (psw&PSW_PM)>>12;
		if((m2&070) == 0){	/* register */
			if((m2&07) == SP){
				savesp();
				if(prevmode == KERNEL)
					ksp = val;
				else
					usp = val; 
				loadsp();
			}else
				r[m2&07] = val;
		}else{
			dst = getaddr(m2, WORD);
			checkaddr(dst, WORD);
			dst = tophys(dst, WO, prevmode);
			writephys(dst, val, WORD);
		}
		return;

	/* SXT */
	case 0006700:
		if(type == BYTE)
			break;		
		dst = getaddr(m2, WORD);
		psw &= ~PSW_Z;
		if(ISSET(PSW_N))
			writemem(dst, 0177777, WORD);
		else{
			psw |= PSW_Z;
			writemem(dst, 0, WORD);
		}
		return;

	/* JMP */
	case 0000100:
		if(type == BYTE)
			break;
		if((m2 & 070) == 0)
			trap(TRAP_INST);
		r[PC] = getaddr(m2, WORD);
		return;		

	/* RTS; CCC; SCC */
	case 0000200:
		if(type == BYTE)
			break;
		/* RTS */
		if((m2&070) == 0){
			rn = m2&07;
			r[PC] = r[rn];
			src = getaddr(026, WORD);
			r[rn] = readmem(src, WORD);
		/* CCC; SCC */
		}else{
			if(inst&020)
				psw |= inst&017;
			else
				psw &= ~(inst&017);
		}
		return;

	/* SWAB */
	case 0000300:
		if(type == BYTE)
			break;		
		dst = getaddr(m2, WORD);
		val = readmem(dst, WORD);
		val = (val&0377)<<8 | (val>>8)&0377;
		psw &= ~(PSW_N | PSW_Z | PSW_V | PSW_C);
		if((val&0377) == 0) psw |= PSW_Z;
		if(val&0200) psw |= PSW_N;
		writemem(dst, val, WORD);
		return;
	}

	switch(inst&0177400){
	/* BR */
	case 0000400:
		r[PC] += broff;
		return;

	/* BNE */
	case 0001000:
		if(!ISSET(PSW_Z)) r[PC] += broff;
		return;

	/* BEQ */
	case 0001400:
		if(ISSET(PSW_Z)) r[PC] += broff;
		return;

	/* BGE */
	case 0002000:
		if(ISSET(PSW_N) == ISSET(PSW_V)) r[PC] += broff;
		return;

	/* BLT */
	case 0002400:
		if(ISSET(PSW_N) != ISSET(PSW_V)) r[PC] += broff;
		return;

	/* BGT */
	case 0003000:
		if(ISSET(PSW_N) == ISSET(PSW_V) && !ISSET(PSW_Z)) r[PC] += broff;
		return;

	/* BLE */
	case 0003400:
		if(ISSET(PSW_N) != ISSET(PSW_V) || ISSET(PSW_Z)) r[PC] += broff;
		return;

	/* BPL */
	case 0100000:
		if(!ISSET(PSW_N)) r[PC] += broff;
		return;

	/* BMI */
	case 0100400:
		if(ISSET(PSW_N)) r[PC] += broff;
		return;

	/* BHI */
	case 0101000:
		if(!ISSET(PSW_C) && !ISSET(PSW_Z)) r[PC] += broff;
		return;

	/* BLOS */
	case 0101400:
		if(ISSET(PSW_C) || ISSET(PSW_Z)) r[PC] += broff;
		return;

	/* BVC */
	case 0102000:
		if(!ISSET(PSW_V)) r[PC] += broff;
		return;

	/* BVS */
	case 0102400:
		if(ISSET(PSW_V)) r[PC] += broff;
		return;

	/* BCC,BHIS */
	case 0103000:
		if(!ISSET(PSW_C)) r[PC] += broff;
		return;

	/* BCS,BLO */
	case 0103400:
		if(ISSET(PSW_C)) r[PC] += broff;
		return;

	/* EMT */
	case 0104000:
		trap(TRAP_EMT);

	/* TRAP */
	case 0104400:
		//print("software trap\n");
		trap(TRAP_TRAP);

	/* MARK */
	case 0006400:
		/* TODO */
		print("instruction mark to implemented\n");
		running = 0;
		return;
	}

	switch(inst){
	/* HALT */
	case 0000000:
		if(psw&PSW_CM)
			trap(TRAP_BUS);
		running = 0;
		return;

	/* WAIT */
	case 0000001:
		waiting = 1;
		return;

	/* RTI */
	case 0000002:
		r[PC] = readw(r[SP]);
		r[SP] += 2;
		val = readw(r[SP]);
		r[SP] += 2;
		setpsw(val, 0);
		return;

	/* BPT */
	case 0000003:
		trap(TRAP_BPT);

	/* IOT */
	case 0000004:
		trap(TRAP_IOT);

	/* RESET */
	case 0000005:
		if(psw&PSW_CM)
			return;
		clockreset();
		klreset();
		rkreset();
		return;

	/* RTT */
	case 0000006:
		r[PC] = readw(r[SP]);
		//print("rtt: %06uo ", r[PC]);
		r[SP] += 2;
		val = readw(r[SP]);
		//print("%06uo (sp: %06uo)\n", val, r[SP]-2);
		r[SP] += 2;
		setpsw(val, 0);
		return;
	}

	//fprint(2, "illegal instruction: %06uo %06uo\n", inst, r[PC]-2);
	trap(TRAP_INST);
}
