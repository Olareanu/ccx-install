#!/bin/bash
set -e

# Install dependencies
sudo apt-get update
sudo apt-get install -y build-essential openmpi-bin openmpi-common libopenmpi-dev libblas-dev liblapack-dev


# Build SPOOLES
cd SPOOLES.2.2
make global -j
cd ..

# Build ARPACK
cd ARPACK

# Modify ARmake.inc 'home' value to current absolute path
ARPACK_PATH=$(pwd)
sed -i "s|^home.*|home = $ARPACK_PATH|" ARmake.inc

make lib -j
cd ..

# Build CalculiX
cd CalculiX/ccx_2.22/src
make -j

# Copy executable to nicer location
cp ccx_2.22 ../../..

echo "Build complete. Executable copied to CalculiX/ccx_2.22/ccx_2.22"
