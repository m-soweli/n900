#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "../port/systab.h"

#include "tos.h"
#include "ureg.h"

void
callwithureg(void (*f) (Ureg *))
{
	Ureg u;

	u.pc = getcallerpc(&f);
	u.sp = (uintptr) &f - 4;
	f(&u);
}

void
dumpstackureg(Ureg *ureg)
{
	uintptr l, v, i, estack;
	int x;

	x = 0;
	x += iprint("ktrace /arm/9n900 %#.8lux %#.8lux %#.8lux <<EOF\n",
		ureg->pc, ureg->sp, ureg->r14);

	i = 0;
	if(up)
		estack = (uintptr)up;
	else
		estack = (uintptr)m+MACHSZ;

	x += iprint("estackx %p\n", estack);
	for(l = (uintptr)&l; l < estack; l += sizeof(uintptr)) {
		v = *(uintptr*)l;
		if((KTZERO < v && v < (uintptr)etext) || estack-l < 32) {
			x += iprint("%.8p=%.8p ", l, v);
			i++;
		}

		if(i == 4) {
			i = 0;
			x += iprint("\n");
		}
	}

	if(i)
		iprint("\n");
	iprint("EOF\n");
}

void
dumpstack(void)
{
	callwithureg(dumpstackureg);
}

void
dumpureg(Ureg *ureg)
{
	if(up)
		iprint("cpu%d: registers for %s %lud\n", m->machno, up->text, up->pid);
	else
		iprint("cpu%d: registers for kernel\n", m->machno);

	iprint("r0 %#.8lux\tr1 %#.8lux\tr2 %#.8lux\tr3 %#.8lux\n", ureg->r0, ureg->r1, ureg->r2, ureg->r3);
	iprint("r4 %#.8lux\tr5 %#.8lux\tr6 %#.8lux\tr7 %#.8lux\n", ureg->r4, ureg->r5, ureg->r6, ureg->r7);
	iprint("r8 %#.8lux\tr9 %#.8lux\tr10 %#.8lux\tr11 %#.8lux\n", ureg->r8, ureg->r9, ureg->r10, ureg->r11);
	iprint("r12 %#.8lux\tr13 %#.8lux\tr14 %#.8lux\tr15 %#.8lux\n", ureg->r12, ureg->r13, ureg->r14, ureg->pc);
}


uintptr
userpc(void)
{
	return ((Ureg*)up->dbgreg)->pc;
}

uintptr
dbgpc(Proc *)
{
	if(up->dbgreg)
		return userpc();

	return 0;
}

void
procsetup(Proc *p)
{
	p->fpstate = FPinit;
	fpoff();
}

void
procsave(Proc *p)
{
	if(p->fpstate == FPactive) {
		if(p->state == Moribund)
			fpclear();
		else
			fpsave(p->fpsave);
		p->fpstate = FPinactive;
	}
}

void
procrestore(Proc *)
{
}

void
procfork(Proc *p)
{
	ulong s;

	s = splhi();
	switch(up->fpstate & ~FPillegal) {
	case FPactive:
		fpsave(up->fpsave);
		up->fpstate = FPinactive;
	case FPinactive:
		memmove(p->fpsave, up->fpsave, sizeof(FPsave));
		p->fpstate = FPinactive;
	}

	splx(s);
}

void
kprocchild(Proc *p, void (*entry)(void))
{
	p->sched.pc = (uintptr) entry;
	p->sched.sp = (uintptr) p;
}

void
forkchild(Proc *p, Ureg *ureg)
{
	Ureg *cureg;

	p->sched.sp = (uintptr) p - sizeof(Ureg);
	p->sched.pc = (uintptr) forkret;

	cureg = (Ureg*) p->sched.sp;
	memmove(cureg, ureg, sizeof(Ureg));
	cureg->r0 = 0;
}

uintptr
execregs(uintptr entry, ulong ssize, ulong nargs)
{
	ulong *sp;
	Ureg *ureg;

	sp = (ulong*)(USTKTOP - ssize); *--sp = nargs;
	ureg = up->dbgreg;
	ureg->sp = (uintptr) sp;
	ureg->pc = entry;
	ureg->r14 = 0;

	return USTKTOP-sizeof(Tos);
}

