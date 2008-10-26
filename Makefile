MCHN_FLAGS  = machine=nslu2
PROJ_FLAGS  = project=sos
TOOLCHAIN   = toolchain=nslu2.toolchain
DEBUG       = ENABLE_DEBUG=True ENABLE_KDB_CONS=True ENABLE_KDB_CLI=True KDB_BREAKIN=True
SCONS       = tools/build.py $(MCHN_FLAGS) $(PROJ_FLAGS) $(TOOLCHAIN) $(DEBUG)
TLA         = baz
SCONSRESULT = build/images/image.boot.bin
ELFDIR      = build/userland_elf/bin

# Only try to assign TFTPROOT if it hasn't already been.
ifeq ($(TFTPROOT),)
    TFTPROOT = /srv/tftp
endif

TARGET = $(TFTPROOT)/bootimg.bin

# Ditto SERIAL_PORT.
ifeq ($(SERIAL_PORT),)
    ifeq ($(OS), Darwin)
        SERIAL_PORT = $(firstword $(wildcard /dev/cu.usbserial-*))
    else
        SERIAL_PORT = $(firstword $(wildcard /dev/ttyUSB*))
    endif
endif

ifeq ($(SERIAL_PORT),)
    $(warning Warning: USB serial port not found. nslu2 commands will not be issued)
    SLUG_CMD = @true
else
    SLUG_CMD = nslu2 -p $(SERIAL_PORT)
endif


NODISTLIST = packages

all: $(TARGET) reset

noreset: $(TARGET)

.IGNORE: on off up down reset
on off up down reset:
	$(SLUG_CMD) $(patsubst on,up,$(patsubst off,down,$@))

$(TARGET): $(SCONSRESULT)
	mkdir -p $(TFTPROOT)
	cp $(SCONSRESULT) $(TARGET) || true
	chmod 755 $(ELFDIR)/*
	cp -p $(ELFDIR)/* $(TFTPROOT)

$(SCONSRESULT): on tools
	$(SCONS)

.PHONY: $(SCONSRESULT)

clean:
	-rm -rf build

cleanconfig: 
	@$(TLA) cat-config packages | cut -f 1 | xargs -t rm -rf

distclean:
	@excl="-name 'cscope.*' -o -name '.sconsign.dblite'";		\
	excl="$$excl -o -type d -name build";				\
	eval "find . \( $$excl \) -print | xargs -t rm -rf"

tools:
	$(TLA) build-config packages

config:	tools

dist:	tools distclean
	@dstroot=$${DSTROOT:-/tmp};					\
	project=`pwd`;project=`basename $$project`;			\
	dstdir=$$dstroot/$$project/aos-2006;				\
	excl="-name '{arch}' -prune -o -name .arch-ids -prune";		\
	excl="$$excl -o -name '*.swp'";					\
	incl="\( -type f -o -type l \)";				\
	     								\
	rm -rf $$dstdir; mkdir -p $$dstdir;				\
	cmd="find * $$excl -o $$incl -print | pax -rw $$dstdir";	\
       	echo $$cmd; eval $$cmd;						\
	(								\
	    cd $$dstdir; echo cd $$dstdir;				\
	    cmd="rm $(NODISTLIST)"; echo $$cmd; eval $$cmd;		\
	    find */* -type f -print | xargs fgrep -l AOS_STRIP		\
	    | while read stripfile;					\
	    do								\
		echo Stripping $$stripfile;				\
		cmd="unifdef -DAOS_STRIP $$stripfile > $$stripfile~";	\
		eval "$$cmd; mv $$stripfile~ $$stripfile";		\
	    done || true;						\
	    cmd="cd .. && tar -cjf aos-2006.tbz2 aos-2006";		\
	    echo "$$cmd"; eval "$$cmd";					\
	)

listtest: sos/listtest.c sos/list.c
	gcc -std=c99 -O -Wall -Werror -g $^ -o listtest

