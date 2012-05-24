#!/bin/bash

sudo ./ospf6d -d -i /var/run/quagga/rospf6d_1.pid -P 2607 -f ospf6d_1.conf &
sudo ./ospf6d -d -i /var/run/quagga/rospf6d_2.pid -P 2608 -f ospf6d_2.conf &
sudo ./ospf6d -d -i /var/run/quagga/rospf6d_3.pid -P 2609 -f ospf6d_3.conf &
