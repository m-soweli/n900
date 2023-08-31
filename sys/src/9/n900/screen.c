#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

#define Image IMAGE
#include <draw.h>
#include <memdraw.h>
#include "screen.h"

enum {
	/* display controller registers */
	RDrev			= 0x400,
	RDsysconf		= 0x410,
		DSCidle			= 1<<0,
		DSCreset		= 1<<1,
		DSCidlesmart	= 2<<3,
	RDsysstat		= 0x414,
		DSSreset		= 1<<0,
	RDirqstat		= 0x418,
	RDirqen			= 0x41c,
	RDcontrol		= 0x440,
		DClcdon			= 1<<0,
		DClcdactive		= 1<<3,
		DClcdclockrun	= 1<<27,
		DClcdenable		= 1<<28,
		DClcdenablehi	= 1<<29,
		DClcdbits24		= 3<<8,
	RDconfig		= 0x444,
		DCNgamma		= 1<<3,
	RDdefcolor		= 0x44c,
	RDtranscolor	= 0x454,
	RDlinestat		= 0x45c,
	RDlineintr		= 0x460,
	RDtimeh			= 0x464,
	RDtimev			= 0x468,
	RDsize			= 0x47c,

	/* display controller graphics layer registers */
	RDgfxba				= 0x480,
	RDgfxpos			= 0x488,
	RDgfxsize			= 0x48c,
	RDgfxattr			= 0x4a0,
		DGAenable			= 1<<0,
		DGAfmt				= 0x6<<1,
		DGAburst			= 0x2<<6,
	RDgfxrowinc			= 0x4ac,
	RDgfxpixelinc		= 0x4b0,
};

enum {
	Tab	= 4,
	Scroll = 8,
};

#define RDdefcolorn(n)		(RDdefcolor + (n)*4)
#define RDtranscolorn(n)	(RDtranscolor + (n)*4)

#define RDgfxban(n)			(RDgfxba + (n)*4)

#define TIME(s, f, b)		((s & 0xff) | (f & 0xfff) << 8 | (b & 0xfff) << 20)
#define POS(x, y)			((x)<<16 | (y))
#define SIZE(w, h)			((h-1)<<16 | (w-1))

typedef struct Ctlr Ctlr;
struct Ctlr {
	Lock;

	u32int *io;

	Memdata scrdata;
	Memimage *scrimg;
	Memsubfont *scrfont;

	Rectangle win;
	Point font;
	Point pos;
};

static Ctlr ctlr = {
	.io = (u32int*) PHYSDSS,
};

#define csr32r(c, r) ((c)->io[(r)/4])
#define csr32w(c, r, w) ((c)->io[(r)/4] = (w))

static void myscreenputs(char *s, int n);

static int
screendatainit(Ctlr *ctlr)
{
	Rectangle r;

	if(memimageinit() < 0)
		return -1;

	r = Rect(0, 0, 800, 480);
	ctlr->scrdata.ref = 1;
	ctlr->scrdata.bdata = ucalloc(r.max.x * r.max.y * 2);
	if(!ctlr->scrdata.bdata)
		return -1;

	ctlr->scrimg = allocmemimaged(r, RGB16, &ctlr->scrdata);
	if(!ctlr->scrimg)
		return -1;

	ctlr->scrfont = getmemdefont();
	return 0;
}

static Memimage*
screenmkcolor(Memimage *scr, ulong color)
{
	Memimage *i;

	i = allocmemimage(Rect(0, 0, 1, 1), scr->chan);
	if(i) {
		i->flags |= Frepl;
		i->clipr = scr->r;
		memfillcolor(i, color);
	}

	return i;
}

static void
screenwin(Ctlr *ctlr)
{
	Rectangle r;
	Memimage *i;
	Point p;
	int h;

	ctlr->font.y = h = ctlr->scrfont->height;
	ctlr->font.x = ctlr->scrfont->info[' '].width;
	
	r = ctlr->scrimg->r;
	if(i = screenmkcolor(ctlr->scrimg, 0x0D686BFF)) {
		memimagedraw(ctlr->scrimg, r, i, ZP, memopaque, ZP, S);
		freememimage(i);
	}

	r = insetrect(r, 16); memimagedraw(ctlr->scrimg, r, memblack, ZP, memopaque, ZP, S);
	r = insetrect(r, 4); memimagedraw(ctlr->scrimg, r, memwhite, ZP, memopaque, ZP, S);
	if(i = screenmkcolor(ctlr->scrimg, 0xaaaaaaff)) {
		memimagedraw(ctlr->scrimg, Rpt(r.min, Pt(r.max.x, r.min.y+h+12)), i, ZP, memopaque, ZP, S);
		freememimage(i);

		r = insetrect(r, 6);
		p = r.min;
		memimagestring(ctlr->scrimg, p, memblack, ZP, ctlr->scrfont, " Plan 9 Console ");
	}

	ctlr->win = Rpt(addpt(r.min, Pt(0, h + 6)), subpt(r.max, Pt(6, 6)));
	ctlr->pos = ctlr->win.min;
	screenputs = myscreenputs;
}

