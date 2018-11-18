# es100-shm
NTP shared memory driver for the es100 WWVB BPSK receiver

Background:
WWVB is a radio station operated by NIST that transmits a time signal on a 60 KHz carrier.  This signal can be used in North America to synchronize wall clocks, alarm clocks, etc.
NTPD is a deamon running on computers to keep a computer's internal clock synchronized to other internet time servers or to a local reference clock.  NTPD includes drivers for several reference clocks.  An inexpensive and reliable way of synchronizing to a reference clock is by using a GPS receiver that provieds a one pulse-per-second output.  NTPD could potentially use the signal from WWVB as a reference clock.
A company called Everset Technologies produces a receiver for the WWVB signal.  This receiver utilizes WWVB's new binary phase-shift keyeing (BPSK) modulation.

About the es100 receiver:
The es100 receives WWVB's signal using two ferrite antennas.  It communicates to the host computer using an I2C bus.  The es100 shows up as device 0x32 on the I2C bus.  It needs 3.3V of power.  It also needs two GPIO lines to/from the host computer.

Aboout this project:
This small program is designed to run on a computer with an I2C bus and GPIO lines, such as the Raspberry Pi or the Minnowboard.  The computer needs a Linux operating system and NTPD.  This program will communitate with the es100 receiver using I2C and the GPIO lines.  It gets time information from the es100, and feeds it to NTPD using a shared memory driver.

Configuring NTPD:
Add the following line to your ntp.conf file:
server 127.127.28.0

Compiling the program:
As of now, you may have to change the source code to customize the name of the I2C bus and gpio chip.  This works for me:
gcc -Wall -o es100-shm -li2c -lgpiod -I/usr/include/glib-2.0 es100-shm.c

Running the program:
Just run it.  It will need appropriate permissions to access the I2C bus and GPIO chip.  I run it as root.  Logging information goes to syslog.

Acknowlegements:
Oren Eliezer.  Genius.
