#!/usr/bin/env python
# -*- coding: utf-8 -*-

import re
import codecs
import time
import serial
import textwrap
from mpd import MPDClient, MPDError, CommandError
from time import gmtime, strftime
from datetime import datetime


def debug(message):
    if Config.debug:
        print message


class Config:

    COLS = 21
    ROWS = 3

    debug = True

    serial_dev = ["/dev/ttyAMA0"]
    serial_speed = 9600

    playlist = "/home/pi/PiRadio/data/radio.m3u"
    state = "/home/pi/PiRadio/data/state.txt"
    alarm = "/home/pi/PiRadio/data/alarm.txt"
    save_timeout = 500

    mpd_host = "localhost"
    mpd_port = 6600
    mpd_password = "admin"

    init_delay = 0.1
    write_delay = 0.35
    read_delay = 0.1


class Interface:

    serial = None
    serial_connected = False

    encoder = 0
    volume = 0

    min_value = 0
    max_value = 0

    min_volume = 0
    max_volume = 100

    alarm_hours = 0
    alarm_minutes = 0
    alarm_on = False

    stations = []

    def __init__(self, encoder=0, min_value=0, max_value=0, stations=[], alarm_hours=0, alarm_minutes=0, alarm_on = False):
        self.encoder = encoder
        self.min_value = min_value
        self.max_value = max_value
        self.stations = stations
        self.alarm_hours = alarm_hours
        self.alarm_minutes = alarm_minutes
        self.alarm_on = alarm_on
        self.try_serial()

    def send_init(self):
        # put some line feeds before actual init
        self.try_write("");
        self.try_write("")
        self.try_write('TM:' + datetime.now().strftime("%H:%M"))
        self.try_write('AL:' + str(self.alarm_hours) + ':' + str(self.alarm_minutes) + ':' + str(self.alarm_on))
        self.try_write('D:' + str(self.encoder) + ':' + str(self.max_value))

    def try_serial(self):
        for serial_dev in Config.serial_dev:
            if not self.serial_connected:
                try:
                    self.serial = serial.Serial(serial_dev, Config.serial_speed)
                    time.sleep(Config.init_delay)
                    self.serial_connected = True
                    self.send_init()
                    debug("serial port: " + serial_dev)
                except Exception as e:
                    debug(e)
                    self.serial_connected = False

    def try_write(self, data):
        try:
            self.serial.write(data + "\r\n")
            debug("write: " + data)
            time.sleep(Config.write_delay)
            return True
        except Exception as e:
            self.serial_connected = False
            self.try_serial()
            return False

    def try_read(self):
        init_request = False
        try:
            if self.serial_connected and self.serial.inWaiting():
                ln = self.serial.readline()
                ln = ln.strip()
                debug("read: " + ln)
                if ln == "init":
                    time.sleep(Config.init_delay)
                    self.send_init()
                    init_request = True
                elif ln != "":
                    parts = ln.split(":")
                    if (parts[0] == 'E'):
                        self.encoder = int(parts[0])
                    if (parts[0] == 'A')
                        self.alarm_hours = parts[1]
                        self.alarm_minutes = parts[2]
                        if (parts[3] == '1')
                            self.alarm_on = True
                        else 
                            self.alarm_on = False
            elif not self.serial_connected:
                self.try_serial()
            return init_request
        except Exception as e:
            return None


class PollerError(Exception):
    """Fatal error in poller."""


