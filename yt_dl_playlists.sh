#!/bin/bash

if [ $# -ne 1 ]
then
    echo "Usage $0 <playlists_file>";
    exit;
fi

while read playlist
do
    playlist_name="$(youtube-dl --flat-playlist $playlist  | grep Downloading\ playlist | cut -d\: -f 2)"
    # remove leading space
    # http://stackoverflow.com/questions/369758/how-to-trim-whitespace-from-bash-variable
    playlist_name_sanitized="$(echo -e "${playlist_name}" | sed -e 's/^[[:space:]]*//')"
    echo -ne "\rtrying \"$playlist\" -> name: \"$playlist_name_sanitized\""
    [ -d "$playlist_name_sanitized" ] && echo "playlist folder(\"$playlist_name_sanitized\") exists already. skipping.." && continue
    mkdir "$playlist_name_sanitized"
    cd "$playlist_name_sanitized"
    echo "Starting download: \"$playlist_name_sanitized\""
    youtube-dl --ignore-errors --yes-playlist "$playlist"
    cd ..
#    7za e -o+ -p$pw "$1" >/dev/null
#    STATUS=$?
#    if [ $STATUS -eq 0 ]; then
#	echo -e "\nArchive password is: \"$pw\""
#	break
#    fi
done < "$1"
