/*
 * Raspberry Pi internet radio project
 * Arduino based front-end for custom 8MHz 3v3 board using serial communication, 128x64 OLED display, 
 * potentiometer, encoder, ET2314 board (from the old DAB radio)
 *
 * @author Andrey Karpov <andy.karpov@gmail.com>
 * @copyright 2013 Andrey Karpov
 */

#include <Encoder.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <PT2314.h>
#include <EEPROM.h>

#define COLS 21 // number of display columns

#define NUM_READINGS 1 // number of analog reading to average it's value
#define SERIAL_SPEED 9600 // serial port speed
#define SERIAL_BUF_LEN 32 // serial reading buffer

#define EEPROM_ADDRESS_OFFSET 400 // address offset to start reading/wring to the EEPROM 

#define DELAY_VOLUME 3000 // volume bar show decay  
#define DELAY_ENCODER 100 // delay between sending encoder changes back to Pi 
#define DELAY_ALARM 100 // delay between sending alarm changes back to Pi
#define DELAY_MODE 400 // mode switch debounce delay
#define DELAY_EEPROM 10000 // delay to store to the EEPROM
#define DELAY_BLINK 500 // bink delay

#define OLED_RESET 4 // oled display reset pin
#define POWER 5 // D5 connected to power control transistor

PT2314 audio; // PT2314 board connected to i2c bus (GND, A4, A5)
Adafruit_SSD1306 display(OLED_RESET); // OLED display conneced to i2c bus (GND, A4, A5) and it's RESET pin to D4
Encoder enc(2, 3); // encoder pins A and B connected to D2 and D3 

char buf[SERIAL_BUF_LEN+1]; // serial buffer
byte index = 0; // current buffer position
const char sep = ':';  // incoming command separator

bool buffering = true; // buffering mode, default to On
bool need_enc = false; // need to send encoder value to the Pi backend, default to false
bool need_vol = false; // need to send volume value to the Pi backend, default to false
bool init_done = false; // boot done (pyradio service has been started and sent initial data), default to false
bool power_on = true; // power on state
bool blink_on = true; // blink state

int station = 0; // current station value
int prev_station = 0; // previous encoder value
int max_stations = 0; // max stations value

int vol = 0; // current volume value
int prev_vol = 0; // previous volume value
int max_vol = 100; // max volume value

unsigned long current = 0; // current timestamp
unsigned long last_vol = 0; // timestamp of last volume changed
unsigned long last_enc = 0; // timestamp of last encoder changed
unsigned long last_blink = 0; // timestamp of last blink state changed
unsigned long last_alarm = 0; // timestamp of last alarm state changed

char title[COLS+1]; // LCD buffer for title
char song1[COLS+1]; // LCD buffer for song row1
char song2[COLS+1]; // LCD buffer for song row2
char pstr_buf[COLS+1]; // LCD buffer for special command titles
char cbuf[5]; // LCD buffer for int to string conversion

// enum with application states
enum app_mode_e {
  app_mode_station = 0,
  app_mode_tone_bass,
  app_mode_tone_treble,
  app_mode_set_alarm,
  app_mode_alarm,
  app_mode_clock1,
  app_mode_clock2
};

// enum with button names
enum e_buttons {
  btn_power = 0,
  btn_menu,
  btn_preset,
  btn_info,
  btn_mode,
  btn_scan,
  btn_alarm,
  btn_encoder,
  btn_none
};

int btn = btn_none; // pressed button id
int mode = app_mode_station; // mode set to default (station display)
int submode = 0; // submode - used in some apps
bool mode_changed = true; // mode button has been pressed
unsigned long last_mode = 0; // timestamp of last mode changed
unsigned long last_submode = 0; // timestamp of last submode changed

int tones[2]; // bass, treble
int prev_tones[2]; // bass, treble
bool need_store = false; // flag if we need to store something to the EEPROM
unsigned long last_tone = 0; // timestamp of last tone change

