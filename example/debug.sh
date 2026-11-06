#!/bin/bash

qemu-system-x86_64 \
  -machine accel=tcg \
  -S -s \
  -drive format=raw,file=build/thread-os-$ARCH &
pid=$!

gdb -x bootloader.gdb; kill -9 $!
