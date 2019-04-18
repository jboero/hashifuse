#!/bin/bash
# John Boero
# Example of how to run an instance.

# Note for your own private TFE instance, you'll need your CA registered at system level.
# No mechanism here for lib curl's CA override.
#export TFE_ADDR="https://localhost"
export ATLAS_TOKEN="[YOURTOKEN]"

# Debug:
#./tfefs -d -f -s -o direct_io ~/vault

# Not Debug, single thread:
./tfefs -s -o direct_io ~/vault
