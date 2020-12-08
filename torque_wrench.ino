#include <SimpleRotary.h>

// Pin A, Pin B, Button Pin
SimpleRotary rotary(7,6,8);

/**************************************************************************
 This is an example for our Monochrome OLEDs based on SSD1306 drivers

 Pick one up today in the adafruit shop!
 ------> http://www.adafruit.com/category/63_98

 This example is for a 128x64 pixel display using I2C to communicate
 3 pins are required to interface (two I2C and one reset).

 Adafruit invests time and resources providing this open
 source code, please support Adafruit and open-source
 hardware by purchasing products from Adafruit!

 Written by Limor Fried/Ladyada for Adafruit Industries,
 with contributions from the open source community.
 BSD license, check license.txt for more information
 All text above, and the splash screen below must be
 included in any redistribution.
 **************************************************************************/

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <HX711_ADC.h>
#include <EEPROM.h>

const int HX711_dout = 3; //mcu > HX711 dout pin
const int HX711_sck = 5; //mcu > HX711 sck pin

#define uint unsigned int
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define uint unsigned int
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)

HX711_ADC LoadCell(HX711_dout, HX711_sck);
// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

enum SCREEN {MAIN_SCREEN, CAL_SCREEN, SETTINGS_SCREEN, LAST_SCREEN};
enum MAIN_WIDGETS {SET_VALUE_WIDGET, SET_UNIT_WIDGET, CURRENT_VALUE_WIDGET, LAST_MAIN_WIDGET};
enum CONFIG_WIDGETS {SET_USE_TARE, SET_PERC, LAST_CONFIG_WIDGET};
enum CONFIG_STEP {HOLD_STILL, DEFINE_MASS, DEFINE_LENGHT, SET, SAVE, LAST_CONFIG_STEP};
enum SETTINGS_WIDGETS {LAST_SETTINGS_WIDGET};
enum UNITS {NM, KGM, FTLBS, LAST_UNIT};
enum EEPROM {UNIT, CALIBRATION, PERC_VALUE, TARE_FIRST};

char* unit_str[]  = { "Nm", "Kgm", "ft.lbs"};
char* yes_no_str[] = {"No", "Yes"};
uint current_screen;
uint current_widget; 
uint current_unit;
uint mass_setting = 1000;
uint length_setting = 50;
uint current_config_step;
float calibration;
float current_set_value;
float current_value;
float newCalibrationValue;
static uint perc_value = 10;
static bool tare_first;
volatile bool refresh_buzzer =false;
int newOCR;
volatile boolean newDataReady;

void setup() {
	current_screen = MAIN_SCREEN; 
	current_widget = SET_VALUE_WIDGET;
	current_unit = EEPROM.read(UNIT);
	perc_value = EEPROM.read(PERC_VALUE);
	tare_first = EEPROM.read(TARE_FIRST);
	EEPROM.get(CALIBRATION, calibration);
	if(current_unit >= LAST_UNIT) {
		EEPROM.write(UNIT, NM);
		current_unit = NM;
	}
	current_set_value = 200.0;
	current_config_step = HOLD_STILL;

	TCCR1A = 0;// set entire TCCR1A register to 0
	TCCR1B = 0;// same for TCCR1B
	TCNT1  = 0;//initialize counter value to 0
	// set compare match register for 1hz increments
	OCR1A = 15624;// = (16*10^6) / (1*1024) - 1 (must be <65536)
	// turn on CTC mode
	TCCR1B |= (1 << WGM12);
	// Set CS10 and CS12 bits for 1024 prescaler
	TCCR1B |= (1 << CS12) | (1 << CS10);

	Serial.begin(9600);
	setBuzzer(-1);

  	// SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  	if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3D for 128x64
  		Serial.println(F("SSD1306 allocation failed"));
    	for(;;); // Don't proceed, loop forever
	}
  	setupLoadCell();
	display.display();
	display.clearDisplay();
	drawscreen();
}

unsigned long rot_idle_elapsed_time;
unsigned long rot_speed = 1;
unsigned long rot_speed_time;
unsigned long last_screen_redraw = 0;
int rmultiplier = 1;
int i,x;
bool refresh_screen = false;

