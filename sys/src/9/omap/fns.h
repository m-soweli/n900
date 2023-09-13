#include "../port/portfns.h"

#define KADDR(a) ((void*)(a))
#define PADDR(a) ((uintptr)(a))

#define userureg(ur) (((ur)->psr & PsrMask) == PsrMusr)

void*	ucallocalign(usize, int, usize);
void*	ucalloc(usize);

int cmpswap(long *, long, long);
int cas(long *, long, long);
int tas(void *);

void evenaddr(uintptr a);
int probeaddr(uintptr a);
void procrestore(Proc *);
void procsave(Proc *);
void procsetup(Proc *);
void procfork(Proc *);

void coherence(void);
void idlehands(void);
void touser(void*);
void setR13(uint, u32int*);
ulong getdfsr(void);
ulong getifsr(void);
uintptr getdfar(void);
uintptr getifar(void);
void setvectors(uintptr);
void breakpt(void);

char* getconf(char*);
int isaconfig(char *, int, ISAConf *);

void mmuinvalidate(void);
void* mmuuncache(void*, usize);
uintptr cankaddr(uintptr);

ulong µs(void);
void delay(int);
void microdelay(int);
void cycles(uvlong*);

void dumpureg(Ureg*);
void dumpstackureg(Ureg*);

void intrenable(int, void (*f)(Ureg *, void*), void *, int, char*);
void intrdisable(int, void (*f)(Ureg *, void*), void *, int, char*);
void intr(Ureg *);

void uartinit(void);
void mmuinit(void);
void trapinit(void);
void intrinit(void);
void timerinit(void);
void screeninit(void);

void links(void);

void l1icacheinv(void);
void l1dcachewb(void);
void l1dcacheinv(void);
void l1dcachewbinv(void);
void l1ucachewbinv(void);

void l2idcacheinv(void);
void l2idcachewb(void);
void l2idcachewbinv(void);
void l2ucachewbinv(void);


void fpinit(void);
void fpoff(void);
void fpclear(void);

void fpsave(FPsave *);
void fprestore(FPsave *);
