#!/bin/bash

sudo sh -c 'echo "8" > /sys/class/gpio/export'
sudo sh -c 'echo "out" > /sys/class/gpio/gpio8/direction'
sudo sh -c 'echo "0" > /sys/class/gpio/gpio8/value'

sleep 2s

sudo sh -c 'echo "1" > /sys/class/gpio/gpio8/value'

sudo sh -c 'echo "8" > /sys/class/gpio/unexport'




