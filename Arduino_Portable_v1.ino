#include <climits>
#include <stdlib.h>
#include <optional>
#include <vector>
#include <deque>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>

#include <esp_pthread.h>
#include <WiFi.h>
//	#include <DNSServer.h>
#include <PsychicHttp.h>
#include <PsychicHttpServer.h>
#include <PsychicHttpsServer.h>
#include <PsychicRequest.h>
#include <PsychicResponse.h>
#include <PsychicStreamResponse.h>
#include <RTClib.h>
#include <Adafruit_SSD1306.h>
#include <SD.h>

#include <Fonts/FreeSans9pt7b.h>
#define FONT_0 FreeSans9pt7b
#define FONT_0_OFFSET 16
#include <Fonts/FreeSerif9pt7b.h>
#define FONT_1 FreeSerif9pt7b
#define FONT_1_OFFSET 16

#include "config.h"

/* *************************************************************************** / ************************************ */
/* OLED display */

static Adafruit_SSD1306 Monitor(128, 64);
static void redraw_display(bool);

/* *************************************************************************** / ************************************ */
/* Sensor */

#if defined(ENABLE_SENSOR_SHT40)
	#include <Adafruit_Sensor.h>
	#include <Adafruit_SHT4x.h>

	static Adafruit_SHT4x SHT4x;
#endif

struct DataField {
	char const *name;
	char const *title;
	char const *unit;
};

namespace Sensor {
	enum Identifier {
		SHT40 = 1,
		number
	};

	static std::optional<int> parameters[number] = DEFAULT_SENSORS;

	struct Model {
		char const *name;
		std::vector<DataField> elements;
	};

	Model const models[number] = {
		[0] = {"(PLACEHOLDER)", {}},
		[SHT40] = {
			"SHT40",
			{
				{"SHT40_temperature", "Temperature", "\u2103"},
				{"SHT40_humidity", "Humidity", "%"}
			}
		}
	};

	void setup(void) {
		/* Turn off SPI of LoRa */
		pinMode(LORA_CS, OUTPUT);
		digitalWrite(LORA_CS, HIGH);

		#if defined(ENABLE_SENSOR_SHT40)
			if (parameters[SHT40]) {
				while (!SHT4x.begin()) {
					Serial.println("ERROR: SHT40 not found");
					Monitor.println("No SHT40");
					Monitor.display();
					delay(reinitialize_interval);
				}
				SHT4x.setPrecision(SHT4X_HIGH_PRECISION);
				SHT4x.setHeater(SHT4X_NO_HEATER);
				Serial.println("SHT40 found");
				Monitor.println("OK SHT40");
				Monitor.display();
			}
		#endif
	}
}

/* *************************************************************************** / ************************************ */
/* Locks and status */

static std::mutex mutex_I2C;
static std::mutex mutex_SD;
#define DISPLAY_LOCK(lock) std::lock_guard<std::mutex> lock(mutex_I2C)
#define DEVICE_LOCK(lock) std::lock_guard<std::mutex> lock(mutex_I2C)
#define SDCARD_LOCK(lock) std::lock_guard<std::mutex> lock(mutex_SD)
static std::mutex mutex_data;
#define DATA_LOCK(lock) std::lock_guard<std::mutex> lock(mutex_data)

static std::mutex wait_measure_mutex;
static std::condition_variable wait_measure_condition;

static bool need_save = false;
static bool need_reboot = false;

/* *************************************************************************** / ************************************ */
/* Real-time clock */

namespace Clock {
	static bool synchronized = false;
	static RTC_Millis internal;
	static bool internal_available = false;
	static RTC_DS3231 external;
	static bool external_available = false;

	static void setup(void) {
		external_available = external.begin();
		if (external_available) {
			Serial.println("Clock found");
			Monitor.println("OK clock");
		}
		else {
			Serial.println("Clock not found");
			Monitor.println("No clock");
		}
	}

	static bool available(void) {
		return external_available || internal_available;
	}

	static void set_time(DateTime const *const datetime) {
		if (external_available) {
			DEVICE_LOCK(device_lock);
			external.adjust(*datetime);
		}
		else {
			internal.adjust(*datetime);
			internal_available = true;
		}
		synchronized = true;
	}

	static DateTime get_time(void) {
		if (external_available) {
			DEVICE_LOCK(device_lock);
			return external.now();
		}
		else
			return internal.now();
	}

	static String show_time(DateTime const *const datetime) {
		if (datetime->isValid())
			return datetime->timestamp();
		else
			return String("?");
	}

	static DateTime round_up_time(DateTime const *const datetime, unsigned long int const interval = measure_interval) {
		uint64_t shifted = (uint64_t)datetime->unixtime() * 1000 + (interval >> 1);
		return DateTime((shifted - shifted % interval) / 1000);
	}
}

/* *************************************************************************** / ************************************ */
/* Data */

class Data {
protected:
	DateTime time;
	DateTime device_time;
	#if defined(ENABLE_SENSOR_SHT40)
		float SHT40_temperature;
		float SHT40_humidity;
	#endif
public:
	static std::vector<DataField> fields;
	static void update_fields(void);
	static void setup(void);

	Data(void);
	DateTime get_device_time(void) const;
	String show_time(void) const;
	String to_CSV(void) const;
	void display(void) const;
	void measure(void);
};

std::vector<DataField> Data::fields;

void Data::update_fields(void) {
	fields.clear();
	fields.push_back(DataField{"time", nullptr, nullptr});
	fields.push_back(DataField{"device_time", nullptr, nullptr});
	fields.push_back(DataField{"clock_synchronized", nullptr, nullptr});
	for (size_t i = 0; i < Sensor::number; ++i)
		if (Sensor::parameters[i])
			for (DataField const &field: Sensor::models[i].elements)
				fields.push_back(field);
}

void Data::setup(void) {
	update_fields();
}

Data::Data(void) {
	if (Clock::available())
		device_time = Clock::get_time();
	else
		device_time = DateTime(0, 0, 0) + TimeSpan(millis() / 1000);
	time = Clock::round_up_time(&device_time);
}

DateTime Data::get_device_time(void) const {
	return device_time;
}

String Data::show_time(void) const {
	return Clock::show_time(&time);
}

String Data::to_CSV(void) const {
	String s = show_time() + ',' + Clock::show_time(&device_time) + ',' + Clock::synchronized;
	#if defined(ENABLE_SENSOR_SHT40)
		if (Sensor::parameters[Sensor::SHT40])
			s = s + ',' + SHT40_temperature + ',' + SHT40_humidity;
	#endif
	return s;
}

void Data::display(void) const {
	#if defined(ENABLE_SENSOR_SHT40)
		if (Sensor::parameters[Sensor::SHT40]) {
			Monitor.print(SHT40_temperature, 1);
			Monitor.println("C");
			Monitor.print(SHT40_humidity, 1);
			Monitor.println("%");
		}
	#endif
}

void Data::measure(void) {
	DEVICE_LOCK(device_lock);
	#if defined(ENABLE_SENSOR_SHT40)
		if (Sensor::parameters[Sensor::SHT40]) {
			sensors_event_t temperature_event, humidity_event;
			SHT4x.getEvent(&humidity_event, &temperature_event);
			SHT40_temperature = temperature_event.temperature * calibration_slope + calibration_intercept;
			SHT40_humidity = humidity_event.relative_humidity;
		}
	#endif
}

/* *************************************************************************** / ************************************ */

namespace WEB {
	static esp_err_t gps_upload_handle(PsychicRequest *, PsychicResponse *);
}

class GPS {
protected:
	DateTime time;
	DateTime browser_time;
	DateTime position_time;
	double latitude;
	double longitude;
	double altitude;
public:
	static std::vector<DataField> const fields;

	GPS(void) :
		time((uint32_t)0), browser_time((uint32_t)0), position_time((uint32_t)0),
		latitude(NAN), longitude(NAN), altitude(NAN)
		{}
	String to_CSV(void) const;

	friend esp_err_t WEB::gps_upload_handle(PsychicRequest *, PsychicResponse *);
};

String GPS::to_CSV(void) const {
	return Clock::show_time(&time)
		+ ',' + Clock::show_time(&browser_time)
		+ ',' + Clock::show_time(&position_time)
		+ ',' + String(latitude,  7)
		+ ',' + String(longitude, 7)
		+ ',' + String(altitude,  7);
}

std::vector<DataField> const GPS::fields{
	{"time", nullptr, nullptr},
	{"browser_time", nullptr, nullptr},
	{"position_time", nullptr, nullptr},
	{"latitude", "Latitude", "\u00B0"},
	{"longitude", "Longitude", "\u00B0"},
	{"altitude", "Altitude", "m"}
};

/* *************************************************************************** / ************************************ */
/* SD card */

namespace SD_card {
	static char const setting_filename[] = "/setting.txt";
	static char const data_filename[] = "/data.csv";
	static char const gps_filename[] = "/gps.csv";
	static String data_header;
	static String gps_header;
	static bool exist;

	class Parser {
	protected:
		int number;
		bool sign;
		unsigned int state : 2;
	public:
		Parser(void);
		void reset(void);
		bool read(char character);
		std::optional<int> result(void) const;
	};

	Parser::Parser(void) {
		reset();
	}

	void Parser::reset(void) {
		number = 0;
		sign = false;
		state = 0;
	}

	std::optional<int> Parser::result(void) const {
		if (state != 2)
			return std::nullopt;
		else if (sign)
			return std::optional(-number);
		else
			return std::optional(number);
	}

	bool Parser::read(char character) {
		if (character >= '0' && character <= '9') {
			number = number * 10 + (character - '0');
			state = 2;
			return true;
		}
		else if (character == '-' && state == 0) {
			sign = !sign;
			state = 1;
			return true;
		}
		else
			return false;
	}

	static void save_sensors(File *const file) {
		bool first_pair = true;
		for (size_t i = 0; i < Sensor::number; ++i) {
			std::optional<int> const parameter = Sensor::parameters[i];
			if (parameter) {
				if (first_pair)
					first_pair = false;
				else
					file->print(',');
				file->print(i);
				if (*parameter) {
					file->print(':');
					file->print(*parameter);
				}
			}
		}
		file->println();
	}

