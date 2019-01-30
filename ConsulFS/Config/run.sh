#!/bin/bash
# John Boero
# Example of how to run an instance.

export CONSUL_HTTP_ADDR="localhost:8500"
#export CONSUL_HTTP_SSL=true
export CONSUL_HTTP_TOKEN="[YOURTOKEN]"
#export CONSULFS_LOG=~/consulfs.log
#exoprt CONSULFS_DC=dc1 #not yet implemented

# Debug:
#./consulfs -d -f -s -o direct_io ~/consul

# Not Debug, single thread:
#./consulfs -s -o direct_io ~/consul

# Not Debug, multi-thread:
./consulfs -o direct_io ~/consul
