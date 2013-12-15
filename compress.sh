#!/bin/bash

if [ -z $1 ]; then
    echo "USAGE: $0 <version>"
    exit 1
fi

TMPLINK="insaned-$1"

if [ -e $TMPLINK ]; then
    rm -vf $TMPLINK
fi
if [ ! -d backup ]; then
    mkdir backup
fi

ln -s . $TMPLINK

make clean
tar cjvf backup/${TMPLINK}.tar.bz2 $TMPLINK/* --exclude=$TMPLINK/$TMPLINK --exclude=$TMPLINK/backup

rm -vf $TMPLINK

