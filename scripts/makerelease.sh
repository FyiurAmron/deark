#!/bin/bash

VER=1.0.0

if [ ! -f formats.txt ]
then
	echo "Run this script from the main directory"
	exit 1
fi

rm -rf .build-tmp
mkdir .build-tmp

D=".build-tmp/deark-$VER"

#echo "Using temporary directory .build-tmp/deark-$VER"

mkdir $D

mkdir $D/src
cp -p src/*.c src/*.h $D/src/

mkdir $D/modules
cp -p modules/*.c modules/*.h $D/modules/

mkdir $D/scripts
cp -p scripts/*.sh $D/scripts/

mkdir $D/proj
mkdir $D/proj/vs2008
cp -p proj/vs2008/*.sln proj/vs2008/*.vcproj $D/proj/vs2008/

mkdir $D/obj
mkdir $D/obj/src
cp -p obj/src/.gitignore $D/obj/src/
mkdir $D/obj/modules
cp -p obj/modules/.gitignore $D/obj/modules/

cp -p readme.txt formats.txt COPYING Makefile $D/

mkdir $D/x64
cp -p Release64/deark.exe $D/x64/

echo "Writing deark-${VER}.tar.gz"
tar --directory .build-tmp -cz -f deark-${VER}.tar.gz deark-$VER

rm -rf .build-tmp

