#!/usr/bin/env bash

if [ ! -f ocvsmd-cli.log ]; then
    touch ocvsmd-cli.log
fi
tail -f ocvsmd-cli.log
