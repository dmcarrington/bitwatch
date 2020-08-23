A basic bitcoin wallet to run on the LILYGO T-Watch 2020

Inspired by Bowser (https://github.com/arcbtc/bowser-bitcoin-hardware-wallet)

T-Watch 2020 has no SD card, so use SPIFFS to host the text file providing wallet commands. The text file is contained in the `bitwatch/data` directory, and needs to be loaded to the bitwatch after flashing using the steps described in https://randomnerdtutorials.com/install-esp32-filesystem-uploader-arduino-ide/. Commands to be written to the file are the same as in the Bowser wallet from which this project is derived:

COMMANDS
Wipe device completely, setup new wallet:
HARD RESET

Restore from a seed backup:
RESTORE husband behind shallow promote....

Sign an Electrum transaction:
SIGN 45505446ff00020000000001016cb....

Tap user button 3x to initiate wallet, otherwise display simple watch interface.

TODO:
Get splash screen from bitmap while waiting for initial keypress
Wallet menu based on power button presses
Start webserver to host PSBT for download
Use SPIFFS to access wallet setup