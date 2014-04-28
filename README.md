Insaned
=======

Insaned is a simple linux daemon for polling button presses on SANE scanners.


BIG FAT WARNING
---------------

Although insaned seems to work wery well for me, it is currently IN BETA VERSION.

I am not responsible for any physical or mental damage it might cause to you, your hardware or your pet. Use it on your own risk.

YOU'VE BEEN WARNED

That said, any help is welcomed. Feel free to contact me if you have any comments/suggestions/patches. Especially interesting are reports if it works with another scanners. Please make sure to include the output of "insaned -L" to your report, and, if you have any problems, also the output of "scanimage -A".


Description
-----------

Insaned periodically polls your scanner using the SANE library and runs the corresponding event handler script when a button is pressed. Because of this, button presses are only detected every N milliseconds, so you will have to press and hold the button for at most N milliseconds until an event is fired. Insaned does only fire the same event once in 2500 ms to prevent unwanted repetitions.

It should work with all backends that expose buttons as "Sensors". The daemon reads the value of all sensors every N milliseconds (default: 500) and starts an event handler script named by the sensor name. Polling does not result in a noticeable CPU load, but produces some I/O load. Therefore, it might not be a good idea to run this daemon on a laptop, since it will probably prevent USB bus from entering a low power mode or even keep the laptop awake (not tested yet).

Currently, insaned was tested on:
* Gentoo Linux with sane-backends-1.0.24 and a Canon LiDE 210 flatbed scanner (genesys backend, USB ID 04a9:190a)
* Raspberry Pi with HP Scanjet 2400c (thanks to GaryA)


Why
---

I've tried scanbuttond (http://scanbuttond.sourceforge.net/) and scanbd (http://scanbd.sourceforge.net/).

* scanbuttond could not detect buttons on my scanner
* scanbd is waaay too complicated, it requires to configure SANE for network scanning. I spend 2 evenings trying to make it work and failed.
* I've heard rumors about sanebuttonsd, but couldn't find any documentation about it


Supported devices
-----------------

You can check if SANE exposes sensors of your scanner by running:

    scanimage -A

You should be able to see a list of detected sensors and their current state. If you press and hold a button while running scanimage -A, you should be able to see a different sensor value.

For example, this is the output for my scanner, when I press the "file" button (note the [yes]):

    All options specific to device `genesys:libusb:001:003':
      Scan Mode:
    ...
    <snip>
    ...
      Sensors:
        --scan[=(yes|no)] [no] [hardware]
            Scan button
        --file[=(yes|no)] [yes] [hardware]
            File button
        --email[=(yes|no)] [no] [hardware]
            Email button
        --copy[=(yes|no)] [no] [hardware]
            Copy button
        --extra[=(yes|no)] [no] [hardware]
            Extra button
      Buttons:
        --clear-calibration
            Clear calibration cache

In this case, insaned detects all of my 5 buttons and emits events named "scan", "file", "email", "copy" and "extra".


Event Handler Scripts
---------------------

Event handler scripts are simple shell scripts. Insaned searches for them in /etc/insaned/events/ directory (configurable). The daemon passes current SANE device name as the first and only argument to the script, in case you need to distinguish between several scanners.

All event handler scripts have to exist and have to have the executable flag set, otherwise insaned will print warnings. Create an empty executable file to silence the warning, e.g. like this:

    touch /etc/insaned/events/scan
    chmod +x /etc/insaned/events/scan


Dependencies
------------

* SANE library version 1.0.23 or later (tested with: 1.0.24)
* Recent GCC with support for C++11 standard (tested with: 4.8.2)


Installation
------------

To compile insaned, run:

    make

in the project directory. The daemon will be created in the project directory and called "insaned". You can run it in foreground for testing purposes as follows:

    ./insaned --dont-fork --events-dir=$PWD/init.d --log-file=$PWD/log.log -vv

See also

    ./insaned --help --list-sensors

for more details.

Installation script is currently only provided for gentoo linux.
1. Add insaned/media-gfx/insaned/insaned-0.0.1.ebuild to your local overlay.
2. Run ./compress.sh to create backup/insaned-0.0.1.tar.bz2 and copy backup/insaned-0.0.1.tar.bz2 to /usr/portage/distfiles/
3. cd $YOUR_OVERLAY/media-gfx/insaned
4. ebuild insaned-0.0.1.ebuild manifest
5. emerge insaned

TODO
----

The following features are planned:

* Suspend polling while one of the configured processes are running
* Suspend polling while another process is using the SANE library
* CMake build
* Installation script
* Packages for other linux distributions
* More useful handler scripts
* Handler script stub for starting a GUI frontend
* Run daemon as user daemon when logging into a KDE/Gnome/whatever
* Test with hibernate/suspend
* Test with more hardware
