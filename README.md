[![Build Status](https://travis-ci.org/openlgtv/epk2extract.svg?branch=master)](https://travis-ci.org/openlgtv/epk2extract)

jffs2-image-extract
===========


jffs2-image-extract is a tool that wrriten in C++ and can extract jffs2 mtd image contents

Codes from the LG devices extraction tool epk2extract


To compile on Linux:
===========================================

### Install build dependencies:
Ubuntu/Debian:
```shell
apt-get install git build-essential cmake liblzo2-dev libssl-dev libc6-dev
```
Mandriva/Mageia:
```shell
urpmi git task-c++-devel cmake liblzo-devel libopenssl-devel glibc-devel --auto
```

### Build it
```shell
./build.sh
```

After building, jff2extract can be found in ./build_\<platform\>/ 


To compile on Cygwin:
=====================

### Install Cygwin and during setup select following packages:

    Devel -> gcc-g++, git, cmake, make
    Libs  -> liblzo2-devel, zlib-devel
    Net   -> openssl-devel
    Utils -> ncurses

### Build it
```shell
./build.sh
```

The build script automatically copies required shared libraries to the ./build_cygwin/ folder, so you can use jffs2extract standalone/portable without a full cygwin installation.


=====================
### How to speed up extraction process
You can build the test build, which contains compiler optimizations, with this command
```shell
CMAKE_FLAGS=-DCMAKE_BUILD_TYPE=Test ./build.sh
```
The Test build is orders of magnitude faster than the Debug build

### To use:

Run it via sudo/fakeroot to avoid warnings (while extracting device nodes from rootfs):

    fakeroot ./jffs2extract file -[v/k option]

