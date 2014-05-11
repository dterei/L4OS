# SOS -- Simple Operating System (L4OS)

This is a monolithic kernel built on-top of the OK Labs L4
microkernel. It was done as part of the UNSW Advanced Operaing Sytems
(AOS) course by Ben Kalman and David Terei.

Reasonably complete kernel. Features include:
* MMU support including paging to disk.
* Memory mapped IO and shared memory.
* Process managemen support (ELF).
* VFS layer with support for NFS and pipefs.
* Interactive shell.

It should be able to run a basic subset of C99 and POSIX ELF programs.
Adding extra system calls is fairly straight-forward.

The kernel runs on an ARM, specifically being built and tested on a
[Linksys NSLU2](https://en.wikipedia.org/wiki/NSLU2) NAS device.

## Get involved!

We are happy to receive bug reports, fixes, documentation
enhancements, and other improvements.

Please report bugs via the
[github issue tracker](http://github.com/dterei/Hackager/issues).

Master [git repository](http://github.com/dterei/Hackager):

* `git clone git://github.com/dterei/Hackager.git`

## Licensing

This library is mostly BSD-licensed. Also includes some
[OZPLB](http://www.ok-labs.com/licenses#ozplb) code.

