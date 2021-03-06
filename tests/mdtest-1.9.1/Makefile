#/*****************************************************************************\
#*                                                                             *
#*       Copyright (c) 2003, The Regents of the University of California       *
#*     See the file COPYRIGHT for a complete copyright notice and license.     *
#*                                                                             *
#*******************************************************************************
#*
#* CVS info:
#*   $RCSfile: Makefile,v $
#*   $Revision: 1.1.1.1.2.1 $
#*   $Date: 2010/05/11 21:25:16 $
#*   $Author: loewe6 $
#*
#* Purpose:
#*       Make mdtest executable.
#*
#*       make [mdtest]   -- mdtest
#*       make clean      -- remove executable
#*
#\*****************************************************************************/
include ../../backends/build_config.mk

CC.AIX = mpcc_r -bmaxdata:0x80000000
#CC.Linux = mpicc -Wall
#
# For Cray systems
CC.Linux = mpicc
CC.Darwin = mpicc -Wall

# Requires GNU Make
OS=$(shell uname)

# Flags for compiling on 64-bit machines
LARGE_FILE = -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE=1 -D__USE_LARGEFILE64=1
GIGATABLEFS = -D_HAS_GIGA=1

LIBS =../../backends/$(LEVELDBLIBRARY) ../../common.a -lhdfs -ljvm

CC = $(CC.$(OS))

#
# One needs someting like the following code snippet in one's cshrc file is one
# plans to use PLFS with mdtest.
#
#
# If we're not going to use PLFS with mdtest, we don't need to define this variable.
#
# setenv MDTEST_FLAGS ""
#
# If we're going to use PLFS with mdtest, we need to define this variable based on
# whether we are loading a PLFS module or using the system default PLFS installation.
#
# if ( $?PLFS_CFLAGS ) then
#   setenv MDTEST_FLAGS "-D_HAS_PLFS ${PLFS_CFLAGS} ${PLFS_LDFLAGS}"
# else
#   setenv MDTEST_FLAGS "-D_HAS_PLFS -I${MPICH_DIR}/include -lplfs"
# endif


all: mdtest

mdtest: mdtest.c
	$(CC) -D$(OS) $(LARGE_FILE) $(GIGATABLEFS) $(MDTEST_FLAGS) -g -o mdtest mdtest.c -I../../client/ -L../../ -lgiga_client $(LIBS) -lm -lstdc++

clean:
	rm -f mdtest mdtest.o