class MPDWrapper:

    def __init__(self, host="localhost", port="6600", password=None):
        self._host = host
        self._port = port
        self._password = password
        self._client = MPDClient()

    def connect(self):
        try:
            self._client.connect(self._host, self._port)

        except IOError as (errno, strerror):
            raise PollerError("Could not connect to '%s': %s" % (self._host, strerror))

        except MPDError as e:
            raise PollerError("Could not connect to '%s': %s" % (self._host, e))

        if self._password:
            try:
                self._client.password(self._password)

            except CommandError as e:
                raise PollerError("Could not connect to '%s': "
                                  "password command failed: %s" %
                                  (self._host, e))

            except (MPDError, IOError) as e:
                raise PollerError("Could not connect to '%s': "
                                  "error with password command: %s" %
                                  (self._host, e))

    def disconnect(self):
        try:
            self._client.close()

        except (MPDError, IOError):
            pass

        try:
            self._client.disconnect()

        except (MPDError, IOError):
            self._client = MPDClient()

    def currentsong(self):

        try:
            self._client.command_list_ok_begin()
            self._client.currentsong()
            song = self._client.command_list_end()

        except (MPDError, IOError):
            self.disconnect()

            try:
                self.connect()

            except PollerError as e:
                raise PollerError("Reconnecting failed: %s" % e)

            try:
                self._client.command_list_ok_begin()
                self._client.currentsong()
                song = self._client.command_list_end()

            except (MPDError, IOError) as e:
                raise PollerError("Couldn't retrieve current song: %s" % e)

        return song

    def load_playlist(self, list):
        try:
            self._client.command_list_ok_begin()
            self._client.stop()
            self._client.clear()
            for item in list:
                self._client.add(item.url)
            self._client.command_list_end()

        except (MPDError, IOError):
            self.disconnect()

            try:
                self.connect()

            except PollerError as e:
                raise PollerError("Reconnecting failed: %s" % e)

            try:
                self._client.command_list_ok_begin()
                self._client.stop()
                self._client.clear()
                for item in list:
                    self._client.add(item.url)
                self._client.command_list_end()

            except (MPDError, IOError) as e:
                raise PollerError("Couldn't load playlist: %s" % e)

    def play(self, idx):
        try:
            self._client.command_list_ok_begin()
            self._client.stop()
            self._client.play(idx)
            self._client.command_list_end()

        except (MPDError, IOError):
            self.disconnect()

            try:
                self.connect()

            except PollerError as e:
                raise PollerError("Reconnecting failed: %s" % e)

            try:
                self._client.command_list_ok_begin()
                self._client.stop()
                self._client.play(idx)
                self._client.command_list_end()

            except (MPDError, IOError) as e:
                raise PollerError("Couldn't play song: %s" % e)


class Program:

    playlist = None
    mpd = None
    state = None
    active_song = 0
    active_volume = 0
    alarm_hours = 0
    alarm_minutes = 0
    alarm_on = False
    alarm_changed = False
    last_active_song = 0
    last_changed = 0
    alarm_last_changed = 0
    last_time = 0
    interface = None

    def __init__(self):
        self.begin()

    def millis(self):
        return int(round(time.time() * 1000))

    def begin(self):

        # load playlist
        self.playlist = Playlist()
        self.playlist.load(Config.playlist)

        # init mpd
        self.mpd = MPDWrapper(Config.mpd_host, Config.mpd_port, Config.mpd_password)
        self.mpd.connect()
        self.mpd.load_playlist(self.playlist.list)

        # get active song from saved state
        self.state = State()
		if self.state.load():
        	self.active_song = self.state.active_song
            self.alarm_hours = self.state.alarm_hours
            self.alarm_minutes = self.state.alarm_minutes
            self.alarm_on = self.state.alarm_on
        self.last_active_song = self.active_song

        # init serial encoder instance
        self.interface = Interface(self.active_song, 0, len(self.playlist.list) - 1, self.playlist.list, self.alarm_hours, self.alarm_minutes, self.alarm_on)

        # play active song
        self.mpd.play(self.active_song)

        # run scene
        Main(self)


