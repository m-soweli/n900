# n900

a [plan 9 front][1] port to the [nokia n900][2].

[1]: http://9front.org
[2]: https://en.wikipedia.org/wiki/Nokia_N900

## status

**working**: keyboard, screen, rtc, mmc  
**broken**: audio, touch, battery, usb, networking

## build

clone this repository to a 9front system and bind the directories.

    cd n900
    bind -ac sys/src/9 /sys/src/9
    bind -ac sys/src/boot /sys/src/boot

compile your system for the arm architecture.

    cd /sys/src
    objtype=arm mk install
    objtype=arm mk clean

compile the kernel

    cd /sys/src/9/n900
    mk install
    mk clean

build the boot scripts

    cd /sys/src/boot/n900
    mk