void
setkernur(Ureg *ureg, Proc *p)
{
	ureg->pc = p->sched.pc;
	ureg->sp = p->sched.sp+4;
	ureg->r14 = (uintptr) sched;
}

void
setregisters(Ureg *ureg, char *pureg, char *uva, int n)
{
	uvlong v;

	v = ureg->psr;
	memmove(pureg, uva, n);
	ureg->psr &= ~(PsrMask|PsrDfiq|PsrDirq);
	ureg->psr |= v & (PsrMask|PsrDfiq|PsrDirq);
}

void
evenaddr(uintptr addr)
{
	if(addr & 3) {
		postnote(up, 1, "sys: odd address", NDebug);
		error(Ebadarg);
	}
}

int
notify(Ureg *ureg)
{
	ulong s, sp;
	char *msg;

	if(up->procctl)
		procctl();
	if(up->nnote == 0)
		return 0;
	
	if(up->fpstate == FPactive) {
		fpsave(up->fpsave);
		up->fpstate = FPinactive;
	}
	up->fpstate |= FPillegal;

	s = spllo();
	qlock(&up->debug);
	msg = popnote(ureg);
	if(msg == nil) {
		qunlock(&up->debug);
		splhi();
		return 0;
	}

	sp = ureg->sp;
	sp -= 256;
	sp -= sizeof(Ureg);

	if(!okaddr((uintptr)up->notify, 1, 0)
	|| !okaddr(sp-ERRMAX-4*BY2WD, sizeof(Ureg)+ERRMAX+4*BY2WD, 1)
	|| ((uintptr) up->notify & 3) != 0
	|| (sp & 3) != 0) {
		qunlock(&up->debug);
		pprint("suicide: bad address in notify\n");
		pexit("Suicide", 0);
	}

	memmove((Ureg*)sp, ureg, sizeof(Ureg));
	*(Ureg**)(sp-BY2WD) = up->ureg;
	up->ureg = (void*)sp;
	sp -= BY2WD+ERRMAX;
	memmove((char*)sp, msg, ERRMAX);
	sp -= 3*BY2WD;
	*(uintptr*)(sp+2*BY2WD) = sp+3*BY2WD;
	*(uintptr*)(sp+1*BY2WD) = (uintptr)up->ureg;
	ureg->r0 = (uintptr) up->ureg;
	ureg->sp = sp;
	ureg->pc = (uintptr) up->notify;
	ureg->r14 = 0;

	qunlock(&up->debug);
	splx(s);
	return 1;
}

void
noted(Ureg *ureg, ulong arg0)
{
	Ureg *nureg;
	ulong oureg, sp;

	qlock(&up->debug);
	if(arg0 != NRSTR && !up->notified) {
		qunlock(&up->debug);
		iprint("called to noted when not notified\n");
		pexit("Suicide", 0);
	}

	up->notified = 0;
	up->fpstate &= ~FPillegal;
	nureg = up->ureg;
	oureg = (ulong) nureg;
	if(!okaddr(oureg - BY2WD, BY2WD + sizeof(Ureg), 0) || (oureg & 3) != 0) {
		qunlock(&up->debug);
		pprint("bad ureg in noted or call to noted when not notifed\n");
		pexit("Suicide", 0);
	}

	nureg->psr &= PsrMask|PsrDfiq|PsrDirq;
	nureg->psr |= (ureg->psr & ~(PsrMask|PsrDfiq|PsrDirq));

	memmove(ureg, nureg, sizeof(Ureg));
	switch(arg0) {
	case NCONT:
	case NRSTR:
		if(!okaddr(nureg->pc, BY2WD, 0) || (nureg->pc & 3) != 0
		|| !okaddr(nureg->sp, BY2WD, 0) || (nureg->sp & 3) != 0) {
			qunlock(&up->debug);
			pprint("suicide: trap in noted\n");
			pexit("Suicide", 0);
		}

		up->ureg = (Ureg *) (*(ulong*) (oureg - BY2WD));
		qunlock(&up->debug);
		break;

	case NSAVE:
		if(!okaddr(nureg->pc, BY2WD, 0) || (nureg->pc & 3) != 0
		|| !okaddr(nureg->sp, BY2WD, 0) || (nureg->sp & 3) != 0) {
			qunlock(&up->debug);
			pprint("suicide: trap in noted\n");
			pexit("Suicide", 0);
		}

		qunlock(&up->debug);
		sp = oureg - 4 * BY2WD - ERRMAX;
		splhi();
		ureg->sp = sp;
		ureg->r0 = (uintptr) oureg;
		((ulong*) sp)[1] = oureg;
		((ulong*) sp)[0] = 0;
		break;

	default:
		up->lastnote->flag = NDebug;
		/* wet floor */

	case NDFLT:
		qunlock(&up->debug);
		if(up->lastnote->flag == NDebug)
			pprint("suicide: %s\n", up->lastnote->msg);

		pexit(up->lastnote->msg, up->lastnote->flag != NDebug);
		break;
	}
}

