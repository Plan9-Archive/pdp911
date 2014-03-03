#include <u.h>
#include <libc.h>
#include <thread.h>
#include <ctype.h>
#include <bio.h>

#define min(x,y) ((x) < (y) ? (x) : (y))

typedef signed char sbyte;
typedef signed short sword;
typedef u8int byte;
typedef u16int word;

extern int cons;

/*
 * CPU
 */
extern jmp_buf nextinst;

extern word r[8], ksp, usp;
extern word psw;
extern u32int sr, dr;
extern int running;
extern int waiting;

enum { R0 = 0, R1, R2, R3, R4, R5, SP, PC };

enum { WORD = 0, BYTE };

enum
{
	PSW_CM = 0140000,
	PSW_PM = 0030000,
	PSW_PR = 0000340,
	PSW_TR = 0000020,
	PSW_N  = 0000010,
	PSW_Z  = 0000004,
	PSW_V  = 0000002,
	PSW_C  = 0000001
};

#define ISSET(flg) ((psw & (flg)) != 0)

enum
{
	TRAP_BUS  = 0004,
	TRAP_INST = 0010,
	TRAP_BPT  = 0014,
	TRAP_IOT  = 0020,
	TRAP_PWR  = 0024,
	TRAP_EMT  = 0030,
	TRAP_TRAP = 0034,
	TRAP_SEG  = 0250
};

typedef struct Interrupt Interrupt;
struct Interrupt
{
	int br;
	word vec;
	int asserted;
	Interrupt *next;
};

extern Interrupt *intrq;

void reqintr(Interrupt *intr);
void printintrq(Interrupt *intr);
void trap(word vec);

void initcpu(void);
void run(void);
void savesp(void);

/*
 * Unibus & Memory
 */

#define SETBYTE(dst, val, addr) \
	(dst&((addr & 01) ? 0377 : ~0377) | (val&0377)<<((addr & 01) ? 8 : 0))
#define GETBYTE(dst, addr) \
	((dst>>((addr & 01) ? 8 : 0))&0377)
#define SETVAL(dst, val, addr, type) \
	((type == WORD) ? val : SETBYTE(dst, val, addr))
#define GETVAL(src, addr, type) \
	((type == WORD) ? src : GETBYTE(src, addr))

#define ADDRTOIDX(a) ((a >> 1)&07777)
extern word (*ioreadtbl[4096])(u32int addr, int type);
extern void (*iowritetbl[4096])(u32int addr, word val, int type);

#define MEMSIZE (32*1024)
extern word coremem[MEMSIZE];
extern byte *corememb;
#define REGADDR MEMSIZE*2	/* pseudo virtual address of register array */

enum
{
	RO = 01,
	WO = 02,
	RW = RO | WO
};

enum
{
	KERNEL = 0,
	SUPERV = 1,
	INVMOD = 2,
	USER   = 3
};

extern u32int (*tophys)(word addr, int access, int mode);

extern word mmr2;

void initbus(void);
void checkaddr(word addr, int type);
word readmem(u32int addr, int type);
void writemem(u32int addr, word val, int type);
#define	readw(addr) readmem(addr, WORD)
#define	readb(addr) readmem(addr, BYTE)
#define	writew(addr,val) writemem(addr, val, WORD)
#define	writeb(addr,val) writemem(addr, val, BYTE)
word readphys(u32int addr, int type);
void writephys(u32int addr, word val, int type);

/*
 * Devices
 */

void clockinit(void);
void clockreset(void);

void klinit(void);
void klreset(void);

void rkinit(void);
void rkreset(void);
void rkattach(int n, char *file);
void rkdetach(int n);
void rkboot(int unit);