	static void load_sensors(File *const file) {
		for (std::optional<int> &parameter: Sensor::parameters)
			parameter = std::nullopt;
		String const line = file->readStringUntil('\n');
		char const *p = line.c_str();
		if (*p) {
			int key = 0;
			int value = 0;
			bool rhs = false;
			Parser parser;
			for (;;) {
				char const c = *p;
				if (!c || c == ',') {
					std::optional const x = parser.result();
					if (x) {
						if (rhs)
							value = x.value();
						else {
							key = x.value();
							value = 0;
						}
						if (key < Sensor::number)
							Sensor::parameters[key] = value;
					}
					rhs = false;
					parser.reset();
					if (!c) break;
				}
				else if (c == ':') {
					std::optional const x = parser.result();
					if (!rhs && x)
						key = x.value();
					rhs = true;
					parser.reset();
				}
				else
					parser.read(c);
				++p;
			}
		}
		Data::update_fields();
	}

	static void save_settings(void) {
		if (!exist) return;
		SDCARD_LOCK(sdcard_lock);
		File file = SD.open(setting_filename, "w", true);
		if (!file) {
			Serial.println("ERROR: failed to open setting file");
			return;
		}
		save_sensors(&file);
		file.println(campaign_name);
		file.println(organisation_name);
		file.println(device_name);
		file.println(measure_interval / 1000);
		file.println(int(use_AP_mode));
		file.println(AP_SSID);
		file.println(AP_PASS);
		file.println(STA_SSID);
		file.println(STA_PASS);
		file.println(monitor_URL);
		file.println(upload_URL);
		file.println(upload_username);
		file.println(upload_password);
		file.println(calibration_slope);
		file.println(calibration_intercept);
		file.close();
	}

	static bool load_settings(void) {
		char *e;
		String s;
		unsigned long int u;
		float f;

		SDCARD_LOCK(sdcard_lock);
		File file = SD.open(setting_filename, "r", false);
		if (!file) {
			Serial.println("Failed to open setting file");
			return false;
		}

		/* Active sensors */
		load_sensors(&file);

		/* Campaign name */
		campaign_name = file.readStringUntil('\n');
		campaign_name.trim();

		/* Organisation name */
		organisation_name = file.readStringUntil('\n');
		organisation_name.trim();

		/* Device name */
		device_name = file.readStringUntil('\n');
		device_name.trim();

		/* Measure interval */
		s = file.readStringUntil('\n');
		s.trim();
		u = strtoul(s.c_str(), &e, 10);

		/* AP mode */
		if (!*e && u >= measure_interval_lowerbound && u <= measure_interval_upperbound)
			measure_interval = u * 1000;
		s = file.readStringUntil('\n');
		s.trim();
		u = strtoul(s.c_str(), &e, 10);
		if (!*e) use_AP_mode = bool(u);

		/* AP SSID */
		AP_SSID = file.readStringUntil('\n');
		AP_SSID.trim();

		/* AP PASS */
		AP_PASS = file.readStringUntil('\n');
		AP_PASS.trim();

		/* STA SSID */
		STA_SSID = file.readStringUntil('\n');
		STA_SSID.trim();

		/* STA PASS */
		STA_PASS = file.readStringUntil('\n');
		STA_PASS.trim();

		/* Monitor URL */
		monitor_URL = file.readStringUntil('\n');
		monitor_URL.trim();

		/* Upload URL */
		upload_URL = file.readStringUntil('\n');
		upload_URL.trim();

		/* Upload username */
		upload_username = file.readStringUntil('\n');
		upload_username.trim();

		/* Upload password */
		upload_password = file.readStringUntil('\n');
		upload_password.trim();

		/* Calibration slope */
		s = file.readStringUntil('\n');
		s.trim();
		f = strtof(s.c_str(), &e);
		if (!*e && f >= calibration_slop_lowerbound && f <= calibration_slop_upperbound)
			calibration_slope = f;

		/* Temperature intercept */
		s = file.readStringUntil('\n');
		s.trim();
		f = strtof(s.c_str(), &e);
		if (!*e && f >= calibration_intercept_lowerbound && f <= calibration_intercept_upperbound)
			calibration_intercept = f;

		file.close();
		return true;
	}

	static void setup(void) {
		data_header = Data::fields[0].name;
		if (Data::fields[0].unit)
			data_header = data_header + " (" + Data::fields[0].unit + ')';
		for (size_t i = 1; i < Data::fields.size(); ++i) {
			data_header = data_header + ',' + Data::fields[i].name;
			if (Data::fields[i].unit)
				data_header = data_header + " (" + Data::fields[i].unit + ')';
		}
		gps_header = GPS::fields[0].name;
		if (GPS::fields[0].unit)
			gps_header = gps_header + " (" + GPS::fields[0].unit + ')';
		for (size_t i = 1; i < GPS::fields.size(); ++i) {
			gps_header = gps_header + ',' + GPS::fields[i].name;
			if (GPS::fields[i].unit)
				gps_header = gps_header + " (" + GPS::fields[i].unit + ')';
		}

		//	pinMode(SD_MISO, INPUT_PULLUP);
		SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
		exist = SD.begin(SD_CS, SPI);
		if (exist) {
			Serial.println("SD card found");
			Monitor.println("OK SD card");
			if (digitalRead(reset_pin) == HIGH)
				Serial.println("Setting is not loaded because of hardware switch");
			else if (!load_settings())
				Serial.println("Failed to load settings");
		}
		else {
			Serial.println("SD card not found");
			Monitor.println("No SD card");
		}
	}
}

/* *************************************************************************** / ************************************ */
/* Measurement */

static size_t const records_max_size = 60;
static std::deque<struct Data> data_records;
static std::deque<struct GPS>  gps_records;

static DateTime measure(void) {
	Data data;
	data.measure();

	String const data_string = data.to_CSV();
	Serial.print("INFO: Measure ");
	Serial.println(data_string);

	{
		DATA_LOCK(data_lock);
		if (data_records.size() >= records_max_size)
			data_records.pop_front();
		data_records.push_back(data);
	}

	if (SD_card::exist) {
		SDCARD_LOCK(sdcard_lock);
		File file = SD.open(SD_card::data_filename, "a", true);
		try {
			if (!file.position())
				file.println(SD_card::data_header);
			file.println(data_string);
		}
		catch (...) {
			Serial.println("ERROR: failed to write weather data into SD card");
		}
		file.close();
	}

	redraw_display(true);
	return data.get_device_time();
}

static void wait_to_measure(DateTime const now) {
	unsigned int const t1 = measure_interval - (uint64_t)now.unixtime() * 1000 % measure_interval;
	unsigned int const t2 = t1 < measure_interval >> 1 ? t1 + measure_interval : t1;
	std::unique_lock<std::mutex> wait_lock(wait_measure_mutex);
	wait_measure_condition.wait_for(wait_lock, std::chrono::duration<unsigned int, std::milli>(t2));
}

static void measure_thread(void) {
	wait_to_measure(Clock::get_time());
	for (;;)
		try {
			wait_to_measure(measure());
		}
		catch (...) {
			Serial.println("ERROR: exception in measurement");
		}
}

/* *************************************************************************** / ************************************ */
/* WiFi */

namespace WIFI {
	//	static DNSServer DNSd;

	static void handle_event(WiFiEvent_t const event) {
		switch (event) {
		case ARDUINO_EVENT_WIFI_READY:
			Serial.println("WiFi interface ready");
			break;
		case ARDUINO_EVENT_WIFI_SCAN_DONE:
			Serial.println("Completed scan for access points");
			break;
		case ARDUINO_EVENT_WIFI_STA_START:
			Serial.println("WiFi client started");
			break;
		case ARDUINO_EVENT_WIFI_STA_STOP:
			Serial.println("WiFi clients stopped");
			break;
		case ARDUINO_EVENT_WIFI_STA_CONNECTED:
			Serial.println("Connected to access point");
			break;
		case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
			Serial.println("Disconnected from WiFi access point");
			break;
		case ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE:
			Serial.println("Authentication mode of access point has changed");
			break;
		case ARDUINO_EVENT_WIFI_STA_GOT_IP:
			Serial.print("Obtained IP address: ");
			Serial.println(WiFi.localIP());
			break;
		case ARDUINO_EVENT_WIFI_STA_LOST_IP:
			Serial.println("Lost IP address and IP address is reset to 0");
			break;
		case ARDUINO_EVENT_WPS_ER_SUCCESS:
			Serial.println("WiFi Protected Setup (WPS): succeeded in enrollee mode");
			break;
		case ARDUINO_EVENT_WPS_ER_FAILED:
			Serial.println("WiFi Protected Setup (WPS): failed in enrollee mode");
			break;
		case ARDUINO_EVENT_WPS_ER_TIMEOUT:
			Serial.println("WiFi Protected Setup (WPS): timeout in enrollee mode");
			break;
		case ARDUINO_EVENT_WPS_ER_PIN:
			Serial.println("WiFi Protected Setup (WPS): pin code in enrollee mode");
			break;
		case ARDUINO_EVENT_WIFI_AP_START:
			Serial.println("WiFi access point started");
			break;
		case ARDUINO_EVENT_WIFI_AP_STOP:
			Serial.println("WiFi access point  stopped");
			break;
		case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
			Serial.println("Client connected");
			break;
		case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
			Serial.println("Client disconnected");
			break;
		case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
			Serial.println("Assigned IP address to client");
			break;
		case ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED:
			Serial.println("Received probe request");
			break;
		case ARDUINO_EVENT_WIFI_AP_GOT_IP6:
			Serial.println("AP IPv6 is preferred");
			break;
		case ARDUINO_EVENT_WIFI_STA_GOT_IP6:
			Serial.println("STA IPv6 is preferred");
			break;
		case ARDUINO_EVENT_ETH_GOT_IP6:
			Serial.println("Ethernet IPv6 is preferred");
			break;
		case ARDUINO_EVENT_ETH_START:
			Serial.println("Ethernet started");
			break;
		case ARDUINO_EVENT_ETH_STOP:
			Serial.println("Ethernet stopped");
			break;
		case ARDUINO_EVENT_ETH_CONNECTED:
			Serial.println("Ethernet connected");
			break;
		case ARDUINO_EVENT_ETH_DISCONNECTED:
			Serial.println("Ethernet disconnected");
			break;
		case ARDUINO_EVENT_ETH_GOT_IP:
			Serial.println("Obtained IP address");
			break;
		default:
			Serial.print("Unknown WiFi event ");
			Serial.println(event);
			break;
		}
	}

