#ifndef ESP32TIME_H
#define ESP32TIME_H

#include <Arduino.h>

class ESP32Time {
	
	public:
		ESP32Time();
		ESP32Time(unsigned long offset);
		void setTime(unsigned long epoch = 1609459200, int ms = 0);	// default (1609459200) = 1st Jan 2021
		void setTime(int sc, int mn, int hr, int dy, int mt, int yr, int ms = 0);
		void setTimeStruct(tm t);
		tm getTimeStruct();
		String getTime(String format);
		
		String getTime();
		String getDateTime(bool mode = false);
		String getTimeDate(bool mode = false);
		String getDate(bool mode = false);
		String getAmPm(bool lowercase = false);
		
		unsigned long getEpoch();
		unsigned long getMillis();
		unsigned long getMicros();
		int getSecond();
		int getMinute();
		int getHour(bool mode = false);
		int getDay();
		int getDayofWeek();
		int getDayofYear();
		int getMonth();
		int getYear();
		
		unsigned long offset;
		unsigned long getLocalEpoch();
		
	private:
		bool overflow;
		

};


#endif
