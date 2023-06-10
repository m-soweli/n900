#include "u.h"
#include "../port/lib.h"
#include "../port/error.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/pci.h"
#include "../port/sd.h"

enum {
	Rsysc		= 0x10,
		SCreset		= 1 << 1,
	Rsyss		= 0x14,
		SSreset		= 1 << 0,
	Rcsre		= 0x24,
	Rcon		= 0x28,
	Rpwcnt		= 0x2c,
	Rblk		= 0x104,
	Rarg		= 0x108,
	Rcmd		= 0x10c,
		CRnone		= 0 << 16,
		CR136		= 1 << 16,
		CR48		= 2 << 16,
		CR48busy	= 3 << 16,
		CRmask		= 3 << 16,

		CFcheckidx	= 1 << 19,
		CFcheckcrc	= 1 << 20,
		CFdata		= 1 << 21,
		CFdataread	= 1 << 4,
		CFdatamulti	= 1 << 5,

		CTnone		= 0 << 22,
		CTbus		= 1 << 22,
		CTfunc		= 2 << 22,
		CTio		= 3 << 22,
	Rrsp10		= 0x110,
	Rrsp32		= 0x114,
	Rrsp54		= 0x118,
	Rrsp76		= 0x11c,
	Rdata		= 0x120,
	Rpstate		= 0x124,
	Rhctl		= 0x128,
	Rsysctl		= 0x12c,
	Rstatus		= 0x130,
		STcmd		= 1 << 0,
		STtransfer	= 1 << 1,
		STbufwrite	= 1 << 4,
		STbufread	= 1 << 5,

		STmaskok	= 0xffff << 0,
		STmaskerr	= 0xffff << 16,
	Rie			= 0x134,
	Rise		= 0x138,
	Rac12		= 0x13c,
	Rcapa		= 0x140,
	Rcapacur	= 0x148,
	Rrev		= 0x1fc,
};

#define csr32r(c, r) ((c)->io[(r)/4])
#define csr32w(c, r, w) ((c)->io[(r)/4] = (w))

typedef struct Ctlr Ctlr;
struct Ctlr {
	char *name;
	u32int *io;
	ulong irq;

	struct {
		uint bcount;
		uint bsize;
	} cmd;

	Lock;
	Rendez;
};

static int
n900mmcinit(SDio *io)
{
	Ctlr *ctlr;

	ctlr = io->aux;
	csr32w(ctlr, Rsysc, SCreset);
	while(!(csr32r(ctlr, Rsyss) & SSreset))
		;

	return 0;
}

static void
n900mmcenable(SDio *)
{
}

static int
n900mmcinquiry(SDio *, char *inquiry, int len)
{
	return snprint(inquiry, len, "MMC Host Controller");
}

static void
n900mmcintr(Ureg *, void *aux)
{
	Ctlr *ctlr;

	ctlr = aux;
	ilock(ctlr);
	if(csr32r(ctlr, Rstatus) & STcmd)
		wakeup(ctlr);

	iunlock(ctlr);
}

static int
n900mmcdone(void *aux)
{
	Ctlr *ctlr = aux;

	if(csr32r(ctlr, Rstatus) & STcmd)
		return 1;

	return 0;
}

static int
n900mmccmd(SDio *io, SDiocmd *iocmd, u32int arg, u32int *resp)
{
	Ctlr *ctlr;
	u32int cmd;

	/* prepare flags for this command */
	ctlr = io->aux;
	cmd = iocmd->index << 24;
	switch(iocmd->resp) {
	case 0: cmd |= CRnone; break;
	case 2: cmd |= CR136 | CFcheckcrc; break;
	case 3: cmd |= CR48; break;
	case 1:
		if(iocmd->busy) {
			cmd |= CR48busy | CFcheckidx | CFcheckcrc;
			break;
		}

	default:
		cmd |= CR48 | CFcheckidx | CFcheckcrc;
		break;
	}

	/* if there is data, set the data, read, and multi flags */
	if(iocmd->data) {
		cmd |= CFdata;
		if(iocmd->data & 1)
			cmd |= CFdataread;
		if(iocmd->data > 2)
			cmd |= CFdatamulti;
	}

	/* off it goes, wait for a response */
	csr32w(ctlr, Rstatus, ~0);
	csr32w(ctlr, Rarg, arg);
	csr32w(ctlr, Rcmd, cmd);

	/* wait for command to be done */
	tsleep(ctlr, n900mmcdone, ctlr, 100);
	if(csr32r(ctlr, Rstatus) & STmaskerr)
		error(Eio);

	/* unpack the response */
	switch(cmd & CRmask) {
	case CRnone:
		resp[0] = 0;
		break;

	case CR136:
		resp[0] = csr32r(ctlr, Rrsp10);
		resp[1] = csr32r(ctlr, Rrsp32);
		resp[2] = csr32r(ctlr, Rrsp54);
		resp[3] = csr32r(ctlr, Rrsp76);
		break;

	case CR48:
	case CR48busy:
		resp[0] = csr32r(ctlr, Rrsp10);
		break;
	}

	return 0;
}

