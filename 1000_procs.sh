#!/bin/bash

if [ $# -ne 1 ]
then
	echo "Usage: $0 <host_num>"
	exit 1
else
	for i in {1..10}
	do
		echo "Starting process #$i."
		#./test $i $1
	done
fi
