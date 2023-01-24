

#include <Wire.h>
//#include <LCD.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EEPROM.h>

#define LOOP_DELAY 10           // 10 milliseconds
#define FRAME_LIMIT 200         // 2 seconds
#define SHORT_DEBOUNCE 50L      // 50 milliseconds
#define MEDIUM_DEBOUNCE 200L    // 1/5 second
#define LONG_DEBOUNCE 2000L     // 2 seconds
#define SET_MODE_TIMEOUT 30000L

#define MAX_SETBACK_TEMP 65
#define MIN_SETBACK_TEMP 45
#define MAX_OVERRIDE_TEMP 74
#define MIN_OVERRIDE_TEMP 45
#define MAX_OVERRIDE_TIME 240
#define MIN_OVERRIDE_TIME 5
#define MAX_ANTICIPATION 5.0
#define MIN_ANTICIPATION 0.0
#define MAX_CYCLES_PER_HOUR 10
#define MIN_CYCLES_PER_HOUR 0 // Off

//////////////////////////////////////////////////////////////////////////////////////////
// No user-servicable parts below
//
#define FRAME_HALF_LIMIT (FRAME_LIMIT/2)

#define TEMP_PIN 27
#define RELAY_PIN 33
#define HEATING_LED_PIN 26
#define OVERRIDE_LED_PIN 25

#define SW_SET_PIN 19
#define SW_UP_PIN 17
#define SW_DOWN_PIN 16
#define SW_START_PIN 23
#define SW_CANCEL_PIN 18

#define SETBACK_TEMP_ADDR     0x0
#define OVERRIDE_TEMP_ADDR    (SETBACK_TEMP_ADDR     + sizeof(int))
#define OVERRIDE_MINUTES_ADDR (OVERRIDE_TEMP_ADDR    + sizeof(int))
#define CYCLE_LIMIT_ADDR      (OVERRIDE_MINUTES_ADDR + sizeof(int))
#define CYCLES_PER_HOUR_ADDR  (CYCLE_LIMIT_ADDR      + sizeof(bool))
#define ANTICIPATION_ADDR     (CYCLES_PER_HOUR_ADDR  + sizeof(int))

class Switch {
  private:
    int Pin;
    bool LastVal;
    bool CurrVal;
    unsigned long TriggerTime;
    bool Triggered;

  public:
    Switch(int pin);
    bool Read(unsigned long debounce);
};

Switch::Switch(int pin) {
  Pin = pin;
  CurrVal = false;
  LastVal = false;
  TriggerTime = 0;
  Triggered = false;
}

bool Switch::Read(unsigned long debounce) {
  bool retval = false;
  CurrVal = (digitalRead (Pin) == LOW); // since pullup => pressed equals low

  if (CurrVal && !LastVal) {
    TriggerTime = millis() + debounce;
  }

  if (CurrVal && LastVal && !Triggered && ((long)( millis() - TriggerTime ) >= 0)) {
    Triggered = true;
    retval = true;
  }

  if (!CurrVal) {
    Triggered = false;
  }

  LastVal = CurrVal;
  return (retval);
}

Switch sw_set(SW_SET_PIN);
Switch sw_up(SW_UP_PIN);
Switch sw_down(SW_DOWN_PIN);
Switch sw_start(SW_START_PIN);
Switch sw_cancel(SW_CANCEL_PIN);

LiquidCrystal_I2C  lcd(0x27, 16,2 ); // 0x27 is the I2C bus address for an unmodified backpack
OneWire oneWire(TEMP_PIN);
DallasTemperature sensors(&oneWire);

float curr_temp = 0.0;                   // current temperature
int curr_set_point = 55;                 // setpoint temperature based on override or not
unsigned long override_start_time = 0;   // time (in millis) since override button was pressed
unsigned long override_finish_time = 0;  // time (in millis) when override will finish
bool heating = false;                    // are we currently heating?
unsigned long last_cycle_time = 0;       // time (in millis) since last heat cycle started
int frame = 0 ;                          // current display frame
unsigned long timeout_time = 0;          // time any of set/up/down was pressed + timeout period

// the following are restored from EEPROM at powerup
int setback_temp;     // setback or default temperature
int override_temp;    // override temperature
int override_minutes; // number of minutes an override will last
bool cycle_limit;     // enable cycles per hour limit
int cycles_per_hour;  // max number of cycles per hour
float anticipation;   // hysteresis degrees



enum TempMode {
  Setback,
  Override
} temp_mode;

