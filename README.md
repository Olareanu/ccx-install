# ccx-install

This repo contains all the files needed to build Calculux CCX, includeing the SPOOLES and ARPACK libraries.


### How these files were made

```

# Download and unzip Calculix 2.22 source
wget https://www.dhondt.de/ccx_2.22.src.tar.bz2
tar -xjf ccx_2.22.src.tar.bz2
rm ccx_2.22.src.tar.bz2

# Download and unzip Spooles
wget https://www.netlib.org/linalg/spooles/spooles.2.2.tgz
mkdir SPOOLES.2.2
tar -xzf spooles.2.2.tgz -C SPOOLES.2.2
rm spooles.2.2.tgz


# Modify the SPOOLES.2.2/Make.inc file with correct compiler flags (See file)


# Follow CCX note about incorrect Spooles make file:
Note: makefile ~/SPOOLES.2.2/Tree/src/makeGlobalLib contains an
         error: file drawTree.c does not exist and should be replaced
         by draw.c


# Download ARPACK
git clone https://bitbucket.org/chaoyang2013/arpack.git
rm -rf arpack/.git


# Mofify the ARPACK/ARmake.inc file, set home and plat

```


### Building SPOOLES, ARPACK then Calculix

First instal the dependencies requiered:
```
apt-get install -y build-essential openmpi-bin openmpi-common libopenmpi-dev libblas-dev liblapack-dev
```

Then just run
```
make global -j
```
inside the SPOOLES.2.2 folder

and run
```
make lib -j
```
inside the ARPACK folder

and run
```
make -j
```
in the CalculiX/ccx_2.22/src folder. An executable named ccx_2.22 should appear. Copy it to a nicer location with:
```
cp ccx_2.22 ../../..
```


