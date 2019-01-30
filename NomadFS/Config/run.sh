#!/bin/bash
# John Boero
# Example of how to run an instance.

export NOMAD_ADDR="http://localhost:4646"
#export NOMAD_TOKEN="[YOURTOKEN]"

# Debug:
#./nomadfs -d -f -s -o direct_io ~/nomad

# Not Debug, single thread:
#./nomadfs -s -o direct_io ~/nomad

# Not Debug, multi-thread:
./nomadfs -o direct_io ~/nomad
