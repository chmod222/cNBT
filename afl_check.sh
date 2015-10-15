#!/usr/bin/env bash

afl_check="$1"

echo -ne '\x09\x00\x00' | $afl_check