	static char const *status_message(signed int const WiFi_status) {
		switch (WiFi_status) {
		case WL_NO_SHIELD:
			return "WiFi no shield";
		case WL_IDLE_STATUS:
			return "WiFi idle";
		case WL_NO_SSID_AVAIL:
			return "WiFi no SSID";
		case WL_SCAN_COMPLETED:
			return "WiFi scan completed";
		case WL_CONNECTED:
			return "WiFi connected";
		case WL_CONNECT_FAILED:
			return "WiFi connect failed";
		case WL_CONNECTION_LOST:
			return "WiFi connection lost";
		case WL_DISCONNECTED:
			return "WiFi disconnected";
		default:
			return "WiFi Status: " + WiFi_status;
		}
	}

	static signed int WiFi_check_status(void) {
		static signed int last_status = WL_NO_SHIELD;
		signed int const status = WiFi.status();
		if (status != last_status) {
			last_status = status;
			Serial.println(status_message(status));
			if (status == WL_CONNECTED) {
				String const SSID = WiFi.SSID();
				Serial.print("WiFi SSID: ");
				Serial.println(WiFi.SSID());
				Serial.print("IP address: ");
				WiFi.localIP().printTo(Serial);
				Serial.println();
			}
		}
		return status;
	}

	static void thread(void) {
		for (;;)
			try {
				delay(WiFi_check_interval);
				WiFi_check_status();
			}
			catch (...) {
				Serial.println("ERROR: exception in WiFi checking");
			}
	}

	static void setup(void) {
		WiFi.disconnect();
		WiFi.onEvent(handle_event);
		WiFi.setHostname("WeatherStation");

		if (use_AP_mode) {
			/* WiFi access-point */
			WiFi.mode(WIFI_AP);
			//	IPAddress my_IP_address = IPAddress(8, 8, 8, 8);
			//	WiFi.softAPConfig(my_IP_address, my_IP_address, IPAddress(255, 255, 255, 0));
			while (!WiFi.softAP(AP_SSID.c_str(), AP_PASS, 1, 0, 4)) {
				Serial.println("ERROR: failed to create soft AP");
				Monitor.println("ERROR: WiFi AP");
				Monitor.display();
				delay(reinitialize_interval);
			}
			Serial.print("WiFi SSID: ");
			Serial.println(WiFi.softAPSSID());
			Serial.print("IP address: ");
			WiFi.softAPIP().printTo(Serial);
			Serial.println();
			Monitor.print("AP:");
			Monitor.println(WiFi.softAPSSID());
			WiFi.softAPIP().printTo(Monitor);
			Monitor.println();
			Monitor.display();

			/* DNS server */
			//	static uint16_t const DNS_port = 53;
			//	static String const DNS_domain("*");
			//	while (!DNSd.start(DNS_port, DNS_domain, my_IP_address)) {
			//		Serial.println("ERROR: failed to create DNS server");
			//		Monitor.println("ERROR: DNS server");
			//		delay(reinitialize_interval);
			//	}
		}
		else {
			/* WiFi stationary */
			WiFi.mode(WIFI_STA);
			WiFi.begin(STA_SSID, STA_PASS);
			while (WiFi.status() == WL_NO_SHIELD) {
				Serial.println("ERROR: no WiFi shield");
				Monitor.println("No WiFi shield");
				Monitor.display();
				delay(reinitialize_interval);
			}
			/* Spawn WiFi thread */
			set_pthread_stack_size(4096);
			std::thread(thread).detach();
		}
	}
}

/* *************************************************************************** / ************************************ */
/* Web server */

namespace WEB {
	static PsychicHttpServer HTTPd;
	static PsychicHttpsServer HTTPSd;

	static PROGMEM char const tls_key[] =
R"(-----BEGIN PRIVATE KEY-----
MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQDYS583x94YS1lJ
4RxV0prgBQD79ldJjhR+XejEpq/ht+x7LhileW0So9bc+1xcIkUby4VMiSoTx7Ty
Kt2Z20WjKjxjVQAPTQ2Zbz4LxbODmKwHfrAcnMm1BFymSEQRzfoQROcmaPEvUlMh
b1mrPa3HyP5vcPjTvAa7ApLLsbeDDg8kwgWFktsuteRRrPvQ6lA19M2KR1oAuyil
Ci/ynweJIR9Mzqu/V52wZ0PZQX5JITcCUpvhVQy+dukCNeD2EEVxI6op0Q7vTHSa
OwnCVQmEcApwd6VA5l+bkIuYZu8Bo/OQRbVSpmo2m21cdniBjIHlU4RjEGf3UK9k
cJ65WFT/AgMBAAECggEBALDaVFs1ryFKKr2/tH1v8HaPYNij+YcJBzSz8GkqqdDz
pAasEDbs7AQ7tqmFVWV4F+28IUgNNzxpJEiGSB9PLMdW9314uM3KAP6d+KuDgV5u
bOrL6Y/bmwnJgT/tAstUEc1PqXi8gchhldtWwojDq94ZOAFC7BGkQhSS7BhlPVmH
mlqTwdDRouRZPemNPHbyWggJNNyUqlrBLXDMeyo8DOalMhHJ9WggNxkKRyxTrNBv
o0QFI3hdbdAx4AfYAtm8R5NdoOVe9uc2kL72LqSOfi7h7XinEzlNfu9y17T/wJR5
OkZyOIIn/vjpLWOZdkxxu7D56bLjbRe3fcAwsSCIzjECgYEA8oBkiEdvDcpJw4HO
xnZonM4dE6rc5aXwF2vBxBxR7aoRe/yEI5pUd61NEsjn6QjNuUVE3zDggy3bN5yj
pmXYi5gR0O9beAhL1fdwfph0LnOaIS2jl34iVLv/6mGK1s5EiedwZzzsSdGpO26u
JQ0PkYQ7jD0AoxVp0WDwppuDnf0CgYEA5FXLy/iTDxwci6imXtbd/q9/TWE1qP6V
LXorYnNxLJ+7Wirdd+awRkDbh9su703afc6YHueetgtBFZbMCBY/ABsHpJFDx4Gl
+V6zIHjN+NdfdZ4JBCgPyB6R55EKN52VhIKGouaDw6bYKC3meL9jMDYIRviuSD65
KPGECIGwEasCgYBDTakZPaIv1J3mWgeWg1SDeJ0PUVOflQ9uoKSVljqS2KmjnLDb
5MBeusVyWjorLhtSuUvlGf6lybtW0u2EiC2yiJEhSN09EihiCRu6tvs/zSvQ24bU
y9ghZlAfr9TFy1ewYoCK1pjJ4Bu0+AHzHI3emDGiuWeM26uTxfDkfLLpzQKBgCdO
pcC9UAOf4UIhjFJzRtAbQhz+CRDIksG7cFCIcwktjkEdc/a6HcpaS/B9SP0lN+HE
eOeJFAdetJuU1BboTXwlKxGneDWWGg5twQRsB3k5ClPjGsY+Z0kaCiAFFe8xD5Y6
KhdM+43o4Pk5vZ03xUl9Y7tkAAyrz5A+023rdXX1AoGATYT7lNR8iHnPPpYML0DT
dgWtawEzilbX5xV0Ir4/vtpHHOSLW4BEWGwsIYlnF/0OoDW46hhauH/RYK1YXhxn
acrVR6rScHJTw9lRq2kTA6zse0IEWFmRoGCVpuzZKV6h0xVH3e/ySqJ77GpMY4CZ
2OMdeiV8rFzB32aRRmARp1s=
-----END PRIVATE KEY-----)";

	static PROGMEM char const tls_cert[] =
R"(-----BEGIN CERTIFICATE-----
MIIDDTCCAfWgAwIBAgIUQIhTsfe1swM0cbHTX7yRMbTs7B8wDQYJKoZIhvcNAQEL
BQAwFjEUMBIGA1UEAwwLMTkyLjE2OC40LjEwHhcNMjUwMzE0MDkxNTU5WhcNMzUw
MzEyMDkxNTU5WjAWMRQwEgYDVQQDDAsxOTIuMTY4LjQuMTCCASIwDQYJKoZIhvcN
AQEBBQADggEPADCCAQoCggEBANhLnzfH3hhLWUnhHFXSmuAFAPv2V0mOFH5d6MSm
r+G37HsuGKV5bRKj1tz7XFwiRRvLhUyJKhPHtPIq3ZnbRaMqPGNVAA9NDZlvPgvF
s4OYrAd+sBycybUEXKZIRBHN+hBE5yZo8S9SUyFvWas9rcfI/m9w+NO8BrsCksux
t4MODyTCBYWS2y615FGs+9DqUDX0zYpHWgC7KKUKL/KfB4khH0zOq79XnbBnQ9lB
fkkhNwJSm+FVDL526QI14PYQRXEjqinRDu9MdJo7CcJVCYRwCnB3pUDmX5uQi5hm
7wGj85BFtVKmajabbVx2eIGMgeVThGMQZ/dQr2RwnrlYVP8CAwEAAaNTMFEwHQYD
VR0OBBYEFPZZCszrdTQR7llJGkwi6KXO3IQdMB8GA1UdIwQYMBaAFPZZCszrdTQR
7llJGkwi6KXO3IQdMA8GA1UdEwEB/wQFMAMBAf8wDQYJKoZIhvcNAQELBQADggEB
AKnnpPxOIrjFKHKId+l7mfhQAl2iUVCQ9pliILVqZ31VJQ6P9in5KnWKxykU7GJv
RYOSYP4O6WtexNDvn62hmvJVIuxxdKZcxrgXhHvvZ2+MI3yINuvtON+DimF65Eve
AFXWbkpGdw7s5ZBkribr01oChkgd0Z5X1/T/R7gUlPk+KnUF8q3K/tDbfbEstdbr
q8l5Skz5Y64ymusdMYPYUJFxjse0pktYDun5KQDDSPt7X4H+LNsuFvzFbhY8a/dO
5zKPulRqJDk5eGA2x3RXUsHGrIs9F0IvtuJ4hQ0azHPOsjLcRdxoUg4VRiwDEmBa
MrWoZgP+8FAGtQjTN8KiRUc=
-----END CERTIFICATE-----)";

	static String const XHTML_content_type = "application/xhtml+xml; charset=UTF-8";

	static String javascript_escape(String const &string) {
		String result;
		for (char const c: string)
			switch (c) {
			case '\"':
				result.concat("\\\"");
				break;
			case '\'':
				result.concat("\\\'");
				break;
			default:
				result.concat(c);
			}
		return result;
	}

	static PROGMEM char const home_html_1[] =