enum SetMode {
  Run,
  SetSetbackTemp,
  SetOverrideTemp,
  SetOverrideTime,
  SetAnticipation,
  SetCyclesPerHour
} set_mode;


void setup()
{
  // set up inputs and output
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(HEATING_LED_PIN, OUTPUT);
  pinMode(OVERRIDE_LED_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(HEATING_LED_PIN, HIGH);
  digitalWrite(OVERRIDE_LED_PIN, HIGH);

  pinMode(SW_SET_PIN, INPUT_PULLUP);
  pinMode(SW_UP_PIN, INPUT_PULLUP);
  pinMode(SW_DOWN_PIN, INPUT_PULLUP);
  pinMode(SW_START_PIN, INPUT_PULLUP);
  pinMode(SW_CANCEL_PIN, INPUT_PULLUP);

  // activate LCD module
  lcd.begin (); // for 16 x 2 LCD module
  //lcd.setBacklightPin(3, POSITIVE);
  lcd.setBacklight(HIGH);
  lcd.home(); // set cursor to 0,0
  lcd.print("Hello!");

  // initialize temp sensor
  sensors.begin();

  // initialize variables
  temp_mode = Setback;
  set_mode = Run;

  EEPROM.begin(128);

  setback_temp = EEPROM.read(SETBACK_TEMP_ADDR);
  override_temp = EEPROM.read(OVERRIDE_TEMP_ADDR);
  override_minutes = EEPROM.read(OVERRIDE_MINUTES_ADDR);
  cycle_limit = EEPROM.read(CYCLE_LIMIT_ADDR);
  cycles_per_hour = EEPROM.read(CYCLES_PER_HOUR_ADDR);
  anticipation = EEPROM.read(ANTICIPATION_ADDR);

  bool upflag = false;
  if (setback_temp < MIN_SETBACK_TEMP) {
    setback_temp = MIN_SETBACK_TEMP;
    upflag = true;
  }
  if (setback_temp > MAX_SETBACK_TEMP) {
    setback_temp = MAX_SETBACK_TEMP;
    upflag = true;
  }
  
  if (override_temp < MIN_OVERRIDE_TEMP) {
    override_temp = MIN_OVERRIDE_TEMP;
    upflag = true;
  }
  if (override_temp > MAX_OVERRIDE_TEMP) {
    override_temp = MAX_OVERRIDE_TEMP;
    upflag = true;
  }
  
  if (override_minutes < MIN_OVERRIDE_TIME) {
    override_minutes = MIN_OVERRIDE_TIME;
    upflag = true;
  }
  if (override_minutes > MAX_OVERRIDE_TIME) {
    override_minutes = MAX_OVERRIDE_TIME;
    upflag = true;
  }

  if (cycles_per_hour < MIN_CYCLES_PER_HOUR) {
    cycles_per_hour = MIN_CYCLES_PER_HOUR;
    cycle_limit = (cycles_per_hour != MIN_CYCLES_PER_HOUR);
    upflag = true;
  }
  if (cycles_per_hour > MAX_CYCLES_PER_HOUR) {
    cycles_per_hour = MAX_CYCLES_PER_HOUR;
    cycle_limit = (cycles_per_hour != MIN_CYCLES_PER_HOUR);
    upflag = true;
  }
  
  if (anticipation < MIN_ANTICIPATION) {
    anticipation = MIN_ANTICIPATION;
    upflag = true;
  } else if (anticipation > MAX_ANTICIPATION) {
    anticipation = MAX_ANTICIPATION;
    upflag = true;
  } else {
    anticipation = MAX_ANTICIPATION;
    upflag = true;
  }
  if (upflag) {
    UpdateEEPROM();
  }
}

void loop()
{
  ModeControl ();
  TempControl ();
  Display ();

  delay(LOOP_DELAY);
}

void ModeControl() {

  // Handle Override and Cancel
  //
  if (sw_start.Read(MEDIUM_DEBOUNCE)) {
    temp_mode = Override;
    override_start_time = millis();
  }
  if (sw_cancel.Read(MEDIUM_DEBOUNCE)) {
    temp_mode = Setback;
  }

  // Handle Set/Up/Down
  //
  bool set_pressed = sw_set.Read((set_mode == Run) ? LONG_DEBOUNCE : SHORT_DEBOUNCE);
  bool up_pressed = sw_up.Read(SHORT_DEBOUNCE);
  bool down_pressed = sw_down.Read(SHORT_DEBOUNCE);

  if (set_pressed || up_pressed || down_pressed) {
    timeout_time = millis() + SET_MODE_TIMEOUT;
  }

  switch (set_mode) {
    case Run:
      if (set_pressed) {
        set_mode = SetSetbackTemp;
      }
      break;

    case SetSetbackTemp:
      if (set_pressed) set_mode = SetOverrideTemp;
      if (up_pressed) {
        if (setback_temp < MAX_SETBACK_TEMP) {
          setback_temp += 1;
        }
      }
      if (down_pressed) {
        if (setback_temp > MIN_SETBACK_TEMP) {
          setback_temp -= 1;
        }
      }
      break;

    case SetOverrideTemp:
      if (set_pressed) set_mode = SetOverrideTime;
      if (up_pressed) {
        if (override_temp < MAX_OVERRIDE_TEMP) {
          override_temp += 1;
        }
      }
      if (down_pressed) {
        if (override_temp > MIN_OVERRIDE_TEMP) {
          override_temp -= 1;
        }
      }
      break;

    case SetOverrideTime:
      if (set_pressed) set_mode = SetAnticipation;
      if (up_pressed) {
        if (override_minutes < MAX_OVERRIDE_TIME) {
          override_minutes += 5;
        } else {
          override_minutes = MIN_OVERRIDE_TIME;
        }
      }
      if (down_pressed) {
        if (override_minutes > MIN_OVERRIDE_TIME) {
          override_minutes -= 5;
          if (override_minutes < 0) override_minutes = MIN_OVERRIDE_TIME;
        } else {
          override_minutes = MAX_OVERRIDE_TIME;
        }
      }
      break;

    case SetAnticipation:
      if (set_pressed) set_mode = SetCyclesPerHour;
      if (up_pressed) {
        if (anticipation < MAX_ANTICIPATION) {
          anticipation += 0.5;
        }
      }
      if (down_pressed) {
        if (anticipation > MIN_ANTICIPATION) {
          anticipation -= 0.5;
          if (anticipation < 0) anticipation = 0;
        }
      }
      break;

    case SetCyclesPerHour:
      if (set_pressed) {
        set_mode = Run;
        UpdateEEPROM();
      }
      if (up_pressed) {
        if (cycles_per_hour < MAX_CYCLES_PER_HOUR) {
          cycles_per_hour += 1;
        } else {
          cycles_per_hour = MIN_CYCLES_PER_HOUR; // wraps
        }
      }
      if (down_pressed) {
        if (cycles_per_hour > MIN_CYCLES_PER_HOUR) {
          cycles_per_hour -= 1;
        } else {
          cycles_per_hour = MAX_CYCLES_PER_HOUR; // wraps
        }
      }
      cycle_limit = (cycles_per_hour != MIN_CYCLES_PER_HOUR);
      break;

    default:
      break;
  }

  // Leave set mode if no buttons are pressed for a period of time
  //
  if ((set_mode != Run) && (((long) (millis() - timeout_time) >= 0))) {
    set_mode = Run;
    UpdateEEPROM();
  }

}

void UpdateEEPROM() {
  int old_setback_temp;
  int old_override_temp;
  int old_override_minutes;
  bool old_cycle_limit;
  int old_cycles_per_hour;
  float old_anticipation;

  old_setback_temp = EEPROM.read(SETBACK_TEMP_ADDR);
  old_override_temp = EEPROM.read(OVERRIDE_TEMP_ADDR);
  old_override_minutes = EEPROM.read(OVERRIDE_MINUTES_ADDR);
  old_cycle_limit = EEPROM.read(CYCLE_LIMIT_ADDR);
  old_cycles_per_hour = EEPROM.read(CYCLES_PER_HOUR_ADDR);
  old_anticipation = EEPROM.read(ANTICIPATION_ADDR);
  
  if (setback_temp != old_setback_temp)         EEPROM.write(SETBACK_TEMP_ADDR, setback_temp);
  if (override_temp != old_override_temp)       EEPROM.write(OVERRIDE_TEMP_ADDR, override_temp);
  if (override_minutes != old_override_minutes) EEPROM.write(OVERRIDE_MINUTES_ADDR, override_minutes);
  if (cycle_limit != old_cycle_limit)           EEPROM.write(CYCLE_LIMIT_ADDR, cycle_limit);
  if (cycles_per_hour != old_cycles_per_hour)   EEPROM.write(CYCLES_PER_HOUR_ADDR, cycles_per_hour);
  if (anticipation != old_anticipation)         EEPROM.write(ANTICIPATION_ADDR, anticipation);

  EEPROM.commit();
}

// This is the heart of the thermostat: the control loop
// It reads the temperature and engages or disengages the relay as needed
void TempControl() {
  // read the temperature every 100th frame if we are not in set mode - o.w. there can be delays between
  // pressing a button and it taking effect
  //
  if ((set_mode == Run) && ((frame % 100) == 0)) {
    sensors.requestTemperatures(); // Send the command to get temperatures
    curr_temp = sensors.getTempFByIndex(0);
  }

  // see if the override has timed out
  //
  override_finish_time = override_start_time + (60000UL * (unsigned long) override_minutes);
  if ((temp_mode == Override) && (((long) (millis() - override_finish_time) >= 0))) {
    temp_mode = Setback;
  }

  // calculate set point and whether it's OK to start a new cycle based on cycles_per_hour
  //
  curr_set_point = (temp_mode == Override) ? override_temp : setback_temp;

  unsigned long next_cycle_time = cycle_limit ? (last_cycle_time + (3600000UL / (unsigned long) cycles_per_hour)) : last_cycle_time;

  if (!heating
      && (curr_temp < (((float) curr_set_point) - anticipation))
      && (!cycle_limit || (((long)( millis() - next_cycle_time ) >= 0)))) {
    heating = true;
    last_cycle_time = millis();
  }

  if (heating && (curr_temp >= (float) curr_set_point)) {
    heating = false;
  }

  if (heating) {
    digitalWrite(RELAY_PIN, HIGH);
  } else {
    digitalWrite(RELAY_PIN, LOW);
  }
}


void Display() {

  // set status LEDs
  digitalWrite(HEATING_LED_PIN, heating);
  digitalWrite(OVERRIDE_LED_PIN, (temp_mode == Override));

  // write to LCD matrix display
  lcd.home (); // set cursor to 0,0

  switch (set_mode) {

    // RUN
    case Run:
      lcd.print("Temp:      ");
      lcd.print(curr_temp, 0);
      lcd.print("F  ");

      lcd.setCursor (0, 1);       // go to start of 2nd line

      if ((temp_mode == Setback) || (frame < FRAME_HALF_LIMIT)) {
        lcd.print("Set Point: ");
        lcd.print(curr_set_point);
        lcd.print("F  ");
      }
      if ((temp_mode == Override) && (frame >= FRAME_HALF_LIMIT)) {
        lcd.print("Ovrd Time: ");
        long time_left = (override_finish_time - millis());
        print_minutes ((int) (time_left / 60000L) + 1);
      }
      break;

    // Setback Temp
    case SetSetbackTemp:
      lcd.print("Setback Temp:   ");
      lcd.setCursor (0, 1); 
      lcd.print(setback_temp);
      lcd.print("              ");
      break;

    // Override Temp
    case SetOverrideTemp:
      lcd.print("Override Temp:  ");
      lcd.setCursor (0, 1);
      lcd.print(override_temp);
      lcd.print("              ");
      break;

    // Override Time
    case SetOverrideTime:
      lcd.print("Override Time:  ");
      lcd.setCursor (0, 1);
      print_minutes(override_minutes);
      lcd.print("            ");
      break;

    // Anticipation
    case SetAnticipation:
      lcd.print("Anticipation:   ");
      lcd.setCursor (0, 1);
      lcd.print(anticipation,1);
      lcd.print("             ");
      break;

    // Cycles/Hour
    case SetCyclesPerHour:
      lcd.print("Cycles/Hour:    ");
      lcd.setCursor (0, 1);
      if (cycle_limit) {
        lcd.print(cycles_per_hour);
        lcd.print("              ");
      } else {
        lcd.print("-               ");
      }
      break;

    default:
      break;

  }

  //lcd.setBacklight(LOW);      // Backlight off
  //delay(250);
  //lcd.setBacklight(HIGH);     // Backlight on
  //delay(1000);
  frame += 1;
  frame = frame % FRAME_LIMIT;
}

void print_minutes(int t) {
  int hours = t / 60;
  int minutes = t % 60;
  if (hours > 0) {
    lcd.print(hours);
  } else {
    lcd.print(" ");
  }
  lcd.print(":");
  if (minutes < 10) {
    lcd.print("0");
  }
  lcd.print(minutes);
  lcd.print(" ");
}
