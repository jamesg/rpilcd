#!/bin/sh

# Raspberry Pi Audio Player startup script.
# This script should be copied to /opt/bootlocal.sh in the TinyCore
# installation for it to run on startup.

# Mount the first USB device (this contains audio files to play).
sudo mount -o ro /dev/sda1 /mnt/sda1

# Update the rpilcd executable to prevent having to remove the SD card from an
# embedded device.
if [ -d /mnt/sda1/rpilcd ]
then
    cp /mnt/sda1/rpilcd/play /opt/rpilcd/play
    # Write the executable to mydata.tgz on the SD card.
    filetool.sh -b
fi

/opt/rpilcd/play /mnt/sda1

