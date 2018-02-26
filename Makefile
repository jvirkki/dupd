#
#  Copyright 2012-2018 Jyri J. Virkki <jyri@virkki.com>
#
#  This file is part of dupd.
#
#  dupd is free software: you can redistribute it and/or modify it
#  under the terms of the GNU General Public License as published by
#  the Free Software Foundation, either version 3 of the License, or
#  (at your option) any later version.
#
#  dupd is distributed in the hope that it will be useful, but
#  WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#  General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with dupd.  If not, see <http://www.gnu.org/licenses/>.
#

TOP:=$(shell  pwd)
BUILD_OS:=$(shell uname)
BUILD_MACHINE:=$(shell uname -m)
VERSION:=$(shell cat version)
GITHASH:=$(shell git rev-parse HEAD)
OPTGEN:=$(shell which optgen | head -c1)

ifeq ($(LCOV_OUTPUT_DIR),)
LCOV_OUTPUT_DIR=./lcov.out
endif

# The install target uses these default locations to install bin/dupd
# and man1/dupd.1. Override in the environment if desired.  Note that
# install prefixes these with the value of DESTDIR which should
# normally be empty so it has no default. It is only used by build
# environments which install to a temporary staging area. This is used
# by the MacPorts build, for instance.
# https://www.gnu.org/prep/standards/html_node/DESTDIR.html

INSTALL_PREFIX ?= /usr/local
MAN_BASE ?= $(INSTALL_PREFIX)/man

BUILD=$(TOP)/build
CCC=$(CC) -Wall -Wextra -std=gnu99 $(OPT) $(LIB)

SRCS:=$(wildcard src/*.c)
OBJS:=$(patsubst src/%.c,$(BUILD)/%.o,$(SRCS))

ifeq ($(BUILD_OS),Linux)
OBJCP=objcopy
CFLAGS=-D_FILE_OFFSET_BITS=64 -DDIRENT_HAS_TYPE
USAGE=$(BUILD)/usage.o
# On Linux, gcc by default compiles to the same bitness as the OS,
# so need to set the flags to objcopy accordingly.
GCCBITS := $(shell getconf LONG_BIT)
ifeq ($(GCCBITS),64)
USAGE_ARCH=-O elf64-x86-64 -B i386
else
USAGE_ARCH=-O elf32-i386 -B i386
endif
endif

ifeq ($(BUILD_OS),OpenBSD)
OBJCP=objcopy
CFLAGS=-m64
USAGE=$(BUILD)/usage.o
USAGE_ARCH=-O elf64-x86-64 -B i386
endif

ifeq ($(BUILD_OS),SunOS)
CC=gcc
CFLAGS=-m64
OBJCP=gobjcopy
USAGE=$(BUILD)/usage.o
USAGE_ARCH=-O elf64-x86-64 -B i386
endif

ifeq ($(BUILD_OS),Darwin)
OBJCP=
USAGE=
CFLAGS=-DDIRENT_HAS_TYPE -m64
endif

ifeq ($(DEBUG),1)
OPT=-g $(DEBUGOPT)
else
OPT=-O3
endif


dupd: src/optgen.c src/optgen.h $(OBJS) $(USAGE)
	$(CCC) $(CFLAGS) $(OPT) $(OBJS) $(USAGE) \
	    -lsqlite3 -lcrypto -lpthread -lm -o dupd

$(BUILD)/%.o: src/%.c src/%.h
	mkdir -p $(BUILD)
	$(CCC) $(INC) $(CFLAGS) \
		-DDUPD_VERSION=\"$(VERSION)\" -DGITHASH=\"$(GITHASH)\" \
		 -c $< -o $@

$(BUILD)/usage.o: man/dupd
	$(OBJCP) -I binary $(USAGE_ARCH) man/dupd $(BUILD)/usage.o

clean:
	rm -f dupd
	rm -rf $(BUILD)
	rm -f dupd*.tar.gz

lint:
	lint -x -errfmt=simple $(LIB) $(INC) $(SRCS)

test:
	(cd tests && ./run)

valgrind:
	(cd tests && DUPD_VALGRIND=1 ./run)

gcov:
	$(MAKE) clean
	DEBUG=1 DEBUGOPT="-fprofile-arcs -ftest-coverage" $(MAKE) dupd
	$(MAKE) test
	(cd $(BUILD) && \
		cp ../src/*.c . && \
		ln -s ../src . && \
		gcov -bf *.c | tee gcov.output)
	@echo Remember to make clean to remove instrumented objects

lcov: gcov
	lcov --capture --directory build --output-file lcov.info
	lcov --remove lcov.info xxhash.c --output-file lcov.info
	genhtml lcov.info --no-branch-coverage \
		--output-directory $(LCOV_OUTPUT_DIR)
	rm -f lcov.info
	$(MAKE) clean

# dupd uses optgen to generate its option parsing code based on
# the config file src/options.conf
#
# optgen is a ruby gem, install it as follows:
#
# sudo gem install optgen
#
# If optgen is not present, skip option handling code generation.
# This allows compiling dupd without optgen present. The downside is
# that optgen.c is checked in although it really should not be.
src/optgen.c src/optgen.h: src/options.conf
ifeq (/,$(OPTGEN))
	(cd src; optgen options.conf)
else
	@echo optgen not found, unable to regenerate option code
endif

.PHONY: man
man:
	MANWIDTH=80 man -l man/dupd.1 > man/dupd

release:
	$(MAKE) clean
	DEBUG=0 $(MAKE)
	rm -rf tar/
	mkdir tar
	( cd tar && \
	mkdir bin man docs && \
	cp ../dupd bin/ && strip bin/dupd && \
	cp ../man/dupd.1 man/ && \
	cp ../docs/* docs/ && \
	tar -cvf dupd_$(VERSION)_$(BUILD_OS)_$(BUILD_MACHINE).tar \
		bin docs man && \
	gzip --best *.tar)
	cp tar/*.tar.gz .
	rm -rf tar/

install: dupd
	mkdir -p $(DESTDIR)$(INSTALL_PREFIX)/bin/
	cp dupd $(DESTDIR)$(INSTALL_PREFIX)/bin/
	mkdir -p $(DESTDIR)$(MAN_BASE)/man1
	cp man/dupd.1 $(DESTDIR)$(MAN_BASE)/man1

uninstall:
	rm -f $(DESTDIR)$(INSTALL_PREFIX)/bin/dupd
	rm -f $(DESTDIR)$(MAN_BASE)/man1/dupd.1
