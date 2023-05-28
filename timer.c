#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

enum {
	Rrev		= 0x00,
	Rsyscfg		= 0x10,
		Cidle		= 1<<0,
		Creset		= 1<<1,
	Rsysstat	= 0x14,
		Sreset		= 1<<0,
	Risr		= 0x18,
	Rier		= 0x1c,
		Imatch		= 1<<0,
		Ioverflow	= 1<<1,
		Icapture	= 1<<2,
	Rclr		= 0x24,
		CLst		= 1<<0,
		CLar		= 1<<1,
		CLtgovf		= 1<<10,
		CLtgmatch	= 1<<11,
	Rcrr		= 0x28,
	Rldr		= 0x2c,
};

typedef union Counter Counter;
typedef struct Ctlr Ctlr;

union Counter {
	uvlong cnt;
	struct {
		ulong cntlo;
		ulong cnthi;
	};
};

struct Ctlr {
	Lock;
	Counter;

	u32int *io;
};

static Ctlr timers[] = {
	{ .io = (u32int*) PHYSTIMER1 }, /* for cycles */
	{ .io = (u32int*) PHYSTIMER2 }, /* for interrupts */
};

#define csr32r(c, r) ((c)->io[(r)/4])
#define csr32w(c, r, w) ((c)->io[(r)/4] = (w))

static void
timerreset(Ctlr *ctlr)
{
	int i;
	int cfg;

	cfg = csr32r(ctlr, Rsyscfg);
	cfg |= Creset;
	cfg &= ~Cidle;

	ilock(ctlr);
	csr32w(ctlr, Rsyscfg, cfg);
	for(i = 40000; i > 0; i++) {
		if(csr32r(ctlr, Rsysstat) & Sreset)
			break;
	}

	if(i == 0)
		panic("clock reset failed");

	iunlock(ctlr);
}

static void
timerstartcycles(Ctlr *ctlr)
{
	timerreset(ctlr);

	/* configure this timer for measuring cycles */
	ilock(ctlr);
	csr32w(ctlr, Rldr, 0);
	csr32w(ctlr, Rclr, CLst | CLar);
	iunlock(ctlr);
}

static void
timerstartintr(Ctlr *ctlr, ulong t)
{
	timerreset(ctlr);

	/* configure this timer for periodic interrupts */
	ilock(ctlr);
	csr32w(ctlr, Rier, Ioverflow);
	csr32w(ctlr, Rldr, -t);
	csr32w(ctlr, Rcrr, -t);
	csr32w(ctlr, Rclr, CLst | CLar);
	iunlock(ctlr);
}

static void
timerinterrupt(Ureg *u, void *arg)
{
	Ctlr *ctlr = arg;
	csr32w(ctlr, Risr, Ioverflow);

	timerintr(u, 0);
}

void
timerinit(void)
{
	intrenable(IRQTIMER2, timerinterrupt, &timers[1], BUSUNKNOWN, "timer");

	timerstartcycles(&timers[0]);
	timerstartintr(&timers[1], 32);
}

uvlong
fastticks(uvlong *hz)
{
	Counter c;
	Ctlr *ctlr;

	/* FIXME: this has poor precision, but qemu has no cycle counter */
	ctlr = &timers[0];
	if(hz)
		*hz = 32*1024;

	ilock(ctlr);
	c.cnt = ctlr->cnt;
	c.cntlo = csr32r(ctlr, Rcrr);
	if(c.cnt < ctlr->cnt)
		c.cnthi++;

	ctlr->cnt = c.cnt;
	iunlock(ctlr);

	return ctlr->cnt;
}

ulong
µs(void)
{
	return fastticks2us(fastticks(nil));
}

void
microdelay(int n)
{
	ulong now;

	now = µs();
	while(µs() - now < n)
		;
}

void
delay(int n)
{
	while(--n >= 0)
		microdelay(1000);
}

void
timerset(Tval)
{
	/* FIXME: ? */
}
