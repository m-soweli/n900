#include "mem.h"
#include "io.h"

TEXT _start(SB), $-4
	/* load static base */
	MOVW $setR12(SB), R12

	/* fiqs and irqs off, svc mode (cortex a8 trm figure 2.12) */
	MOVW $(PsrDfiq|PsrDirq|PsrMsvc), R0
	MOVW R0, CPSR

	/* mmu and l1 caches off (cortex a8 trm table 3.46) */
	MRC 15, 0, R0, C1, C0, 0
	BIC $(1<<12), R0 /* level 1 instruction cache */
	BIC $(1<<1), R0 /* level 1 data cache */
	BIC $(1<<0), R0 /* mmu */
	MCR 15, 0, R0, C1, C0, 0
	ISB

	/* l2 caches off (cortex a8 trm table 3.49) */
	MCR 15, 0, R0, C1, C0, 1
	BIC $(1<<1), R0 /* level 2 cache */
	MCR 15, 0, R0, C1, C0, 1
	ISB

	/* fill mach with 0 */
	MOVW $0, R0
	MOVW $MACH(0), R1
	MOVW $MACH(MAXMACH), R2
zeromach:
	MOVW R0, (R1)
	ADD $4, R1
	CMP R1, R2
	BNE zeromach

	/* fill page tables with 0 */
	MOVW R0, R0
	MOVW $MACHL1(0), R1 /* bottom of l1 tables */
	MOVW $MACHL1(MAXMACH), R2 /* top of l1 tables */
zeropte:
	MOVW R0, (R1)
	ADD $4, R1
	CMP R1, R2
	BNE zeropte

	/* fill bss with 0 */
	MOVW $0, R0
	MOVW $edata(SB), R1
	MOVW $end(SB), R2
zerobss:
	MOVW R0, (R1)
	ADD $4, R1
	CMP R1, R2
	BNE zerobss

	/* fill page tables for memory:
	 * 1mb section, cached, buffered, kernel read-write */
	MOVW $((1<<1)|(1<<2)|(1<<3)|(1<<10)), R1
	MOVW $PHYSMEM, R2
	MOVW $(MACHL1(0)+L1X(PHYSMEM)), R3
	MOVW $(MACHL1(0)+L1X(PHYSMEMEND)), R4
ptemem:
	ORR R2, R1, R0
	MOVW R0, (R3)
	ADD $(MiB), R2
	ADD $4, R3
	CMP R3, R4
	BNE ptemem

	/* fill page tables for l4 interconnect:
	 * 1mb section, kernel read-write */
	MOVW $((1<<1)|(1<<4)|(1<<10)), R1
	MOVW $PHYSL4, R2
	MOVW $(MACHL1(0)+L1X(PHYSL4)), R3
	MOVW $(MACHL1(0)+L1X(PHYSL4END)), R4
ptel4:
	ORR R2, R1, R0
	MOVW R0, (R3)
	ADD $(MiB), R2
	ADD $4, R3
	CMP R3, R4
	BNE ptel4

	/* fill page tables for l3 interconnect:
	 * 1mb section, kernel read-write */
	MOVW $((1<<1)|(1<<4)|(1<<10)), R1
	MOVW $PHYSL3, R2
	MOVW $(MACHL1(0)+L1X(PHYSL3)), R3
	MOVW $(MACHL1(0)+L1X(PHYSL3END)), R4
ptel3:
	ORR R2, R1, R0
	MOVW R0, (R3)
	ADD $(MiB), R2
	ADD $4, R3
	CMP R3, R4
	BNE ptel3

	/* fpu on (set bits 20-23 in CPACR) but disabled */
	MRC 15, 0, R0, C1, C0, 2
	ORR $(0xf<<20), R0
	MCR 15, 0, R0, C1, C0, 2

	VMRS(FPEXC, 0)
	BIC $(FPEXCEX|FPEXCEN), R0
	VMSR(0, FPEXC)

	/* invalidate caches */
	BL l1dcacheinv(SB)
	BL l1icacheinv(SB)

	/* l2 caches back on */
	MRC 15, 0, R0, C1, C0, 1
	ORR $(1<<1), R0
	MCR 15, 0, R0, C1, C0, 1

	/* l1 caches back on */
	MRC 15, 0, R0, C1, C0, 0
	ORR $(1<<12), R0
	ORR $(1<<1), R0
	MCR 15, 0, R0, C1, C0, 0

	/* set domain access control to client. */
	MOVW $1, R0
	BL putdac(SB)

	/* set translation table base */
	MOVW $MACHL1(0), R0
	BL putttb(SB)

	/* mmu on, time to get virtual */
	MOVW $virt(SB), R2
	BL mmuinvalidate(SB)
	BL mmuenable(SB)
	MOVW R2, R15

TEXT virt(SB), $-4
	/* setup register variables */
	MOVW $setR12(SB), R12
	MOVW $(MACH(0)), R(Rmach)
	MOVW $0, R(Rup)

	/* setup stack in mach */
	MOVW $(MACH(0)), R13
	ADD $(MACHSZ), R13
	SUB $4, R13

	BL main(SB)

_limbo:
	BL idlehands(SB)
	B _limbo

	/* hack to load div */
	BL _div(SB)


TEXT mmuenable(SB), 1, $-4
	MRC 15, 0, R0, C1, C0, 0
	ORR $(1<<0), R0
	MCR 15, 0, R0, C1, C0, 0
	MCR 15, 0, R0, C7, C5, 6
	DMB; DSB; ISB
	RET