void
trapinit(void)
{
	extern ulong vectors[];

	/* install stack pointer for other exception modes */
	setR13(PsrMfiq, m->save);
	setR13(PsrMirq, m->save);
	setR13(PsrMiabt, m->save);
	setR13(PsrMund, m->save);
	setR13(PsrMsys, m->save);

	/* install vectors and vtable to MACHVEC because vectors must be
	 * aligned on a 128 byte boundary */
	memmove((ulong*)MACHVEC(m->machno), vectors, 64 * 4);

	/* set vectors base address */
	setvectors(MACHVEC(m->machno));
}

static void
trapfpu(void)
{
	int s;

	if((up->fpstate & FPillegal) != 0) {
		postnote(up, 1, "sys: floating point in note handler", NDebug);
		return;
	}

	switch(up->fpstate) {
	case FPinit:
		s = splhi();
		fpinit(); up->fpstate = FPactive;
		splx(s);
		break;

	case FPinactive:
		s = splhi();
		fprestore(up->fpsave); up->fpstate = FPactive;
		splx(s);
		break;

	case FPactive:
		postnote(up, 1, "sys: floating point error", NDebug);
		break;
	}
}

static void
traparm(Ureg *ureg, ulong fsr, uintptr far)
{
	int user;
	int read;
	int syscall;

	static char buf[ERRMAX];

	read = (fsr & (1<<11)) == 0;
	user = userureg(ureg);
	if(!user) {
		if(far >= USTKTOP)
			panic("kernel fault: bad address pc=%#.8lux far=%#.8lux fsr=%#.8lux",
				ureg->pc, far, fsr);
		if(up == nil)
			panic("kernel fault: no user process pc=%#.8lux far=%#.8lux fsr=%#.8lux",
				ureg->pc, far, fsr);
	}

	if(up == nil) {
		panic("user fault: up=nil pc=%#.8lux far=%#.8lux fsr=%#.8lux",
			ureg->pc, far, fsr);
	}

	syscall = up->insyscall; up->insyscall = 1;
	switch(fsr & 0x1f) {
	case 0x03: /* l1 access flag fault */
	case 0x05: /* l1 translation fault */
	case 0x06: /* l2 access flag fault */
	case 0x07: /* l2 translation fault */
	case 0x09: /* l1 domain fault */
	case 0x0b: /* l2 domain fault */
	case 0x0d: /* l1 permission fault */
	case 0x0f: /* l2 permission fault */
		if(fault(far, ureg->pc, read) == 0)
			break;

	default:
		if(!user)
			panic("kernel fault: pc=%#.8lux far=%#.8lux fsr=%#.8lux",
				ureg->pc, far, fsr);

		dumpureg(ureg);
		dumpstackureg(ureg);
		snprint(buf, sizeof(buf), "sys: trap: fault %s far=%#.8lux fsr=%#.8lux",
			read ? "read" : "write", far, fsr);
		postnote(up, 1, buf, NDebug);
	}

	up->insyscall = syscall;
}