static void
n900mmciosetup(SDio *io, int, void *, int bsize, int bcount)
{
	Ctlr *ctlr;

	ctlr = io->aux;
	if(bsize == 0 || (bsize & 3) != 0)
		error(Egreg);

	ctlr->cmd.bsize = bsize;
	ctlr->cmd.bcount = bcount;
	csr32w(ctlr, Rblk, (bsize & 0x3ff) | (bcount << 16));
}

static void
n900mmcbufread(Ctlr *ctlr, uchar *buf, int len)
{
	for(len >>= 2; len > 0; len--) {
		*((u32int*)buf) = csr32r(ctlr, Rdata);
		buf += 4;
	}
}

static void
n900mmcbufwrite(Ctlr *ctlr, uchar *buf, int len)
{
	for(len >>= 2; len > 0; len--) {
		csr32w(ctlr, Rdata, *((u32int*)buf));
		buf += 4;
	}
}

static void
n900mmcio(SDio *io, int write, uchar *buf, int len)
{
	Ctlr *ctlr;
	u32int stat, n;

	ctlr = io->aux;
	if(len != ctlr->cmd.bsize * ctlr->cmd.bcount)
		error(Egreg);

	while(len > 0) {
		stat = csr32r(ctlr, Rstatus);
		if(stat & STmaskerr) {
			csr32w(ctlr, Rstatus, STmaskerr);
			error(Eio);
		}

		if(stat & STbufwrite) {
			csr32w(ctlr, Rstatus, STbufwrite);
			if(!write)
				error(Eio);

			n = len;
			if(n > ctlr->cmd.bsize)
				n = ctlr->cmd.bsize;

			n900mmcbufwrite(ctlr, buf, n);
			len -= n;
			buf += n;
		}

		if(stat & STbufread) {
			csr32w(ctlr, Rstatus, STbufread);
			if(write)
				error(Eio);

			n = len;
			if(n > ctlr->cmd.bsize)
				n = ctlr->cmd.bsize;

			n900mmcbufread(ctlr, buf, n);
			len -= n;
			buf += n;
		}

		if(stat & STtransfer) {
			csr32w(ctlr, Rstatus, STtransfer);
			if(len != 0)
				error(Eio);
		}
	}
}

static void
n900mmcbus(SDio *, int, int)
{
	/* FIXME: change bus width */
}

void
mmcn900link(void)
{
	int i;
	static Ctlr ctlr[2] = {
		{ .name = "mmc1", .io = (u32int*) PHYSMMC1, .irq = IRQMMC1, },
		{ .name = "mmc2", .io = (u32int*) PHYSMMC2, .irq = IRQMMC2, },
	};

	static SDio io[nelem(ctlr)];
	for(i = 0; i < nelem(io); i++) {
		io[i].name = "mmc",
		io[i].init = n900mmcinit,
		io[i].enable = n900mmcenable,
		io[i].inquiry = n900mmcinquiry,
		io[i].cmd = n900mmccmd,
		io[i].iosetup = n900mmciosetup,
		io[i].io = n900mmcio,
		io[i].bus = n900mmcbus,
		io[i].aux = &ctlr[i];

		addmmcio(&io[i]);
		intrenable(ctlr[i].irq, n900mmcintr, &ctlr[i], BUSUNKNOWN, ctlr[i].name);
	}
}
