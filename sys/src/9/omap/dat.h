typedef struct Conf Conf;
typedef struct Confmem Confmem;
typedef struct FPsave FPsave;
typedef struct Label Label;
typedef struct Lock Lock;
typedef struct Mach Mach;
typedef struct MMMU MMMU;
typedef struct Page Page;
typedef struct Proc Proc;
typedef struct PFPU PFPU;
typedef struct PMMU PMMU;
typedef struct Ureg Ureg;

typedef u32int PTE;
typedef uvlong Tval;

#pragma incomplete Ureg

#define MAXSYSARG 5
#define AOUT_MAGIC (E_MAGIC)

struct Lock {
	ulong key;
	u32int sr;
	uintptr pc;
	Proc *p;
	Mach *m;
	int isilock;
};

struct Label {
	uintptr sp;
	uintptr pc;
};

struct Confmem {
	uintptr base;
	uintptr limit;
	uintptr kbase;
	uintptr klimit;
	ulong npage;
};

struct Conf {
	ulong nmach;
	ulong nproc;
	Confmem mem[1];
	ulong npage;
	ulong upages;
	ulong copymode;
	ulong ialloc;
	ulong pipeqsize;
	ulong nimage;
	ulong nswap;
	int nswppo;
	int monitor;
};

struct FPsave {
	ulong exc;
	ulong scr;
	uchar regs[256];
};

struct PFPU {
	enum {
		FPinit,
		FPactive,
		FPinactive,

		FPillegal = 0x100,
	} fpstate;
	FPsave fpsave[1];
};

#define NCOLOR 1
struct PMMU {
	Page *mmul2;
	Page *mmul2cache;
};

struct MMMU {
	PTE *mmul1;
	uint mmupid;
};

#include "../port/portdat.h"

struct Mach {
	int machno;
	uintptr splpc;
	Proc *proc;
	MMMU;

	PMach;

	u32int save[5];
	uintptr stack[1];
};

typedef struct ISAConf ISAConf;
typedef struct Devport Devport;
typedef struct DevConf DevConf;

#define BUSUNKNOWN 0
#define BUSMODEM 1

#define NISAOPT 8
struct ISAConf {
	char *type;
	uintptr port;
	int irq;
	ulong dma;
	ulong mem;
	ulong size;
	ulong freq;

	int nopt;
	char *opt[NISAOPT];
};

struct Devport {
	ulong port;
	int size;
};

struct DevConf {
	ulong intnum;
	char *type;
	int nports;
	Devport *ports;
};

typedef void KMap;
#define VA(p) ((uintptr)(p))
#define kmap(p) (KMap*)((p)->pa|KZERO)
#define kunmap(p)
#define kmapinval()
#define getpgcolor(p) 0

struct {
	char machs[MAXMACH];
	int exiting;
} active;

extern register Mach *m;
extern register Proc *up;
