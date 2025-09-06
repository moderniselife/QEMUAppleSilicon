#!/bin/bash
#
# Restore Script
# Must be run on the companion VM
# This script restores the iPhone to the specified IPSW
# It will also use the specified root ticket
# It will also use the specified SEP ROM
# It will also use the specified SEP Firmware
#
idevicerestore --erase --restore-mode -i 0x1122334455667788 iPhone11,8,iPhone12,1_14.0_18A5351d_Restore.ipsw -T root_ticket.der