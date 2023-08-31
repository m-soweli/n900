#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

enum {
	Rrev			= 0x00,
	Rsysconf		= 0x10,
	Rsysstat		= 0x14,
	Rirq			= 0x40,
	Rfiq			= 0x44,
	Rcontrol		= 0x48,
		Cnewirqgen		= 1<<0,
	Rprot			= 0x4c,
	Ridle			= 0x50,
	Rirqprio		= 0x60,
	Rfiqprio		= 0x64,
	Rthreshold		= 0x68,

	Ritr		= 0x80,
	Rmir		= 0x84,
	Rmirclear	= 0x88,
	Rmirset		= 0x8c,
	Risrset		= 0x90,
	Risrclear	= 0x94,
	Rirqpend	= 0x98,
	Rfiqpend	= 0x9c,

	Rilr		= 0x100,
};

enum {
	Nmir = 3,
	Nitr = 3,
	Nintrs = 96,
};

#define Ritrn(n)		(Ritrn + 32*(n))
#define Rmirn(n)		(Rmirn + 32*(n))
#define Rmirclearn(n)	(Rmirclear + 32*(n))
#define Rmirsetn(n)		(Rmirset + 32*(n))
#define Rirqpendn(n)	(Rirqpend + 32*(n))
#define Rfiqpendn(n)	(Rfiqpend + 32*(n))

#define Rilrn(n)	(Rilr + 4*(n))

#define csr32r(c, r) ((c)->io[(r)/4])
#define csr32w(c, r, w) ((c)->io[(r)/4] = (w))

typedef struct Intr Intr;
typedef struct Ctlr Ctlr;

struct Intr {
	void (*f)(Ureg *, void *);
	void *arg;
	char *name;

	Intr *next;
};

struct Ctlr {
	Lock;

	u32int *io;

	Intr *intrs[Nintrs];
};

static Ctlr ctlrmpu = { .io = (u32int*) PHYSINTRMPU };

void
intrinit(void)
{
	Ctlr *ctlr = &ctlrmpu;
	int i;

	/* mask all interrupts */
	for (i = 0; i < Nmir; i++)
		csr32w(ctlr, Rmirsetn(i), ~0);

	/* protection off, threshold off, set all intrs priority 0, mapped to irq */
	csr32w(ctlr, Rcontrol, 0);
	csr32w(ctlr, Rthreshold, 0xff);
	for (i = 0; i < Nintrs; i++)
		csr32w(ctlr, Rilrn(i), 0);

	coherence();
}

void
intrenable(int n, void (*f)(Ureg *, void *), void *arg, int, char *name)
{
	Ctlr *ctlr = &ctlrmpu;
	Intr *intr;

	if (n >= nelem(ctlr->intrs) || n < 0)
		panic("intrenable %d", n);

	intr = malloc(sizeof(*intr));
	if(!intr)
		panic("intrenable: no memory for interrupt");

	intr->f = f;
	intr->arg = arg;
	intr->name = name;

	lock(ctlr);

	/* chain this interrupt */
	intr->next = ctlr->intrs[n];
	ctlr->intrs[n] = intr;

	/* new handler assigned, unmask this interrupt */
	csr32w(ctlr, Rmirclearn(n >> 5), 1 << (n & 31));

	unlock(ctlr);
	coherence();
}

void
intrdisable(int n, void (*f)(Ureg *, void *), void *arg, int, char *name)
{
	Ctlr *ctlr = &ctlrmpu;
	Intr *intr, **ip;

	if (n >= nelem(ctlr->intrs) || n < 0)
		panic("intrdisable %d", n);

	lock(ctlr);
	for(ip = &ctlr->intrs[n]; intr = *ip; ip = &intr->next) {
		if(intr->f == f && intr->arg == arg && strcmp(intr->name, name) == 0) {
			*ip = intr->next;
			free(intr);
			break;
		}
	}

	/* no more handlers assigned, mask this interrupt */
	if(ctlr->intrs[n] == nil)
		csr32w(ctlr, Rmirsetn(n >> 5), 1 << (n & 31));

	unlock(ctlr);
	coherence();
}

void
intr(Ureg *ureg)
{
	Ctlr *ctlr = &ctlrmpu;
	Intr *intr;
	int n, h, s;

	h = 0;
	n = csr32r(ctlr, Rirq) & 0x7f;
	s = csr32r(ctlr, Rirq) & ~0x7f;
	if(s) {
		/* interrupt controller reports spurious interrupt flag. */
		iprint("cpu%d: spurious interrupt\n", m->machno);
		csr32w(ctlr, Rcontrol, Cnewirqgen);
		return;
	}

	if(n >= nelem(ctlr->intrs)) {
		iprint("cpu%d: invalid interrupt %d\n", m->machno, n);
		csr32w(ctlr, Rcontrol, Cnewirqgen);
		return;
	}

	/* call all handlers for this interrupt number */
	for (intr = ctlr->intrs[n]; intr; intr = intr->next) {
		if(intr->f) {
			if(islo())
				panic("trap: islo() in interrupt handler\n");

			intr->f(ureg, intr->arg);
			if(islo())
				panic("trap: islo() after interrupt handler\n");
		}

		h++;
	}

	csr32w(ctlr, Rcontrol, Cnewirqgen);
	coherence();

	if(!h) iprint("cpu%d: spurious interrupt %d\n", m->machno, n);
	if(up) {
		if(n >= IRQTIMER1 && n <= IRQTIMER11) {
			if(up->delaysched) {
				splhi();
				sched();
			}
		} else {
			preempted();
		}
	}
}
