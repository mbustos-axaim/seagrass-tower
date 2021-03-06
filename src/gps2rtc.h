/*

    Copyright (C) 2016 Mauricio Bustos (m@bustos.org)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

//
//  GPS2RTC
//  Randy John
//  May 25, 2013
//
//  This code will set the RTC from a GPS receiver.  After 10 seconds it will set the compensation.
//  Subsequently, it will resychronize the RTC on each second while connected.  It will not change the compensation again.
//  Compensation is saved by the RTC battery.  If you wish to recalculate the compensation you need to call "RTC_TCR = 0"
//  or remove the RTC battery.  The battery is not required.
//
//  To use this software it is only necessary to create an instance of the class and call 'begin' specifying the serial port,
//  the baud rate and the pin that is connected to the 1pps pin:
//      #include <TinyGPS.h>
//      #include "gps2rtc.h"
//      const int gps_1pps_pin = 9;
//      const int gps_onoff_pin = 16;
//      GPS2RTC gps2rtc;
//      gps2rtc.begin(&Serial3, 9600, gps_1pps_pin, gps_onoff_pin);
//
//  Notes.
//  o Runs on the Teensy 3.0 (Freescale K20).
//  o Requires a HardwareSerial with its RX pin connected to the GPS receiver TX pin (RX is unused).
//  o Requires the modified HardwareSerial.h and serial1.c (and 2 and 3) that supports 'set_rx_isr'.
//  o Requires an input pin connected to the GPS receiver 1PPS pin.  This software will set the pin to be an input with a pull-down.
//  o Requires the TinyGPS library.
//
//  The time is set in terms of seconds past midnight UT which you can fetch by calling "rtc_get()".
//  Oh yes, the latitude and longitude is saved.
//

#include <hardwareserial.h>
#include "TinyGPS.h"

enum GPS_TO_RTC_STATE {
  looking_for_serial_data = 0,
  waiting_for_1pps = 1,
  done = 2,
  synchronize_to_pps = 3
};

const int sample_time_secs = 5;              // We will check the RTC after 10 seconds to see how far off it is from the GPS receiver.

class GPS2RTC
{
 public:
  void begin(HardwareSerial* a_gps_serial, int a_baud_rate, int a_gps_1pps_pin, int a_gps_onoff_pin)
  // This began life as a constructor but it couldn't handle the HardwareSerial.
  {
	gps_serial = a_gps_serial;
	gps_1pps_pin = a_gps_1pps_pin;

	gps_serial->begin(a_baud_rate);				// Initialize the serial port (both pins) and set the baud rate.
	pinMode_input_with_pulldown(gps_1pps_pin);	        // The 1PPS signal from the GPS receiver is connected to this pin.
	// We need the pull-down because the receiver is sometimes disconnected.
	//gps_serial->set_rx_isr(&handle_gps_receiver_data);
	state = looking_for_serial_data;
	receiving_serial_data = false;
	// Turn on GPS Unit
	gps_onoff_pin = a_gps_onoff_pin;
	pinMode(gps_onoff_pin, OUTPUT);
	digitalWrite(gps_onoff_pin, LOW);
	delay(1000);
	digitalWrite(gps_onoff_pin, HIGH);
	delay(1000);
	digitalWrite(gps_onoff_pin, LOW);

	pinMode(gps_1pps_pin, INPUT);
	attachInterrupt(gps_1pps_pin, gps_1pps_isr, RISING);
  };

  static void pinMode_input_with_pulldown(uint8_t pin)
  // Sets pin 'pin' as an input with a pulldown resistor.
  {
	volatile uint32_t *config;

	*portModeRegister(pin) = 0;
	config = portConfigRegister(pin);
	*config = PORT_PCR_MUX(1) | PORT_PCR_PE; // Pull-down.
  }

  static void handle_gps_record()
  // Handle one record sent from the GPS receiver.
  {
	  unsigned long l_gps_time;
	  unsigned long l_new_gps_time;
	  long l_latitude;		// In decimal degrees * 100000.
	  long l_longitude;		// In decimal degrees * 100000.
	  unsigned long l_fix_age;	// Milliseconds since the last fix.

	  last_sentence_receipt = millis();
	  // Try to get the time.
	  gps.get_datetime(NULL, &l_gps_time, &l_fix_age);

	  // If it's available and recent, convert it to seconds since midnight and save it.
	  if (l_gps_time != 0 && l_fix_age != TinyGPS::GPS_INVALID_AGE && l_fix_age <= 2000) {
		l_gps_time = l_gps_time / 100;				    // Convert HHMMSS00 to HHMMSS.
		l_new_gps_time = l_gps_time % 100;			    // Get SS.
		l_gps_time = l_gps_time / 100;				    // Convert HHMMSS to HHMM.
		l_new_gps_time = l_new_gps_time + (l_gps_time % 100) * 60;  // Get MM and convert to seconds.
		l_gps_time = l_gps_time / 100;				    // Convert HHMM to HH.
		gps_time = l_new_gps_time + (l_gps_time % 100) * 3600;	    // Get HH and convert to seconds.
	  }

	  // Try to get the position.
	  gps.get_position(&l_latitude, &l_longitude, &l_fix_age);

	  // If it's available and recent, save it.
	  if (l_fix_age != TinyGPS::GPS_INVALID_AGE && l_fix_age <= 2000) {
		latitude = l_latitude;
		longitude = l_longitude;
	  }
  }

  static void handle_gps_receiver_data(char a_char)
  // Handle one character of data from the GPS receiver. 
  {
#ifdef DUMP_RAW_NMEA
	Serial.print(a_char);
#endif
	receiving_serial_data = true;
	if (gps.encode(a_char)) handle_gps_record();
  }
  
  static void gps_1pps_isr()
  // Handle an interrupt generated by the rising edge of the 1PPS signal from the GPS receiver.
  {
#ifdef GPS_DIAGNOSTICS
	Serial.print(".");
#endif
	  if (handle_interrupts) {
		switch (state) {
		case synchronize_to_pps:
#ifdef GPS_DIAGNOSTICS
		  Serial.print("|");
#endif
		  lastPPSTime = millis();
		  if (last_misalign == 0) {
			// Restart timer to get close to 1pps
			PIT_TCTRL2 = 0; // Disable Timer 2
			PIT_TCTRL2 = 0x2; // Enable Timer 2 interrupts
			PIT_TCTRL2 |= 0x1; // Start Timer 2
			last_misalign = PIT_CVAL2;
			pit_reset_count = sample_time_secs;
		  } 
		  if (pit_reset_count <= 0) {
			pit_reset_count = sample_time_secs;
			long track_misalign = 0;
			unsigned long current_counter = PIT_CVAL2;
			unsigned long tipping_point = PIT_LDVAL2 / 2;
			if (current_counter < tipping_point) {
			  track_misalign = -current_counter;
			} else {
			  track_misalign = -(current_counter - PIT_LDVAL2);
			}

			/*How long since we last calculated*/
			/*Compute all the working error variables*/
			double error = track_misalign;
			errSum += (error * sample_time_secs);
			double dErr = (error - lastErr) / sample_time_secs;
			
			/*Compute PID Output*/
			double output = 0.7 * error + 0.00005 * errSum + 1.0 * dErr;
