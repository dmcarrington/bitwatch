# Bitwatch
Transform your LILYGO T-Watch 2020 into a Bitcoin hardware wallet!

Inspired by Bowser (https://github.com/arcbtc/bowser-bitcoin-hardware-wallet).

**WARNING**
This project is work-in-progress! While I make every effory to make the wallet as reliable as possible, make sure you know what you are doing and I am not responsible for any losses.

### Note - Version 0.2 makes use of WIFI autoconnect portals to apply settings and transactions. This removes the need to set up fancy partitioning schemes and use scripts to send transactions to and from the watch.

## Introduction
The LilyGo TTwatch is a simple ESP32 based smartwatch that can be programmed like any other ESP32 board, and due to its appearance, I thought it would make an excellent candidate for a 'stealth' Bitcoin hardware wallet that can easily be disguised as a conventional cheap smartwatch. The hardware is very basic, with no camera, mic, USB storage or microSD card slot. To get arounf this, we use Wireless Access Point Autoconnect windows to input data to the device, and QR codes on the watch itself where possible. The end-to-end usage can work like this:

1. Turn device on for the first time. If there is no wallet present, it will enter configuration mode, starting a wireless acess point. From here you can set a PIN code, record your seed phrase, or restore a wallet from a seed phrase.
2. From the menu in the wallet mode, export the wallet zpub (public master key for the wallet). This can be accessed in two ways:
    a. Select "Show ZPUB" from the wallet menu, which shows the ZPUB as a QR code for reading by a mobile app e.g. sentinel.
    b. Select "Settings" from the wallet menu, then connect to the wireless access point on the device with Electrum on. Click the "ZPUB" menu item at the top right of the autoconnect window, and copy the text string beginning "zpub..."
3. Fire up Electrum, and set up a watch-only wallet using the zpub - instructions for how to do this are here if you need it: https://bitcointalk.org/index.php?topic=4573616.0
4. Send some sats to your wallet - either using Electrum or by displaying the receive address as a QR code on the watch
5. Now when we want to send some sats, we create a partially-signed transaction (PSBT) using Electrum, which we need to send to the watch for signing. Copy the hex of the PSBT, then tap the "Sign Transaction" button on the watch menu. Connect your device to the Wireless AP, and paste the transaction hex into the text field.
6. Click "Save" to sign the transaction. If successful, the signed transaction will be displayed on the access point page. You can now take the transaction and broadcast it through a tool of your choice.

<a href="https://odysee.com/@davidcarrington:3/bitwatch-introduction:c" target="_blank"><img src="https://user-images.githubusercontent.com/32391650/177209415-e0f21b06-1e7b-4d71-94c7-2392d891b7b4.png"></a>

## Boards and Libraries
The T-Watch uses the standard ESP32 board. If you don't already have the ESP32 board installed, go to `File`, select `Preferences` and under the "additional sources" text box, enter this URL: `https://dl.espressif.com/dl/package_esp32_index.json`. Then go into Tools->Board->Boards Manager. Search for esp32 and install the latest version. Then go to Tools->Boards, then under Boards, scroll down until you get to `TTGO T-watch`.
To load the library, go to the GitHub site, download the repository in zip format, and then import it into Arduino using the Library import ZIP function.
`https://github.com/Xinyuan-LilyGO/TTGO_TWatch_Library`.
Other required libraries are provided in the `libraries` folder of the repo, copy these into your local Adruino libraries folder.

## Accessing the wallet
On startup, the watch will display a black screen for 5 seconds. During this period, press the power button 3 times to access the wallet. If not pressed, the watch will continue to boot into a standard watch mode.

## Initialization
On first use, there will no no wallet present on the device. When first started in wallet mode, a new wallet will be created, and the watch will enter AP mode. The access point will be called `Bitwatch-<mac_address>`, and the default password is `ToTheMoon1`. The default is set in the code and be configured via the config AP. Please note, given the limited hardware capabilities of the watch, connecting can take some time, and the autoconnect page can take a while to load. Be patient!
<img src="https://user-images.githubusercontent.com/32391650/182398204-efba176f-8211-4f0a-a328-3dac35febf44.png"/>
Note down the 24-word seed phrase as you would for any other wallet, and set the PIN to a memorable number. Restart the watch once you have finished. You should now be prompted to enter the PIN, after which you will see the main wallet menu on the watch.

## Wallet Menu

After creating and accessing the wallet, the following menu options are available:
### Display Receive Address
Displays the current receive address as a QR code on screen for making payments to the wallet.

### Sign Transaction
Signs an unsigned transaction exported as hex from Electrum wallet. To load the transaction, generate the transaction in Electrum, then export it as hex. Note - I had to use Electrum 3.3.8 to get hex output, more recent versions seem to have stopped exporting in hex format.
Once you have the hex of the transaction, tap the `Sign Transaction` button of the watch, which will launch the AP on the watch. Connect to this, and you will see a text box to paste the PSBT. Paste the hex here and click `save`, and the watch will sign the transaction and display the result. 
<img src="https://user-images.githubusercontent.com/32391650/182398206-bf8cf7d0-b2b9-4845-a8bd-6426b3a16366.png"/>
<img src="https://user-images.githubusercontent.com/32391650/182398208-044d2eff-789e-4e32-95cb-68163c4f5a6c.png"/>
Copy the signed transaction, reconnect to your regular wifi, and paste the signed transaction hex into a Bitcoin transaction broadcast website, e.g. https://blockstream.info/tx/push, or for something more private RoninDojo has a nice one, alternatively <a href="http://explorerzydxu5ecjrkwceayqybizmpjjznk5izmitf2modhcusuqlid.onion/tx/push">Blockstream's tor page</a> .

### Show ZPUB
Displays the ZPUB of the wallet as a QR code to allow a mobile app e.g. Sentinel to scan and set up a watch-only wallet if desired.

### Settings
Starts an access point that allows a PIN, AP password and seed phrase to be viewed or entered in order to restore a wallet. Also displays the ZPUB in text format to allow it to be copied and pasted into Electrum.

<img src="https://user-images.githubusercontent.com/32391650/182398206-bf8cf7d0-b2b9-4845-a8bd-6426b3a16366.png"/>

### Wipe device
Erases the wallet and starts the creation of a fresh one.

### Restart
Reboots the watch