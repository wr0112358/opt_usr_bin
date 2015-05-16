#!/bin/bash
echo "RAR-JTR Decrypt Script";
if [ $# -ne 2 ]
then
    echo "Usage $0 <rarfile> <wordlist>";
    exit;
fi

unrar l $1
john --wordlist=$2 --rules --stdout \
    | while read pw
      do
	  echo -ne "\rtrying \"$pw\" "
	  unrar x -e -o+ -inul -p$pw "$1" >/dev/null
	  STATUS=$?
	  if [ $STATUS -eq 0 ]; then
	      echo -e "\nArchive password is: \"$pw\""
	      break
	  fi
      done
