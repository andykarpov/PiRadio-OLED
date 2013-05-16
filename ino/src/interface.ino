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

#define ROWS 6 // number of display rows
#define COLS 21 // number of display columns
#define ROW_TITLE 0
#define ROW_STATION 1
#define ROW_SONG1 2
#define ROW_SONG2 3
#define ROW_TIME 4
#define ROW_MODE 5

#define NUM_READINGS 1 // number of analog reading to average it's value
#define SERIAL_SPEED 9600 // serial port speed
#define SERIAL_BUF_LEN 64 // serial reading buffer

#define NUM_MODES 3 // number of app modes

#define EEPROM_ADDRESS_OFFSET 400 // address offset to start reading/wring to the EEPROM 

#define DELAY_VOLUME 3000 // volume bar show decay  
#define DELAY_ENCODER 100 // delay between sending encoder changes back to Pi 
#define DELAY_MODE 400 // mode switch debounce delay
#define DELAY_EEPROM 10000 // delay to store to the EEPROM

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
bool need_display = false; // need to refresh display
bool init_done = false; // boot done (pyradio service has been started and sent initial data), default to false
bool power_on = true; // power on state

int station = 0; // current station value
int prev_station = 0; // previous encoder value
int max_stations = 0; // max stations value

int vol = 0; // current volume value
int prev_vol = 0; // previous volume value
int max_vol = 100; // max volume value

unsigned long last_vol = 0; // timestamp of last volume changed
unsigned long last_enc = 0; // timestamp of last encoder changed

char t[ROWS][COLS+1]; // LCD buffer

