#!/bin/bash
set -e

# Install dependencies
sudo apt-get update
sudo apt-get install -y build-essential openmpi-bin openmpi-common libopenmpi-dev libblas-dev liblapack-dev libarpack2-dev libparpack2-dev


# Build SPOOLES
cd SPOOLES.2.2
make global -j
cd ..

# Build CalculiX
cd CalculiX/ccx_2.22/src
make -j

# Copy executable to nicer location
cp ccx_2.22 ../../..

echo "Build complete. Executable copied to CalculiX/ccx_2.22/ccx_2.22"