int time_hours = 0; // time hours
int time_minutes = 0; // time minutes

int year = 0; // date year
int month = 0; // date month
int day = 0; // date day

int alarm_hours = 0; // alarm hours
int alarm_minutes = 0; // alarm minutes
bool alarm_on = false; // alarm on / off
int prev_alarm_hours = 0;
int prev_alarm_minutes = 0;
bool prev_alarm_on  = false;
int alarm_tone = 0; // alarm tone preset

const char str_booting[] PROGMEM = "   BOOTING RADIO";
const char str_please_wait[] PROGMEM = "   PLEASE WAIT...";

const char str_volume[] PROGMEM = "VOLUME ";
const char str_bass[] PROGMEM = "BASS ";
const char str_treble[] PROGMEM = "TREBLE ";
const char str_station[] PROGMEM = "STATION ";
const char str_alarm_on[] PROGMEM = "ALARM ON";
const char str_alarm_off[] PROGMEM = "ALARM OFF";

static const unsigned char PROGMEM icon_alarm_bmp[] =
{ 
  B11111111, B11111111,
  B11000001, B10111111,
  B10111110, B10111111,
  B10000000, B10111111,
  B10111110, B10111111,
  B10111110, B10000001,
  B11111111, B11111111,
  B00000000, B00000000 };

/**
 * Print progmem string into the pstr_buf
 */
void printProgStr(const char str[]) {
  pstr_buf[0] = '\0';
  char c;
  int i = 0;
  if(!str) return;
  while((c = pgm_read_byte(str++))) {
    pstr_buf[i] = c;
    i++;
  }
  pstr_buf[i] = '\0'; 
}

/**
 * Arduino setup routine
 */
void setup() {

  // set default analog reference
  analogReference(DEFAULT);
  
  // set A0 as input for volume regulator
  pinMode(A0, INPUT);
  
  // set A1 to handle analog keyboard presses
  pinMode(A1, INPUT_PULLUP);

  // set D5 to POWER control pin
  pinMode(POWER, OUTPUT);
  
  // set default tones
  for (int i=0; i<2; i++) {
    tones[i] = 0;
    prev_tones[i] = 0;
  }
  
  // restore saved tone values from EEPROM
  restoreTones();

  // read volume knob position
  vol = readVolume();
  
  // read keyboard press
  btn = readKeyboard();
  
  // start i2c master
  Wire.begin();

  // init hardware (audio processor, display, etc)
  powerOn();

  // setup serial
  Serial.begin(SERIAL_SPEED);
  Serial.flush();

  // send "init" command to the Raspberry Pi backend
  Serial.println("init");  
}

void powerOn() {
    power_on = true;
    mode = app_mode_station;
    digitalWrite(POWER, HIGH);
    delay(200);
    audio.init();
    sendPT2314();
    display.begin(SSD1306_EXTERNALVCC, 0x3D);
    display.clearDisplay();
    mode_changed = true;
}

void powerOff() {
    power_on = false;
    vol = 0;
    sendPT2314();
    display.clearDisplay();
    display.display();
    digitalWrite(POWER, LOW);
}

/**
 * Application mode to handle station display
 */
void AppStation() {
  
  if (mode_changed) {
    if (mode_changed && !init_done) {
      printProgStr(str_booting);
      strcpy(title, pstr_buf);
      updateDisplay(false, 0);
    }
    mode_changed = false;
    setEncoder(station);
  }
  
  if (init_done) {
    
    station = readEncoder(max_stations);
  
    if (station != prev_station) {
      prev_station = station;
      last_enc = current;
      need_enc = true;
    }

    if (current - last_vol <= DELAY_VOLUME) {
      if (need_vol) {
          printProgStr(str_volume);
          updateDisplay(true, vol);
      }
    } else {
        updateDisplay(false, 0);
    }
      
    // send encoder and volume to serial port with a small delay  
    if ((need_enc) && (current - last_enc >= DELAY_ENCODER)) {
      sendStation();
      need_enc = false;
    }
  }
}

