#include "mem.h"
#include "io.h"

/* l1 instruction cache invalidate */
TEXT l1icacheinv(SB), $-4
	MOVW $0, R0
	MCR 15, 0, R0, C7, C5, 0
	ISB
	RET

/* l1 data cache writeback */
TEXT l1dcachewb(SB), $-4
	MOVW $cacheopwb(SB), R0
	MOVW $0, R1
	B cacheop(SB)

/* l1 data cache invalidate */
TEXT l1dcacheinv(SB), $-4
	MOVW $cacheopinv(SB), R0
	MOVW $0, R1
	B cacheop(SB)

/* l1 data cache writeback + invalidate */
TEXT l1dcachewbinv(SB), $-4
	MOVW $cacheopwbinv(SB), R0
	MOVW $0, R1
	B cacheop(SB)

/* l1 unified instruction + data cache writeback + invalidate */
TEXT l1ucachewbinv(SB), $-4
	MOVM.DB.W [R14], (SP)
	BL l1dcachewbinv(SB)
	BL l1icacheinv(SB)
	MOVM.IA.W (SP), [R14]
	RET

/* l2 instruction + data cache writeback */
TEXT l2idcachewb(SB), $-4
	MOVW $cacheopwb(SB), R0
	MOVW $1, R1
	B cacheop(SB)

/* l2 instruction + data cache invalidate */
TEXT l2idcacheinv(SB), $-4
	MOVW $cacheopinv(SB), R0
	MOVW $1, R1
	B cacheop(SB)

/* l2 instruction + data cache writeback + invalidate */
TEXT l2idcachewbinv(SB), $-4
	MOVW $cacheopwbinv(SB), R0
	MOVW $1, R1
	B cacheop(SB)

/* l1 unified instruction + data cache writeback + invalidate */
TEXT l2ucachewbinv(SB), $-4
	MOVM.DB.W [R14], (SP)
	BL l2idcachewbinv(SB)
	BL l2idcacheinv(SB)
	MOVM.IA.W (SP), [R14]
	RET

/* set/way operations for cacheop */
TEXT cacheopwb(SB), $-4;	MCR 15, 0, R0, C7, C10, 2; RET
TEXT cacheopinv(SB), $-4;	MCR 15, 0, R0, C7, C6, 2; RET
TEXT cacheopwbinv(SB), $-4;	MCR 15, 0, R0, C7, C14, 2; RET

#define Rop R2
#define Rcache R3
#define Rways R4
#define Rwayshift R5
#define Rsets R6
#define Rsetshift R7
#define Rset R8

/* apply a cache operation to the whole cache */
TEXT cacheop(SB), $-4
	/* stash */
	MOVM.DB.W [R2,R14], (SP)
	MOVW R0, Rop
	MOVW R1, Rcache

	/* get cache geometry */
	MCR 15, 2, Rcache, C0, C0, 0; ISB
	MRC 15, 1, R0, C0, C0, 0

	/* compute ways = ((R0 >> 3) & 0x3ff) + 1) */
	SRA $3, R0, Rways
	AND $0x3ff, Rways
	ADD $1, Rways

	/* compute wayshift = log₂(ways) */
	CLZ(4, 5) /* Rways, Rwayshift */
	ADD $1, Rwayshift

	/* compute sets = ((R0 >> 13) & 0x7fff) + 1) */
	SRA $13, R0, Rsets
	AND $0x7fff, Rsets
	ADD $1, Rsets

	/* compute setshift = log₂(cache line size) */
	AND $0x7, R0, Rsetshift
	ADD $4, Rsetshift

cacheopways:
	MOVW Rsets, Rset
cacheopsets:
	/* compute set / way register contents */
	SLL Rwayshift, Rways, R0
	SLL Rsetshift, Rset, R1
	ORR R1, R0
	SLL $1, Rcache, R1
	ORR R1, R0

	BL (Rop)
	SUB $1, Rset; CMP $0, Rset; BEQ cacheopsets /* loop sets */
	SUB $1, Rways; CMP $0, Rways; BEQ cacheopways /* loop ways */

	/* restore */
	MOVM.IA.W (SP), [R2,R14]
	RET
