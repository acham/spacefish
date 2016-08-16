#!/bin/sh

if [ -z "$1" ]
	then
		echo "No argument supplied"
		exit 1
fi

PREFIX="LD_PRELOAD=../../bin/libufs.so LD_LIBRARY_PATH=../../bin/"



echo "ORIGINAL $1"
eval "./$1"

echo "USERLEVEL $1"
eval "$PREFIX ./$1"












