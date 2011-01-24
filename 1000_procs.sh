#!/bin/bash

if [ $# -ne 1 ]
then
	echo "Usage: $0 <host_num>"
	exit 1
else
	for i in {1..1000}
	do
		ptype=$(($i % 230))
		ptype=$(($ptype + 1))
		./test1 $ptype $1 54321 &
	done
fi
