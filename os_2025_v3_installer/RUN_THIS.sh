#!/usr/bin/env bash
CMD='./avrdude.exe -c usbtiny -p attiny85 \
  -U flash:w:os_2025_v3.srec \
  -U eeprom:w:os_2025_v3.eep \
  -U lfuse:w:0x62:m \
  -U efuse:w:0xff:m \
  -U hfuse:w:0x5f:m'

while true; do
  $CMD
  echo
  read -p "Press [Enter] to flash again, or Ctrl+C to quit…"
done