void
trap(Ureg *ureg)
{
	int user;
	u32int op, cp;

	user = kenter(ureg);
	switch(ureg->type) {
	case PsrMfiq:
	case PsrMirq:
		ureg->pc -= 4;
		intr(ureg);
		break;

	case PsrMiabt:
		ureg->pc -= 4;
		traparm(ureg, getifsr(), getifar());
		break;

	case PsrMdabt:
		ureg->pc -= 8;
		traparm(ureg, getdfsr(), getdfar());
		break;

	case PsrMund:
		ureg->pc -= 4;
		if(user) {
			spllo();
			if(okaddr(ureg->pc, 4, 0)) {
				op = *(u32int*)ureg->pc;
				if((op & 0x0f000000) == 0x0e000000 || (op & 0x0e000000) == 0x0c000000) {
					cp = op >> 8 & 15;
					if(cp == 10 || cp == 11) {
						trapfpu();
						break;
					}
				}
			}
				
			postnote(up, 1, "sys: trap: invalid opcode", NDebug);
			break;
		}

		panic("invalid opcode at pc=%#.8lux lr=%#.8lux", ureg->pc, ureg->r14);
		break;

	default:
		panic("unknown trap at pc=%#.8lux lr=%#.8lux", ureg->pc, ureg->r14);
		break;
	}

	splhi();
	if(user) {
		if(up->procctl || up->nnote)
			notify(ureg);

		kexit(ureg);
	}
}

void
syscall(Ureg *ureg)
{
	char *e;
	uintptr sp;
	long ret;
	int i, s;
	ulong scallnr;
	vlong startns, stopns;

	if(!kenter(ureg))
		panic("syscall: from kernel: pc=%#.8lux", ureg->pc);

	m->syscall++;
	up->insyscall = 1;
	up->pc = ureg->pc;

	scallnr = up->scallnr = ureg->r0;
	sp = ureg->sp;

	spllo();

	up->nerrlab = 0;
	ret = -1;
	if(!waserror()) {
		if(scallnr >= nsyscall) {
			pprint("bad sys call number %lux pc %#lux", scallnr, ureg->pc);
			postnote(up, 1, "sys: bad sys call", NDebug);
			error(Ebadarg);
		}

		if(sp < (USTKTOP-BY2PG) || sp > (USTKTOP-sizeof(Sargs)-BY2WD)) {
			validaddr(sp, sizeof(Sargs)+BY2WD, 0);
			evenaddr(sp);
		}

		up->s = *((Sargs*)(sp + BY2WD));
		up->psstate = sysctab[scallnr];
		if (up->procctl == Proc_tracesyscall) {
			syscallfmt(scallnr, ureg->pc, (va_list)up->s.args);
			s = splhi();
			up->procctl = Proc_stopme;
			procctl();
			splx(s);
			startns = todget(nil);
		}

		ret = systab[scallnr]((va_list)up->s.args);
		poperror();
	} else {
		e = up->syserrstr;
		up->syserrstr = up->errstr;
		up->errstr = e;
	}

	if(up->nerrlab) {
		print("bad errstack [%lud]: %d extra\n", scallnr, up->nerrlab);
		for (i = 0; i < NERR; i++)
			print("sp=%lux pc=%lux\n", up->errlab[i].sp, up->errlab[i].pc);

		panic("error stack");
	}

	ureg->r0 = ret;
	if(up->procctl == Proc_tracesyscall) {
		stopns = todget(nil);
		sysretfmt(scallnr, (va_list)up->s.args, ret, startns, stopns);
		s = splhi();
		up->procctl = Proc_stopme;
		procctl();
		splx(s);
	}

	up->insyscall = 0;
	up->psstate = 0;
	if(scallnr == NOTED)
		noted(ureg, *((ulong *)up->s.args));

	if(scallnr != RFORK && (up->procctl || up->nnote)) {
		splhi();
		notify(ureg);
	}

	if(up->delaysched)
		sched();

	kexit(ureg);
	splhi();
}
