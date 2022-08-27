# Bitwatch
Transform your LILYGO T-Watch 2020 into a Bitcoin hardware wallet!

Inspired by Bowser (https://github.com/arcbtc/bowser-bitcoin-hardware-wallet).

**WARNING**
This project is work-in-progress! While I make every effory to make the wallet as reliable as possible, make sure you know what you are doing and I am not responsible for any losses.

### Note - Version 0.2 makes use of WIFI autoconnect portals to apply settings and transactions. This removes the need to set up fancy partitioning schemes and use scripts to send transactions to and from the watch.

## Introduction
The LILYGO T-watch is a simple ESP32 based smartwatch that can be programmed like any other ESP32 board and, due to its appearance, makes an excellent candidate for a 'stealth' Bitcoin hardware wallet that can easily be disguised as a conventional cheap smartwatch. 
The hardware is very basic with no camera, mic, USB storage or microSD card slot. To get around this, we make use of a Wireless Access Point Autoconnect feature to input data through a second device where needed and present QR codes on the watch where ever possible. 

## Features & UX

1. Export zpub in the wallet menue brings up the public master key for the wallet as a QR readable by mobile apps like e.g. Sentinel. 
Fire up an Electrum watch-only wallet with this zpub (preferably on the device where the AP is located) https://bitcointalk.org/index.php?topic=4573616.0 
2. Send some sats to your wallet by either using Electrum OR by displaying the receive address as a QR code on the watch 
3. To send sats we create a PSBT using Electrum and send it to the watch to get signed, saved and broadcasted through the auto-connect window.

<a href="https://odysee.com/@davidcarrington:3/bitwatch-introduction:c" target="_blank"><img src="https://user-images.githubusercontent.com/32391650/177209415-e0f21b06-1e7b-4d71-94c7-2392d891b7b4.png"></a>

## Setup Instructions 

### First steps
Install Electrum v3.3.8 (preferably on the device from where the Access Point will be accessed).

The T-Watch uses the standard ESP32 board. To be able to run the Arduino IDE, which we will need to write data to your watch, you will need the lastest python version https://www.python.org/downloads/release/python-3105/. 

After this we can happily install the latest Arduino IDE https://www.arduino.cc/en/software.
Within the Arduino IDE select `Arduino->Preferences` from the menu and in the `additional sources` text field enter the URL "https://dl.espressif.com/dl/package_esp32_index.json". To finally include it in your project go to `Tools->Board->Boards Manager`, search for "esp32" and install the latest version. 

## Boards and Libraries
Download the library from GitHub in zip format and then import it into Arduino using `Sketch->Insert libraries` https://github.com/Xinyuan-LilyGO/TTGO_TWatch_Library. Include it by going ahead with `Tools->Boards->TTGO T-watch`. 
We will on top need the uBitcoin library https://github.com/micro-bitcoin/uBitcoin which is inserted and included in the same way .
Copy all files into your local `Documents/Arduino/libraries` folder like shown below 

![bitwatch](https://user-images.githubusercontent.com/63317640/183272149-0b45f2ab-fb64-4b77-b722-24a3300c9da6.jpg)

Next we will need to open a CLI (like Terminal on MacOS) and open Arduino IDE by navigating to the directory and write the command `open Arduino`. 
This is to prevent us from demotivating python errors later.

We will now start to do get the watch magic running by open the ./libraries/bitwatch-master/bitwatch/bitwatch.ino sketch with Arduino IDE. 
On the top left of the sketch-window you will find a button for "Test" and one for "Upload". 

![bitwatcharduino](https://user-images.githubusercontent.com/63317640/183271911-078868b6-11db-4acf-953c-4139a01c507a.jpg)

Attach the watch to your device and press the left `Verify` button Arduino IDE to build the code. 
If all is fine press `UPLOAD`. Assuming the code is successfully loaded to the watch, it will restart and by default will start in its decoy watch mode unless you enter the secret code (more on this below).

You´re done 🎉 Good job!
    

## Accessing the wallet for the first time
On startup, the watch will display a black screen for 5 seconds. During this period, press the power button 3 times to access the wallet. 
If not pressed the watch will continue to boot into a standard watch mode and remain invisible on the main screen.
On first use there will be no wallet present on the device which leads to a newly created wallet and the watch to enter AP mode. The access point will be called Bitwatch-`MAC_ADRESS` and the default password to log into it from a second device (preferably the one where Electrum is installed as well) is `ToTheMoon1`. 

![bitwatchAP](https://user-images.githubusercontent.com/63317640/183272001-795950c3-c5f1-40e8-9737-9da1135b5143.jpg)

This default PIN and PWD are set in the code and should be changed through the autoconnect window soon. 
Please note, given the limited hardware capabilities of the watch, connecting will take a few seconds more than you are used to and the auto-connect page might sometimes. 

<img src="https://user-images.githubusercontent.com/32391650/182398204-efba176f-8211-4f0a-a328-3dac35febf44.png"/>

Note down the 24-word seed phrase as you would for any other wallet, and set the PIN to a memorable number. Restart the watch once you have finished by pressing the side button of the watch 3 times on startup. 
You should now be prompted to enter the PIN after which you will see the main wallet menu on the watch.

## Wallet Menu

Let´s now dive into the menu of your new hardware-wallet.

### Receive
Displays the current receive address as a QR code on screen for making payments to the wallet.

### Sign Transaction
Signs an unsigned transaction exported as hex from Electrum wallet. To load the transaction, generate the transaction in Electrum and export it as hex. Note - I had to use Electrum 3.3.8 to get hex output, more recent versions seem to have stopped exporting in hex format.
Once you have the hex of the transaction, tap the `Sign Transaction` button of the watch, which will launch the AP on the watch. Connect to this, and you will see a text box to paste the PSBT. Paste the hex here and click `save`, and the watch will sign the transaction and display the result. 

<img src="https://user-images.githubusercontent.com/32391650/182398206-bf8cf7d0-b2b9-4845-a8bd-6426b3a16366.png"/>
<img src="https://user-images.githubusercontent.com/32391650/182398208-044d2eff-789e-4e32-95cb-68163c4f5a6c.png"/>

Copy the signed transaction, reconnect to your regular wifi, and paste the signed transaction hex into a Bitcoin transaction broadcast website, e.g. https://blockstream.info/tx/push, or for something more private RoninDojo has a nice one, alternatively you can use 
<a href="http://explorerzydxu5ecjrkwceayqybizmpjjznk5izmitf2modhcusuqlid.onion/tx/push">Blockstream's tor page</a> .

### Show ZPUB
Displays the ZPUB of the wallet as a QR code to allow a mobile app e.g. Sentinel to scan and set up a watch-only wallet if desired.

### Settings
Starts an access point that allows a PIN, AP password and seed phrase to be viewed or entered in order to restore a wallet. Also displays the ZPUB in text format to allow it to be copied and pasted into Electrum.

<img src="https://user-images.githubusercontent.com/32391650/182398206-bf8cf7d0-b2b9-4845-a8bd-6426b3a16366.png"/>

### Wipe device
Erases the wallet and starts the creation of a fresh one.

### Restart
Reboots the watch
