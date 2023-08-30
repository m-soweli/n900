#define KiB (1024u)
#define MiB (1024*1024u)
#define GiB (1024*1024*1024u)

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define Rmach 10
#define Rup 9

#define HZ (100)
#define MS2HZ (1000/HZ)
#define TK2SEC(t) ((t)/HZ)

#define CONFADDR 0x80010000

#define KZERO 0x80000000
#define KTZERO 0x80020000
#define KSTACK (8*KiB)

#define UZERO 0
#define UTZERO BY2PG
#define USTKTOP 0x20000000
#define USTKSIZE (16*MiB)

#define MAXMACH 1
#define MACHSZ (16*KiB)
#define L1SZ (4*4096)
#define L2SZ (4*256)

#define L1X(va) ((((va)>>20) & 0xfff) << 2)

#define MACH(n) (KZERO+(n)*MACHSZ)
#define MACHP(n) ((Mach*)MACH(n))
#define MACHL1(n) (MACH(MAXMACH)+(n)*L1SZ)
#define MACHVEC(n) (MACHL1(MAXMACH)+(n)*64*4)

#define BY2PG (4*KiB)
#define BY2SE 4
#define BY2WD 4
#define BY2V 8

#define CACHELINEZ 64
#define BLOCKALIGN 32

#define ROUND(s, sz) (((s)+((sz)-1))&~((sz)-1))
#define PGROUND(s) ROUND(s, BY2PG)
#define PGSHIFT 12

#define PTEMAPMEM MiB
#define PTEPERTAB (PTEMAPMEM/BY2PG)
#define SEGMAPSIZE 1984
#define SSEGMAPSIZE 16

#define PPN(p) ((p)&~(BY2PG-1))

#define PTEVALID (1<<0)
#define PTERONLY (0<<1)
#define PTEWRITE (1<<1)
#define PTECACHED (0<<2)
#define PTEUNCACHED (1<<2)
#define PTENOEXEC (1<<3)

#define PsrDfiq 0x40
#define PsrDirq 0x80

#define PsrMusr 0x10
#define PsrMfiq 0x11
#define PsrMirq 0x12
#define PsrMsvc 0x13
#define PsrMmon 0x16
#define PsrMiabt 0x17
#define PsrMdabt 0x18
#define PsrMund 0x1b
#define PsrMsys 0x1f

#define PsrMask 0x1f

#define WFI WORD $0xe320f003
#define DSB WORD $0xf57ff04f
#define DMB WORD $0xf57ff05f
#define ISB WORD $0xf57ff06f

#define CPSIE WORD $0xf1080080
#define CPSID WORD $0xf10c0080

#define CLZ(s, d) WORD $(0xe16f0f10 | (d) << 12 | (s))
#define VMSR(cpu, fp) WORD $(0xeee00a10|(fp)<<16|(cpu)<<12)
#define VMRS(fp, cpu) WORD $(0xeef00a10|(fp)<<16|(cpu)<<12)

#define FPSID 0x0
#define FPSCR 0x1
#define MVFR1 0x6
#define MVFR0 0x7
#define FPEXC 0x8
#define FPEXCEX (1<<31)
#define FPEXCEN (1<<30)

/* vlmdia r0!, {d0-d15}
 * vldmia r0!, {d16-d31} */
#define VLDMIA WORD $0xecb00b20; WORD $0xecf00b20;

/* vstmia r0!, {d0-d15}
 * vstmia r0!, {d16-d31} */
#define VSTMIA WORD $0xeca00b20; WORD $0xece00b20;
