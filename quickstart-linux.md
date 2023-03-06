Quick start guide for Linux users
=================================

Installing the emulator
-----------------------

### Getting the emulator

Obtain the X16 emulator by downloading an official release from the 'releases' tab in Github
(note: currently no recent official releases are available).
Alternatively, download a development build which are automatically created by github every time
an update gets made to the emulator. You find these here: https://github.com/X16Community/x16-emulator/actions/workflows/build.yml?query=branch%3Amaster
You'll need to be signed in into Github to access these files. Look for the Artifact called 'X16-Emu Linux 64-bit'.

Ofcourse, you could also compile the emulator from source, but that is outside the scope of this guide.


### Installing the emulator

* Extract the .zip file containing the emulator into an appropriate location such as `~/x16/emulator/`.
For the rest of this guide we will assume this as the install location.
* This directory should now contain at least `x16emu` (the emulator itself) and `rom.bin` (the X16 system rom).
You can ignore all other files in the zip file for now.
* Make sure the emulator is executable: `chmod u+x ~/x16/emulator/x16emu`


### Create a place for the programs

Programs for the X16 will usually be stored on an SD memory card that the system sees as the main disk drive.
The emulator can use an SD card image file to replicate this, but we are not going to use this feature now,
because the emulator can also use a regular directory on your computer as the disk drive for the X16.
Notice that the X16 usually expects the file names to be in UPPERCASE.

* Create a `disk` directory that will be the X16's storage location: `mkdir ~/x16/disk`


### Convenient launching

The emulator has many options to tweak its behavior. To avoid having to retype these every time,
we'll be using a small shell script to launch the emulator.

* Create a launch script somewhere (for example `~/x16/startemu`) with the following:
    ```sh
    #!/usr/bin/env sh
    ~/x16/emulator/x16emu -fsroot ~/x16/disk -scale 2 $*
    ```
* make the script executable: `chmod u+x ~/x16/startemu`


Installing programs on the 'disk'
---------------------------------
The X16 is capable of using subdirectories. This can be used to avoid cluttering the disk contents
with files from different programs together in a single location.
Instead, create a directory for each program and the files it needs.

* assume we have a game called "kart" and it comes in a zip file that contains the game program,
  the music and the graphics files.
* create a directory for the game: `mkdir ~/x16/disk/KART`  Remember that the X16 usually needs
  directories and file names in UPPER CASE.
* extract the zip file into that directory.


Running programs on the X16
---------------------------

* start the emulator with the launch script
* on the X16, go to the directory containing the program: `DOS"CD:/DIRNAME"`
* load and run the desired program:  `LOAD"GAME"`, followed by `RUN`

