#!/bin/bash

export LD_LIBRARY_PATH=$HOME/simgrid-3.11.1/lib

./hello_bighybrid.bin 2>&1 | $HOME/simgrid-3.11.1/bin/simgrid-colorizer