class Main:

    program = None
    last_current_song = 0
    current_song = ''

    texts = ['', '', '', '', '']
    last_texts = ['---', '---', '---', '---', '---']

    def __init__(self, program):
        self.begin(program)

    def begin(self, program):

        self.program = program

        while True:

            init_request = self.program.interface.try_read()
            if init_request:
                self.last_texts = ['', '', '', '', '']

            if self.program.interface.encoder != self.program.active_song:
                self.program.active_song = self.program.interface.encoder
                self.program.last_changed = self.program.millis()

            if self.program.interface.volume != self.program.active_volume:
                self.program.active_volume = self.program.interface.volume

            if self.program.interface.alarm_hours != self.program.alarm_hours or self.program.interface.alarm_minutes != self.program.alarm_minutes or self.program.interface.alarm_on != self.program.alarm_on:
	           self.program.alarm_hours = self.program.interface.alarm_hours
	           self.program.alarm_minutes = self.program.interface.alarm_minutes
	           self.program.alarm_on = self.program.interface.alarm_on
	           self.program.last_alarm_changed = self.program.millis()
	           self.program.alarm_changed = True

            if self.program.millis() - self.program.last_changed >= Config.save_timeout and (self.program.last_active_song != self.program.active_song or self.program.alarm_changed):
				if (self.program.last_active_song != self.program.active_song):
                	self.program.last_active_song = self.program.active_song
                	self.program.mpd.play(self.program.active_song)
                self.program.state.save(self.program.active_song, self.program.alarm_hours, self.program.alarm_minutes, self.program.alarm_on)
				self.program.alarm_changed = False

            # fetch time
            self.texts[4] = datetime.now().strftime("%H:%M")
            if (self.texts[4] != self.last_texts[4]):
                self.program.interface.try_write('TM:' + self.texts[4])
                self.last_texts[4] = self.texts[4]

            # fetch current song from mpd every 500ms
            if self.program.millis() - self.last_current_song > 500:
                current_song = self.program.mpd.currentsong()

                if current_song is not None and current_song != '' and 'title' in current_song[0]:
                    title = current_song[0]['title'].strip()

                    try:
                        title = title.decode('utf-8')
                        title = title.encode('ascii', 'ignore')
                    except Exception as e:
                        pass

                    if title != self.current_song:
                        self.current_song = title.upper()
                else:
                    self.current_song = ''

                self.last_current_song = self.program.millis()

            # print station title
            station = self.program.playlist.list[self.program.active_song]
            self.texts[0] = station.name.upper()
            if self.texts[0] != self.last_texts[0]:
                self.program.interface.try_write('S0:' + self.texts[0])
                self.last_texts[0] = self.texts[0]

            part = textwrap.wrap(self.current_song, 20)
            if len(part) >= 2:
                self.texts[2] = part[0]
                self.texts[3] = part[1]
            else:
                self.texts[2] = ''
                self.texts[3] = self.current_song

            if self.texts[2] != self.last_texts[2]:
                self.program.interface.try_write('S1:' + self.texts[2])
                self.last_texts[2] = self.texts[2]

            if self.texts[3] != self.last_texts[3]:
                self.program.interface.try_write('S2:' + self.texts[3])
                self.last_texts[3] = self.texts[3]

            time.sleep(Config.read_delay)


class State:

    active_menu = 0
    alarm_hours = 0
    alarm_minutes = 0
    alarm_on = False

    def load(self):
        try:
            result = 0
            fsrc = codecs.open(Config.state, mode="r", encoding="utf-8")
            ln = fsrc.readline().strip()
            if ln != "":
	            parts = ln.split(":")
                self.active_menu = int(parts[0])
                self.alarm_hours = int(parts[1])
                self.alarm_minutes = int(parts[2])
                if (parts[3] == '1')
                    self.alarm_on = True
            fsrc.close()
            return True
        except Exception as e:
            debug("Unable to load state: " + e.message)
            return False

    def save(self, active_menu, alarm_hours, alarm_minutes, alarm_on):
        try:
            fsrc = codecs.open(Config.state, mode="w", encoding="utf-8")
            fsrc.write(str(active_menu))
            fsrc.write(':')
            fsrc.write(str(alarm_hours))
            fsrc.write(':')
            fsrc.write(str(alarm_minutes))
            fsrc.write(':')
            if (alarm_on)
                fsrc.write('1')
            else 
                fsrc.write('0')
            fsrc.write(str(alarm_on))             
            fsrc.close()
        except Exception as e:
            debug("Unable to store state: " + e.message)


class PlaylistItem:

    def __init__(self):
        self.name = None
        self.url = None
        self.payload = []


class Playlist:

    list = []

    def __init__(self):
        self.list = []

    def load(self, filename):
        try:
            fsrc = codecs.open(filename, mode="r", encoding="utf-8")
            self.parse(fsrc)
            fsrc.close()
        except Exception as e:
            debug("Error while loading playlist: " + e.message)

    def parse(self, infile):
        ln = None
        self.list = []

        while ln != "" and ln != u"#EXTM3U\n":
            ln = infile.readline()

        ln = infile.readline()
        while ln != "":
            while ln != "" and ln.find(u"#EXTINF") == -1:
                ln = infile.readline()
            match = re.search(ur"#EXTINF:.*,(.*)", ln)
            name = match.group(1)
            nitem = PlaylistItem()
            nitem.name = name
            ln = infile.readline()
            while ln != "" and ln.find(u"#EXTINF") == -1:
                nitem.payload.append(ln)
                ln = infile.readline()
            nitem.url = nitem.payload[-1].strip()
            self.list.append(nitem)


if __name__ == '__main__':
    Program()
