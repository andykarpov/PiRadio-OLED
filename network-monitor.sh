#!/bin/bash

if ifconfig wlan0 | grep -q "inet addr:" ; then

else
      echo "Network connection down! Attempting reconnection."
      ifdown wlan0
      ifup --force wlan0
fi

