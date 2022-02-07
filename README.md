# gc-pedometer-emulator

Game Boy Advance homebrew designed to emulate the Inrou-Kun, a GameCube pedometer.

## License
gc-pedometer-emulator is Free Open Source Software available under the 2-Clause BSD license. See the LICENSE file for full details.

Based on Extrems' Corner's gba-as-controller project -> https://github.com/extremscorner/gba-as-controller

Background images were taken from Public Domain sources.

## Overview

The Inrou-Kun worked exclusively with the GameCube title Ohenro-San: Hosshin no Dojo. To use this homebrew ROM, copy the file to an appropiate flashcart, then attach the GBA-to-GCN Cable (DOL-011) to the Game Boy Advance. Finally, attach the cable to Controller Port 4 of the GameCube or Wii system.

From the Main Menu, select "Edit Data" to edit information the Game Boy Advance will send to the GameCube. Use the D-Pad to edit specific entries (Up/Down changes values, Left/Right edits specific positions of certain values). Press the A button to move down the list, and press the L or R triggers to switch between pages. When finished, press the B button to return to the main menu.

To actually send data to the GameCube, select "Send Data" from the Main Menu. Only when the Game Boy Advance displays this screen will the ROM emulate the Inrou-Kun pedometer. The process is automatic. Press the B button to return to the main menu.

## Compiling

This ROM requires DevKitPro and DevKitARM to build.
