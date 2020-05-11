
#include "DHTesp.h"
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <U8g2lib.h>
//#include <Wire.h>

#define DHTPIN 0
int BUZPIN = 3;

// AP Wi-Fi credentials
const char* ssid = "DataTransfer";
const char* password = "BelovSer";
DHTesp dht;
const String  Devicename = "2";
float h;
float t;
int missed = 0;//repated falure of DHT11 sensor 
//------------------------------------------------------------------------------------
  // WIFI Module Role & Port
IPAddress     TCP_Server(192, 168, 4, 1);
IPAddress     TCP_Gateway(192, 168, 4, 1);
IPAddress     TCP_Subnet(255, 255, 255, 0);
IPAddress Own(192, 168, 4, 102);

unsigned int  TCPPort = 2390;

WiFiClient    TCP_Client;

U8G2_SSD1306_128X32_UNIVISION_F_SW_I2C u8g2(U8G2_R0,2,1);  
class TimeSet {
	int monthDays[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 }; // API starts months from 1, this array starts from 0
	int *shift;
	time_t lastUpdate;//time when the time was updated, used for current time calculation
	time_t actual_Time;
	time_t UNIXtime;
public:
	int NowMonth;
	int NowYear;
	int NowDay;
	int NowWeekDay;
	int lastShift;
	int NowHour;
	int NowMin;
	int NowSec;
	int Now10Min;
	String FileNewManeParameter; //defines file name assingnement whether: min, 10min, hour, day
	int sec() {
		updateCurrecnt();
		return actual_Time % 60;
	};
	int min() {
		updateCurrecnt();
		return actual_Time / 60 % 60;
	};
	int hour() {
		updateCurrecnt();
		return actual_Time / 3600 % 24;
	};

	time_t SetCurrentTime(time_t timeToSet) { //call each time after recieving updated time
		lastUpdate = millis();
		UNIXtime = timeToSet;
	}//call each time after recieving updated time
	void updateDay() {
		updateCurrecnt();
		breakTime(&NowYear, &NowMonth, &NowDay, &NowWeekDay);
		Serial.println(String(NowYear) + ":" + String(NowMonth) + ":" + String(NowDay) + ":" + String(NowWeekDay));
	}// call in setup and each time when new day comes 
	bool Shift() {  //check whether there is a new day comes, first call causes alignement of tracked and checked day 
		//Serial.println("lastShift " + String(lastShift) + "  *shift " + String(*shift));
		if (lastShift != *shift) {
			lastShift = *shift;
			return true;
		}
		else return false;
	} //call to check whether the day is changed
	void begin(int shiftType = 1) //1-day,2-hiur,3-10 minutes
	{
		switch (shiftType)
		{
		case 1:shift = &NowDay;
			FileNewManeParameter = "day";
			break;
		case 2:shift = &NowHour;
			FileNewManeParameter = "hour";
			break;
		case 3:shift = &Now10Min;
			FileNewManeParameter = "10min";
			break;
		case 4:shift = &NowMin;
			FileNewManeParameter = "min";
			break;
		default:shift = &NowDay;
			FileNewManeParameter = "day";
			break;
		}
	};
private:

	void updateCurrecnt() {
		actual_Time = UNIXtime + (millis() - lastUpdate) / 1000;

	}

	bool LEAP_YEAR(time_t Y) {
		//((1970 + (Y)) > 0) && !((1970 + (Y)) % 4) && (((1970 + (Y)) % 100) || !((1970 + (Y)) % 400)) ;
		if ((1970 + (Y)) > 0) {
			if ((1970 + (Y)) % 4 == 0) {
				if ((((1970 + (Y)) % 100) != 0) || (((1970 + (Y)) % 400) == 0)) return true;
			}
			else return false;
		}
		else return false;
	}

