#!/bin/bash
N=/root/linux-evergo-new
D=arch/arm64/boot/dts/mediatek
mkdir -p "$N/$D"
cp -f "$N/out/$D/mt6833-xiaomi-evergo.dtb" "$N/$D/"
ls -l "$N/$D/mt6833-xiaomi-evergo.dtb"
