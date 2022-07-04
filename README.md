# Bitwatch
Transform your LILYGO T-Watch 2020 into a Bitcoin hardware wallet!

Inspired by Bowser (https://github.com/arcbtc/bowser-bitcoin-hardware-wallet).

**WARNING**
This project is work-in-progress! While I make every effory to make the wallet as reliable as possible, make sure you know what you are doing and I am not responsible for any losses.

## Introduction
The LilyGo TTwatch is a simple ESP32 based smartwatch that can be programmed like any other ESP32 board, and due to its appearance, I thought it would make an excellent candidate for a 'stealth' Bitcoin hardware wallet that can easily be disguised as a conventional cheap smartwatch. The hardware is very basic, with no camera, mic, USB storage or microSD card slot. This makes getting data on and off the device a bit challenging, but still possible. In summary, the usage of this device would go something like this:
1. Turn device on for the first time. Initialise your wallet, set a PIN code, record your seed phrase as normal.
2. From the menu in the wallet mode, export the wallet zpub (public master key for the wallet). We need to get this back to our laptop to set up a watch-only wallet, however we have no USB-accessible storage on the device, and no microSD. So we start up a wireless access point on the watch, and serve up the zpub file over it. Connect to this AP on the laptop and download the file.
3. Fire up Electrum, and set up a watch-only wallet using the zpub - instructions for how to do this are here if you need it: https://bitcointalk.org/index.php?topic=4573616.0
4. Send some sats to your wallet - either using Electrum or by displaying the receive address & QR code on the watch
5. Now when we want to send some sats, we create a partially-signed transaction (PSBT) using Electrum, which we need to send to the watch for signing. Again, we are limited in the ways in which we can get data onto the device at run-time, so we have to use either Arduino Studio or the `esptool` python script to load the PSBT over USB to the watch - more details on exactly how to do this below. TODO: investigate if we can POST this to a webserver on the watch's AP instead
6. Once the PSBT has been sent to the watch, use the `sign transaction` menu item. If successful, the signed transaction will be made available on the watch's AP. Connect to this again and download the transaction as you did with the zpub. You can now take the transaction and broadcast it through a tool of your choice - RoninDojo has a nice one.

<a href="https://odysee.com/bitwatch-introduction:c?r=H9XobdjBDvK7Z1bQPUZSQccvEYEA8qPf" target="_blank"><img src="https://user-images.githubusercontent.com/32391650/177209415-e0f21b06-1e7b-4d71-94c7-2392d891b7b4.png"></a>

## Boards and Libraries
The T-Watch uses the standard ESP32 board. If you don't already have the ESP32 board installed, go to `File`, select `Preferences` and under the "additional sources" text box, enter this URL: `https://dl.espressif.com/dl/package_esp32_index.json`. Then go into Tools->Board->Boards Manager. Search for esp32 and install the latest version. Then go to Tools->Boards, then under Boards, scroll down until you get to `TTGO T-watch`.
To load the library, go to the GitHub site, download the repository in zip format, and then import it into Arduino using the Library import ZIP function.
`https://github.com/Xinyuan-LilyGO/TTGO_TWatch_Library`.
Other libraries to be installed in the same manner:
 - uBitcoin - https://github.com/micro-bitcoin/uBitcoin

Finally, through the Libraries Manager, install the following:
- QRcode 0.0.1

## Persistence
T-Watch 2020 has no SD card, so use SPIFFS to host the text file providing wallet commands. We require two writable partitions in our internal storage to hold wallet data. The nature of the SPIFFS partition is that we must write to it as a whole, replacing/erasing any files currently on the partition. This would have the undesirable effect of removing our saved wallet keys every time we want to write a new command.
To address this, we set up two partitions on the device, one to hold persistent data such as our wallet keys file, and another to hold our command file.

For reading back signed transactions and ZPUBs from the device, we host a WIFI access point on the watch, and access the files via a web browser.

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

### Loading Commands

From the repository, edit the file `bitwatch.txt`, and add the commands following the pattern above. Then either load it using the Arduino IDE using the steps above, or the following command:
`python esptool.py --chip esp32 --port [port] --baud [baud] write_flash -z 0x110000 spiffs.bin`. Note that loading a new file to SPIFFS will cause the watch to restart.

## Accessing the wallet
On startup, the watch will display a black screen for 5 seconds. During this period, press the power button 3 times to access the wallet. If not pressed, the watch will continue to boot into a standard watch mode.

## Wallet Menu

After creating and accessing the wallet, the following menu options are available:
### Display Pubkey
Displays the public key as a QR code and text on screen. Can be used for making payments to the wallet.

### Sign Transaction
Signs an unsigned transaction exported as hex from Electrum wallet. To load the transaction, generate the transaction in Electrum, then export it as hex. Note - I had to use Electrum 3.3.8 to get hex output, more recent versions seem to have stopped exporting in hex format.
Once you have the hex of the transaction, save it into `bitwatch.txt`, prefaced with `SIGN `. Now load the file onto the watch (see the section above on loading files into SPIFFS). This will cause the watch to reboot. If the transaction is valid, it will be signed successfully and the signed transaction will be published on the local access point to be accessed from a browser (see the section below on reading files from SPIFFS).
Once complete, connect to the watch access point, copy the signed transaction, reconnect to your regular wifi, and paste the signed transaction hex into a Bitcoin transaction broadcast website, e.g. https://blockstream.info/tx/push

### Export ZPUB
The ZPUB of the wallet allows Electrum (or any other watch-only wallet) track the current value held in the wallet, and generate unsigned transactions. The ZPUB will be displayed as a QR code and written as text form to SPIFFS, accessible via the local access point. This file can then be imported into your wallet software e.g. Electrum from where you can receive transactions and create a PSBT for signing on your watch.

### Show seed
Shows the seed words to allow recovery of the wallet.

### Wipe device
Erases the wallet and starts the creation of a fresh one.

### Restore from seed
Restores a wallet from seed words, which need to have been stored in `bitwatch.txt` prefaced with `RESTORE ` as described above.

### Restart
Reboots the watch

## Accessing files from the watch
The `Export ZPUB` and `Sign Transaction` operations require us to read back files that have been modified on the watch. The SPIFFS filesystem is not directly readable over USB, so as a workaround, I have set up a WIFI access point that is started when the wallet is started, and hosts the contents of `bitwatch.txt`. This could charitably be described as 'suboptimal' from a security standpoint, but it is enough to make the end-to-end workflow functional for now :)
By default, the access point is named 'yourAp' with a password of 'yourPassword'. Feel free to edit this in the source before deploying to your watch. Once connected, browse to 192.168.4.1 in your browser to view the updated content of bitwatch.txt.

