#!/bin/bash

echo "10.100.1.19:"
./rc 10.100.1.19 -v1 -Ov -Oq -c public
echo "10.100.1.21:"
./rc 10.100.1.21 -v1 -Ov -Oq -c public
