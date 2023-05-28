#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"

enum {
	Rdll	= 0x00,
	Rrhr	= 0x00,
	Rthr	= 0x00,
	Rdlh	= 0x04,
	Rier	= 0x04,
		IErhr	= 1 << 0,
		IEthr	= 1 << 1,
		IEls	= 1 << 2,
		IEms	= 1 << 3,
	Riir	= 0x08,
		Ipending = 1 << 0,
		Imodem	= 0x00,
		Ithr	= 0x01,
		Irhr	= 0x02,
		Ils		= 0x03,
		Irxt	= 0x06,
		Ixoff	= 0x08,
		Icts	= 0x10,
		Imask 	= 0x1f,
	Rfcr	= 0x08,
		FCRen		= 1 << 0,
	Refr	= 0x08,
	Rlcr	= 0x0c,
	Rmcr	= 0x10,
	Rxon1	= 0x10,
	Rlsr	= 0x14,
		LSRrxempty	= 1 << 0,
		LSRrxover	= 1 << 1,
		LSRrxparity	= 1 << 2,
		LSRrxframe	= 1 << 3,
		LSRrxbreak	= 1 << 4,
		LSRtxempty	= 1 << 5,
		LSRtxshift	= 1 << 6,
		LSRrxstat	= 1 << 7,
	Rxon2	= 0x14,
	Rmsr	= 0x18,
	Rtcr	= 0x18,
	Rxoff1	= 0x18,
	Rspr	= 0x1c,
	Rtlr	= 0x1c,
	Rxoff2	= 0x1c,
	Rmdr1	= 0x20,
	Rmdr2	= 0x24,
	Rsysc	= 0x54,
		SCreset		= 1 << 1,
	Rsyss	= 0x58,
		SSreset		= 1 << 0,
};

#define csr32r(c, r) ((c)->io[(r)/4])
#define csr32w(c, r, w) ((c)->io[(r)/4] = (w))

typedef struct Ctlr Ctlr;
struct Ctlr {
	Lock;

	u32int *io;
	ulong irq;

	int ie;
};

extern PhysUart n900physuart;

static Ctlr ctlr[] = {
	{ .io = (u32int*) PHYSUART1, .irq = IRQUART1, },
	{ .io = (u32int*) PHYSUART2, .irq = IRQUART2, },
	{ .io = (u32int*) PHYSUART3, .irq = IRQUART3, },
};

static Uart n900uart[] = {
	{
		.regs = &ctlr[0],
		.name = "uart1",
		.freq = 48000000,
		.phys = &n900physuart,
		.next = &n900uart[1],
	},
	{
		.regs = &ctlr[1],
		.name = "uart2",
		.freq = 48000000,
		.phys = &n900physuart,
		.next = &n900uart[2],
	},
	{
		.regs = &ctlr[2],
		.name = "uart3",
		.freq = 48000000,
		.phys = &n900physuart,
		.next = nil,
	},
};

static Uart *
n900uartpnp(void)
{
	return n900uart;
}

static long
n900uartstatus(Uart *, void *, long, long)
{
	return 0;
}

static void
n900uartintr(Ureg *, void *arg)
{
	Uart *uart;
	Ctlr *ctlr;
	int lsr;
	char c;

	uart = arg;
	ctlr = uart->regs;

	ilock(ctlr);
	switch((csr32r(ctlr, Riir) >> 1) & Imask) {
	case Ithr:
		uartkick(uart);
		break;

	case Irhr:
		while((lsr = csr32r(ctlr, Rlsr)) & LSRrxempty) {
			c = csr32r(ctlr, Rrhr);

			if(lsr & LSRrxover) { uart->oerr++; break; }
			if(lsr & LSRrxparity) { uart->perr++; break; }
			if(lsr & LSRrxframe) { uart->ferr++; break; }

			uartrecv(uart, c);
		}

		break;
	}

	iunlock(ctlr);
}

static void
n900uartenable(Uart *uart, int ie)
{
	Ctlr *ctlr;

	ctlr = uart->regs;
	ilock(ctlr);

	csr32w(ctlr, Rsysc, SCreset);
	while(!(csr32r(ctlr, Rsyss) & SSreset))
		;

	csr32w(ctlr, Rfcr, FCRen);
	if(ie) {
		if(!ctlr->ie) {
			intrenable(ctlr->irq, n900uartintr, uart, 0, uart->name);
			ctlr->ie = 1;
		}

		csr32w(ctlr, Rier, IErhr);
	}

	iunlock(ctlr);
}

static void
n900uartdisable(Uart *uart)
{
	Ctlr *ctlr;

	ctlr = uart->regs;

	ilock(ctlr);
	csr32w(ctlr, Rier, 0);
	if(ctlr->ie) {
		intrdisable(ctlr->irq, n900uartintr, uart, 0, uart->name);
		ctlr->ie = 0;
	}

	iunlock(ctlr);
}

static void
n900uartkick(Uart *uart)
{
	Ctlr *ctlr;
	int i;

	ctlr = uart->regs;
	if(uart->blocked)
		return;

	for(i = 0; i < 128; i++) {
		if(csr32r(ctlr, Rlsr) & LSRtxempty) {
			if(uart->op >= uart->oe && uartstageoutput(uart) == 0)
				break;

			csr32w(ctlr, Rthr, *uart->op++);
		}
	}
}

static int
n900uartgetc(Uart *uart)
{
	Ctlr *ctlr;

	ctlr = uart->regs;
	while(!(csr32r(ctlr, Rlsr) & LSRrxempty))
		;

	return csr32r(ctlr, Rrhr);
}

static void
n900uartputc(Uart *uart, int c)
{
	Ctlr *ctlr;

	ctlr = uart->regs;
	while(!(csr32r(ctlr, Rlsr) & LSRtxempty))
		;

	csr32w(ctlr, Rthr, c);
}

static void n900uartnop(Uart *, int) {}
static int n900uartnope(Uart *, int) { return -1; }

PhysUart n900physuart = {
	.name = "n900",

	.pnp = n900uartpnp,
	.enable = n900uartenable,
	.disable = n900uartdisable,
	.kick = n900uartkick,
	.status = n900uartstatus,
	.getc = n900uartgetc,
	.putc = n900uartputc,

	.dobreak = n900uartnop,
	.baud = n900uartnope,
	.bits = n900uartnope,
	.stop = n900uartnope,
	.parity = n900uartnope,
	.modemctl = n900uartnop,
	.rts = n900uartnop,
	.dtr = n900uartnop,
	.fifo = n900uartnop,
	.power = n900uartnop,
};

void
uartinit(void)
{
	consuart = &n900uart[2];
	consuart->console = 1;
	uartputs(kmesg.buf, kmesg.n);
}