void loop() {
#if !defined(USE_HX_INT)
  if(current_screen == MAIN_SCREEN)
    if (!digitalRead(HX711_dout))
      if(LoadCell.update()) newDataReady = true;
#endif
	if (newDataReady && current_screen == MAIN_SCREEN) {
    	float i = LoadCell.getData();
      	if(i < 0)
        	i = 0;
    	refresh_screen = true;
    	switch(current_unit) {
    		case NM:
    		break;
    		case KGM:
    			i = i * 0.10197162129779283;
    		break;
    		case FTLBS:
    			i = i * 0.7375621;
    		break;
    	}
    	current_value = i;
    	newDataReady = 0;                       //10
     	float Xpercent = (current_set_value * perc_value) / 100;
                       //180      i=0 togo = -180 // i=180 togo = 0 // i = 200 togo = 20 
	    float togo = i - (current_set_value - Xpercent);
     	if(togo < 0)
      		togo = 0;
     	float perc = togo / Xpercent * 100;
     	if(perc >= 100)
     		display.invertDisplay(true);
     	else
     		display.invertDisplay(false);
     	setBuzzer(round(perc));
	}
	i = rotary.pushType(1000);
	byte ii;
	if(i == 1) {//short click
		refresh_screen = true;
		if(current_screen == MAIN_SCREEN || current_screen == SETTINGS_SCREEN)
			++current_widget;
		else if(current_screen == CAL_SCREEN)
			++current_config_step;
		if(current_screen == MAIN_SCREEN && current_widget == LAST_MAIN_WIDGET)
			current_widget = 0;
		else if(current_screen == SETTINGS_SCREEN && current_widget == LAST_CONFIG_WIDGET)
			current_widget = 0;
		if(current_screen == CAL_SCREEN) {
			if(current_config_step == LAST_CONFIG_STEP) {
				current_screen = MAIN_SCREEN;
				EEPROM.put(CALIBRATION, newCalibrationValue);
			}
			else if(current_config_step == SAVE) {
				float kgcm = mass_setting / 1000 * length_setting;
				float known_nm = kgcm * 0.0980665;
				Serial.println(kgcm);
				Serial.println(known_nm);
				LoadCell.update();
				LoadCell.refreshDataSet(); //refresh the dataset to be sure that the known mass is measured correct
			  	newCalibrationValue = LoadCell.getNewCalibration(known_nm); //get the new calibration value
			  	Serial.print(F("New calibration value has been set to: "));
			  	Serial.print(newCalibrationValue);
			}
			else if(current_config_step == DEFINE_MASS) {
			  	delay(2000);
			  	boolean _resume = false;
			  	LoadCell.tareNoDelay();
			  	while (_resume == false) {
			  		LoadCell.update();
			  		if (LoadCell.getTareStatus() == true) {
			  			Serial.println(F("Tare complete"));
			  			_resume = true;
			  		}
			  	}
			}
		}
	}
	else if(i == 2) {//long click
		setBuzzer(-1);
		refresh_screen = true;
		++current_screen;
		current_widget = 0;
		current_config_step = HOLD_STILL;
		if(current_screen == LAST_SCREEN)
			current_screen = 0;
#ifdef USE_HX_INT
    if(current_screen == CAL_SCREEN)
      detachInterrupt(digitalPinToInterrupt(HX711_dout));
    else
      attachInterrupt(digitalPinToInterrupt(HX711_dout), dataReadyISR, FALLING);
#endif
	}
  	// 0 = not turning, 1 = CW, 2 = CCW
	ii = rotary.rotate();
	int rot_val = 0;
	if(ii == 0) {
		if(millis() - rot_idle_elapsed_time > 500) {
			rmultiplier = 1;
			rot_speed = 0;
			rot_speed_time = millis();
		}
	}
	else {
		++rot_speed;
		rot_idle_elapsed_time = millis();
		if(millis() - rot_speed_time > 1000) {
			rot_speed_time = millis();
			if(rot_speed > 5) 
				rmultiplier = (rot_speed - 5) * 2;
			else 
				rmultiplier = 1;
			rot_speed = 1;
		}
	}
	if (ii == 1) {
		++rot_val;
	}
	else if (ii == 2) {
		--rot_val;
	}
	
	if(rot_val != 0) {
		refresh_screen = true;
		if(current_screen == MAIN_SCREEN) {
			if(current_widget == SET_VALUE_WIDGET) {
				current_set_value += rot_val * 0.1 * rmultiplier;
				if(current_set_value < 0)
					current_set_value = 0;
			}
			else if(current_widget == SET_UNIT_WIDGET) {
				current_unit += rot_val;
				if(current_unit >= LAST_UNIT || current_unit < 0)
					current_unit = 0;
				EEPROM.write(UNIT, current_unit);
			}
		}
		else if(current_screen == CAL_SCREEN) {
			if(current_config_step == DEFINE_MASS) {
				mass_setting += rot_val * rmultiplier;
				if(mass_setting < 0)
					mass_setting = 0;
			}
			else if(current_config_step == DEFINE_LENGHT) {
				length_setting += rot_val * rmultiplier;
				if(length_setting < 0)
					length_setting = 0;
			}
		}
		else if(current_screen == SETTINGS_SCREEN) {
			if(current_widget == SET_USE_TARE) {
				tare_first = !tare_first;
				EEPROM.write(TARE_FIRST, tare_first);
			}
			else if(current_widget == SET_PERC) {
				perc_value += rot_val;
				if(perc_value > 100)
					perc_value = 100;
				else if(perc_value < 0)
					perc_value = 0;
				EEPROM.write(PERC_VALUE, perc_value);
			}
		}
	}
	if(refresh_screen && (millis() - last_screen_redraw > 100)) {
		drawscreen();
    	last_screen_redraw = millis();
    	refresh_screen = false;
    }
}