#ifdef GPS_DIAGNOSTICS
			Serial.println("");
			Serial.println(current_counter);
			Serial.println(PIT_LDVAL2);
			Serial.print("OUTPUT=");
			Serial.println(output);
			Serial.print("error=");
			Serial.println(error);
			Serial.print("errSum=");
			Serial.println(errSum);
			Serial.print("dErr=");
			Serial.println(dErr);
#endif
			/*Remember some variables for next time*/
			lastErr = error;

			PIT_LDVAL2 = PIT_LDVAL2 + output / (60 * sample_time_secs);
		  }
		  pit_reset_count--;
		  break;
		default:
		  break;
		}
	  }
  }

  static unsigned long lastPPSTime;
  static double errSum, lastErr;
  static bool handle_interrupts;
  static bool receiving_serial_data;
  static unsigned long last_sentence_receipt;
  static unsigned long last_misalign;
  static enum GPS_TO_RTC_STATE state;             // See enum GPS_TO_RTC_STATE.
  static HardwareSerial* gps_serial;              // The serial port that is connected to the GPS receiver.
  static int gps_1pps_pin;                        // The pin number that is connected to the GPS receiver's 1PPS pin.
  static int gps_onoff_pin;                        // The pin number that is connected to the GPS receiver's on/off pin.
  static TinyGPS gps;                             // Routines to decode GPS NMEA messages.
  static int gps_time;				// Seconds since midnight decoded from the GPS receiver stream.
  static int16_t tpr_counter;
  static int8_t pit_reset_count;           // Number of seconds to wait after the initial 1pps to ensure that RTC_TCR has been reset
  static unsigned long latitude;			// In decimal degrees * 100000.
  static unsigned long longitude;			// In decimal degrees * 100000.
  static const int seconds_per_day = 86400;	// Number of seconds in a day.
};

