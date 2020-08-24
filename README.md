# Bitwatch
A basic bitcoin wallet to run on the LILYGO T-Watch 2020

Inspired by Bowser (https://github.com/arcbtc/bowser-bitcoin-hardware-wallet)


## Pre-build setup
T-Watch 2020 has no SD card, so use SPIFFS to host the text file providing wallet commands. We require two writable partitions in our internal storage to hold wallet data. The nature of the SPIFFS partition is that we must write to it as a whole, replacing/erasing any files currently on the partition. This would have the undesirable effect of removing our saved wallet keys every time we want to write a new command.
To address this, we set up two partitions on the device, one to hold persistent data such as our wallet keys file, and another to hold our command file.

In order to set this configuration up, we need to create a new partition table on the device. From the project folder, take the `dual_storage.csv` file, and copy it into `~/.arduino15/packages/esp32/hardware/esp32/1.0.4/tools/partitions/`
In the same directory, edit the file `boards.txt` and add the line
`ttgo-t-watch.menu.PartitionScheme.dual_spiffs=Dual storage (2 x 6.5 MB app, 3.6 MB SPIFFS)
ttgo-t-watch.menu.PartitionScheme.dual_spiffs.build.partitions=dual_storage
ttgo-t-watch.menu.PartitionScheme.dual_spiffs.upload.maximum_size=6553600`

Nor (re)start the Arduino IDE, and select 'Tools -> Partition Scheme' and select the entry that we just created. Now you should be able to compile and build the solution successfully.

## Wallet commands

The wallet is initialised and used to sign transactions by sending commands in a file written to the SPIFFS partition. The text file is contained in the `bitwatch/data` directory, and needs to be loaded to the bitwatch after flashing using the steps described in https://randomnerdtutorials.com/install-esp32-filesystem-uploader-arduino-ide/.
Commands to be written to the file are the same as in the Bowser wallet from which this project is derived:

COMMANDS
Wipe device completely, setup new wallet:
HARD RESET

Restore from a seed backup:
RESTORE husband behind shallow promote....

Sign an Electrum transaction:
SIGN 45505446ff00020000000001016cb....

## Accessing the wallet
On startup, the watch will display a grey screen for 5 seconds. During this period, press the power button once to access the wallet. If not pressed, the watch will continue to boot into a standard watch more

TODO:
Get splash screen from bitmap while waiting for initial keypress
Wallet menu based on power button presses
Start webserver to host PSBT for download
Use SPIFFS to access wallet setup

python esptool.py --chip esp32 --port [port] --baud [baud] write_flash -z 0x110000 spiffs.bin