void
screeninit(void)
{
	Ctlr *c;

	c = &ctlr;
	if(screendatainit(c) < 0)
		return;

	screenwin(c);
	screenputs(kmesg.buf, kmesg.n);

	/* reset the display controller */
	csr32w(c, RDsysconf, DSCreset);
	while(!(csr32r(c, RDsysstat) & DSSreset))
		;

	/* configure the display controller */
	csr32w(c, RDsysconf, DSCidle | DSCidlesmart);
	csr32w(c, RDcontrol, DClcdbits24);
	csr32w(c, RDconfig, DCNgamma);

	/* configure display size and timings */
	csr32w(c, RDtimeh, TIME(3, 15, 11));
	csr32w(c, RDtimev, TIME(2, 3, 3));
	csr32w(c, RDsize, SIZE(800, 480));

	/* enable the lcd interface */
	csr32w(c, RDcontrol, csr32r(c, RDcontrol) | DClcdactive | DClcdclockrun | DClcdenablehi);
	csr32w(c, RDcontrol, csr32r(c, RDcontrol) | DClcdenable);

	/* configure the graphics layer */
	csr32w(c, RDgfxban(0), (uintptr) c->scrdata.bdata);
	csr32w(c, RDgfxpos, POS(c->scrimg->r.min.x, c->scrimg->r.min.y));
	csr32w(c, RDgfxsize, SIZE(c->scrimg->r.max.x, c->scrimg->r.max.y));
	csr32w(c, RDgfxattr, DGAfmt | DGAburst);
	csr32w(c, RDgfxrowinc, 1);
	csr32w(c, RDgfxpixelinc, 1);

	/* enable gfx pipeline and turn lcd on */
	csr32w(c, RDgfxattr, csr32r(c, RDgfxattr) | DGAenable);
	csr32w(c, RDcontrol, csr32r(c, RDcontrol) | DClcdon);

	conf.monitor = 1;
}

static void
screenscroll(Ctlr *ctlr)
{
	int o, h;
	Point p;
	Rectangle r;

	h = ctlr->font.y;
	o = Scroll * h;
	r = Rpt(ctlr->win.min, Pt(ctlr->win.max.x, ctlr->win.max.y - o));
	p = Pt(ctlr->win.min.x, ctlr->win.min.y + o);
	memimagedraw(ctlr->scrimg, r, ctlr->scrimg, p, nil, p, S);
	flushmemscreen(r);

	r = Rpt(Pt(ctlr->win.min.x, ctlr->win.max.y - o), ctlr->win.max);
	memimagedraw(ctlr->scrimg, r, memwhite, ZP, nil, ZP, S);
	flushmemscreen(r);

	ctlr->pos.y -= o;
	
}

static void
screenputc(Ctlr *ctlr, char *buf)
{
	Point p;
	Rectangle r;
	uint chr;
	int w, h;
	static int *xp;
	static int xbuf[256];

	w = ctlr->font.x;
	h = ctlr->font.y;
	if(xp < xbuf || xp >= &xbuf[nelem(xbuf)])
		xp = xbuf;

	switch(buf[0]) {
	case '\n':
		if(ctlr->pos.y + h >= ctlr->win.max.y)
			screenscroll(ctlr);

		ctlr->pos.y += h;
		screenputc(ctlr, "\r");
		break;

	case '\r':
		xp = xbuf;
		ctlr->pos.x = ctlr->win.min.x;
		break;

	case '\t':
		if(ctlr->pos.x >= ctlr->win.max.x - 4 * w)
			screenputc(ctlr, "\n");

		chr = (ctlr->pos.x - ctlr->win.min.x) / w;
		chr = Tab - chr % Tab;
		*xp++ = ctlr->pos.x;

		r = Rect(ctlr->pos.x, ctlr->pos.y, ctlr->pos.x + chr * w, ctlr->pos.y + h);
		memimagedraw(ctlr->scrimg, r, memwhite, ZP, memopaque, ZP, S);
		flushmemscreen(r);
		ctlr->pos.x += chr * w;
		break;

	case '\b':
		if(xp <= xbuf)
			break;

		xp--;
		r = Rect(*xp, ctlr->pos.y, ctlr->pos.x, ctlr->pos.y + h);
		memimagedraw(ctlr->scrimg, r, memwhite, ZP, memopaque, ZP, S);
		ctlr->pos.x = *xp;
		break;

	case '\0':
		break;

	default:
		p = memsubfontwidth(ctlr->scrfont, buf); w = p.x;
		if(ctlr->pos.x >= ctlr->win.max.x - w)
			screenputc(ctlr, "\n");

		*xp++ = ctlr->pos.x;
		r = Rect(ctlr->pos.x, ctlr->pos.y, ctlr->pos.x + w, ctlr->pos.y + h);
		memimagedraw(ctlr->scrimg, r, memwhite, ZP, memopaque, ZP, S);
		memimagestring(ctlr->scrimg, ctlr->pos, memblack, ZP, ctlr->scrfont, buf);
		ctlr->pos.x += w;
		break;
	}
}

static void
myscreenputs(char *s, int n)
{
	Ctlr *c;
	Rune r;
	int i;
	char buf[UTFmax];

	c = &ctlr;
	if(!c->scrimg)
		return;

	if(!islo()) {
		/* don't deadlock trying to print in an interrupt */
		if(!canlock(c))
			return;
	} else {
		lock(c);
	}

	while(n > 0) {
		i = chartorune(&r, s);
		if(i == 0) {
			s++; n--;
			continue;
		}

		memmove(buf, s, i);
		buf[i] = 0;
		s += i; n -= i;
		screenputc(c, buf);
	}

	unlock(c);
}

Memdata*
attachscreen(Rectangle *r, ulong *chan, int *d, int *width, int *softscreen)
{
	Ctlr *c;

	c = &ctlr;
	if(!c->scrimg)
		return nil;

	*r = c->scrimg->r;
	*d = c->scrimg->depth;
	*chan = c->scrimg->chan;
	*width = c->scrimg->width;
	*softscreen = 1;

	c->scrdata.ref++;
	return &c->scrdata;
}

void
getcolor(ulong, ulong *, ulong *, ulong *)
{
}

int
setcolor(ulong, ulong, ulong, ulong)
{
	return 0;
}

void
flushmemscreen(Rectangle)
{
}

void
mouseresize(void)
{
}
