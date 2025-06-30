.POSIX:
#----------------------------------------
#
#  file created by Alexandru Olareanu
#  based on the one from the source
#
#----------------------------------------
#
#  place your favorite compiler here
#
CC = mpicc
#
# mpicc is a wraper over gcc with mpi paths configured
#
#----------------------------------------
#
#  set the compiler flags
# 
OPTLEVEL = -O3
CFLAGS = -Wall $(OPTLEVEL)
#
#----------------------------------------
#
#  set any load flags
#
LDFLAGS =
#
#---------------------------------------------------------------------
#
#  set any thread libraries
#
THREAD_LIBS = -D_POSIX_C_SOURCE=199506L -lpthread
# unsure if necessary
#
#---------------------------------------------------------------------
#
#  set the purify environment (a memory monitoring tool)
#
PURIFY =
PURIFY_GCC_VERSION =
# unsure if necessary
#
#---------------------------------------------------------------------
#
#  set the archive flags
#
  AR = ar
  ARFLAGS = rv
#
#---------------------------------------------------------------------
#
#  set the ranlib environment
#  (if ranlib is not needed, we echo the library name)
#
  RANLIB = echo
# unsure if necessary
#
#---------------------------------------------------------------------
#
#  set suffix rule *.c --> *.o
#
.c.o : 
	$(PURIFY) $(CC) -c $(CFLAGS) $<
#
#---------------------------------------------------------------------
#
#  set suffix rule *.c --> *.a
#
.c.a : 
	$(PURIFY) $(CC) -c $(CFLAGS) $<
	$(AR) $(ARFLAGS) $@ $*.o
	rm -f $*.o
#
#---------------------------------------------------------------------
#
# Unsure if any MPI path is necessary as we're using mpicc
#
#---------------------------------------------------------------------
#
#  MPI install library
#
  MPI_INSTALL_DIR = /usr/local/mpich-1.0.13
#
#---------------------------------------------------------------------
#
#  MPI library path
#
#
  MPI_LIB_PATH = -L$(MPI_INSTALL_DIR)/lib/solaris/ch_p4
#
#
#---------------------------------------------------------------------
#
#  MPI libraries 
#
#
  MPI_LIBS = $(MPI_LIB_PATH) -D_REENTRANT -lmpi -lsocket -lnsl -lthread
#
#
#---------------------------------------------------------------------
#
#  MPI include path
#
  MPI_INCLUDE_DIR = -I$(MPI_INSTALL_DIR)/include
#
#---------------------------------------------------------------------