/**
 * Application mode to control Bass tone
 */
void AppToneBass() {
  if (mode_changed) {
    mode_changed = false;
    setEncoder(tones[0]);
  }
  tones[0] = readEncoder(100);
  printProgStr(str_bass);
  updateDisplay(true, tones[0]);
}

/**
 * Application mode to control Treble tone
 */
void AppToneTreble() {
 if (mode_changed) {
    mode_changed = false;
    setEncoder(tones[1]);
  }
  tones[1] = readEncoder(100);
  printProgStr(str_treble);
  updateDisplay(true, tones[1]);
}

/**
 * Application: alarm set 
 */
void AppSetAlarm() {

  if (mode_changed) {
    mode_changed = false;
    submode = 0;
    setEncoder(alarm_hours);
  }

  if (btn == btn_encoder && current - last_submode >= DELAY_MODE) {
    submode++;
    if (submode >= 3) {
      submode = 0;
    }
    switch(submode) {
      case 0:
        setEncoder(alarm_hours);
      break;
      case 1:
        setEncoder(alarm_minutes);
      break;
      case 2:
        setEncoder((alarm_on) ? 1 : 0);
      break;
      case 3:
        setEncoder(alarm_tone);
      break;
    }
    last_submode = current;
  }

  display.clearDisplay();

  bool need_alarm = false;
 
  switch (submode) {
    case 0:
      alarm_hours = readEncoder(23);
      if (alarm_hours != prev_alarm_hours) {
        prev_alarm_hours = alarm_hours;
        need_alarm = true;
      }
      display.fillTriangle(16, 6, 16, 13, 12, 9, WHITE);
      display.drawLine(19, 26, 49, 26, (blink_on) ? WHITE : BLACK);
      display.drawLine(19, 27, 49, 27, (blink_on) ? WHITE : BLACK);
      display.fillTriangle(54, 6, 54, 13, 58, 9, WHITE);
    break;
    case 1:
      alarm_minutes = readEncoder(59);
      if (alarm_minutes != prev_alarm_minutes) {
        prev_alarm_minutes = alarm_minutes;
        need_alarm = true;
      }
      display.fillTriangle(70, 6, 70, 13, 66, 9, WHITE);
      display.drawLine(73, 26, 103, 26, (blink_on) ? WHITE : BLACK);
      display.drawLine(73, 27, 103, 27, (blink_on) ? WHITE : BLACK);
      display.fillTriangle(108, 6, 108, 13, 112, 9, WHITE);
    break;
    case 2:
      alarm_on = (readEncoder(1) == 1) ? true : false;
      if (alarm_on != prev_alarm_on) {
        prev_alarm_on = alarm_on;
        need_alarm = true;
      }
      display.fillTriangle(16, 46, 16, 53, 12, 49, WHITE); 
      if (alarm_on) {
        display.drawLine(19, 55, 66, 55, (blink_on) ? WHITE : BLACK);
	display.drawLine(19, 56, 66, 56, (blink_on) ? WHITE : BLACK);
        display.fillTriangle(68, 46, 68, 53, 72, 49, WHITE);
      } else {
        display.drawLine(19, 55, 72, 55, (blink_on) ? WHITE : BLACK);
	display.drawLine(19, 56, 72, 56, (blink_on) ? WHITE : BLACK);
        display.fillTriangle(74, 46, 74, 53, 78, 49, WHITE); 
      }
    break;
    case 3:
      alarm_tone = readEncoder(10);
      // todo
    break;
  }

  display.setTextSize(1);
  display.setCursor(19,45);
  if (alarm_on) {
    printProgStr(str_alarm_on);
    display.println(pstr_buf);
  } else {
    printProgStr(str_alarm_off);
    display.println(pstr_buf);
  }
  display.setCursor(19,0);
  display.setTextSize(3);
  sprintf(cbuf, "%d%d", (alarm_hours>9) ? (alarm_hours/10) : 0, alarm_hours%10);
  display.print(cbuf);
  if (submode == 2) {
    display.print(":");
  }
  display.setCursor(73, 0);
  sprintf(cbuf, "%d%d", (alarm_minutes>9) ? (alarm_minutes/10) : 0, alarm_minutes%10);  
  display.print(cbuf);
  display.display();
  
  if (need_alarm) {
    sendAlarm();
    need_alarm = false;
  }
  
}