R"HTML(<html xmlns='http://www.w3.org/1999/xhtml'>
<head>
<meta content-type='application/xhtml+xml; charset=UTF-8' />
<meta charset='UTF-8' />
<meta name='viewport' content='width=device-width, initial-scale=1' />
<title>Weather data</title>
<link rel='stylesheet' type='text/css' href='style.css' />
)HTML";

	static PROGMEM char const home_html_2[] =
R"HTML(<link
	rel='stylesheet'
	href='https://unpkg.com/leaflet@1.9.4/dist/leaflet.css'
	integrity='sha256-p4NxAoJBhIIN+hmNHrzRCf9tD/miZyoHS5obTRR9BMY='
	crossorigin='' />
<script
	type='text/javascript'
	src='https://unpkg.com/leaflet@1.9.4/dist/leaflet.js'
	integrity='sha256-20nQCchB9co0qIjJZRGuk2/Z9VM+kNiyxNV1lvTlZBo='
	crossorigin=''
></script>
)HTML";

	static PROGMEM char const home_html_3[] =
R"HTML(</head>
<body>
<noscript>Javascript is required for this web page.</noscript>
<script type='text/javascript'>
	(function(p){document.readyState!=="loading"?p():document.addEventListener("DOMContentLoaded",p)})(function(p){
		window.Application = {
			operator: location.pathname === "/operator",
)HTML";

	static PROGMEM char const home_html_4[] =
R"HTML(
		};
		if (typeof Uint8Array.prototype.toBase64 !== "function") {
			/* polyfill for compatible */
			Uint8Array.prototype.toBase64 = function () {
				return btoa(Array.from(this).map(function (c) {return String.fromCharCode(c);}).join(""));
			}
		}
		return function () {
			import("./script.js").catch(p);
		};
	}(function (SD_load_error) {
		"use strict";
		console.log("Failed to load script from SD card:", SD_load_error);
		var GPS_watch = false;
		function $T(string) {
			return document.createTextNode(string);
		}
		function $E(name) {
			return document.createElementNS(document.documentElement.namespaceURI, name);
		}
		function c_(parent, child) {
			return parent.appendChild(child);
		}
		function s_(element, name, value) {
			return element.style[name] = value;
		}
		function a_(element, name, value) {
			return element.setAttribute(name, value);
		}
		function string_from_Date(value, seperator = " ") {
			var date = new Date(value);
			return (
				date.getFullYear().toString()
					+ "-"
					+ (date.getMonth() + 1).toString().padStart(2, "0")
					+ "-"
					+ date.getDate().toString().padStart(2, "0")
					+ seperator
					+ date.getHours().toString().padStart(2, "0")
					+ ":"
					+ date.getMinutes().toString().padStart(2, "0")
					+ ":"
					+ date.getSeconds().toString().padStart(2, "0")
			);
		}
		var MILLISECONDS_FROM_1970_TO_2000 = 946684800000; /* = Date.UTC(2000, 0, 1, 0, 0, 0, 0) */
		document.body.textContent = "";
		void function () {
			var $p;
			$p = $E("p");
			c_($p, $T("Campaign: "));
			c_($p, $T(Application.campaign));
			c_($p, $T(" | Organisation: "));
			c_($p, $T(Application.organisation));
			c_($p, $T(" | Device: "));
			c_($p, $T(Application.device));
			c_(document.body, $p);
		}
		void function () {
			var $p, $a;
			function style_$a() {
				s_($a, "margin", "1ex");
				s_($a, "border", "solid thin gray");
				s_($a, "padding", "1ex");
			}
			$p = $E("p");
			s_($p, "display", "flex");
			s_($p, "flex-flow", "row wrap");
			s_($p, "text-align", "center");
			if (Application.operator) {
				$a = $E("a");
				style_$a();
				a_($a, "href", "setting.html");
				c_($a, $T("Settings"));
				c_($p, $a);
			}
			$a = $E("a");
			style_$a();
			a_($a, "href", "data/recent.csv");
			a_($a, "download", "data_recent.csv");
			c_($a, $T("Recent weather data"));
			c_($p, $a);
			$a = $E("a");
			style_$a();
			a_($a, "href", Application.data_file);
			a_($a, "download", "");
			c_($a, $T("All weather data"));
			c_($p, $a);
			$a = $E("a");
			style_$a();
			a_($a, "href", "gps/recent.csv");
			a_($a, "download", "gps_recent.csv");
			c_($a, $T("Recent GPS data"));
			c_($p, $a);
			$a = $E("a");
			style_$a();
			a_($a, "href", Application.gps_file);
			a_($a, "download", "");
			c_($a, $T("All GPS data"));
			c_($p, $a);
			c_(document.body, $p);
		}();
		var $refresh, $refresh_auto;
		void function () {
			var $form, $button, $label, $input;
			$refresh = $form = $E("form");
			s_($form, "display", "inline-block");
			s_($form, "margin", "1ex");
			s_($form, "border", "solid thin gray");
			s_($form, "padding", "1ex");
			$label = $E("label");
			s_($label, "margin-right", "1ex");
			s_($label, "padding", "1ex");
			$refresh_auto = $input = $E("input");
			a_($input, "type", "checkbox");
			c_($label, $input);
			c_($label, $T("Auto refresh"));
			c_($form, $label);
			$button = $E("button");
			a_($button, "type", "submit");
			s_($button, "margin-left", "1ex");
			c_($button, $T("Refresh now"));
			c_($form, $button);
			c_(document.body, $form);
		}();
		if (Application.operator) {
			var $report_auto;
			var $upload;
			void function () {
				var $div, $input, $button;
				$div = $E("div");
				s_($div, "display", "inline-block");
				s_($div, "margin-top", "1ex");
				s_($div, "margin-bottom", "1ex");
				s_($div, "margin-left", "1ex");
				s_($div, "margin-right", "2ex");
				s_($div, "border", "solid thin gray");
				s_($div, "padding", "1ex");
				$report_auto = $input = $E("input");
				a_($input, "type", "checkbox");
				c_($div, $input);
				c_($div, $T("Report position"));
				c_(document.body, $div);
				$div = $E("div");
				s_($div, "display", "inline-block");
				s_($div, "margin-top", "1ex");
				s_($div, "margin-bottom", "1ex");
				s_($div, "margin-left", "1ex");
				s_($div, "margin-right", "2ex");
				s_($div, "border", "solid thin gray");
				s_($div, "padding", "1ex");
				$upload = $button = $E("button");
				a_($button, "type", "button");
				c_($button, $T("Upload data"));
				c_($div, $button);
				c_(document.body, $div);
			}();
		}
		var $data_list;
		var data_latest = null;
		void function () {
			var $table, $caption, $thead, $tr, $th, $tbody;
			$table = $E("table");
			s_($table, "margin-bottom", "3ex");
			s_($table, "border-collapse", "collapse");
			s_($table, "width", "100%");
			$caption = $E("caption");
			c_($caption, $T("Sensor data"));
			c_($table, $caption);
			$thead = $E("thead");
			s_($thead, "border-bottom-style", "solid");
			$tr = $E("tr");
			Application.data_fields.forEach(
				function (field) {
					var text = field.name;
					if (field.unit)
						text = text + " (" + field.unit + ")";
					$th = $E("th");
					c_($th, $T(text));
					c_($tr, $th);
				}
			);
			c_($thead, $tr);
			c_($table, $thead);
			$data_list = $tbody = $E("tbody");
			c_($table, $tbody);
			c_(document.body, $table);
		}();
		var $data_loading = $E("p");
		$data_loading.hidden = true;
		c_($data_loading, $T("Loading..."));
		c_(document.body, $data_loading);
		var $GPS_list;
		void function () {
			var $table, $caption, $thead, $tr, $th, $tbody;
			$table = $E("table");
			s_($table, "margin-bottom", "3ex");
			s_($table, "border-collapse", "collapse");
			s_($table, "width", "100%");
			$caption = $E("caption");
			c_($caption, $T("GPS data"));
			c_($table, $caption);
			$thead = $E("thead");
			s_($thead, "border-bottom-style", "solid");
			$tr = $E("tr");
			Application.gps_fields.forEach(
				function (field) {
					var text = field.name;
					if (field.unit)
						text = text + " (" + field.unit + ")";
					$th = $E("th");
					c_($th, $T(text));
					c_($tr, $th);
				}
			);
			c_($thead, $tr);
			c_($table, $thead);
			$GPS_list = $tbody = $E("tbody");
			c_($table, $tbody);
			c_(document.body, $table);
		}();
		var $GPS_loading = $E("p");
		$GPS_loading.hidden = true;
		c_($GPS_loading, $T("Loading..."));
		c_(document.body, $GPS_loading);
		function data_load() {
			return new Promise(
				function (resolve, reject) {
					$data_list.textContent = null;
					$data_loading.hidden = false;
					var xhr = new XMLHttpRequest();
					xhr.onerror = reject;
					xhr.onloadend = function (event) {
						$data_loading.hidden = true;
						var text = xhr.responseText;
						if (text == null || xhr.status !== 200) {
							alert("Failed to load data");
							return reject(xhr);
						}
						var fields = null;
						var lines = text.split("\r\n");
						if (!lines || !(lines.length > 0)) return;
						for (var i = 1; lines.length > i; ++i) {
							var line = lines[lines.length - i].trim();
							if (!line || typeof line !== "string") continue;
							fields = line.split(",");
							var $tr = $E("tr");
							data_latest = new Object();
							for (var j = 0; fields.length > j; ++j) {
								var $td = $E("td");
								s_($td, "border-style", "solid");
								s_($td, "border-width", "thin");
								s_($td, "text-align", "center");
								var v = j === 0 ? string_from_Date(fields[j]) : fields[j];
								c_($td, $T(v));
								c_($tr, $td);
								if (Application.data_fields[j].title != null)
									data_latest[Application.data_fields[j].name] = v;
							}
							c_($data_list, $tr);
						}
						return resolve();
					};
					xhr.open("GET", "data/recent.csv", true);
					xhr.send();
				}
			);
		}
		function GPS_load() {
			return new Promise(
				function (resolve, reject) {
					$GPS_list.textContent = null;
					$GPS_loading.hidden = false;
					var xhr = new XMLHttpRequest();
					xhr.onerror = reject;
					xhr.onloadend = function (event) {
						$GPS_loading.hidden = true;
						var text = xhr.responseText;
						if (text == null || xhr.status !== 200) {
							alert("Failed to load GPS records");
							return reject(xhr);
						}
						var lines = text.split("\r\n");
						if (!lines || !(lines.length > 0)) return;
						for (var i = 1; lines.length > i; ++i) {
							var line = lines[lines.length - i].trim();
							if (!line || typeof line !== "string") continue;
							var fields = line.split(",");
							var $tr = $E("tr");
							for (var j = 0; fields.length > j; ++j) {
								var $td = $E("td");
								s_($td, "border-style", "solid");
								s_($td, "border-width", "thin");
								s_($td, "text-align", "center");
								if (j === 0) c_($td, $T(string_from_Date(fields[j])));
								else c_($td, $T(fields[j]));
								c_($tr, $td);
							}
							c_($GPS_list, $tr);
						}
						return resolve();
					};
					xhr.open("GET", "gps/recent.csv", true);
					xhr.send();
				}
			);
		}
		function load_all() {
			return (
				data_load()
					.catch(function () {})
					.then(function () {return GPS_load();})
					.catch(function () {})
			);
		}
		$refresh.addEventListener(
			"submit",
			function (event) {
				event.preventDefault();
				return load_all();
			}
		);
		var refresh_timer = null;
		$refresh_auto.addEventListener(
			"change",
			function (event) {
				if ($refresh_auto.checked) {
					if (refresh_timer !== null) return;
					refresh_timer = setInterval(load_all, Application.measure_interval);
				}
				else {
					if (refresh_timer === null) return;
					clearInterval(refresh_timer);
					refresh_timer = null;
				}
			}
		);
		setTimeout(load_all, 3000);
		if (Application.operator)
			void function () {
				if (window.isSecureContext) if ("geolocation" in window.navigator) {
					function GPS_upload(planned_time, browser_time, position_time, coords) {
						var body = new URLSearchParams();
						body.append("campaign",      Application.campaign);
						body.append("organisation",  Application.organisation);
						body.append("device",        Application.device);
						body.append("time",          planned_time);
						body.append("browser_time",  browser_time);
						body.append("position_time", position_time);
						body.append("latitude",      coords.latitude);
						body.append("longitude",     coords.longitude);
						body.append("altitude",      coords.altitude);
						var xhr = new XMLHttpRequest();
						xhr.open("POST", "/gps/upload.exe", true);
						xhr.send(body);
					}
					function GPS_report(timestamp, coords) {
						var body = new URLSearchParams();
						body.append("campaign",     Application.campaign);
						body.append("organisation", Application.organisation);
						body.append("device",       Application.device);
						body.append("time",         timestamp);
						body.append("latitude",     coords.latitude);
						body.append("longitude",    coords.longitude);
						body.append("altitude",     coords.altitude);
						for (var field in data_latest)
							body.append(field, data_latest[field]);
						var xhr = new XMLHttpRequest();
						xhr.open("POST", Application.monitor_URL, true);
						xhr.send(body);
					}
					function GPS_record(planned_time, spacetime) {
						if (spacetime === null || typeof spacetime === "undefined") return;
						var browser_time = string_from_Date(Date.now(), "T");
						var position_time = string_from_Date(spacetime.timestamp, "T");
						var coords = spacetime.coords;
						if (Application.operator) {
							GPS_upload(planned_time, browser_time, position_time, coords);
							if ($report_auto.checked)
								GPS_report(position_time, coords);
						}
					}
					function GPS_error(error) {
						console.error("GeoLocationError: ", error.message);
					}
					var GPS_options = {
						timeout: Application.measure_interval / 4,
						enableHighAccuracy: true
					};
					function GPS_request() {
						var now_plus_half =
							Date.now()
								- MILLISECONDS_FROM_1970_TO_2000
								+ Application.measure_interval / 2;
						var planned_time =
							string_from_Date(
								now_plus_half
									- now_plus_half % Application.measure_interval
									+ MILLISECONDS_FROM_1970_TO_2000,
								"T"
							);
						navigator.geolocation.getCurrentPosition(GPS_record.bind(this, planned_time), GPS_error, GPS_options)
					}
					function GPS_start() {
						setInterval(GPS_request, Application.measure_interval);
					}
					setTimeout(GPS_start, (Date.now() - MILLISECONDS_FROM_1970_TO_2000) % Application.measure_interval);
					setTimeout(GPS_request, 0);
					function GPS_callback(spacetime) {
						GPS_record(string_from_Date(spacetime.timestamp), spacetime);
					}
					if (GPS_watch) navigator.geolocation.watchPosition(GPS_callback, GPS_error, GPS_options);
				}
				void function () {
					var subtle = null;
					if (window.isSecureContext) if ("crypto" in window) if ("subtle" in window.crypto) {
						subtle = window.crypto.subtle;
					}
					function authorization(query, body) {
						if (subtle == null)
							return Promise.resolve("BEARER " + Application.upload_password);
						var credential = Application.upload_username + query + body + Application.upload_username;
						var binary = new TextEncoder().encode(credential);
						return subtle.digest("SHA-256", binary).then(
							function (hash) {
								var digest = new Uint8Array(hash);
								var password = new TextEncoder().encode(Application.upload_password);
								var binary = new Uint8Array(password.length + 1 + digest.byteLength);
								binary.set(password, 0);
								binary[password.length] = 0x3A;  /* ASCII 3A = ':' */
								binary.set(digest, password.length + 1);
								var base64 = binary.toBase64();
								return Promise.resolve("BASIC " + base64);
							}
						);
					}
					function upload(site, device, body) {
						var params = new URLSearchParams();
						params.set("site", Application.campaign);
						params.set("device", Application.device);
						var query = params.toString();
						return authorization(query, body).then(
							function (auth) {
								return new Promise(
									function (resolve, reject) {
										var xhr = new XMLHttpRequest();
										xhr.onerror = reject;
										xhr.onloadend = function () {
											if (200 > xhr.status || xhr.status >= 300)
												return reject();
											return resolve();
										};
										xhr.open("POST", Application.upload_URL + "?" + query, true);
										xhr.setRequestHeader("AUTHORIZATION", auth);
										xhr.send(body);
									}
								);
							}
						);
					}
					$upload.addEventListener(
						"click",
						function (event) {
							event.preventDefault();
							new Promise(
								function (resolve, reject) {
									var xhr = new XMLHttpRequest();
									xhr.onerror = reject;
									xhr.onloadend = function () {
										var text = xhr.responseText;
										if (xhr.status !== 200 || text == null)
											return reject();
										return resolve(text);
									};
									xhr.open("GET", Application.data_file, true);
									xhr.send();
								}
							).then(
								function (text) {
									return upload(Application.campaign, Application.device, text);
								}
							).then(
								function (text) {
									return new Promise(
										function (resolve, reject) {
											var xhr = new XMLHttpRequest();
											xhr.onerror = reject;
											xhr.onloadend = function () {
												var text = xhr.responseText;
												if (xhr.status !== 200 || text == null)
													return reject();
												return resolve(text);
											};
											xhr.open("GET", Application.gps_file, true);
											xhr.send();
										}
									);
								}
							).then(
								function (text) {
									return upload(Application.campaign, Application.device, text);
								}
							).then(
								function () {
									alert("Success to upload data");
								},
								function (e) {
									console.error(e);
									alert("Failed to upload data");
								}
							);
						}
					);
				}();
				void function () {
					/* set device time */
					var xhr = new XMLHttpRequest();
					var body = new URLSearchParams();
					body.append("time", string_from_Date(new Date(), "T"));
					xhr.open("POST", "setting.exe", true);
					xhr.send(body);
				}();
			}();
	}));
