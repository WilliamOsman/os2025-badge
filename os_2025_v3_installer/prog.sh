#!/bin/bash

./avrdude.exe -c usbtiny -p attiny85 -U flash:w:os_2025_v2.srec -U eeprom:w:os_2025_v2.eep -U lfuse:w:0x62:m -U efuse:w:0xff:m -U hfuse:w:0x5f:m
