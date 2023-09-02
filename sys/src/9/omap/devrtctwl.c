#include "u.h"
#include "../port/lib.h"
#include "../port/error.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/i2c.h"

enum {
	Rsec		= 0x1c,
	Rmin	= 0x1d,
	Rhour	= 0x1e,
	Rday	= 0x1f,
	Rmonth	= 0x20,
	Ryear	= 0x21,
	Rweeks	= 0x22,
	Rctrl		= 0x29,
		Cget		= 1<<6,

	Qdir = 0,
	Qrtc,

	SecMin	= 60,
	SecHour	= 60*SecMin,
	SecDay	= 24*SecHour,
};

typedef struct Ctlr Ctlr;
struct Ctlr {
	I2Cdev *dev;

	int sec;
	int min;
	int hour;
	int day;
	int month;
	int year;
};

static Ctlr ctlr;
static int dmsize[] = { 365, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
static int ldmsize[] = { 366, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
static Dirtab rtctab[] = {
	".",		{Qdir, 0, QTDIR},	0, 0555,
	"rtc",	{Qrtc, 0, QTFILE},	0, 0440,
};

#define bcddec(x) (((x) & 0xf) + ((x) >> 4) * 10)
#define bcdenc(x) (((x / 10) << 4) + (x) % 10)
#define leap(x) (((x) % 4) == 0 && ((x % 100) != 0 || (x % 400) == 0))

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

static vlong
rtcsnarf(void)
{
	vlong s;
	int i;

	/* latch and snarf */
	csr8w(&ctlr, Rctrl, csr8r(&ctlr, Rctrl) | Cget);
	ctlr.sec = bcddec(csr8r(&ctlr, Rsec)) % 60;
	ctlr.min = bcddec(csr8r(&ctlr, Rmin)) % 60;
	ctlr.hour = bcddec(csr8r(&ctlr, Rhour)) % 24;
	ctlr.day = bcddec(csr8r(&ctlr, Rday));
	ctlr.month = bcddec(csr8r(&ctlr, Rmonth));
	ctlr.year = bcddec(csr8r(&ctlr, Ryear)) % 100;
	ctlr.year += 2000;

	/* seconds per year */
	s = 0;
	for(i = 1970; i < ctlr.year; i++) {
		if(leap(i))
			s += ldmsize[0] * SecDay;
		else
			s += dmsize[0] * SecDay;
	}

	/* seconds per month */
	for(i = 1; i < ctlr.month; i++) {
		if(leap(ctlr.year))
			s += ldmsize[i] * SecDay;
		else
			s += dmsize[i] * SecDay;
	}

	/* days, hours, minutes, seconds */
	s += (ctlr.day - 1) * SecDay;
	s += ctlr.hour * SecHour;
	s += ctlr.min * SecMin;
	s += ctlr.sec;
	return s;
}

static void
rtcreset(void)
{
	ctlr.dev = i2cdev(i2cbus("i2c1"), 0x4b);
	if(!ctlr.dev)
		return;

	ctlr.dev->subaddr = 1;
	ctlr.dev->size = 0x100;
}

static void
rtcshutdown(void)
{
}

static Chan *
rtcattach(char *spec)
{
	if(!ctlr.dev)
		error(Enonexist);

	return devattach('r', spec);
}

static Walkqid *
rtcwalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, rtctab, nelem(rtctab), devgen);
}

static int
rtcstat(Chan *c, uchar *dp, int n)
{
	return devstat(c, dp, n, rtctab, nelem(rtctab), devgen);
}

static Chan *
rtcopen(Chan *c, int mode)
{
	mode = openmode(mode);
	if(c->qid.path == Qrtc) {
		if(!iseve() && mode != OREAD)
			error(Eperm);
	}

	return devopen(c, mode, rtctab, nelem(rtctab), devgen);
}

static void
rtcclose(Chan *)
{
}

static long
rtcread(Chan *c, void *a, long n, vlong off)
{
	if(c->qid.path == Qdir)
		return devdirread(c, a, n, rtctab, nelem(rtctab), devgen);
	if(c->qid.path == Qrtc)
		return readnum(off, a, n, rtcsnarf(), 12);

	error(Egreg);
	return 0;
}

static long
rtcwrite(Chan *, void *, long, vlong)
{
	error(Egreg);
	return 0;
}

Dev rtctwldevtab = {
	'r',
	"rtc",

	rtcreset,
	devinit,
	rtcshutdown,
	rtcattach,
	rtcwalk,
	rtcstat,
	rtcopen,
	devcreate,
	rtcclose,
	rtcread,
	devbread,
	rtcwrite,
	devbwrite,
	devremove,
	devwstat,
};