void setBuzzer(int frequency) {
	static bool isStoped = true;
  	static bool isFull = false;
	if(frequency > 100)
		frequency = 100;
	if(frequency < -1)
		frequency = -1;
	if(frequency <= 0) {
		TIMSK1 &= ~(1 << OCIE1A);
    	isFull = false;
		isStoped = true;
    	noTone(4);
	}
	else if(frequency == 100) {
		TIMSK1 &= ~(1 << OCIE1A);
		isStoped = true;
	    if(!isFull)
			  tone(4, 1000);
	    isFull = true;
	}    
	else {
    	isFull = false;
		if(isStoped) {
			TIMSK1 |= (1 << OCIE1A);
			isStoped = false;
		}
	}
	newOCR = 15600 - (frequency * 150);// = (16*10^6) / (1*1024) - 1 (must be <65536)
  	if(newOCR != OCR1A) {
    	refresh_buzzer = true;
   	}
}

ISR(TIMER1_COMPA_vect){
	tone(4,1000,(float)OCR1A / 15000 * 500);
	if(refresh_buzzer) {
		refresh_buzzer = false;
		OCR1A = newOCR;
		TCNT1 = 0;
	}
}

void drawscreen(void) {
	char vBuf[7] = {};
	display.clearDisplay();
	if(current_screen == MAIN_SCREEN) {
	    display.setTextSize(2);
	    if(current_widget == SET_VALUE_WIDGET)
	    	display.setTextColor(SSD1306_BLACK, SSD1306_WHITE); 
	    else
	    	display.setTextColor(SSD1306_WHITE);        
	    display.setCursor(0,0);
	    dtostrf(current_set_value,5, 1, vBuf);
	    display.println(vBuf);
	    display.setCursor(65,0);             
	    snprintf(vBuf, sizeof(vBuf)-1, "%5s", unit_str[current_unit]);
	    if(current_widget == SET_UNIT_WIDGET)
	    	display.setTextColor(SSD1306_BLACK, SSD1306_WHITE); 
	    else
	    	display.setTextColor(SSD1306_WHITE);        
	    display.println(vBuf);
	    display.setCursor(10,30);             
	    display.setTextSize(3);             
	    display.setTextColor(SSD1306_WHITE);
	    dtostrf(current_value,5, 1, vBuf);
	    if(current_widget == CURRENT_VALUE_WIDGET)
	    	display.setTextColor(SSD1306_BLACK, SSD1306_WHITE); 
	    else
	    	display.setTextColor(SSD1306_WHITE);        
	    display.println(vBuf);  
	}
	if(current_screen == SETTINGS_SCREEN) {
		display.setTextSize(1);             
		display.setCursor(10,0);             
		display.setTextColor(SSD1306_WHITE);        
		display.println(F("Set tare on start?"));
	    display.setTextSize(2);
	    if(current_widget == SET_USE_TARE)
	    	display.setTextColor(SSD1306_BLACK, SSD1306_WHITE); 
	    else
	    	display.setTextColor(SSD1306_WHITE);        
	    display.setCursor(40,15);
	    snprintf(vBuf, sizeof(vBuf)-1, "%3s", yes_no_str[tare_first]);
	    display.println(vBuf);
	    display.setTextSize(1);             
		display.setCursor(5,35);             
		display.setTextColor(SSD1306_WHITE);        
		display.println(F("Percentage to bip?"));
	    display.setTextSize(2);
	    display.setCursor(40,50);             
		dtostrf(perc_value,3, 0, vBuf);
	    if(current_widget == SET_PERC)
	    	display.setTextColor(SSD1306_BLACK, SSD1306_WHITE); 
	    else
	    	display.setTextColor(SSD1306_WHITE);        
	    display.println(vBuf);
	}
	else if(current_screen == CAL_SCREEN) {
		if(current_config_step == HOLD_STILL) {
			display.setTextSize(1);             
			display.setCursor(0,30);             
			display.setTextColor(SSD1306_WHITE);        
			display.println(F("Hold still and press"));
		}
		else if(current_config_step == DEFINE_MASS) {
			display.setTextSize(1);             
			display.setCursor(0,0);             
			display.setTextColor(SSD1306_WHITE);        
			display.println(F("Define mass in grams"));
			dtostrf(mass_setting,5, 0, vBuf);
			display.setTextSize(3);             
			display.setCursor(10,30);             
			display.setTextColor(SSD1306_BLACK, SSD1306_WHITE); 
			display.println(vBuf);
		}
		else if(current_config_step == DEFINE_LENGHT) {
			display.setTextSize(1);             
			display.setCursor(0,0);             
			display.setTextColor(SSD1306_WHITE);        
			display.println(F("Define length in cm"));
			dtostrf(length_setting,5, 0, vBuf);
			display.setTextSize(3);             
			display.setCursor(10,30);             
			display.setTextColor(SSD1306_BLACK, SSD1306_WHITE); 
			display.println(vBuf);
		}
		else if(current_config_step == SET) {
			display.setTextSize(1);             
			display.setCursor(0,0);             
			display.setTextColor(SSD1306_WHITE);        
			display.println(F("Place weigth and click"));
		}
		else if(current_config_step == SAVE) {
			display.setTextSize(1);             
			display.setCursor(0,0);             
			display.setTextColor(SSD1306_WHITE);        
			display.println(F("Done. Click to save"));
		}
	}
	display.display();
}

void setupLoadCell() {
	LoadCell.begin();
	long stabilizingtime = 2000; // preciscion right after power-up can be improved by adding a few seconds of stabilizing time
	boolean _tare = true; //set this to false if you don't want tare to be performed in the next step
	LoadCell.start(stabilizingtime, _tare);
	if (LoadCell.getTareTimeoutFlag() || LoadCell.getSignalTimeoutFlag()) {
		Serial.println(F("Timeout, check MCU>HX711 wiring and pin designations"));
		while (1);
	}
	else {
		LoadCell.setCalFactor(calibration); // user set calibration value (float), initial value 1.0 may be used for this sketch
		Serial.println("Startup is complete");
	}
	while (!LoadCell.update());
#ifdef USE_HX_INT
	attachInterrupt(digitalPinToInterrupt(HX711_dout), dataReadyISR, FALLING);
#endif
}

//interrupt routine:
void dataReadyISR() {
  if (LoadCell.update()) {
    newDataReady = 1;
  }
}
