#!/bin/bash
set -e

# Install dependencies
apt-get update
apt-get install -y \
    build-essential \
    openmpi-bin \
    openmpi-common \
    libopenmpi-dev \
    libblas-dev \
    liblapack-dev \
    libarpack2-dev \
    libparpack2-dev \
    # cmake \
    # libopenblas64-openmp-dev \
    # liblapacke-dev \
    # libhwloc-dev \
    # libscotch-dev \
    # flex\
    # bison \
    # pkg-config\
    # 2to3

# Build SPOOLES
cd SPOOLES.2.2
make global -j
cd ..

# Build CalculiX
cd CalculiX/ccx_2.22/src
make -j

# Copy executable to nicer location
cp ccx_2.22 ../../../..

echo "Build complete. Executable copied to parent folder"
