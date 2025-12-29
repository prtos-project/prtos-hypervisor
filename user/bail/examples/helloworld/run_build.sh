#!/bin/bash

load_addr=0x40200000
entry_point=0x40200000
output_name=resident_sw_image
image_name=resident_sw.bin

# mkimage


aarch64-linux-gnu-objcopy -O binary -R .note -R .note.gnu.build-id    -R .comment -S resident_sw resident_sw.bin
mkimage -A arm64 -O linux -C none -a $load_addr -e $entry_point -d ${image_name} ${output_name}


