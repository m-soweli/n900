#include "mem.h"
#include "io.h"

TEXT vectors(SB), $-4
	MOVW 24(R15), R15
	MOVW 24(R15), R15
	MOVW 24(R15), R15
	MOVW 24(R15), R15
	MOVW 24(R15), R15
	MOVW 24(R15), R15
	MOVW 24(R15), R15
	MOVW 24(R15), R15

TEXT vtable(SB), $-4
	WORD $_vund(SB)
	WORD $_vund(SB)
	WORD $_vsvc(SB)
	WORD $_viabt(SB)
	WORD $_vdabt(SB)
	WORD $_vund(SB)
	WORD $_virq(SB)
	WORD $_vfiq(SB)

TEXT _vsvc(SB), $-4
	CLREX
	DSB

	/* stash in ureg */
	MOVM.DB.W [R14], (R13) /* ureg->pc */
	MOVW SPSR, R14
	MOVM.DB.W [R14], (R13) /* ureg->psr */
	MOVW $PsrMsvc, R14
	MOVM.DB.W [R14], (R13) /* ureg->type */

	/* save user regs.
	 * not MOVM.DB.W.S because the saved value of R13 is undefined.
	 * (arm v7 manual §A8.8.199) */
	MOVM.DB.S [R0-R14], (R13)
	SUB $(15*4), R13

	/* get our sb, mach, up */
	MOVW $setR12(SB), R12
	MOVW $(MACH(0)), R(Rmach)
	MOVW 8(R(Rmach)), R(Rup)

	/* make space for debugger and go to syscall passing ureg */
	MOVW R13, R0
	SUB $8, R13
	BL syscall(SB)
	ADD $8, R13

	/* restore link, spsr */
	ADD $(15*4), R13
	MOVW 8(R13), R14
	MOVW 4(R13), R0
	MOVW R0, SPSR

	/* restore user regs */
	MOVM.DB.S (R13), [R0-R14]

	/* pop past ureg->type, ureg->psr and restore ureg->pc.
	 * omap and others have RFE here but 5a has no idea about newer instructions
	 * and simulates it with the MOVM below */
	ADD $8, R13
	MOVM.IA.S.W (R13), [R15]

TEXT _viabt(SB), $-4
	CLREX
	DSB
	MOVM.IA [R0-R4], (R13)
	MOVW $PsrMiabt, R0
	B _vswitch

TEXT _vdabt(SB), $-4
	CLREX
	DSB
	MOVM.IA [R0-R4], (R13)
	MOVW $PsrMdabt, R0
	B _vswitch

TEXT _virq(SB), $-4
	DSB
	MOVM.IA [R0-R4], (R13)
	MOVW $PsrMirq, R0
	B _vswitch

TEXT _vfiq(SB), $-4
	CLREX
	DSB
	MOVM.IA [R0-R4], (R13)
	MOVW $PsrMfiq, R0
	B _vswitch

TEXT _vund(SB), $-4
	CLREX
	DSB
	MOVM.IA [R0-R4], (R13)
	MOVW $PsrMund, R0
	B _vswitch

_vswitch:
	/* stash pointer to previous R0-R4, stash SPSR and R14 for ureg */
	MOVW SPSR, R1
	MOVW R14, R2
	MOVW R13, R3

	/* back to svc mode */
	MOVW CPSR, R14
	BIC $PsrMask, R14
	ORR $(PsrDirq|PsrDfiq|PsrMsvc), R14
	MOVW R14, CPSR

	/* from user or kernel mode? */
	AND.S $0xf, R1, R4
	BEQ _vuser

	/* from kernel mode */
	/* set ureg->type, ureg->psr, ureg->pc */
	MOVM.DB.W [R0-R2], (R13)
	MOVM.IA (R3), [R0-R4]

	/* save kernel regs
	 * not MOVM.DB.W.S because the saved value of R13 is undefined.
	 * (arm v7 manual §A8.8.199) */
	MOVM.DB [R0-R14], (R13)
	SUB $(15*4), R13

	/* get our sb, mach, up */
	MOVW $setR12(SB), R12

	/* make space for debugger and go to trap passing ureg */
	MOVW R13, R0
	SUB $8, R13
	BL trap(SB)
	ADD $8, R13

	/* restore link, spsr */
	ADD $(15*4), R13
	MOVW 8(R13), R14
	MOVW 4(R13), R0
	MOVW R0, SPSR

	/* restore kernel regs */
	MOVM.DB (R13), [R0-R14]

	/* pop past ureg->type, ureg->psr, and restore ureg->pc. */
	ADD $8, R13
	MOVM.IA.S.W (R13), [R15]

_vuser:
	/* from user mode */
	/* set ureg->type, ureg->psr, ureg->pc */
	MOVM.DB.W [R0-R2], (R13)
	MOVM.IA (R3), [R0-R4]

	/* save kernel regs
	 * not MOVM.DB.W.S because the saved value of R13 is undefined.
	 * (arm v7 manual §A8.8.199) */
	MOVM.DB.S [R0-R14], (R13)
	SUB $(15*4), R13

	/* get our sb, mach, up */
	MOVW $setR12(SB), R12
	MOVW $(MACH(0)), R(Rmach)
	MOVW 8(R(Rmach)), R(Rup)

	/* make space for debugger and go to trap passing ureg */
	MOVW R13, R0
	SUB $8, R13
	BL trap(SB)
	ADD $8, R13

	/* restore link, spsr */
	ADD $(15*4), R13
	MOVW 8(R13), R14
	MOVW 4(R13), R0
	MOVW R0, SPSR

	/* restore kernel regs */
	MOVM.DB.S (R13), [R0-R14]

	/* pop past ureg->type, ureg->psr, and restore ureg->pc */
	ADD $8, R13
	MOVM.IA.S.W (R13), [R15]

TEXT setR13(SB), $-4
	MOVW 4(FP), R1

	/* switch to new mode */
	MOVW CPSR, R2
	BIC $PsrMask, R2, R3
	ORR R0, R3
	MOVW R3, CPSR

	/* set r13 */
	MOVW R1, R13

	/* back to old mode */
	MOVW R2, CPSR
	RET