	void breakTime(int *Year, int *Month, int *day, int *week) {
		// break the given time_t into time components
		// this is a more compact version of the C library localtime function
		// note that year is offset from 1970 !!!

		int year;
		int month, monthLength;
		uint32_t time;
		unsigned long days;

		time = (uint32_t)actual_Time;
		NowSec = time % 60;
		time /= 60; // now it is minutes
		NowMin = time % 60;
		Now10Min = time % 600;
		time /= 60; // now it is hours
		NowHour = time % 24;
		time /= 24; // now it is days
		*week = int(((time + 4) % 7));  // Monday is day 1 

		year = 0;
		days = 0;
		while ((unsigned)(days += (LEAP_YEAR(year) ? 366 : 365)) <= time) {
			year++;
		}
		*Year = year + 1970; // year is offset from 1970 

		days -= LEAP_YEAR(year) ? 366 : 365;
		time -= days; // now it is days in this year, starting at 0

		days = 0;
		month = 0;
		monthLength = 0;
		for (month = 0; month < 12; month++) {
			if (month == 1) { // february
				if (LEAP_YEAR(year)) {
					monthLength = 29;
				}
				else {
					monthLength = 28;
				}
			}
			else {
				monthLength = monthDays[month];
			}

			if (time >= monthLength) {
				time -= monthLength;
			}
			else {
				break;
			}
		}
		*Month = month + 1;  // jan is month 1  
		*day = time + 1;     // day of month
	}
};
TimeSet Time_set;
class task {
public:
	unsigned long period;
	bool ignor = false;
	void reLoop() {
		taskLoop = millis();
	};
	bool check() {
		if (!ignor) {
			if (millis() - taskLoop > period) {
				taskLoop = millis();
				return true;
			}
		}
		return false;
	}
	void StartLoop(unsigned long shift) {
		taskLoop = millis() + shift;
	}
	task(unsigned long t) {
		period = t;
	}
private:
	unsigned long taskLoop;

};

task lasTimeReadSensor(2000);
task sendRequestToServer(10000);
task sendLogToSrver(30000);
task refreshOLED(1000);
task askTimeTask(20000);
String fieldsInLogMes = "Device:2;get:3;Time general,Time device,Signal,Temp,Humid;";
unsigned long UNIXtime;
unsigned long actualtime;
unsigned long lastNTPResponse;
int contrast;
void setup() {
	//Serial.begin(115200);
	dht.setup(DHTPIN, DHTesp::DHT11);
	u8g2.begin();
	u8g2.setFont(u8g2_font_crox1c_tf);
	//Wire.begin();
	WiFi.hostname("ESP_Box_w_OLED");      // DHCP Hostname (useful for finding device for static lease)
	WiFi.config(Own, TCP_Gateway, TCP_Subnet);
	Check_WiFi_and_Connect_or_Reconnect();          // Checking For Connection
	pinMode(BUZPIN, OUTPUT);
	digitalWrite(BUZPIN, LOW);
}
void showOnOled(String line1, String line2,String line3,int contrast) {
	u8g2.setContrast(contrast);
	u8g2.clearBuffer();
	u8g2.setCursor(0, 10);
	u8g2.print(line1);
	u8g2.setCursor(0, 21);
	u8g2.print(line2);
	u8g2.setCursor(0, 32);
	u8g2.print(line3);
	u8g2.sendBuffer();
}


void readDHTSensor() {
	//delay(dht.getMinimumSamplingPeriod());
	float t1, h1;
	h1 = dht.getHumidity();
	t1 = dht.getTemperature();
	//h = 55.00;
	//t = 99.99;
	if (isnan(h1) || isnan(t1)) {
		if (missed > 3) {
			t = 0.00;
			h = 0.00;
		}
		else missed++;
	}
	else { h = h1; t = t1; missed = 0; }
	//Serial.println("- temperature read : " + String(t));
	//Serial.println("- humidity read : " + String(h));
}

void askTime() {

	if (TCP_Client.connected()) {
		TCP_Client.setNoDelay(1);
		TCP_Client.println("Device:" + Devicename + ";get:1;");
		//Serial.print("- data stream: ");	Serial.println("Device:" + Devicename + ";" + "time:" + result + ";" + "signal:" + String(WiFi.RSSI()) + ";" + data);//Send sensor data

	}

}


void Send_Log_To_Server() {
	   	
	if (TCP_Client.connected()) {
		TCP_Client.setNoDelay(1);
		TCP_Client.println("Device:" + Devicename + ";get:2;" + String(millis()) + "," +
			String(WiFi.RSSI()) + "," + String(t) + "," + String(h)+";");
		
	}

}
void Send_Request_To_Server() {

	if (TCP_Client.connected()) {
		TCP_Client.setNoDelay(1);
		TCP_Client.println("Device:" + Devicename + ";time:" + String(millis()) + ";signal:" +
			String(WiFi.RSSI()) + ";temp:" + String(t) + ";humid:" + String(h) + ";");
			sendRequestToServer.period = 30000;
	}

}
//====================================================================================

