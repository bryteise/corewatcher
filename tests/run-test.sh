#!/bin/bash

exec 2> /dev/null
ulimit -c unlimited

original_core_pattern=$(cat /proc/sys/kernel/core_pattern)

sudo echo "$PWD/core" | sudo tee /proc/sys/kernel/core_pattern > /dev/null

./bad-write

sudo echo "$original_core_pattern" | sudo tee /proc/sys/kernel/core_pattern > /dev/null
