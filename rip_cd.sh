#!/bin/bash


if [ $# -ne 1 ]
then
    echo "Usage $0 <folder>";
    exit;
fi

[ -d "$1" ] && echo "$1 exists already" && exit

mkdir "$1"
cd "$1"

cdparanoia -B

for i in *.wav; do lame --preset cbr 320 "$i" "$(basename "$i" .wav)".mp3; done

zip -r $(basename $(pwd)).zip *.mp3
