# n900

a [plan 9 front][1] port to the [nokia n900][2].

[1]: http://9front.org
[2]: https://en.wikipedia.org/wiki/Nokia_N900

## status

**working**: keyboard, screen, rtc, mmc  
**broken**: audio, touch, battery, usb, networking, camera

## build

clone this repository to a 9front system and bind the directories.

    cd n900
    bind -bc sys/src/9 /sys/src/9
    bind -bc sys/src/boot /sys/src/boot
    bind -bc sys/lib/dist /sys/lib/dist

compile the system for the arm architecture.

    cd /sys/src
    objtype=arm mk install
    objtype=arm mk clean

compile the kernel

    cd /sys/src/9/omap
    mk install
    mk clean

build the boot scripts

    cd /sys/src/boot/n900
    mk

build a bootable image suitable for writing to an sd card.

    bind /root /n/src9
    bind -a /dist/plan9front /n/src9

    cd /sys/lib/dist
    mk 9front.n900.img

## links

**hardware:** [schematic][3], [sysinfo][4]  
**components:** [processor][5],  [touch][6]

[3]: http://plan9.stanleylieber.com/hardware/nokia/n900/n900.schematics.pdf
[4]: http://plan9.stanleylieber.com/hardware/nokia/n900/sysinfo
[5]: https://archive.org/download/omap-3430-trm/Omap3430-trm.pdf
[6]: https://www.ti.com/lit/ds/symlink/tsc2005.pdf
