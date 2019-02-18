# es100-shm
NTP shared memory driver for the es100 WWVB BPSK receiver

Background:  
WWVB is a radio station operated by the National Institute of Standards and Technology (NIST) that transmits a time signal on a 60 kHz carrier.  This signal can be used in North America to synchronize wall clocks, alarm clocks, etc.

`ntpd` is a deamon running on computers to keep a computer's internal clock synchronized to other internet time servers or to a local reference clock.  `ntpd` includes drivers for several reference clocks.  An inexpensive and reliable way of synchronizing to a reference clock is by using a GPS receiver that provieds a one pulse-per-second output.  `ntpd` could potentially use the signal from WWVB as a reference clock.

`chrony` is another program that can be used to synchronize a computer's internal clock to a reference clock.

A company called Everset Technologies produces a receiver for the WWVB signal.  This receiver utilizes WWVB's new binary phase-shift keyeing (BPSK) modulation.

About the ES100 receiver:  
The ES100 receives WWVB's signal using two ferrite antennas.  It communicates to the host computer using an I2C bus.  The ES100 shows up as device 0x32 on the I2C bus.  It needs 3.3V of power.  It also needs two GPIO lines to/from the host computer.

Aboout this project:  
This small program is designed to run on a computer with an I2C bus and GPIO lines, such as the Raspberry Pi or the Minnowboard.  The computer needs a Linux operating system and `ntpd` or `chrony`.  This program will communitate with the ES100 receiver using I2C and the GPIO lines.  It gets time information from the ES100, and feeds it to `ntpd` or `chrony` using a shared memory driver or unix socket.

Configuring `ntpd`:  
Add the following line to your ntp.conf file:  
```
server 127.127.28.0
```

Compiling the program:  
As of now, you may have to change the source code to customize the name of the I2C bus and gpio chip.  This works for me:  
```
gcc -Wall -o es100-shm -li2c -lgpiod -I/usr/include/glib-2.0 es100-shm.c
```

Running the program:  
Just run it.  It needs one or two command line options to tell the program which shared memory segment to use, or to use a unix socket (or both).  It will need appropriate permissions to access the I2C bus and GPIO chip.  I run it as root.  Logging information goes to syslog.  You may want to have it start automatically; add it to `/etc/rc.local` or whatever is required by your Linux distribution.

Acknowlegements:  
Oren Eliezer.  Genius.
