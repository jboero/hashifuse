#!/bin/bash
# John Boero
# Example of how to run an instance.

export VAULT_ADDR="http://localhost:8200"
export VAULT_TOKEN="[YOURTOKEN]"

# Debug:
#./vaultfs -d -f -s -o direct_io ~/vault

# Not Debug, single thread:
./vaultfs -s -o direct_io ~/vault

# Not Debug, multi-thread:
#./vaultfs -o direct_io ~/vault