</script>
</body>
</html>
)HTML";

	static void stream_print_fields(PsychicStreamResponse *const stream, std::vector<DataField> const *const fields) {
		char const *prefix = "[";
		for (DataField const field: *fields) {
			stream->print(prefix);
			prefix = ", ";
			stream->print("{name:\"");
			stream->print(javascript_escape(field.name));
			stream->print("\",title:");
			if (field.title == nullptr)
				stream->print("null");
			else {
				stream->print('"');
				stream->print(javascript_escape(field.title));
				stream->print('"');
			}
			stream->print(",unit:");
			if (field.unit == nullptr)
				stream->print("null");
			else {
				stream->print('"');
				stream->print(javascript_escape(field.unit));
				stream->print('"');
			}
			stream->print('}');
		}
		stream->print(']');
	}

	static esp_err_t home_handle(PsychicRequest *const request, PsychicResponse *const response) {
		PsychicStreamResponse stream(response, XHTML_content_type);
		stream.addHeader("CONTENT-SECURITY-POLICY", "connect-src *");
		stream.beginSend();
		stream.write(reinterpret_cast<uint8_t const *>(home_html_1), sizeof home_html_1 - 1);
		if (!use_AP_mode)
			stream.write(reinterpret_cast<uint8_t const *>(home_html_2), sizeof home_html_2 - 1);
		stream.write(reinterpret_cast<uint8_t const *>(home_html_3), sizeof home_html_3 - 1);
		stream.print("\t\tcampaign: \"");
		stream.print(javascript_escape(campaign_name));
		stream.print("\",\r\n\t\t\t\torganisation: \"");
		stream.print(javascript_escape(organisation_name));
		stream.print("\",\r\n\t\t\tdevice: \"");
		stream.print(javascript_escape(device_name));
		stream.print("\",\r\n\t\t\tmeasure_interval: \"");
		stream.print(measure_interval);
		stream.print("\",\r\n\t\t\tdata_file: \"");
		stream.print(javascript_escape(SD_card::data_filename));
		stream.print("\",\r\n\t\t\tgps_file: \"");
		stream.print(javascript_escape(SD_card::gps_filename));
		stream.print("\",\r\n\t\t\tmonitor_URL: \"");
		stream.print(javascript_escape(monitor_URL));
		stream.print("\",\r\n\t\t\tupload_URL: \"");
		stream.print(javascript_escape(upload_URL));
		stream.print("\",\r\n\t\t\tupload_username: \"");
		stream.print(javascript_escape(upload_username));
		stream.print("\",\r\n\t\t\tupload_password: \"");
		stream.print(javascript_escape(upload_password));
		stream.print("\",\r\n\t\t\tdata_fields: ");
		stream_print_fields(&stream, &Data::fields);
		stream.print(",\r\n\t\t\tgps_fields: ");
		stream_print_fields(&stream, &GPS::fields);
		stream.print("\r\n");
		stream.write(reinterpret_cast<uint8_t const *>(home_html_4), sizeof home_html_4 - 1);
		return stream.endSend();
	}

	static PROGMEM char const icon_data[] = {
		/* PNG signature */
		0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
		/* data length */
		0x00, 0x00, 0x00, 0x0D,
		/* "IHDR" as ASCII */
		0x49, 0x48, 0x44, 0x52,
		/* width */
		0x00, 0x00, 0x00, 0x01,
		/* height */
		0x00, 0x00, 0x00, 0x01,
		/* bit depth */
		0x01,
		/* colour type */
		0x00,
		/* compression method */
		0x00,
		/* filter method */
		0x00,
		/* interlace method */
		0x00,
		/* header checksum */
		0x37, 0x6E, 0xF9, 0x24,
		/* data length */
		0x00, 0x00, 0x00, 0x0A,
		/* chunk type "IDAT" */
		0x49, 0x44, 0x41, 0x54,
		/* zlib header */
		0x78, 0x01,
		/* compressed DEFLATE block */
		0x63, 0x60, 0x00, 0x00,
		/* zlib checksum */
		0x00, 0x02, 0x00, 0x01,
		/* chunk checksum */
		0x73, 0x75, 0x01, 0x18
	};

	static esp_err_t icon_handle(PsychicRequest *const request, PsychicResponse *const response) {
		return response->send(200, "image/png", icon_data);
	}

	static esp_err_t data_recent_handle(PsychicRequest *const request, PsychicResponse *const response) {
		PsychicStreamResponse stream(response, "text/csv");
		stream.addHeader("CONTENT-SECURITY-POLICY", "connect-src *");
		stream.beginSend();
		stream.println(SD_card::data_header);
		DATA_LOCK(data_lock);
		for (Data const &record: data_records)
			stream.println(record.to_CSV());
		return stream.endSend();
	}

	static esp_err_t data_latest_handle(PsychicRequest *const request, PsychicResponse *const response) {
		PsychicStreamResponse stream(response, "text/csv");
		stream.addHeader("CONTENT-SECURITY-POLICY", "connect-src *");
		stream.beginSend();
		stream.println(SD_card::data_header);
		DATA_LOCK(data_lock);
		if (!data_records.empty())
			stream.println(data_records.back().to_CSV());
		return stream.endSend();
	}

	static esp_err_t gps_recent_handle(PsychicRequest *const request, PsychicResponse *const response) {
		PsychicStreamResponse stream(response, "text/csv");
		stream.addHeader("CONTENT-SECURITY-POLICY", "connect-src *");
		stream.beginSend();
		stream.println(SD_card::gps_header);
		DATA_LOCK(data_lock);
		for (GPS const &record: gps_records)
			stream.println(record.to_CSV());
		return stream.endSend();
	}

	static esp_err_t gps_latest_handle(PsychicRequest *const request, PsychicResponse *const response) {
		PsychicStreamResponse stream(response, "text/csv");
		stream.addHeader("CONTENT-SECURITY-POLICY", "connect-src *");
		stream.beginSend();
		stream.println(SD_card::gps_header);
		DATA_LOCK(data_lock);
		if (!gps_records.empty())
			stream.println(gps_records.back().to_CSV());
		return stream.endSend();
	}

	static esp_err_t gps_upload_handle(PsychicRequest *const request, PsychicResponse *const response) {
		GPS gps;
		PsychicWebParameter *parameter;
		parameter = request->getParam("time");
		if (parameter != nullptr) {
			String const &value = parameter->value();
			DateTime const datetime(value.c_str());
			if (datetime.isValid())
				gps.time = datetime;
			else {
				Serial.print("WARN: incorrect GPS time = ");
				Serial.println(value);
			}
		}
		parameter = request->getParam("browser_time");
		if (parameter != nullptr) {
			String const &value = parameter->value();
			DateTime const datetime(value.c_str());
			if (datetime.isValid())
				gps.browser_time = datetime;
			else {
				Serial.print("WARN: incorrect GPS browser_time = ");
				Serial.println(value);
			}
		}
		parameter = request->getParam("position_time");
		if (parameter != nullptr) {
			String const &value = parameter->value();
			DateTime const datetime(value.c_str());
			if (datetime.isValid())
				gps.position_time = datetime;
			else {
				Serial.print("WARN: incorrect GPS position_time = ");
				Serial.println(value);
			}
		}
		parameter = request->getParam("latitude");
		if (parameter != nullptr) {
			String const &value = parameter->value();
			if (value != "null") {
				char *end;
				double const x = strtod(value.c_str(), &end);
				if (!*end)
					gps.latitude = x;
				else {
					Serial.print("WARN: incorrect GPS latitude = ");
					Serial.println(value);
				}
			}
		}
		parameter = request->getParam("longitude");
		if (parameter != nullptr) {
			String const &value = parameter->value();
			if (value != "null") {
				char *end;
				double const x = strtod(value.c_str(), &end);
				if (!*end)
					gps.longitude = x;
				else {
					Serial.print("WARN: incorrect GPS longitude = ");
					Serial.println(value);
				}
			}
		}
		parameter = request->getParam("altitude");
		if (parameter != nullptr) {
			String const &value = parameter->value();
			if (value != "null") {
				char *end;
				double const x = strtod(value.c_str(), &end);
				if (!*end)
					gps.altitude = x;
				else {
					Serial.print("WARN: incorrect GPS altitude = ");
					Serial.println(value);
				}
			}
		}
		String const GPS_string = gps.to_CSV();
		Serial.print("INFO: GPS ");
		Serial.println(GPS_string);

		{
			DATA_LOCK(data_lock);
			if (gps_records.size() >= records_max_size)
				gps_records.pop_front();
			gps_records.push_back(gps);
		}

		if (SD_card::exist) {
			SDCARD_LOCK(sdcard_lock);
			File file = SD.open(SD_card::gps_filename, "a", true);
			try {
				if (!file.position())
					file.println(SD_card::gps_header);
				file.println(GPS_string);
			}
			catch (...) {
				Serial.println("ERROR: failed to write GPS data into SD card");
			}
			file.close();
		}

		return response->send(204, "text/plain", "");
	}

	static PROGMEM char const setting_html_1[] =
