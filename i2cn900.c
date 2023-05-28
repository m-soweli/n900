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
		Ial		= 1 << 0, /* arbitration lost */
		Inack	= 1 << 1, /* no acknowledgement */
		Iardy	= 1 << 2, /* address ready */
		Irrdy		= 1 << 3, /* receive ready */
		Ixrdy	= 1 << 4, /* transmit ready */
		Ibb		= 1 << 12, /* bus busy */
		Iall		= 0xffff,

	Rwe		= 0x0c,
	Rsyss	= 0x10,
		SSreset	= 1 << 0, /* reset status */

	Rbuf	= 0x14,
	Rcnt	= 0x18,
	Rdata	= 0x1c,
	Rsysc	= 0x20,
		SCreset	= 1 << 1, /* software reset */

	Rcon	= 0x24,
		Cstt		= 1 << 0, /* start condition */
		Cstp		= 1 << 1, /* stop condiction */
		Cxoa3	= 1 << 4, /* expand address */
		Cxoa2	= 1 << 5,
		Cxoa1	= 1 << 6,
		Cxoa0	= 1 << 7,
		Cxa		= 1 << 8,
		Ctrx		= 1 << 9, /* transmit mode */
		Cmst	= 1 << 10, /* master mode */
		Cstb		= 1 << 11, /* start byte */
		Cen		= 1 << 15, /* enable */

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
n900i2cwaitbus(Ctlr *ctlr)
{
	/* FIXME: timeout here? */
	while(csr32r(ctlr, Ris) & Ibb)
		;
}

static int
n900i2cwaitirq(void *arg)
{
	Ctlr *ctlr = arg; return csr32r(ctlr, Ris);
}

static uint
n900i2cwait(Ctlr *ctlr)
{
	uint s;

	/* FIXME: timeout here? */
	while(!(s = csr32r(ctlr, Ris))) {
		if(!up || !islo())
			continue;

		tsleep(ctlr, n900i2cwaitirq, ctlr, 5);
	}

	return s;
}

static void
n900i2cflush(Ctlr *ctlr)
{
	while(csr32r(ctlr, Ris) & Irrdy) {
		USED(csr32r(ctlr, Rdata));
		csr32w(ctlr, Ris, Irrdy);
	}
}

static void
n900i2cintr(Ureg *, void *arg)
{
	Ctlr *ctlr;

	ctlr = arg;
	wakeup(ctlr);
}

static int
n900i2cinit(I2Cbus *bus)
{
	Ctlr *ctlr;

	/* reset the ctlr */
	ctlr = bus->ctlr;
	csr32w(ctlr, Rsysc, SCreset);
	csr32w(ctlr, Rcon, Cen);

	/* FIXME: timeout here? */
	while(!(csr32r(ctlr, Rsyss) & SSreset))
		;

	intrenable(ctlr->irq, n900i2cintr, ctlr, 0, bus->name);
	return 0;
}

static int
n900i2cio(I2Cbus *bus, uchar *pkt, int olen, int ilen)
{
	Ctlr *ctlr;
	uint con, addr, stat;
	uint o;

	ctlr = bus->ctlr;
	if(olen <= 0 || pkt == nil)
		return -1;

	o = 0;
	con = Cen | Cmst | Ctrx | Cstp | Cstt;
	if((pkt[o] & 0xf8) == 0xf0) {
		/* 10-bit address: qemu has bugs, nothing on the n900 needs them.
		 * con |= Cxa;
		 * addr = ((pkt[o++] & 6) << 7) | pkt[o++];
		 */
		return -1;
	} else {
		/* 7-bit address */
		addr = pkt[o++] >> 1;
	}

	/* wait for bus */
	n900i2cwaitbus(ctlr);

	/* first attempt to probe, will get nack here if no dev */
	csr32w(ctlr, Rcnt, olen);
	csr32w(ctlr, Raddr, addr);
	csr32w(ctlr, Rcon, con);
	stat = n900i2cwait(ctlr);
	if(stat & Inack || stat & Ial) {
		o = -1; goto err;
	}

	/* transmit */
	while(o < olen) {
		stat = n900i2cwait(ctlr);
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
		stat = n900i2cwait(ctlr);
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
	n900i2cflush(ctlr);
	csr32w(ctlr, Ris, Iall);
	return o;
}

void
i2cn900link(void)
{
	int i;
	static I2Cbus bus[] = {
		{ "i2c1", 4000000, &ctlr[0], n900i2cinit, n900i2cio },
		{ "i2c2", 4000000, &ctlr[1], n900i2cinit, n900i2cio },
		{ "i2c3", 4000000, &ctlr[2], n900i2cinit, n900i2cio },
	};

	for(i = 0; i < nelem(bus); i++)
		addi2cbus(&bus[i]);
}
