#!/bin/sh

# TODO:
# -d <dest-folder>
# -> extract basename and append dest-folder

for arg in "$@"
do
    [ ${arg: -4} != ".png" ] \
	&& echo "$arg is not a png-file." && continue;
    in="$arg"
    #out=$(echo "$in" | cut -d. -f 1).pdf
    out="${in%.png}.pdf";
    echo $in;
    echo $out;
    convert "$in" -compress jpeg -resize 1240x1753 \
	-extent 1240x1753 -gravity center \
        -units PixelsPerInch -density 150x150 "$out";
done