// enum with application states
enum app_mode_e {
  app_mode_station = 0,
  app_mode_tone_bass,
  app_mode_tone_treble
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
bool mode_changed = true; // mode button has been pressed
unsigned long last_mode = 0; // timestamp of last mode changed

int tones[2]; // bass, treble
int prev_tones[2]; // bass, treble
bool need_store = false; // flag if we need to store something to the EEPROM
unsigned long last_tone = 0; // timestamp of last tone change

const char str_loading1[] PROGMEM = "";
const char str_loading2[] PROGMEM = "   BOOTING RADIO";
const char str_loading3[] PROGMEM = "   PLEASE WAIT...";
const char str_loading4[] PROGMEM = "";

const char str_volume[] PROGMEM = "VOLUME ";
const char str_bass[] PROGMEM = "BASS ";
const char str_treble[] PROGMEM = "TREBLE ";
const char str_station[] PROGMEM = "STATION ";

void printProgStr(const char str[], int idx) {
  char c;
  int i = 0;
  if(!str) return;
  while((c = pgm_read_byte(str++))) {
    t[idx][i] = c;
    i++;
  }
  t[idx][i] = '\0'; 
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

  powerOn();

  // setup serial
  Serial.begin(SERIAL_SPEED);
  Serial.flush();

  // send "init" command to the Raspberry Pi backend
  Serial.println("init");  
}

void powerOn() {
    power_on = true;
    digitalWrite(POWER, HIGH);
    audio.init();
    sendPT2314();
    display.begin(SSD1306_EXTERNALVCC, 0x3D);
    display.clearDisplay();
    need_display = true;
}

void powerOff() {
    power_on = false;
    sendPT2314();
    display.clearDisplay();
    display.display();
    digitalWrite(POWER, LOW);
}

/**
 * Application mode to handle station display
 * @param unsigned long current - current timestamp
 */
void AppStation(unsigned long current) {
  
  if (mode_changed) {
    if (mode_changed && !init_done) {
      printProgStr(str_loading1, ROW_TITLE);
      printProgStr(str_loading2, ROW_STATION);
      printProgStr(str_loading3, ROW_SONG1);
      printProgStr(str_loading4, ROW_SONG2);
      updateDisplay(false, 0);
    }
    mode_changed = false;
    setEncoder(station);
    need_display = true;
  }
  
  if (init_done) {
    
    station = readEncoder(max_stations);
  
    if (station != prev_station) {
      printStation(station);
      prev_station = station;
      last_enc = current;
      need_enc = true;
      need_display = true;
    }

    if (current - last_vol <= DELAY_VOLUME) {
      if (need_vol) {
          printProgStr(str_volume, ROW_MODE);
          updateDisplay(true, vol);
          need_display = true;
      }
    } else {
      if (need_display) {
        updateDisplay(false, 0);
      }
    }
      
    // send encoder and volume to serial port with a small delay  
    if ((need_enc) && (current - last_enc >= DELAY_ENCODER)) {
      Serial.print(station);
      Serial.print(":");
      Serial.println(100);
      need_enc = false;
    }
  }
}

/**
 * Application mode to control Bass tone
 * @param unsigned long current - current timestamp
 */
void AppToneBass(unsigned long current) {
  if (mode_changed) {
    mode_changed = false;
    setEncoder(tones[0]);
  }
  tones[0] = readEncoder(100);
  printProgStr(str_bass, ROW_MODE);
  updateDisplay(true, tones[0]);
}

/**
 * Application mode to control Treble tone
 * @param unsigned long current - current timestamp
 */
void AppToneTreble(unsigned long current) {
 if (mode_changed) {
    mode_changed = false;
    setEncoder(tones[1]);
  }
  tones[1] = readEncoder(100);
  printProgStr(str_treble, ROW_MODE);
  updateDisplay(true, tones[1]);
}

/**
 * Main application loop
 */ 
void loop() {

  unsigned long current = millis();
    
  vol = readVolume();
  btn = readKeyboard();

  if (current - last_mode >= DELAY_MODE) {
    switch (btn) {

      case btn_encoder:
        last_mode = current;
         if (mode == NUM_MODES-1) {
           mode = 0;
         } else {
           mode = mode++;
         }
         mode_changed = true;
      break;

      case btn_preset:
        if (mode == app_mode_tone_bass) {
          mode = app_mode_tone_treble;
        } else {
          mode = app_mode_tone_bass;
        }
        last_mode = current;
        mode_changed = true;
      break;

      case btn_menu:
        mode = app_mode_station;
        last_mode = current;
        mode_changed = true;
      break;

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

      // todo: other modes

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
        AppStation(current);
      break;
      case app_mode_tone_bass:
        AppToneBass(current);
      break;
      case app_mode_tone_treble:
        AppToneTreble(current);
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
     if (strlen(buf) == 0 || cmd == NULL) return;  

     // command T1:<some text> will print text on the first line of the lcd
     if (strcmp(cmd,"T1") == 0) {
         strcpy(t[ROW_TITLE], arg1);
         need_display = true;
     } 

     // command T2:<some text> will print text on the second line of the lcd
     if (strcmp(cmd, "T2") == 0) {
         strcpy(t[ROW_STATION], arg1);
         need_display = true;
     }

     // command T3:<some text> will print text on the third line of the lcd
     if (strcmp(cmd, "T3") == 0) {
         strcpy(t[ROW_SONG1], arg1);
         need_display = true;
     }

     // command T4:<some text> will print text on the fourth line of the lcd
     if (strcmp(cmd, "T4") == 0) {
         strcpy(t[ROW_SONG2], arg1);
         need_display = true;
     }

     // command T5:HH:MM
     if (strcmp(cmd, "T5") == 0) {
         strcpy(t[ROW_TIME], arg1);
         strcat(t[ROW_TIME], ":");
         strcat(t[ROW_TIME], arg2);
         need_display = true;
     }
          
     // done init
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
 
void updateDisplay(boolean show_progress, int percentage) {
  
  display.clearDisplay();
  display.setTextColor(WHITE);
  
  if (show_progress) {
    display.setTextSize(2);
    display.setCursor(0, 15);
    char tt[COLS+1];
    char ib[4];
    itoa(percentage, ib, 10);
    strcpy(tt, t[ROW_MODE]);
    strcat(tt, ib);
    display.println(tt);
    display.drawRect(0, 45, 127, 8, WHITE);
    display.fillRect(2, 47, map(percentage, 0, 100, 2, 123), 4, WHITE);
    
  } else {
    display.setTextSize(1);
    display.setCursor(0,0);
    display.println(t[ROW_TITLE]);

    display.setCursor(0,15);
    display.println(t[ROW_STATION]);

    display.setCursor(90,15);
    display.println(t[ROW_TIME]);

    display.setCursor(0,30);
    display.println(t[ROW_SONG1]);

    display.setCursor(0,45);
    display.println(t[ROW_SONG2]);
  }
  
  display.display();
  need_display = false;
}

/**
 * Print station name and index into the LCD buffer
 */
void printStation(int s) {

    char ib[4];
    int station_id = s + 1;
    int station_count = max_stations + 1;
  
    t[ROW_STATION][0] = '\0';
    // printProgStr(str_station, 1);

    itoa(station_id, ib, 10);
    strcat(t[ROW_STATION], ib);

    strcat(t[ROW_STATION], " / ");

    itoa(station_count, ib, 10);
    strcat(t[ROW_STATION], ib);
  
    t[ROW_SONG1][0] = '\0';
    t[ROW_SONG2][0] = '\0';
}


