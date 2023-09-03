#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"

#include "tos.h"
#include "ureg.h"
#include "pool.h"

#define MAXCONF 64

static char *confname[MAXCONF];
static char *confval[MAXCONF];
static int nconf;

Conf conf;

char*
getconf(char *name)
{
	int i;

	for(i = 0; i < nconf; i++)
		if(cistrcmp(confname[i], name) == 0)
			return confval[i];

	return nil;
}

int
isaconfig(char *, int, ISAConf *)
{
	return 0;
}

void
cpuidprint(void)
{
	/* FIXME: how fast are we really? */
	print("cpu%d: %dMHz ARM Cortex-A8\n", m->machno, 600);
}

void
plan9iniinit(void)
{
	char *c, *p, *q;
	char *v[MAXCONF];
	int i, n;

	c = (char*) CONFADDR;
	for(p = q = c; *q; q++) {
		if(*q == '\r')
			continue;
		if(*q == '\t')
			*q = ' ';
		*p++ = *q;
	}

	*p = 0;
	n = getfields(c, v, MAXCONF, 1, "\n");
	for(i = 0; i < n; i++) {
		if(v[i][0] == '#')
			continue;

		c = strchr(v[i], '=');
		if(!c)
			continue;

		confname[nconf] = v[i];
		confval[nconf] = c;
		nconf++;
	}
}

void
confinit(void)
{
	int i;
	uintptr pa;
	ulong kp;

	conf.nmach = 1;
	conf.npage = 0;
	conf.mem[0].base = PHYSMEM;
	conf.mem[0].limit = PHYSMEMEND;

	pa = PADDR(PGROUND((uintptr)end));
	for(i = 0; i < nelem(conf.mem); i++) {
		if(pa > conf.mem[i].base && pa < conf.mem[i].limit)
			conf.mem[i].base = pa;

		conf.mem[i].npage = (conf.mem[i].limit - conf.mem[i].base)/BY2PG;
		conf.npage += conf.mem[i].npage;
	}

	conf.upages = (conf.npage*80)/100;
	conf.ialloc = ((conf.npage-conf.upages)/2)*BY2PG;

	conf.nproc = 100 + ((conf.npage*BY2PG)/MB)*5;
	if(cpuserver)
		conf.nproc *= 3;
	if(conf.nproc > 4000)
		conf.nproc = 4000;

	conf.nswap = conf.npage*3;
	conf.nswppo = 4096;
	conf.nimage = 200;
	conf.copymode = 0;

	kp = conf.npage - conf.upages;
	kp *= BY2PG;
	kp -= conf.upages * sizeof(Page)
		+ conf.nproc * sizeof(Proc*)
		+ conf.nimage * sizeof(Image)
		+ conf.nswap
		+ conf.nswppo * sizeof(Page*);

	mainmem->maxsize = kp;
	if(!cpuserver)
		imagmem->maxsize = kp;
}

void
machinit(void)
{
	m->machno = 0;

	active.machs[0] = 1;
	active.exiting = 0;

	up = nil;
}

void
init0(void)
{
	char **sp, buf[KNAMELEN];
	int i;

	chandevinit();
	if(!waserror()) {
		ksetenv("cputype", "arm", 0);
		if(cpuserver)
			ksetenv("service", "cpu", 0);
		else
			ksetenv("service", "terminal", 0);

		snprint(buf, sizeof(buf), "nokia %s", conffile);
		ksetenv("terminal", buf, 0);
		ksetenv("console", "2", 0);
		ksetenv("kbmap", "n900", 0);
		for(i = 0; i < nconf; i++) {
			if(*confname[i] != '*')
				ksetenv(confname[i], confval[i], 0);
			ksetenv(confname[i], confval[i], 1);
		}

		poperror();
	}

	kproc("alarm", alarmkproc, 0);

	/* prepare stack for boot */
	sp = (char**)(USTKTOP - sizeof(Tos) - 8 - sizeof(sp[0])*4);
	sp[3] = sp[2] = sp[1] = nil;
	strcpy(sp[0] = (char*)&sp[4], "boot");
	touser(sp);
}

void
main(void)
{
	uartinit();
	machinit();
	mmuinit();
	plan9iniinit();
	confinit();
	printinit();
	quotefmtinstall();
	fmtinstall(L'H', encodefmt);
	print("\nPlan 9\n");

	xinit();
	trapinit();
	intrinit();
	timerinit();
	timersinit();
	cpuidprint();
	procinit0();
	initseg();
	links();

	screeninit();
	chandevreset();

	pageinit();
	userinit();
	schedinit();

	panic("schedinit returned");
}

void
exit(int)
{
	for(;;)
		idlehands();
}

void
reboot(void *, void *, ulong)
{
}

void
setupwatchpts(Proc *, Watchpt *, int n)
{
	if(n > 0)
		error("no watchpoints");
}
