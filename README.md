PiRadio-OLED
=======

Internet radio project based on RaspberryPi hardware + atmega328p 8MHz 3v3 custom board + old DAB radio analog part with amplifier, PT2314 audio processor, 128x64 OLED screen, power supply etc.
Everything is controlled by rotary encoder (with button) and 10k potentiometer as well as 7 analog buttons.
Arduino-based board is connected to the RaspberryPi via serial line, also MOSI, MISO, SCK and RESET lines are connected to use RaspbrryPi as AVR programmer 
to easily upload new firmwares to the custom board without disconnecting it.

**Prerequesties:**

1. ArchLinuxArm as base. To reduce boot time it is possible to use another distro, such as OpenWRT-based or something made from scrach.
2. Also we assumes that your network connection (wired or wireless) is also running well, you have SSH access to the Pi
3. Your arduino-based board is connected to the Pi via hardware serial port (TX, RX, GND), as well it's SCK,MOSI,MISO,RESET lines are connected to GPIO8..GPIO11 pins.
4. All external hardware is connected propertly (audio processor and OLED screen
4. You are logged in as newly created user 'pi' with it's default home at /home/pi/ 
5. You have sudo, python, python-mpd2, mpd, mpc, python-serial, git, mc, patched avrdude (that support GPIO programmer), arduino, ino, wiringPi packages are installed and configured
6. Your user pi is allowed to run sudo without password

**Installation:**

1. checkout https://github.com/andykarpov/PiRadio-OLED.git as /home/pi/PiRadio
2. install pi-radio.service from etc/system.d/ subfolder 
2. control pi-radio.service via systmctl to start/stop.
3. enable pi-radio via systemctl to start on boot

