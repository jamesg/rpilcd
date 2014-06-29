Raspberry Pi Audio Player
=========================
        _______________________
       /   (-) (-) (-) (-)    /|   * Eight push-to-make switches.
      /                      / |   * USB port for audio file storage.
     /   (-) (-) (-) (-)    / +|   * 2x16 character LCD (4x20 also
    /______________________/ /B|     supported).
    |                      |+S |
    |  +----------------+  |U  |   Button Layout
    |  |Now Playing     |  |   |   -------------
    |  |Raspberry Pi    |  |  /    VOL+ FILE NOW  VOL
    |  +----------------+  | /     VOL-  <<  PLAY  >>
    |______________________|/

An audio player for the Raspberry Pi, controlled through a HD44780 LCD screen
and switches connected to the Raspberry Pi GPIO pins.

Building
--------

Mike McCauley`s [BCM 2835 library](http://www.airspayce.com/mikem/bcm2835/)
library is used to control the GPIO pins.  This library is required to run
RPILCD with a real LCD and switches.  Otherwise, add SIMULATE_LCD=1 to the
command line to print LCD contents to the command line.  SDL and SDL Mixer are
required for playing audio.

Run this command to build the play executable.

    make SIMULATE_LCD=1

Or this command if you have a Raspberry Pi with a HD44780 screen.

    make

Hardware
--------

The hardware consists of a Raspberry, a Humble Pi board with eight push-to-make
buttons and circuitry, and a HD44780 20x4 character board and LCD.

### Button circuit

     ------------------------------------ +
                 |
                 ~ r
                 |     _
                 +----- -----+
                 |           |
     -------------- GPIO n   ------------ GND

Each button pulls a GPIO pin to ground through a resistor.  The value of the
resistor isn`t too important, 1k is fine.

### LCD circuit

     GPIO 22 ----- D4        GPIO  4 ----- E
     GPIO 23 ----- D5        GPIO 10 ----- RS
     GPIO 24 ----- D6
     GPIO 25 ----- D7

### Power circuit

The Raspberry Pi is powered by USB and only provides power to the LCD, so this
step is just a matter of connecting some wires.

     HD44780                Raspberry Pi

     RW           -----     Ground
     VSS          -----     Ground (through resistor to adjust contrast)
     VDD          -----     5V
     V0           -----     Ground
     A            -----     5V (through resistor)
     K            -----     Ground

User Interface
--------------

The audio player presents a modal user interface due to the small number of
buttons (eight) and small screen size (2x16 characters).

Physical buttons:

* Mode (now playing, file list, volume).
* Play (play file, enter directory, play/pause) - mode function accessible by
  holding down button.
* Up (volume up, list up, skip forward).
* Down.


### Modal Screens

Modes are selected by pressing one of the three mode buttons.

#### "Now Playing" screen.

    +--------------------+
    |    Now Playing     |
    |00:00               | The track title scrolls left so that the entire
    |Track title         | name is readable.
    |                    |
    +--------------------+

#### "Files" screen; list scrollable using up/down buttons.

    +--------------------+
    |File List      00:00| The middle item is always the one selected - it is
    | Directory/         | possible to move one space outside of the list.
    |-File.mp3           | The selected item scrolls left so the entire name is
    |                    | readable.
    +--------------------+

#### "Volume Control" screen

    +--------------------+
    |Volume Control 00:00|
    |                    | '+' indicates current volume.
    ||         +        ||
    |                    |
    +--------------------+

Raspberry Pi Setup
------------------

The program will compile and run happily with the offical Raspbian
distribution, but a lighter distribution such as TinyCore should be considered
for deployment in an embedded environment.  TinyCore Linux boots faster then
Raspbian and mounts filesystems in read-only mode by default, meaning that the
filesystem won`t be damaged if there is a power failure.

To deploy on TinyCore Linux,

1. Install the SDL_mixer package and enable it on boot.  Installing SDL_mixer
   should also install smpeg, which is required for playing MP3 files.
2. Create a mydata.tgz archive (or boot the system and get TinyCore to create
   it for you).  Install the play executable as /opt/rpilcd/play and
   bootlocal.sh as /opt/bootlocal.sh.

There used to be a bug in SMPEG (which SDL Mixer uses for MP3 decoding) causing
any program using the library to crash when seeking through an MP3 file.  This
is fixed in newer versions of SMPEG and should now be fixed in the TinyCore
repositories.  If the play executable crashes when seeking through an MP3 file,
building and installing a newer version of SMPEG will fix the problem.

