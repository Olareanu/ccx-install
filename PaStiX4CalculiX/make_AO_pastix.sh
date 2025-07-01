#!/bin/bash
# Minimal PaStiX build script using system's 64-bit libraries
# Uses libmumps64-scotch-dev and libopenblas64-openmp-dev from apt

set -e  # Exit on any error

if ! [[ -d build ]]; then
    mkdir build
fi
cd build

# Set installation directory (expand tilde)
INSTALLPATH="$HOME/pastix_minimal"

# System library paths for 64-bit versions
SCOTCH_DIR="/usr"
SCOTCH_INCDIR="/usr/include/scotch-int64"
SCOTCH_LIBDIR="/usr/lib/x86_64-linux-gnu/scotch-int64"
HWLOC_DIR="/usr"
# Use system OpenBLAS64 libraries
CBLAS_INCDIR="/usr/include/x86_64-linux-gnu/openblas64-openmp"
CBLAS_LIBDIR="/usr/lib/x86_64-linux-gnu"

cmake \
    -DCMAKE_INSTALL_PREFIX=${INSTALLPATH} \
    -DCMAKE_BUILD_TYPE=Release \
    -DPASTIX_INT64=ON \
    -DPASTIX_ORDERING_SCOTCH=ON \
    -DSCOTCH_DIR=${SCOTCH_DIR} \
    -DSCOTCH_INCDIR=${SCOTCH_INCDIR} \
    -DSCOTCH_LIBDIR=${SCOTCH_LIBDIR} \
    -DHWLOC_DIR=${HWLOC_DIR} \
    -DCBLAS_INCDIR=${CBLAS_INCDIR} \
    -DCBLAS_LIBDIR=${CBLAS_LIBDIR} \
    -DPASTIX_WITH_CUDA=OFF \
    -DPASTIX_WITH_PARSEC=OFF \
    -DPASTIX_WITH_STARPU=OFF \
    -DPASTIX_WITH_MPI=OFF \
    -DPASTIX_ORDERING_METIS=OFF \
    -DBUILD_TESTING=OFF \
    -DBUILD_EXAMPLES=OFF \
    -DPASTIX_WITH_FORTRAN=OFF \
    -DCMAKE_C_COMPILER=gcc \
    -DCMAKE_CXX_COMPILER=g++ \
    -DCMAKE_Fortran_COMPILER=gfortran \
    -DCMAKE_C_FLAGS="-fopenmp" \
    -DCMAKE_CXX_FLAGS="-fopenmp" \
    -DCMAKE_Fortran_FLAGS="-fopenmp" \
    ..

echo "CMake configuration completed successfully!"
echo "Starting build..."
make -j$(nproc)

echo "Build completed successfully!"
echo "Installing PaStiX..."
make install

echo "Build complete! PaStiX installed to: ${INSTALLPATH}"
echo "You can now use PaStiX by setting:"
echo "  export PASTIX_DIR=${INSTALLPATH}"