TEXT mmudisable(SB), 1, $-4
	MRC 15, 0, R0, C1, C0, 0
	BIC $(1<<0), R0
	MCR 15, 0, R0, C1, C0, 0
	MCR 15, 0, R0, C7, C5, 6
	DMB; DSB; ISB
	RET

TEXT mmuinvalidate(SB), 1, $-4
	MOVW CPSR, R1
	CPSID

	MOVW R15, R0
	MCR 15, 0, R0, C8, C7, 0
	MCR 15, 0, R0, C7, C5, 6
	DMB; DSB; ISB

	MOVW R1, CPSR
	RET

/* get and put domain access control */
TEXT getdac(SB), 1, $-4; MCR 15, 0, R0, C3, C0; RET
TEXT putdac(SB), 1, $-4
	MRC 15, 0, R0, C3, C0
	ISB
	RET


/* get and put translation table base */
TEXT getttb(SB), 1, $-4; MRC 15, 0, R0, C2, C0, 0; RET
TEXT putttb(SB), 1, $-4
	MCR 15, 0, R0, C2, C0, 0
	MCR 15, 0, R0, C2, C0, 1
	ISB
	RET

TEXT getdfsr(SB), $-4; MRC 15, 0, R0, C5, C0, 0; RET
TEXT getifsr(SB), $-4; MRC 15, 0, R0, C5, C0, 1; RET
TEXT getdfar(SB), $-4; MRC 15, 0, R0, C6, C0, 0; RET
TEXT getifar(SB), $-4; MRC 15, 0, R0, C6, C0, 2; RET

TEXT setvectors(SB), $-4;
	MCR 15, 0, R0, C12, C0, 0
	RET

TEXT setlabel(SB), $-4
	MOVW R13, 0(R0)
	MOVW R14, 4(R0)
	MOVW $0, R0
	RET

TEXT gotolabel(SB), $-4
	MOVW 0(R0), R13
	MOVW 4(R0), R14
	MOVW $1, R0
	RET

TEXT cas(SB), $0
TEXT cmpswap(SB), $0
	MOVW ov+4(FP), R1
	MOVW nv+8(FP), R2
casspin:
	LDREX (R0), R3
	CMP R3, R1
	BNE casfail
	STREX R2, (R0), R4
	CMP $0, R4
	BNE casspin
	MOVW $1, R0
	DMB
	RET
casfail:
	CLREX
	MOVW $0, R0
	RET

TEXT tas(SB), $0
TEXT _tas(SB), $0
	MOVW $0xdeaddead, R2
tasspin:
	LDREX (R0), R1
	STREX R2, (R0), R3
	CMP $0, R3
	BNE tasspin
	MOVW R1, R0
	DMB
	RET

TEXT idlehands(SB), $-4
	DMB; DSB; ISB
	WFI
	RET

TEXT coherence(SB), $-4
	DMB; DSB; ISB
	RET

TEXT splhi(SB), $-4
	MOVW R14, 4(R(Rmach))
	MOVW CPSR, R0
	CPSID
	RET

TEXT spllo(SB), $-4
	MOVW CPSR, R0
	CPSIE
	RET

TEXT splx(SB), $-4
	MOVW R14, 4(R(Rmach))
	MOVW R0, R1
	MOVW CPSR, R0
	MOVW R1, CPSR
	RET

TEXT spldone(SB), $-4
	RET

TEXT islo(SB), $0
	MOVW CPSR, R0
	AND $(PsrDirq), R0
	EOR $(PsrDirq), R0
	RET

TEXT perfticks(SB), $0
	MCR 15, 0, R0, C9, C13, 0
	RET

TEXT touser(SB), $-4
	MOVM.DB.W [R0], (R13)
	MOVM.S (R13), [R13]
	ADD $4, R13

	MOVW CPSR, R0
	BIC $(PsrMask|PsrDirq|PsrDfiq), R0
	ORR $PsrMusr, R0
	MOVW R0, SPSR

	MOVW $(UTZERO+0x20), R0
	MOVM.DB.W [R0], (R13)

	MOVM.IA.S.W (R13), [R15]

TEXT forkret(SB), $-4
	ADD $(15*4), R13
	MOVW 8(R13), R14
	MOVW 4(R13), R0
	MOVW R0, SPSR
	MOVM.DB.S (R13), [R0-R14]
	ADD $8, R13
	MOVM.IA.S.W (R13), [R15]

TEXT peek(SB), $0
	MOVW src+0(FP), R0
	MOVW dst+4(FP), R1
	MOVW cnt+8(FP), R2
TEXT _peekinst(SB), $0
_peekloop:
	MOVB (R0), R3
	MOVB R3, (R1)
	SUB.S $1, R0
	BNE _peekloop
	RET

TEXT fpinit(SB), $0
	MOVW $FPEXCEN, R0
	VMSR(0, FPEXC)
	MOVW $0, R0
	VMSR(0, FPSCR)
	RET

TEXT fpoff(SB), $0
TEXT fpclear(SB), $0
	MOVW $0, R1
	VMSR(1, FPEXC)
	RET

TEXT fpsave(SB), $0
	VMRS(FPEXC, 1)
	VMRS(FPSCR, 2)
	MOVM.IA.W [R1-R2], (R0)
	VSTMIA
	RET

TEXT fprestore(SB), $0
	MOVM.IA.W (R0), [R1-R2]
	VMSR(1, FPEXC)
	VMSR(2, FPSCR)
	VLDMIA
	RET
