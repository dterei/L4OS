# TODO

## Untested

* Pulling the network in the pager - mmap should block forever, while
  all pagefualts should still continue (assumin they don't require the
  network).

* Killing processes at awkward times, for example in the middle of
  requests, or in the middle of actual NFS requests, etc.

## High Priority

* New pager is dying mysteriously. Either funny memory corruption, or
  race conditions with VFS. Or some combination of both. Or just a
  plain bug. Try running forkbomb2 and leaving it for 5 minutes, or
  run nuke and leave it for 30 seconds.

* Argv/Argc Passing.

* Dirty-Bit Optimisation.

* Shared Memory.

## Medium Priority

* Move more things to the generic linked list implementation:
	* Process management (with threads)
	* IRQ
	* Bootinfo

## Special Items

* Process Deletion - set to zombie and check for that everywhere.

* Cache issues (once again...).

## Wish-list

* User network library. Handled through VFS? Internally use TCP-over-UDP?

* USB stack/driver/library.

* Editor (user program, nothing to do with SOS unless it involves
  writing a few more system calls). Like read. Need to implement a few
  more things, like tmpfile, which requiries that the temporary file
  be deleted after the processs dies. This wouldn't be that hard
  actually, just write a thing in libsos that opens a file, and in SOS.

* Thread management. As a note, this is what we need to have the
  `init_thread` kill itself without a hack.

* Port a unix tool to our OS to prove its robustness. (e.g grep or
  something similar which should be complicated in function but not
  use to many system calls since we are not POSIX compliant).

* Support multiple consoles.

* Support directories and properly support multiple filesystems
  properly. Each directory basically should be associated somehow with
  a filesystem.

* Have a hard code stable program which runs and is responsible for
  spawning sosh. If sosh crashes, it starts it up again.

