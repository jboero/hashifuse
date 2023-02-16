#!/bin/bash
# John Boero
# Example of how to run an instance.

# Note for your own private TFE instance, you'll need your CA registered at system level.
# No mechanism here for lib curl's CA override.
#export TFE_ADDR="https://localhost"
export ATLAS_TOKEN="[YOURTOKEN]"

# Debug:
#./openapifs -d -f -s -o direct_io PATH

# Not Debug, single thread:
./openapifs -s -o direct_io PATH
