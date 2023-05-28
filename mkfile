CONF=n900
CONFLIST=n900

p=9
objtype=arm
ktzero=0x80020000

</$objtype/mkfile

DEVS=`{rc ../port/mkdevlist $CONF}
PORT=\
	alarm.$O\
	alloc.$O\
	allocb.$O\
	auth.$O\
	cache.$O\
	chan.$O\
	dev.$O\
	edf.$O\
	fault.$O\
	mul64fract.$O\
	rebootcmd.$O\
	page.$O\
	parse.$O\
	pgrp.$O\
	portclock.$O\
	print.$O\
	proc.$O\
	qio.$O\
	qlock.$O\
	segment.$O\
	sysfile.$O\
	sysproc.$O\
	taslock.$O\
	tod.$O\
	xalloc.$O\
	random.$O\
	rdb.$O\
	syscallfmt.$O\
	userinit.$O\
	ucalloc.$O\

OBJ=\
	l.$O\
	lcache.$O\
	ltrap.$O\
	main.$O\
	mmu.$O\
	timer.$O\
	trap.$O\
	intr.$O\
	$CONF.root.$O\
	$CONF.rootc.$O\
	$DEVS\
	$PORT\

LIB=\
	/$objtype/lib/libmemlayer.a\
	/$objtype/lib/libmemdraw.a\
	/$objtype/lib/libdraw.a\
	/$objtype/lib/libip.a\
	/$objtype/lib/libsec.a\
	/$objtype/lib/libmp.a\
	/$objtype/lib/libc.a\
	/$objtype/lib/libdtracy.a\

$p$CONF.u:D: $p$CONF
	aux/aout2uimage -o $target -Z0 $prereq

$p$CONF:D: $OBJ $CONF.$O $LIB
	$LD -o $target -T$ktzero -l $prereq

<../boot/bootmkfile
<../port/portmkfile
<|../port/mkbootrules $CONF

initcode.out: init9.$O initcode.$O /$objtype/lib/libc.a
	$LD -l -R1 -s -o $target $prereq

install:V: $p$CONF.u
	cp $p$CONF /$objtype/
	cp $p$CONF.u /$objtype/