void AppAlarm() {
  // todo
}

void AppClock1() {
  if (mode_changed) {
    mode_changed = false;
    setEncoder(station);
  }
  display.clearDisplay();
  display.setCursor(4, 0);
  display.setTextSize(4);
  
  sprintf(cbuf, "%d%d", (time_hours>9) ? (time_hours/10) : 0, time_hours%10);
  display.print(cbuf);
  display.print((blink_on) ? ":" : " ");
  sprintf(cbuf, "%d%d", (time_minutes>9) ? (time_minutes/10) : 0, time_minutes%10);
  display.print(cbuf);

  if (init_done) {

    station = readEncoder(max_stations);

    if (station != prev_station) {
      prev_station = station;
      last_enc = current;
      need_enc = true;
    }

    // send encoder and volume to serial port with a small delay  
    if ((need_enc) && (current - last_enc >= DELAY_ENCODER)) {
      sendStation();
      need_enc = false;
    }

    display.setCursor(0,45);
    display.setTextSize(1);
    display.println(title);
  }

  display.display();
}

/**
 * Main application loop
 */ 
void loop() {

  current = millis();
    
  vol = readVolume();
  btn = readKeyboard();

  // blink flag update
  if (current - last_blink >= DELAY_BLINK) {
    blink_on = !blink_on;
    last_blink = current;
  }

  // global keys handler
  if (current - last_mode >= DELAY_MODE) {
    switch (btn) {

      // PRESET: loop via station / tone_bass / tone_treble modes
      case btn_preset:
        if (mode == app_mode_tone_bass) {
          mode = app_mode_tone_treble;
        } else if (mode == app_mode_tone_treble) {
          mode = app_mode_station;
        } else {
          mode = app_mode_tone_bass;
        }
        last_mode = current;
        mode_changed = true;
      break;

      // MENU/EXIT: exit to the station mode
      case btn_menu:
        mode = app_mode_station;
        last_mode = current;
        mode_changed = true;
      break;

      // POWER: power on / off the radio
      case btn_power:
        power_on = !power_on;
        if (!power_on) {
            powerOff();
        } else {
            powerOn();
        }
        last_mode = current;
        mode_changed = true;
      break;

      // ALARM: set alarm time, on/off it.
      case btn_alarm:
          if (mode == app_mode_set_alarm) {
            mode = app_mode_station;
          } else {
            mode = app_mode_set_alarm;
          }
          last_mode = current;
          mode_changed = true;
      break;

      // MODE: switch between station / clock1 / clock2 modes
      case btn_mode: 
        if (mode == app_mode_station) {
          mode = app_mode_clock1;
        } else if (mode == app_mode_clock1) {
          mode = app_mode_station;
        }
        last_mode = current;
        mode_changed = true;
      break;
    }
  }

  if (abs(vol-prev_vol) > 2) {
    prev_vol = vol;
    last_vol = current;
    need_vol = true;
    sendPT2314();
  }
  
  if (tones[0] != prev_tones[0] || tones[1] != prev_tones[1]) {
    last_tone = current;
    prev_tones[0] = tones[0];
    prev_tones[1] = tones[1];
    need_store = true;
    sendPT2314();
  }
  
  // store settings in EEPROM with delay to reduce number of EEPROM write cycles
  if (need_store && current - last_tone >= DELAY_EEPROM) {
      storeTones();
      need_store = false;
  }

  if (power_on) {
    switch (mode) {
      case app_mode_station:
        AppStation();
      break;
      case app_mode_tone_bass:
        AppToneBass();
      break;
      case app_mode_tone_treble:
        AppToneTreble();
      break;
      case app_mode_set_alarm:
        AppSetAlarm();
      break;
      case app_mode_alarm:
        AppAlarm();
      break;
      case app_mode_clock1:
        AppClock1();
      break;
    }
  }
  
   readLine();
   if (!buffering) {
     processInput();
     index = 0;
     buf[index] = '\0';
     buffering = true;
   } 
 
   //delay(20);  
}