R"HTML(<html xmlns='http://www.w3.org/1999/xhtml'>
<head>
<meta content-type='application/xhtml+xml; charset=UTF-8' />
<meta charset='UTF-8' />
<meta name='viewport' content='width=device-width, initial-scale=1' />
<title>Settings</title>
<link rel='icon' type='image/png' href='favicon.ico' />
</head>
<body>
<style>
	form.setting {
		margin: 1ex;
		border: solid thin;
		padding: 1ex;
	}
	form.setting > p:first-child {
		margin-top: 0;
	}
	form.setting label {
		display: flex;
		justify-content: start;
		column-gap: 0.5em;
	}
	form.setting label input[type="text"] ,
	form.setting label input[type="number"] ,
	form.setting label input[type="datetime-local"] {
		flex-grow: 1;
	}
	form.setting label:has(input[type="checkbox"]:not(:checked)) ~ button {
		display: none;
	}
	form#set_wifi > label {
		display: inline;
	}
	form#set_wifi > div {
		margin-bottom: 1ex;
	}
	form#set_wifi > label:has(> input[type="radio"]:not(:checked)) + div {
		background-color: lightgray;
		filter: opacity(0.5);
	}
</style>
<p><a href='operator'>&#x2190; Back</a></p>
)HTML";

	static PROGMEM char const setting_html_2[] =
R"HTML(
</body>
</html>
)HTML";

	static PROGMEM char const setting_form_1[] = "<form\r\n\tid='";

	static PROGMEM char const setting_form_2[] =
R"HTML('
	class='setting'
	action='setting.exe'
	method='POST'
>
)HTML";

	static PROGMEM char const setting_form_3[] =
R"HTML(
	<button type='submit'>Set</button>
