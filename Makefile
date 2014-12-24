#
#  Copyright 2012 Jyri J. Virkki <jyri@virkki.com>
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

BUILD=$(TOP)/build
INC=
LIB=
CC=gcc -Wall -Wextra -std=gnu99 $(OPT) $(INC) $(LIB)
CFLAGS=

SRCS:=$(wildcard src/*.c)
OBJS:=$(patsubst src/%.c,$(BUILD)/%.o,$(SRCS))

ifeq ($(BUILD_OS),Linux)
OBJCP=objcopy
CFLAGS=-D_FILE_OFFSET_BITS=64
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

ifeq ($(BUILD_OS),SunOS)
OBJCP=gobjcopy
USAGE=$(BUILD)/usage.o
USAGE_ARCH=-O elf32-i386 -B i386
endif

ifeq ($(BUILD_OS),Darwin)
OBJCP=
USAGE=
endif

ifeq ($(DEBUG),1)
OPT=-g $(DEBUGOPT)
else
OPT=-O3
endif


dupd: $(OBJS) $(USAGE)
	$(CC) $(OPT) $(OBJS) $(USAGE) -lsqlite3 -lcrypto -o dupd

$(BUILD)/%.o: src/%.c
	mkdir -p $(BUILD)
	$(CC) $(INC) $(CFLAGS) -c $< -o $@

$(BUILD)/usage.o: USAGE
	$(OBJCP) -I binary $(USAGE_ARCH) USAGE $(BUILD)/usage.o

clean:
	rm -f dupd
	rm -rf $(BUILD)

lint:
	lint -x -errfmt=simple $(LIB) $(INC) $(SRCS)

test: dupd
	(cp testfiles/* $(BUILD) && \
		cd $(BUILD) && \
		./runtest)

gcov:
	$(MAKE) clean
	DEBUG=1 DEBUGOPT="-fprofile-arcs -ftest-coverage" $(MAKE) dupd
	$(MAKE) test
	(cd $(BUILD) && \
		cp ../src/*.c . && \
		ln -s ../src . && \
		gcov -bf *.c)
	@echo Remember to make clean to remove instrumented objects