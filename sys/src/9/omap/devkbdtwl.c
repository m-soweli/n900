#include "u.h"
#include "../port/lib.h"
#include "../port/error.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/i2c.h"

enum {
	Rctrl		= 0xd2,
		Csoftreset	= 1<<0,
		Csoftmode	= 1<<1,
		Cenable		= 1<<6,
	Rcode	= 0xdb,
	Risr		= 0xe3,
	Rimr		= 0xe4,
		Ikp			= 1<<0,
		Ilk			= 1<<1,
		Ito			= 1<<2,
	Rsir		= 0xe7,
	Redr		= 0xe8,
		Ekpfalling		= 1<<0,
		Ekprising		= 1<<1,
		Elkfalling		= 1<<2,
		Elkrising		= 1<<3,
		Etofalling		= 1<<4,
		Etorising		= 1<<5,
		Emisfalling	= 1<<6,
		Emisrising	= 1<<7,
	Rsih		= 0xe9,
		Scor			= 1<<2,
};

enum {
	Qdir,
	Qscan,
};

typedef struct Ctlr Ctlr;
struct Ctlr {
	Ref;
	Lock;

	I2Cdev *dev;
	Queue *q;

	uchar cur[8];
	uchar prev[8];
};

static Ctlr ctlr;
static Dirtab kbdtab[] = {
	".",			{Qdir, 0, QTDIR},		0, 0555,
	"scancode",	{Qscan, 0, QTFILE},	0, 0440,
};

static u8int
csr8r(Ctlr *ctlr, u8int r)
{
	uchar buf;

	i2crecv(ctlr->dev, &buf, sizeof(buf), r);
	return buf;
}

static u8int
csr8w(Ctlr *ctlr, u8int r, u8int w)
{
	i2csend(ctlr->dev, &w, sizeof(w), r);
	return w;
}

static void
kbdinterrupt(Ureg *, void*)
{
	int i, j, c, k;

	ilock(&ctlr);
	if(!(csr8r(&ctlr, Risr) & Ikp)) {
		iunlock(&ctlr);
		return;
	}

	/* scan key columns */
	for(i = 0; i < 8; i++) {
		ctlr.prev[i] = ctlr.cur[i];
		ctlr.cur[i] = csr8r(&ctlr, Rcode + i);

		/* changed? */
		c = ctlr.cur[i] ^ ctlr.prev[i];
		if(!c)
			continue;

		/* scan key rows */
		for(j = 0; j < 8; j++) {
			if(!(c & (1 << j)))
				continue;

			/* pressed or released? */
			k = i << 3 | j;
			if(ctlr.prev[i] & (1 << j))
				k |= 0x80;

			qproduce(ctlr.q, &k, 1);
		}
	}

	iunlock(&ctlr);
}

static void
kbdreset(void)
{
	ilock(&ctlr);
	ctlr.q = qopen(1024, Qcoalesce, 0, 0);
	if(!ctlr.q) {
		iunlock(&ctlr);
		return;
	}

	ctlr.dev = i2cdev(i2cbus("i2c1"), 0x4a);
	if(!ctlr.dev) {
		iunlock(&ctlr);
		return;
	}

	ctlr.dev->subaddr = 1;
	ctlr.dev->size = 0x100;

	csr8w(&ctlr, Rctrl, Csoftreset | Csoftmode | Cenable);
	csr8w(&ctlr, Rsih, Scor);
	csr8w(&ctlr, Rimr, ~Ikp);

	intrenable(IRQTWL, kbdinterrupt, nil, BUSUNKNOWN, "kbd");
	iunlock(&ctlr);
}

static void
kbdshutdown(void)
{
}

static Chan *
kbdattach(char *spec)
{
	if(!ctlr.dev)
		error(Enonexist);

	return devattach(L'b', spec);
}

static Walkqid *
kbdwalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, kbdtab, nelem(kbdtab), devgen);
}

static int
kbdstat(Chan *c, uchar *dp, int n)
{
	return devstat(c, dp, n, kbdtab, nelem(kbdtab), devgen);
}

static Chan *
kbdopen(Chan *c, int mode)
{
	if(!iseve)
		error(Eperm);

	if(c->qid.path == Qscan) {
		if(waserror()) {
			decref(&ctlr);
			nexterror();
		}

		if(incref(&ctlr) != 1)
			error(Einuse);

		c = devopen(c, mode, kbdtab, nelem(kbdtab), devgen);
		poperror();
		return c;
	}

	return devopen(c, mode, kbdtab, nelem(kbdtab), devgen);
}

static void
kbdclose(Chan *c)
{
	if((c->flag & COPEN) && c->qid.path == Qscan)
		decref(&ctlr);
}

static Block*
kbdbread(Chan *c, long n, ulong off)
{
	if(c->qid.path == Qscan)
		return qbread(ctlr.q, n);

	return devbread(c, n, off);
}

static long
kbdread(Chan *c, void *a, long n, vlong)
{
	if(c->qid.path == Qscan)
		return qread(ctlr.q, a, n);

	if(c->qid.path == Qdir)
		return devdirread(c, a, n, kbdtab, nelem(kbdtab), devgen);

	error(Egreg);
	return 0;
}

static long
kbdwrite(Chan *, void *, long, vlong)
{
	error(Egreg);
	return 0;
}

Dev kbddevtab = {
	L'b',
	"kbd",

	kbdreset,
	devinit,
	kbdshutdown,
	kbdattach,
	kbdwalk,
	kbdstat,
	kbdopen,
	devcreate,
	kbdclose,
	kbdread,
	kbdbread,
	kbdwrite,
	devbwrite,
	devremove,
	devwstat,
};
