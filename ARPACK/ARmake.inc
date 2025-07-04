############################################################################
#
#  Program:         ARPACK
#
#  Module:          ARmake.inc
#
#  Purpose:         Top-level Definitions
#
#  Creation date:   February 22, 1996
#
#  Modified:
#
#  Send bug reports, comments or suggestions to arpack@caam.rice.edu
#
############################################################################
#
#
#  *** If you are using a VENDOR SUPPLIED VERSION of MPI, you  ***
#  *** must replace the mpif.h in the following directories    ***
#  ***                                                         ***
#  ***            ARPACK/PARPACK/SRC/MPI/mpif.h                ***
#  ***            ARPACK/PARPACK/UTIL/MPI/mpif.h               ***
#  ***            ARPACK/PARPACK/EXAMPLES/MPI/mpif.h           ***
#  ***                                                         ***
#  *** with the one for the native implementation.             ***
#
#  The complete path to the top level of ARPACK directory, usually
#  $(HOME)/ARPACK
#
home          = /app/ARPACK
#
#  The platform identifier to suffix to the end of library names
#
PLAT          = AO
#
#  The communication library ( need for parallel library only )
#  Current availible platforms are: BLACS or MPI
#
COMMLIB       = MPI
#
#  The directories to find the various pieces of ARPACK
#
BLASdir      = $(home)/BLAS
LAPACKdir    = $(home)/LAPACK
UTILdir      = $(home)/UTIL
SRCdir       = $(home)/SRC
PSRCdir      = $(home)/PARPACK/SRC/$(COMMLIB)
PUTILdir     = $(home)/PARPACK/UTIL/$(COMMLIB)
#
#DIRS   = $(BLASdir) $(LAPACKdir) $(UTILdir) $(SRCdir)
# Comment out the previous line and uncomment the following
# if you already have the BLAS and LAPACK installed on your system.
# NOTE: ARPACK assumes the use of LAPACK version 2 codes.
DIRS   = $(UTILdir) $(SRCdir)
#
#
#  The name of the libraries to be created/linked to
#
ARPACKLIB  = $(home)/libarpack_$(PLAT).a
LAPACKLIB  = 
BLASLIB    = 

ALIBS =  $(ARPACKLIB) $(LAPACKLIB) $(BLASLIB) 
#

# Libraries needed for Parallel ARPACK 
#
PARPACKLIB = $(home)/libparpack_$(COMMLIB)-$(PLAT).a

MPILIBS =  

PLIBS = $(PARPACKLIB) $(ALIBS) $(MPILIBS)

#  Make our own suffixes' list.
#
.SUFFIXES:
.SUFFIXES:	.f	.o
.SUFFIXES:	.F	.f

#
#  Default command.
#
.DEFAULT:
	@$(ECHO) "Unknown target $@, try:  make help"

#
#  Command to build .o files from .f files.
#
.f.o:
	@$(ECHO) Making $@ from $<
	@$(FC) -c $(FFLAGS) $<

.F.f:
	@$(ECHO) Making $*.f from $<
	@$(CPP) -P -DSINGLE $(CPPFLAGS) $< $*.f
	@$(ECHO) Making d$*.f from $<
	@$(CPP) -P -DDOUBLE $(CPPFLAGS) $< d$*.f

#
#  Various compilation programs and flags.
#  You need to make sure these are correct for your system.
#
FC      = mpif90
FFLAGS	= -O3 -std=legacy

# Set the parallel fortran compiler
PFC      = mpifort
PFFLAGS	= -O3 -std=legacy

LDFLAGS = 
CD	= cd
AR      = ar

CHMOD	 = chmod
CHFLAGS	 = -f

COMPRESS = compress

CP	 = cp

#  To find the path for cpp, try `man cpp', and it should list the path
#  at the top, under `Syntax'.  It is usually in /lib.
CPP	 = /lib/cpp
CPPFLAGS =

ECHO	 = echo

LN	 = ln
LNFLAGS	 = -s

MAKE	 = make

MKDIR	 = mkdir
MDFLAGS	 = -p

MV	 = mv
MVFLAGS	 = -f

RM	 = rm
RMFLAGS  = -f

SHELL	 = /bin/sh

TAR	 = tar

#  For System V based machine without ranlib, like Cray and SGI, 
#  use touch instead.
RANLIB   = touch
#RANLIB   = ranlib

#
#  This is the general help target.
#
help:
	@$(ECHO) "usage: make ?"