/** 
 * Load stored tone control values from the EEPROM (into the tones and prev_tones)
 */
void restoreTones() {
  
  byte value;
  int addr;
  
  // bass / treble
  for (int i=0; i<2; i++) {
    addr = i + EEPROM_ADDRESS_OFFSET;
    value = EEPROM.read(addr);
    // defaults
    if (value < 0 || value > 100) {
      value = 80;
    }
    tones[i] = value;
    prev_tones[i] = value;
  }  
}

/**
 * Store tone values in the EEPROM
 */
void storeTones() {
  // bass / balance
  for (int i=0; i<2; i++) {
    int addr = i + EEPROM_ADDRESS_OFFSET;
    EEPROM.write(addr, tones[i]);
  }
}

/**
 * Send tone control values to the PT2314
 */
void sendPT2314() {
  audio.volume(vol);
  audio.bass(tones[0]);
  audio.treble(tones[1]);
  audio.channel(0);
  if (power_on) {
    audio.loudnessOn();
    audio.muteOff();
  } else {
    audio.loudnessOff();
    audio.muteOn();
  }
}

int readKeyboard() {
  int kbd_val = analogRead(A1);
  if (kbd_val >= 90 && kbd_val <= 110) {
    return btn_power;
  } else if (kbd_val >= 440 && kbd_val <= 480) {
    return btn_menu;
  } else if (kbd_val >= 320 && kbd_val <= 370) {
    return btn_preset;
  } else if (kbd_val >= 200 && kbd_val <= 250) {
    return btn_info;
  } else if (kbd_val >= 120 && kbd_val <= 170) {
    return btn_mode;
  } else if (kbd_val >= 26 && kbd_val <= 90) {
    return btn_scan;
  } else if (kbd_val >= 0 && kbd_val <= 25) {
    return btn_alarm; 
  } else if (kbd_val >= 700 && kbd_val <= 780) {
    return btn_encoder;
  }
  return btn_none;
}

/**
 * Read volume from the potentiometer
 * @return int
 */
int readVolume() {

  int values[NUM_READINGS];
  
  for (int i=0; i<NUM_READINGS; i++) {
    values[i] = map(analogRead(A0), 0, 1023, 0, max_vol);
  }

  int total = 0;
  
  for (int i=0; i<NUM_READINGS; i++) {
    total = total + values[i];
  }
  
  int value = total / NUM_READINGS;
  
  if (value >= max_vol) {
    value = max_vol;
  }
  
  if (value <= 0) {
    value = 0;
  }

  return value;
}

/**
 * Read encoder value with bounds from 0 to max_encoder_value
 * @param int max_encoder_value
 */
int readEncoder(int max_encoder_value) {
  int value = enc.read() / 4;
  if (value > max_encoder_value) {
    value = max_encoder_value;
    setEncoder(max_encoder_value);
  }
  if (value < 0) {
    value = 0;
    setEncoder(0);
  }
  return value;
}

/**
 * Save encoder value
 * @param int value
 */
