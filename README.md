# n900

a [9front][1] kernel port to the [nokia n900][2].

[1]: http://9front.org
[2]: https://en.wikipedia.org/wiki/Nokia_N900

## installation

you will need:

- an up to date u-boot on your n900 compiled with support for plan 9 kernels.
  the u-boot package in the [maemo.org][3] repositories is unfortunately insufficient.
  this is left to the reader.

- an up to date 9front system.

building the kernel requires your system to have the arm libraries
and commands built, so start by installing those:

    cd /sys/src/
    objtype=arm mk clean
    objtype=arm mk install

then, clone this repo to your 9front kernel source, as `/sys/src/9/n900`,
and install it with:

    cd /sys/src/9/n900
    mk clean
    mk install

copy the compiled kernel to a fat formatted micro sd card
and setup the boot scripts:

    cp /arm/9n900.u /n/dos/9n900.u
    aux/txt2uimage -o /n/dos/boot.scr <<EOF
        mw 0x80010000 0x0 0x10000
        ${mmctype}load ${mmcnum}:${mmcpart} 0x80010000 plan9.ini
        ${mmctype}load ${mmcnum}:${mmcpart} 0x80020000 9n900.u
    EOF

now eject the sd card and put it in your n900, boot the system
with the keyboard open, and select external sd card at the [u-boot][4]
menu.

if all goes well, you see the bootargs[] prompt shortly.
no refunds.

[3]: https://maemo.org
[4]: https://www.denx.de/project/u-boot/