void Check_WiFi_and_Connect_or_Reconnect() {
	if (WiFi.status() != WL_CONNECTED) {

		TCP_Client.stop();                                  //Make Sure Everything Is Reset
		WiFi.disconnect();
		//Serial.println("Not Connected...trying to connect...");
		//delay(50);
		WiFi.mode(WIFI_STA);                                // station (Client) Only - to avoid broadcasting an SSID ??
		WiFi.begin(ssid, password);                         // the SSID that we want to connect to
		for (int i = 0; i < 10; ++i) {
			if (WiFi.status() != WL_CONNECTED) {
				delay(500);
				//Serial.print(".");
			}
			else {
				//Serial.println("Connected To      : " + String(WiFi.SSID()));
				//Serial.println("Signal Strenght   : " + String(WiFi.RSSI()) + " dBm");
				//Serial.print("Server IP Address : ");
				//Serial.println(TCP_Server);
				//Serial.print("Device IP Address : ");
				//Serial.println(WiFi.localIP());
				// conecting as a client -------------------------------------
				Tell_Server_we_are_there();
				break;
			}

		}
	}
}

//====================================================================================

void Tell_Server_we_are_there() {
	
	if (TCP_Client.connect(TCP_Server, TCPPort)) {
		TCP_Client.println("<" + Devicename + "-CONNECTED>");
		TCP_Client.println(fieldsInLogMes);
	}
	TCP_Client.setNoDelay(1);                                     // allow fast communication?
}
void process_Msessage(String message) {
	int empty;//only for compatibility of get_field_value()
	unsigned long value;
	if (get_field_value(message, "time:", &value, &empty)) {
		if (value) {
			UNIXtime = value;
			lastNTPResponse = millis();
			askTimeTask.ignor = true;
			Time_set.SetCurrentTime(UNIXtime);
		}
	}
	else if (message.indexOf("ON") != -1) {
		Serial.println("LED on");
		digitalWrite(BUZPIN, HIGH);
	}
	else if (message.indexOf("OFF") != -1) {
		Serial.println("LED off");
		digitalWrite(BUZPIN, LOW);
	}
}
bool get_field_value(String Message, String field, unsigned long* value, int* index) {
	int fieldBegin = Message.indexOf(field) + field.length();
		int check_field = Message.indexOf(field);
		int ii = 0;
		*value = 0;
		*index = 0;
		bool indFloat = false;
		if (check_field != -1) {
			int filedEnd = Message.indexOf(';', fieldBegin);
			if (filedEnd == -1) { return false; }
			int i = 1;
			char ch = Message[filedEnd - i];
			while (ch != ' ' && ch != ':') {
				if (isDigit(ch)) {
					int val = ch - 48;
					if (!indFloat)ii = i - 1;
					else ii = i - 2;
					*value = *value + ((val * pow(10, ii)));
				}
				else if (ch == '.') { *index = i - 1; indFloat = true; }
				i++;
				if (i > (filedEnd - fieldBegin + 1) || i > 10)break;
				ch = Message[filedEnd - i];
			}

		}
		else return false;
	return true;
}
//====================================================================================

void loop() {
	if(lasTimeReadSensor.check()) readDHTSensor();
	if (refreshOLED.check()) {
		Time_set.updateDay();
		showOnOled("tem=" + String(t) +" "+String(WiFi.RSSI()),
			"hum=" + String(h)+ " "+String(Time_set.NowHour) +":" + String(Time_set.NowMin), 
			String(Time_set.NowYear) +"."+String(Time_set.NowMonth) + "." + String(Time_set.NowDay) + 
			" " +  String(Time_set.NowSec), contrast);
	}
	if (Time_set.NowHour>=0 && Time_set.NowHour<7 || Time_set.NowHour==23)contrast = 1;
	else contrast = 255;
	Check_WiFi_and_Connect_or_Reconnect();          // Checking For Connection
	if(sendLogToSrver.check())  Send_Log_To_Server();
	if (sendRequestToServer.check()) Send_Request_To_Server();
	if (!TCP_Client.connected())  Tell_Server_we_are_there();
	if (askTimeTask.check()) askTime();
	if (TCP_Client.connected() && TCP_Client.available() > 0) process_Msessage(TCP_Client.readStringUntil('\r'));
	actualtime = UNIXtime + (millis() - lastNTPResponse) / 1000;
	yield();
}
