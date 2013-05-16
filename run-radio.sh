#!/bin/bash

cd /home/pi/PiRadio
#exec python2 -u run-radio.py > /home/pi/PiRadio/logs/radio.log 2>&1
exec python2 -u run-radio.py > /dev/null 2>&1