</form>
)HTML";

	static String XML_escape(char const *string) {
		String result;
		char c;
		while (c = *(string++))
			switch (c) {
			case '&':
				result.concat("&amp;");
				break;
			case '<':
				result.concat("&lt;");
				break;
			case '>':
				result.concat("&gt;");
				break;
			case '"':
				result.concat("&quot;");
				break;
			case '\'':
				result.concat("&apos;");
				break;
			default:
				result.concat(c);
			}
		return result;
	}

	static String XML_escape(String const &string) {
		return XML_escape(string.c_str());
	}

	static void setting_form(PsychicStreamResponse *const stream, char const *const id) {
		stream->write(reinterpret_cast<uint8_t const *>(setting_form_1), sizeof setting_form_1 - 1);
		stream->print(XML_escape(id));
		stream->write(reinterpret_cast<uint8_t const *>(setting_form_2), sizeof setting_form_2 - 1);
	}

	static void setting_form_set(PsychicStreamResponse *const stream) {
		stream->write(reinterpret_cast<uint8_t const *>(setting_form_3), sizeof setting_form_3 - 1);
	}

	static esp_err_t setting_handle(PsychicRequest *const request, PsychicResponse *const response) {
		PsychicStreamResponse stream(response, XHTML_content_type);
		stream.addHeader("CONTENT-SECURITY-POLICY", "connect-src *");
		stream.beginSend();

		stream.write(reinterpret_cast<uint8_t const *>(setting_html_1), sizeof setting_html_1 - 1);

		setting_form(&stream, "set_time");
		stream.print(
			"\t<label>\r\n"
			"\t\tSet clock\r\n"
			"\t\t<input type='datetime-local' name='time' required='' />"
			"\t</label>\r\n"
		);
		setting_form_set(&stream);

		setting_form(&stream, "set_campaign");
		stream.print(
			"\t<label>\r\n"
			"\t\tCampaign\r\n"
			"\t\t<input type='text' name='campaign' required='' value='"
		);
		stream.print(XML_escape(campaign_name));
		stream.print(
			"' />\r\n"
			"\t</label>\r\n"
		);
		setting_form_set(&stream);

		setting_form(&stream, "set_organisation");
		stream.print(
			"\t<label>\r\n"
			"\t\tOrganisation\r\n"
			"\t\t<input type='text' name='organisation' required='' value='"
		);
		stream.print(XML_escape(organisation_name));
		stream.print(
			"' />\r\n"
			"\t</label>\r\n"
		);
		setting_form_set(&stream);

		setting_form(&stream, "set_device");
		stream.print(
			"\t<label>\r\n"
			"\t\tDevice ID\r\n"
			"\t\t<input type='text' name='device' required='' value='"
		);
		stream.print(XML_escape(device_name));
		stream.print(
			"' />\r\n"
			"\t</label>\r\n"
		);
		setting_form_set(&stream);

		setting_form(&stream, "set_interval");
		stream.print(
			"\t<label>\r\n"
			"\t\tMeasurement Interval (seconds)\r\n"
			"\t\t<input type='number' name='interval' min='10' max='900' required='' value='"
		);
		stream.print(String(measure_interval / 1000));
		stream.print(
			"' />\r\n"
			"\t</label>\r\n"
		);
		setting_form_set(&stream);

		setting_form(&stream, "set_wifi");
		stream.print(
			"\t<p>Wi-Fi</p>\r\n"
			"\t<label>\r\n"
			"\t\t<input type='radio' name='WiFi' value='AP'"
		);
		if (use_AP_mode) stream.print(" checked=''");
		stream.print(
			" />\r\n"
			"\t\tArduino Wi-Fi network\r\n"
			"\t</label>\r\n"
			"\t<div>\r\n"
			"\t\t<label>\r\n"
			"\t\t\tAP SSID\r\n"
			"\t\t\t<input type='text' name='APSSID' value='"
		);
		stream.print(XML_escape(AP_SSID));
		stream.print(
			"' />\r\n"
			"\t\t</label>\r\n"
			"\t\t<label>\r\n"
			"\t\t\tAP PASS\r\n"
			"\t\t\t<input type='text' name='APPASS' value='"
		);
		stream.print(XML_escape(AP_PASS));
		stream.print(
			"' />\r\n"
			"\t\t</label>\r\n"
			"\t</div>\r\n"
			"\t<label>\r\n"
			"\t\t<input type='radio' name='WiFi' value='STA'"
		);
		if (!use_AP_mode) stream.print(" checked=''");
		stream.print(
			" />\r\n"
			"\t\tExternal Wi-Fi network\r\n"
			"\t</label>\r\n"
			"\t<div>\r\n"
			"\t\t<label>\r\n"
			"\t\t\tSTA SSID\r\n"
			"\t\t\t<input type='text' name='STASSID' value='"
		);
		stream.print(XML_escape(STA_SSID));
		stream.print(
			"' />\r\n"
			"\t\t</label>\r\n"
			"\t\t<label>\r\n"
			"\t\t\tSTA PASS\r\n"
			"\t\t\t<input type='text' name='STAPASS' value='"
		);
		stream.print(XML_escape(STA_PASS));
		stream.print(
			"' />\r\n"
			"\t\t</label>\r\n"
			"\t</div>\r\n"
		);
		setting_form_set(&stream);

		setting_form(&stream, "set_monitor");
		stream.print(
			"\t<p>Position</p>"
			"\t<label>\r\n"
			"\t\tServer URL\r\n"
			"\t\t<input type='text' name='monitor' required='' value='"
		);
		stream.print(XML_escape(monitor_URL));
		stream.print(
			"' />\r\n"
			"\t</label>\r\n"
			"<p>* To allow position to be broadcasted to the server</p>\r\n"
		);
		setting_form_set(&stream);

		setting_form(&stream, "set_upload");
		stream.print(
			"\t<p>Upload host server information</p>"
			"\t<label>\r\n"
			"\t\tHost address\r\n"
			"\t\t<input type='text' name='upload' required='' value='"
		);
		stream.print(XML_escape(upload_URL));
		stream.print(
			"' />\r\n"
			"\t</label>\r\n"
		);
		stream.print(
			"\t<label>\r\n"
			"\t\tUsername\r\n"
			"\t\t<input type='text' name='username' required='' value='"
		);
		stream.print(XML_escape(upload_username));
		stream.print(
			"' />\r\n"
			"\t</label>\r\n"
		);
		stream.print(
			"\t<label>\r\n"
			"\t\tPassword\r\n"
			"\t\t<input type='text' name='password' required='' value='"
		);
		stream.print(XML_escape(upload_password));
		stream.print(
			"' />\r\n"
			"\t</label>\r\n"
		);
		setting_form_set(&stream);

		setting_form(&stream, "set_calibration");
		stream.print(
			"\t<p>Linear calibration</p>\r\n"
			"\t<blockquote>\r\n"
			"\t\t<math xmlns='http://www.w3.org/1998/Math/MathML'>\r\n"
			"\t\t\t<mi>y</mi> <mo form='infix'>=</mo> <mi>m</mi> <mi>x</mi> <mo form='infix'>+</mo> <mi>c</mi>"
			"\t\t</math>\r\n"
			"\t</blockquote>\r\n"
			"\t<label>\r\n"
			"\t\tSlop <i>m</i>\r\n"
			"\t\t<input type='text' name='calibration_slope' value='"
		);
		stream.print(calibration_slope);
		stream.print(
			"' />\r\n"
			"\t</label>\r\n"
			"\t<label>\r\n"
			"\t\tIntercept <i>c</i>\r\n"
			"\t\t<input type='text' name='calibration_intercept' value='"
		);
		stream.print(calibration_intercept);
		stream.print(
			"' />\r\n"
			"\t</label>\r\n"
		);
		setting_form_set(&stream);

		setting_form(&stream, "do_measure");
		stream.print(
			"\t<label>\r\n"
			"\t\tMeasure now\r\n"
			"\t\t<input type='checkbox' name='measure' />\r\n"
			"\t</label>\r\n"
			"\t<p>* After checking, please press \"confirm\"</p>\r\n"
			"\t<button type='submit'>Confirm</button>\r\n"
			"</form>\r\n"
		);

		setting_form(&stream, "do_delete");
		stream.print(
			"\t<label>\r\n"
			"\t\tDelete all data\r\n"
			"\t\t<input type='checkbox' name='delete' />\r\n"
			"\t</label>\r\n"
			"\t<p>* After checking, please press \"confirm\"</p>\r\n"
			"\t<button type='submit'>Confirm</button>\r\n"
			"</form>\r\n"
		);

		setting_form(&stream, "do_reboot");
		stream.print(
			"\t<label>\r\n"
			"\t\tReboot\r\n"
			"\t\t<input type='checkbox' name='reboot' />\r\n"
			"\t</label>\r\n"
			"\t<p>* After checking, please press \"confirm\"</p>\r\n"
			"\t<button type='submit'>Confirm</button>\r\n"
			"</form>\r\n"
		);

		stream.write(reinterpret_cast<uint8_t const *>(setting_html_2), sizeof setting_html_2 - 1);
		return stream.endSend();
	}

	static PROGMEM char const command_html[] =
