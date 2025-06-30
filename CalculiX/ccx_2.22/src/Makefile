
CFLAGS = -Wall -O3 -flto=auto -fuse-linker-plugin -I ../../../SPOOLES.2.2 -DARCH="Linux" -DSPOOLES -DARPACK -DMATRIXSTORAGE -DNETWORKOUT
FFLAGS = -Wall -O3 -flto=auto -fuse-linker-plugin

CC=mpicc
FC=mpif90

.c.o :
        $(CC) $(CFLAGS) -c $<
.f.o :
        $(FC) $(FFLAGS) -c $<

include Makefile.inc

SCCXMAIN = ccx_2.22.c

OCCXF = $(SCCXF:.f=.o)
OCCXC = $(SCCXC:.c=.o)
OCCXMAIN = $(SCCXMAIN:.c=.o)

DIR=../../../SPOOLES.2.2

LIBS = \
       $(DIR)/spooles.a \
        ../../../ARPACK/libarpack_AO.a \
        -llapack -lblas -lpthread -lm -lc

ccx_2.22: $(OCCXMAIN) ccx_2.22.a
        ./date.pl; $(CC) $(CFLAGS) -c ccx_2.22.c; $(FC)  -Wall -O3 -o $@ $(OCCXMAIN) ccx_2.22.a $(LIBS) -fopenmp

ccx_2.22.a: $(OCCXF) $(OCCXC)
        ar vr $@ $?

clean:
        rm -f *.o ccx_2.22.a ccx_2.22 date.c
        @echo "Clean complete"

.PHONY: clean