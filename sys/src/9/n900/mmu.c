#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

static int debug = 0;

#define L1(va) (((va)>>20) & 0xfff)
#define L2(va) (((va)>>12) & 0xff)
#define L1AP(ap) (((ap) << 10) & 0xff) /* dont care about ap[2] */
#define L2AP(ap) (((ap) << 4) & 0xff)

enum {
	/* l1 descriptor type (arm v7 manual fig. 12.4) */
	L1coarse = 1,
	L1section = 2,
	L1fine = 3,

	/* l2 descriptor type (arm v7 manual fig. 12.5) */
	L2large = 1,
	L2small = 2,
	L2tiny = 3,

	Fnoexec = 1<<0,
	Fbuffered = 1<<2,
	Fcached = 1<<3,

	APkrw = 1, /* kernel read write */
	APuro = 2, /* user read only */
	APurw = 3, /* user read write */
};

void
mmudebugl2(PTE *l2, uintptr va)
{
	uintptr startpa, pa;
	uintptr startva, endva;
	int i, t;

	t = 0;
	startpa = 0;
	startva = endva = 0;
	for(i = 0; i < 256; i++) {
		pa = l2[i] & ~((4*KiB)-1);
		if(l2[i] == 0) {
			if(endva) {
				iprint("mmudebug: l2 type %#ux %#lux %#lux -> %#lux\n", t, startva, endva, startpa);
				endva = 0;
			}
		} else {
			if(!endva) {
				startva = va;
				startpa = pa;
				t = l2[i] & (L2large | L2small | L2tiny);
			}

			endva = va + (4*KiB);
		}

		va += (4*KiB);
	}

	if(endva)
		iprint("mmudebug: l2 type %#ux %#lux %#lux -> %#lux\n", t, startva, endva, startpa);
}

void
mmudebug(char *where)
{
	PTE *l1;
	uintptr pa, startpa;
	uintptr va, startva, endva;
	int i, t;

	if(!debug)
		return;

	iprint("mmudebug: %s pid %d\n", where, m->mmupid);

	t = 0;
	l1 = m->mmul1;
	startpa = 0;
	startva = endva = 0;
	for(va = i = 0; i < 4096; i++) {
		pa = l1[i] & ~(MiB-1);
		if(l1[i] == 0) {
			if(endva) {
				iprint("mmudebug: l1 type %#ux %#lux %#lux -> %#lux\n", t, startva, endva, startpa);
				endva = 0;
			}
		} else {
			if(!endva) {
				startva = va;
				startpa = pa;
				t = l1[i] & (L1coarse|L1section|L1fine);
			}

			if(t == L1coarse) {
				mmudebugl2((PTE*) (l1[i] & ~(KiB-1)), startva);
				endva = 0;
			} else {
				endva = va + MB;
			}
		}

		va += MB;
	}

	if(endva)
		iprint("mmudebug: l1 type %#ux %#lux %#lux -> %#lux\n", t, startva, endva, startpa);
}

void
mmuinit(void)
{
	m->mmul1 = (PTE*)MACHL1(m->machno);
}

static void
mmul1empty(void)
{
	memset(m->mmul1, 0, (ROUND(USTKTOP, MiB)/MiB)*sizeof(PTE));
}

static void
mmul2empty(Proc *proc, int clear)
{
	PTE *l1;
	Page **l2, *page;

	l1 = m->mmul1;
	l2 = &proc->mmul2;
	for(page = *l2; page != nil; page = page->next) {
		if(clear)
			memset((void*)page->va, 0, BY2PG);

		l1[page->daddr] = 0;
		l2 = &page->next;
	}

	*l2 = proc->mmul2cache;
	proc->mmul2cache = proc->mmul2;
	proc->mmul2 = nil;
}

void
mmuswitch(Proc *proc)
{
	PTE *l1;
	Page *page;
	int l1x;

	if(m->mmupid == proc->pid && !proc->newtlb)
		return;
	m->mmupid = proc->pid;

	/* write back and invalidate caches */
	l1ucachewbinv();
	l2ucachewbinv();

	if(proc->newtlb) {
		mmul2empty(proc, 1);
		proc->newtlb = 0;
	}

	mmul1empty();

	/* switch to new map */
	l1 = m->mmul1;
	for(page = proc->mmul2; page != nil; page = page->next) {
		l1x = page->daddr;
		l1[l1x] = PPN(page->pa)|L1coarse;
	}

	/* FIXME: excessive invalidation */
	l1ucachewbinv();
	l2ucachewbinv();
	mmuinvalidate();
	mmudebug("mmuswitch");
}

void
mmurelease(Proc *proc)
{
	l1ucachewbinv();
	l2ucachewbinv();

	mmul2empty(proc, 0);

	freepages(proc->mmul2cache, nil, 0);
	proc->mmul2cache = nil;

	mmul1empty();

	/* FIXME: excessive invalidation */
	l1ucachewbinv();
	l2ucachewbinv();
	mmuinvalidate();
	mmudebug("mmurelease");
}

void*
mmuuncache(void *v, usize s)
{
	PTE *l1;
	uintptr va;

	assert(!((uintptr)v & (MiB-1)) && s == MiB);

	va = (uintptr)v;
	l1 = &m->mmul1[L1(va)];
	if((*l1 & (L1fine|L1section|L1coarse)) != L1section)
		return nil;

	*l1 &= ~(Fbuffered|Fcached);

	/* FIXME: excessive invalidation */
	l1ucachewbinv();
	l2ucachewbinv();
	mmuinvalidate();
	mmudebug("mmuuncache");

	return v;
}

void
putmmu(uintptr va, uintptr pa, Page *page)
{
	int l1x, s, x;
	PTE *l1, *l2;
	Page *pg;

	l1x = L1(va);
	l1 = &m->mmul1[l1x];

	/* put l1 for l2 table if needed */
	if(*l1 == 0) {
		if(up->mmul2cache == nil) {
			pg = newpage(1, 0, 0);
			pg->va = VA(kmap(pg));
		} else {
			pg = up->mmul2cache;
			up->mmul2cache = pg->next;
			memset((void*)pg->va, 0, BY2PG);
		}

		pg->daddr = l1x;
		pg->next = up->mmul2;
		up->mmul2 = pg;

		/* FIXME: excessive invalidation */
		s = splhi();
		*l1 = PPN(pg->pa)|L1coarse;
		l1ucachewbinv();
		l2ucachewbinv();
		splx(s);
	}

	/* put l2 entry */
	x = L2small;
	if(!(pa & PTEUNCACHED))
		x |= Fbuffered|Fcached;
	if(pa & PTENOEXEC)
		x |= Fnoexec;
	if(pa & PTEWRITE)
		x |= L2AP(APurw);
	else
		x |= L2AP(APuro);

	l2 = KADDR(PPN(*l1)); l2[L2(va)] = PPN(pa)|x;

	/* FIXME: excessive invalidation */
	s = splhi();
	l1ucachewbinv();
	l2ucachewbinv();
	if(needtxtflush(page)) {
		l1icacheinv();
		donetxtflush(page);
	}

	splx(s);
	mmuinvalidate();
	mmudebug("putmmu");
}

void
checkmmu(uintptr, uintptr)
{
	/* this page is intentionally left blank */
}

void
flushmmu(void)
{
	uint s;

	s = splhi();
	up->newtlb = 1; mmuswitch(up);
	splx(s);
}

uintptr
cankaddr(uintptr pa)
{
	if(pa >= PHYSMEM && pa < PHYSMEMEND)
		return PHYSMEMEND-pa;

	return 0;
}