R"HTML(<html xmlns='http://www.w3.org/1999/xhtml'>
<head>
<meta content-type='application/xhtml+xml; charset=UTF-8' />
<meta charset='UTF-8' />
<meta name='viewport' content='width=device-width, initial-scale=1' />
<title>Command redirection</title>
<link rel='stylesheet' type='text/css' href='style.css' />
</head>
<body>
<p>Command received. Redirect to <a href='./setting.html'>homepage.</a></p>
</body>
</html>
)HTML";

	static esp_err_t command_handle(PsychicRequest *const request, PsychicResponse *const response) {
		PsychicWebParameter *parameter;
		parameter = request->getParam("time");
		if (parameter != nullptr) {
			Serial.print("INFO: command time = ");
			Serial.println(parameter->value());
			DateTime const datetime(parameter->value().c_str());
			if (datetime.isValid())
				Clock::set_time(&datetime);
			else {
				Serial.print("WARN: incorrect command time = ");
				Serial.println(parameter->value());
			}
		}
		parameter = request->getParam("campaign");
		if (parameter != nullptr) {
			Serial.print("INFO: command campaign = ");
			Serial.println(parameter->value());
			campaign_name = parameter->value();
			need_save = true;
		}
		parameter = request->getParam("organisation");
		if (parameter != nullptr) {
			Serial.print("INFO: command organisation = ");
			Serial.println(parameter->value());
			organisation_name = parameter->value();
			need_save = true;
		}
		parameter = request->getParam("device");
		if (parameter != nullptr) {
			Serial.print("INFO: command device = ");
			Serial.println(parameter->value());
			device_name = parameter->value();
			need_save = true;
		}
		parameter = request->getParam("interval");
		if (parameter != nullptr) {
			char const *const value = parameter->value().c_str();
			Serial.print("INFO: command interval = ");
			Serial.println(value);
			char *end;
			unsigned long int const x = strtoul(value, &end, 10);
			if (!*end && x >= measure_interval_lowerbound && x <= measure_interval_upperbound) {
				measure_interval = x * 1000;
				need_save = true;
			}
			else {
				Serial.print("WARN: incorrect command interval = \"");
				Serial.print(value);
				Serial.println('"');
			}
		}
		parameter = request->getParam("WiFi");
		if (parameter != nullptr) {
			String const &value = parameter->value();
			Serial.print("INFO: command WiFi = ");
			Serial.println(value);
			if (value == "AP") {
				use_AP_mode = true;
				need_save = true;
			}
			else if (value == "STA") {
				use_AP_mode = false;
				need_save = true;
			}
			else {
				Serial.print("WARN: incorrect command WiFi = \"");
				Serial.print(value);
				Serial.println('"');
			}
		}
		parameter = request->getParam("APSSID");
		if (parameter != nullptr) {
			Serial.print("INFO: command APSSID = ");
			Serial.println(parameter->value());
			AP_SSID = parameter->value();
			need_save = true;
		}
		parameter = request->getParam("APPASS");
		if (parameter != nullptr) {
			Serial.print("INFO: command APPASS = ");
			Serial.println(parameter->value());
			AP_PASS = parameter->value();
			need_save = true;
		}
		parameter = request->getParam("STASSID");
		if (parameter != nullptr) {
			Serial.print("INFO: command STASSID = ");
			Serial.println(parameter->value());
			STA_SSID = parameter->value();
			need_save = true;
		}
		parameter = request->getParam("STAPASS");
		if (parameter != nullptr) {
			Serial.print("INFO: command STAPASS = ");
			Serial.println(parameter->value());
			STA_PASS = parameter->value();
			need_save = true;
		}
		parameter = request->getParam("monitor");
		if (parameter != nullptr) {
			Serial.print("INFO: command monitor = ");
			Serial.println(parameter->value());
			monitor_URL = parameter->value();
			need_save = true;
		}
		parameter = request->getParam("upload");
		if (parameter != nullptr) {
			Serial.print("INFO: command upload = ");
			Serial.println(parameter->value());
			upload_URL = parameter->value();
			need_save = true;
		}
		parameter = request->getParam("username");
		if (parameter != nullptr) {
			Serial.print("INFO: command username = ");
			Serial.println(parameter->value());
			upload_username = parameter->value();
			need_save = true;
		}
		parameter = request->getParam("password");
		if (parameter != nullptr) {
			Serial.print("INFO: command password = ");
			Serial.println(parameter->value());
			upload_password = parameter->value();
			need_save = true;
		}
		parameter = request->getParam("calibration_slope");
		if (parameter != nullptr) {
			char const *const value = parameter->value().c_str();
			Serial.print("INFO: command calibration slope = ");
			Serial.println(value);
			char *end;
			float const x = strtof(value, &end);
			if (!*end && x >= calibration_slop_lowerbound && x <= calibration_slop_upperbound) {
				calibration_slope = x;
				need_save = true;
			}
			else {
				Serial.print("WARN: incorrect command calibration slope = \"");
				Serial.print(value);
				Serial.println('"');
			}
		}
		parameter = request->getParam("calibration_intercept");
		if (parameter != nullptr) {
			char const *const value = parameter->value().c_str();
			Serial.print("INFO: command calibration intercept = ");
			Serial.println(value);
			char *end;
			float const x = strtof(value, &end);
			if (!*end && x >= calibration_intercept_lowerbound && x <= calibration_intercept_upperbound) {
				calibration_intercept = x;
				need_save = true;
			}
			else {
				Serial.print("WARN: incorrect command calibration intercept = \"");
				Serial.print(value);
				Serial.println('"');
			}
		}
		parameter = request->getParam("measure");
		if (parameter != nullptr) {
			Serial.println("INFO: command measure");
			wait_measure_condition.notify_all();
		}
		if (request->hasParam("delete")) {
			Serial.println("INFO: command delete");
			{
				DATA_LOCK(data_lock);
				data_records.clear();
				gps_records.clear();
			}
			SDCARD_LOCK(sdcard_lock);
			File data_file = SD.open(SD_card::data_filename, "w", true);
			try {
				data_file.println(SD_card::data_header);
			}
			catch (...) {
				Serial.println("ERROR: failed to write header into data file");
			}
			data_file.close();
			File gps_file = SD.open(SD_card::gps_filename, "w", true);
			try {
				gps_file.println(SD_card::gps_header);
			}
			catch (...) {
				Serial.println("ERROR: failed to write header into GPS file");
			}
			gps_file.close();
		}
		if (request->hasParam("reboot")) {
			Serial.println("INFO: command reboot");
			Serial.flush();
			need_reboot = true;
			need_save = false;
		}

		response->setCode(303);
		response->setContentType("application/xhtml+xml; charset=UTF-8");
		response->addHeader("LOCATION", "/setting.html");
		response->setContent(reinterpret_cast<uint8_t const *>(command_html), sizeof command_html - 1);
		return response->send();
	}

	static void setup(void) {
		set_pthread_stack_size(16384);

		HTTPd .on("/",                HTTP_GET,  home_handle);
		HTTPSd.on("/",                HTTP_GET,  home_handle);
		HTTPd .on("/operator",        HTTP_GET,  home_handle);
		HTTPSd.on("/operator",        HTTP_GET,  home_handle);
		HTTPd .on("/favicon.ico",     HTTP_GET,  icon_handle);
		HTTPSd.on("/favicon.ico",     HTTP_GET,  icon_handle);
		HTTPd .on("/data/recent.csv", HTTP_GET,  data_recent_handle);
		HTTPSd.on("/data/recent.csv", HTTP_GET,  data_recent_handle);
		HTTPd .on("/data/latest.csv", HTTP_GET,  data_latest_handle);
		HTTPSd.on("/data/latest.csv", HTTP_GET,  data_latest_handle);
		HTTPd .on("/gps/recent.csv",  HTTP_GET,  gps_recent_handle);
		HTTPSd.on("/gps/recent.csv",  HTTP_GET,  gps_recent_handle);
		HTTPd .on("/gps/latest.csv",  HTTP_GET,  gps_latest_handle);
		HTTPSd.on("/gps/latest.csv",  HTTP_GET,  gps_latest_handle);
		HTTPd .on("/gps/upload.exe",  HTTP_POST, gps_upload_handle);
		HTTPSd.on("/gps/upload.exe",  HTTP_POST, gps_upload_handle);
		HTTPd .on("/setting.html",    HTTP_GET,  setting_handle);
		HTTPSd.on("/setting.html",    HTTP_GET,  setting_handle);
		HTTPd .on("/setting.exe",     HTTP_POST, command_handle);
		HTTPSd.on("/setting.exe",     HTTP_POST, command_handle);
		if (SD_card::exist) {
			HTTPd .serveStatic("/", SD, "/", "max-age=604800");
			HTTPSd.serveStatic("/", SD, "/", "max-age=604800");
		}
		else {
			HTTPd .on(SD_card::data_filename, HTTP_GET, data_recent_handle);
			HTTPSd.on(SD_card::data_filename, HTTP_GET, data_recent_handle);
			HTTPd .on(SD_card::gps_filename,  HTTP_GET, gps_recent_handle);
			HTTPSd.on(SD_card::gps_filename,  HTTP_GET, gps_recent_handle);
		}

		while (HTTPd.start() != ESP_OK) {
			Serial.println("ERROR: failed to start HTTP server");
			Monitor.println("Failed to start HTTP server");
			Monitor.display();
			delay(reinitialize_interval);
		}
		Serial.println("HTTP server started");

		HTTPSd.setCertificate(tls_cert, tls_key);
		while (HTTPSd.start() != ESP_OK) {
			Serial.println("ERROR: failed to start HTTPS server");
			Monitor.println("Failed to start HTTPS server");
			Monitor.display();
			delay(reinitialize_interval);
		}
		Serial.println("HTTPS server started");
	}
}

/* *************************************************************************** / ************************************ */
/* Main procedures */

static void redraw_display(bool const start_over) {
	static unsigned short int section = 0;
	if (start_over) section = 0;
	DISPLAY_LOCK(display_lock);
	Monitor.clearDisplay();
	DATA_LOCK(data_lock);
	if (section == 0 && data_records.size()) {
		Data const *const data = &data_records.back();
		char year[6], date[7], time[6];
		String fulltime = data->show_time();
		if (fulltime.length() == 19) {
			memcpy(year, fulltime.c_str(), 5);
			year[5] = 0;
			memcpy(date, fulltime.c_str() + 5, 5);
			date[5] = 0;
			memcpy(time, fulltime.c_str() + 11, 5);
			time[5] = 0;
		}
		else {
			year[0] = date[0] = time[0] = '?';
			year[1] = date[1] = time[1] = 0;
		}
		Monitor.setRotation(3);
		Monitor.setFont(&FONT_0);
		Monitor.setCursor(0, FONT_0_OFFSET);
		Monitor.println(year);
		Monitor.println(date);
		Monitor.println(time);
		Monitor.drawLine(0, 64, 63, 64, SSD1306_WHITE);
		Monitor.setCursor(0, 65 + FONT_0_OFFSET);
		data->display();
		++section;
	}
	else {
		Monitor.setRotation(0);
		Monitor.setFont(&FONT_1);
		Monitor.setCursor(0, FONT_1_OFFSET);
		if (SD_card::exist)
			Monitor.println("SD card found");
		else
			Monitor.println("No SD card");
		if (use_AP_mode) {
			WiFi.softAPIP().printTo(Monitor);
			Monitor.println();
			Monitor.print("AP:");
			Monitor.println(WiFi.softAPSSID());
		}
		else {
			signed int const status = WiFi.status();
			Monitor.println(WIFI::status_message(WiFi.status()));
			if (status == WL_CONNECTED) {
				WiFi.localIP().printTo(Monitor);
				Monitor.println();
				Monitor.print("STA:");
				Monitor.println(WiFi.SSID());
			}
		}
		section = 0;
	}
	Monitor.display();
}

void loop(void) {
	static unsigned short int count = 0;

	delay(main_loop_delay);

	if (need_save) {
		SD_card::save_settings();
		need_save = false;
	}

	if (need_reboot) {
		static unsigned long int reboot_time_0 = 0;
		static unsigned long int reboot_time_1 = 0;
		unsigned long int now = millis();
		if (!reboot_time_1) {
			reboot_time_0 = now;
			reboot_time_1 = now + reboot_wait_time;
		}
		else if (
			reboot_time_0 < reboot_time_1 && (now < reboot_time_0 || reboot_time_1 < now) ||
			reboot_time_1 < reboot_time_0 && reboot_time_1 < now && now < reboot_time_0
		) {
			need_reboot = false;
			esp_restart();
		}
	}

	if (++count >= display_refresh_interval) {
		count = 0;
		redraw_display(false);
	}
}

static void set_pthread_stack_size(size_t const stack_size) {
	static esp_pthread_cfg_t esp_pthread_cfg = esp_pthread_get_default_config();
	esp_pthread_cfg.stack_size = stack_size;
	esp_pthread_set_cfg(&esp_pthread_cfg);
}

void setup(void) {
	/* Reset pin */
	pinMode(reset_pin, INPUT);

	/* Serial port */
	Serial.begin(serial_baudrate);

	/* OLED display */
	Monitor.begin(SSD1306_SWITCHCAPVCC, 0x3C);
	Monitor.setRotation(3);
	Monitor.invertDisplay(false);
	Monitor.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
	Monitor.setCursor(0, 0);
	Monitor.clearDisplay();
	Monitor.display();

	/* Start-up delay */
	delay(start_wait_time);

	/* Initialize modules */
	Data::setup();
	SD_card::setup();
	Clock::setup();
	Sensor::setup();
	WIFI::setup();
	WEB::setup();

	/* Spawn measurement thread */
	set_pthread_stack_size(4096);
	std::thread(measure_thread).detach();
}

/* *************************************************************************** / ************************************ */