void setEncoder(int value) {
  enc.write(value * 4);
}

 /**
  * Fill internal buffer with a single line from the serial port 
  *
  * @return void
  */
 void readLine() {
   if (Serial.available())  {
     while (Serial.available()) {
         char c = Serial.read();
         if (c == '\n' || c == '\r' || index >= SERIAL_BUF_LEN) {
           buffering = false;
         } else {
           buffering = true;
           buf[index] = c;
           index++;
           buf[index] = '\0';
         }
     }
   }
 }
 
 /**
  * Routine to compare input line from the serial port and perform a response, if required
  *
  * @return void
  */
 void processInput() {
  
     char *cmd = strtok(buf, ":");
     char *arg1 = strtok(NULL, ":");
     char *arg2 = strtok(NULL, ":");
     char *arg3 = strtok(NULL, ":");
     if (strlen(buf) == 0 || cmd == NULL) return;  

     // station title
     if (strcmp(cmd,"S0") == 0) {
         strcpy(title, arg1);
     } 

     // current playing song row 1
     if (strcmp(cmd, "S1") == 0) {
         strcpy(song1, arg1);
     }
     
     // current playing song row 2
     if (strcmp(cmd, "S2") == 0) {
         strcpy(song2, arg1);
     }

     // current time HH:MM
     if (strcmp(cmd, "TM") == 0) {
         time_hours = atoi(arg1);
         time_minutes = atoi(arg2);
     }
     
     // current alarm settings HH:MM:on/off
     if (strcmp(cmd, "AL") == 0) {
       alarm_hours = atoi(arg1);
       alarm_minutes = atoi(arg2);
       alarm_on = (strcmp(arg3, "1") == 0) ? true : false;
     }
          
     // done init <encoder value>:<max value>
     if (strcmp(cmd, "D") == 0) {
       station = atoi(arg1);
       max_stations = atoi(arg2);
       printStation(station);
       init_done = true;
       mode_changed = true;
       need_vol = true;
       prev_vol = -1; // force show volume
     }
 }
 
void sendStation() {
  Serial.print("E:");
  Serial.println(station);
}

void sendAlarm() {
  Serial.print("A:");
  Serial.print(alarm_hours);
  Serial.print(":");
  Serial.print(alarm_minutes);
  Serial.print(":");
  Serial.println(alarm_on);
}
 
void updateDisplay(boolean show_progress, int percentage) {

  display.clearDisplay();
  display.setTextColor(WHITE);
  
  if (show_progress) {
    display.setTextSize(2);
    display.setCursor(0, 15);
    itoa(percentage, cbuf, 10);
    display.print(pstr_buf);
    display.print(cbuf);
    display.drawRect(0, 45, 127, 8, WHITE);
    display.fillRect(2, 47, map(percentage, 0, 100, 2, 123), 4, WHITE);
    
  } else {
    display.setTextSize(1);
    display.setCursor(0,0);
    display.println(title);

    if (init_done) {
    	display.setCursor(0,15);
    	int station_id = station + 1;
    	int station_count = max_stations + 1;
    	itoa(station_id, cbuf, 10);
    	display.print(cbuf);
    	display.print(" / ");
    	itoa(station_count, cbuf, 10);
    	display.print(cbuf);

    	if (alarm_on) {
      		display.drawBitmap(71, 15, icon_alarm_bmp, 16, 8, 1);
    	}

    	display.setCursor(97,15);
    	sprintf(cbuf, "%d%d", (time_hours>9) ? (time_hours/10) : 0, time_hours%10);
    	display.print(cbuf);
    	display.print((blink_on) ? ":" : " ");
    	sprintf(cbuf, "%d%d", (time_minutes>9) ? (time_minutes/10) : 0, time_minutes%10);
    	display.print(cbuf);

    	display.setCursor(0,43);
    	display.println(song1);

    	display.setCursor(0,55);
    	display.println(song2);

    	display.drawRect(0, 28, 127, 8, WHITE);
    	display.fillRect(2, 30, map(station_id, 0, station_count, 2, 123), 4, WHITE);
    }
  }
  
  display.display();
}

/**
 * Print station name and index into the LCD buffer
 */
void printStation(int s) {
    song1[0] = '\0';
    song2[0] = '\0';
}


