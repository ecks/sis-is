#!/bin/bash

kill `cat /var/run/quagga/rospf6d_1.pid`
kill `cat /var/run/quagga/rospf6d_2.pid`
kill `cat /var/run/quagga/rospf6d_3.pid`
