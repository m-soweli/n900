#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"
#include "../port/i2c.h"

enum {
	Rrev	= 0x00,
	Rie		= 0x04,
	Ris		= 0x08,
		Ial		= 1 << 0,
		Inack	= 1 << 1,
		Iardy	= 1 << 2,
		Irrdy	= 1 << 3,
		Ixrdy	= 1 << 4,
		Ibb		= 1 << 12,
		Iall	= 0xffff,

	Rwe		= 0x0c,
	Rsyss	= 0x10,
		SSreset	= 1 << 0,

	Rbuf	= 0x14,
	Rcnt	= 0x18,
	Rdata	= 0x1c,
	Rsysc	= 0x20,
		SCreset	= 1 << 1,

	Rcon	= 0x24,
		Cstt	= 1 << 0,
		Cstp	= 1 << 1,
		Cxoa3	= 1 << 4,
		Cxoa2	= 1 << 5,
		Cxoa1	= 1 << 6,
		Cxoa0	= 1 << 7,
		Cxa		= 1 << 8,
		Ctrx	= 1 << 9,
		Cmst	= 1 << 10,
		Cstb	= 1 << 11,
		Cen		= 1 << 15,

	Raddr	= 0x2c,
};

#define csr32r(c, r) ((c)->io[(r)/4])
#define csr32w(c, r, w) ((c)->io[(r)/4] = (w))

typedef struct Ctlr Ctlr;
struct Ctlr {
	u32int *io;
	ulong irq;

	Rendez;
};

static Ctlr ctlr[] = {
	{ .io = (u32int*) PHYSI2C1, .irq = IRQI2C1 },
	{ .io = (u32int*) PHYSI2C2, .irq = IRQI2C2 },
	{ .io = (u32int*) PHYSI2C3, .irq = IRQI2C3 },
};

static void
omapi2cwaitbus(Ctlr *ctlr)
{
	/* FIXME: timeout here? */
	while(csr32r(ctlr, Ris) & Ibb)
		;
}

static int
omapi2cwaitirq(void *arg)
{
	Ctlr *ctlr = arg; return csr32r(ctlr, Ris);
}

static uint
omapi2cwait(Ctlr *ctlr)
{
	uint s;

	/* FIXME: timeout here? */
	while(!(s = csr32r(ctlr, Ris))) {
		if(!up || !islo())
			continue;

		tsleep(ctlr, omapi2cwaitirq, ctlr, 5);
	}

	return s;
}

static void
omapi2cflush(Ctlr *ctlr)
{
	while(csr32r(ctlr, Ris) & Irrdy) {
		USED(csr32r(ctlr, Rdata));
		csr32w(ctlr, Ris, Irrdy);
	}
}

static void
omapi2cintr(Ureg *, void *arg)
{
	Ctlr *ctlr;

	ctlr = arg;
	wakeup(ctlr);
}

static int
omapi2cinit(I2Cbus *bus)
{
	Ctlr *ctlr;

	/* reset the ctlr */
	ctlr = bus->ctlr;
	csr32w(ctlr, Rsysc, SCreset);
	csr32w(ctlr, Rcon, Cen);

	/* FIXME: timeout here? */
	while(!(csr32r(ctlr, Rsyss) & SSreset))
		;

	intrenable(ctlr->irq, omapi2cintr, ctlr, 0, bus->name);
	return 0;
}

static int
omapi2cio(I2Cdev *dev, uchar *pkt, int olen, int ilen)
{
	Ctlr *ctlr;
	uint con, addr, stat;
	uint o;

	ctlr = dev->bus->ctlr;
	if(olen <= 0 || pkt == nil)
		return -1;

	o = 1;
	con = Cen | Cmst | Ctrx | Cstp | Cstt;
	if(dev->a10)
		return -1;

	/* wait for bus */
	omapi2cwaitbus(ctlr);

	/* first attempt to probe, will get nack here if no dev */
	csr32w(ctlr, Rcnt, olen);
	csr32w(ctlr, Raddr, dev->addr);
	csr32w(ctlr, Rcon, con);
	stat = omapi2cwait(ctlr);
	if(stat & Inack || stat & Ial) {
		o = -1; goto err;
	}

	/* transmit */
	while(o < olen) {
		stat = omapi2cwait(ctlr);
		if(stat == 0 || stat & Inack || stat & Ial) {
			o = -1; goto err;
		}

		if(stat & Iardy) {
			csr32w(ctlr, Ris, Iardy);
			break;
		}

		if(stat & Ixrdy) {
			csr32w(ctlr, Rdata, pkt[o++]);
			csr32w(ctlr, Ris, Ixrdy);
		}
	}

	/* receive */
	csr32w(ctlr, Rcnt, ilen);
	csr32w(ctlr, Raddr, addr);
	csr32w(ctlr, Rcon, Cen | Cmst | Cstp | Cstt);
	while(o < olen + ilen) {
		stat = omapi2cwait(ctlr);
		if(stat == 0 || stat & Inack || stat & Ial) {
			o = -1; goto err;
		}

		if(stat & Iardy) {
			csr32w(ctlr, Ris, Iardy);
			break;
		}

		if(stat & Irrdy) {
			pkt[o++] = csr32r(ctlr, Rdata);
			csr32w(ctlr, Ris, Irrdy);
		}
	}

err:
	omapi2cflush(ctlr);
	csr32w(ctlr, Ris, Iall);
	return o;
}

void
i2comaplink(void)
{
	int i;
	static I2Cbus bus[] = {
		{ "i2c1", 4000000, &ctlr[0], omapi2cinit, omapi2cio },
		{ "i2c2", 4000000, &ctlr[1], omapi2cinit, omapi2cio },
		{ "i2c3", 4000000, &ctlr[2], omapi2cinit, omapi2cio },
	};

	for(i = 0; i < nelem(bus); i++)
		addi2cbus(&bus[i]);
}
