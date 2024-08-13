#include <stdlib.h>
#include <vector>
#include <deque>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>

#include <esp_pthread.h>
#include <WiFi.h>
// #include <DNSServer.h>
#include <PsychicHttp.h>
#include <PsychicHttpServer.h>
#include <PsychicHttpsServer.h>
#include <PsychicStreamResponse.h>
#include <RTClib.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/TomThumb.h>
#include <SD.h>

#define SENSOR_BME280 1
#define SENSOR_SHT40 2

#include "config.h"

#if SENSOR == SENSOR_BME280
	#include <Adafruit_BME280.h>
#elif SENSOR == SENSOR_SHT40
	#include <Adafruit_Sensor.h>
	#include <Adafruit_SHT4x.h>
#else
	#error Invalid sensor type
#endif

#define FONT_OFFSET 12

static void redraw_display(void);

static std::mutex mutex_1;
#define DISPLAY_LOCK(lock) std::lock_guard<std::mutex> lock(mutex_1);
#define DEVICE_LOCK(lock) std::lock_guard<std::mutex> lock(mutex_1);
#define SDCARD_LOCK(lock)
#define NETWORK_LOCK(lock)

static std::mutex wait_measure_mutex;
static std::condition_variable wait_measure_condition;

static bool need_save = false;
static bool need_reboot = false;

static Adafruit_SSD1306 Monitor(128, 64);


/* *************************************************************************** / ************************************ */
/* Data */

struct Field {
	char const *name;
	char const *unit;
};

struct Data {
	DateTime time;
	float temperature;
#if SENSOR == SENSOR_BME280
	float pressure;
#endif
	float humidity;
};

static struct Field const data_fields[] = {
	{"time", nullptr},
	{"temperature", "\u2103"},
#if SENSOR == SENSOR_BME280
	{"pressure", "Pa"},
#endif
	{"humidity", "%"}
};

static String CSV_Data(struct Data const *const data) {
	return show_time(&data->time)
		+ ',' + data->temperature
#if SENSOR == SENSOR_BME280
		+ ',' + data->pressure
#endif
		+ ',' + data->humidity;
}

static String pretty_Data(struct Data const *const data) {
	char date[11], time[9];
	String fulltime = show_time(&data->time);
	if (fulltime.length() ==19) {
		memcpy(date, fulltime.c_str(), 10);
		date[10] = 0;
		memcpy(time, fulltime.c_str() + 11, 8);
		time[8] = 0;
	}
	else {
		date[0] = '?';
		date[1] = 0;
		time[0] = '?';
		time[1] = 0;
	}
	return String("Time:\r\n") + date + "\r\n" + time + "\r\n"
		+ "Temperature:\r\n" + String(data->temperature) + "\r\n"
#if SENSOR == SENSOR_BME280
		+ "Pressure:\r\n" + String(data->pressure) + "\r\n"
#endif
		+ "Humidity:\r\n" + String(data->humidity) + "\r\n";
}

struct GPS {
	DateTime time;
	double latitude;
	double longitude;
	double altitude;
};

static struct Field const gps_fields[] = {
	{"time", nullptr},
	{"latitude", "\u00B0"},
	{"longitude", "\u00B0"},
	{"altitude", "m"}
};

static String CSV_GPS(struct GPS const *const data) {
	return show_time(&data->time) + ','
		+ String(data->latitude,  7) + ','
		+ String(data->longitude, 7) + ','
		+ String(data->altitude,  7);
}

/* *************************************************************************** / ************************************ */
/* SD card */

static char const setting_filename[] = "/setting.txt";
static char const data_filename[] = "/data.csv";
static char const gps_filename[] = "/gps.csv";
static String data_header;
static String gps_header;

// static SPIClass SPI_1(HSPI);
static bool has_SD_card;

static void save_settings(void) {
	if (!has_SD_card) return;
	SDCARD_LOCK(sdcard_lock)
	File file = SD.open(setting_filename, "w", true);
	if (!file) {
		Serial.println("ERROR: failed to open setting file");
		return;
	}
	file.println(device_name);
	file.println(measure_interval / 1000);
	file.println(int(use_AP_mode));
	file.println(AP_SSID);
	file.println(AP_PASS);
	file.println(STA_SSID);
	file.println(STA_PASS);
	file.println(report_URL);
	file.close();
}

static bool load_settings(void) {
	char *e;
	String s;
	unsigned long int u;

	if (digitalRead(reset_pin) == HIGH) {
		Serial.println("Setting is not loaded because of hardware switch");
		return false;
	}
	SDCARD_LOCK(sdcard_lock)
	File file = SD.open(setting_filename, "r", false);
	if (!file) {
		Serial.println("Failed to open setting file");
		return false;
	}

	device_name = file.readStringUntil('\n');
	device_name.trim();
	s = file.readStringUntil('\n');
	s.trim();
	u = strtoul(s.c_str(), &e, 10);
	if (!*e && u >= 15 && u <= 900) measure_interval = u * 1000;
	s = file.readStringUntil('\n');
	s.trim();
	u = strtoul(s.c_str(), &e, 10);
	if (!*e) use_AP_mode = bool(u);
	AP_SSID = file.readStringUntil('\n');
	AP_SSID.trim();
	AP_PASS = file.readStringUntil('\n');
	AP_PASS.trim();
	STA_SSID = file.readStringUntil('\n');
	STA_SSID.trim();
	STA_PASS = file.readStringUntil('\n');
	STA_PASS.trim();
	report_URL = file.readStringUntil('\n');
	report_URL.trim();

	file.close();
	return true;
}

/* *************************************************************************** / ************************************ */
/* Real-time clock */

static RTC_Millis internal_clock;
static bool internal_clock_available = false;
static RTC_DS3231 external_clock;
static bool external_clock_available = false;

static bool clock_available(void) {
	return external_clock_available || internal_clock_available;
}

static void set_time(DateTime const datetime) {
	if (external_clock_available) {
		DEVICE_LOCK(device_lock)
		external_clock.adjust(datetime);
	}
	else {
		internal_clock.adjust(datetime);
		internal_clock_available = true;
	}
}

static DateTime get_time(void) {
	if (external_clock_available) {
		DEVICE_LOCK(device_lock)
		return external_clock.now();
	}
	else
		return internal_clock.now();
}

static String show_time(DateTime const *const datetime) {
	if (datetime->isValid())
		return datetime->timestamp();
	else
		return String("?");
}

/* *************************************************************************** / ************************************ */
/* Measurement */

#if SENSOR == SENSOR_BME280
	static Adafruit_BME280 BME280;
#elif SENSOR == SENSOR_SHT40
	static Adafruit_SHT4x SHT4x = Adafruit_SHT4x();;
#endif

static size_t const records_max_size = 60;
static std::deque<struct Data> data_records;
static std::deque<struct GPS> gps_records;

static void measure(void) {
	struct Data data;
	if (clock_available())
		data.time = get_time();
	else
		data.time = DateTime(0, 0, 0);
	DEVICE_LOCK(device_lock)
	#if SENSOR == SENSOR_BME280
		data.temperature = BME280.readTemperature();
		data.pressure = BME280.readPressure();
		data.humidity = BME280.readHumidity();
	#elif SENSOR == SENSOR_SHT40
		sensors_event_t temperature_event, humidity_event;
		SHT4x.getEvent(&humidity_event, &temperature_event);
		data.temperature = temperature_event.temperature;
		data.humidity = humidity_event.relative_humidity;
	#endif
	String const data_string = CSV_Data(&data);
	Serial.print("Measure ");
	Serial.println(data_string);

	if (data_records.size() >= records_max_size) data_records.pop_front();
	data_records.push_back(data);

	if (has_SD_card) {
		SDCARD_LOCK(sdcard_lock)
		File file = SD.open(data_filename, "a", true);
		try {
			file.println(data_string);
		}
		catch (...) {
			Serial.println("ERROR: failed to write weather data into SD card");
		}
		file.close();
	}

	redraw_display();
}

static void measure_thread(void) {
	for (;;)
		try {
			measure();
			std::unique_lock<std::mutex> wait_lock(wait_measure_mutex);
			// delay(measure_interval);
			//	std::this_thread::sleep_for(std::chrono::duration<unsigned long int, std::milli>(measure_interval));
			wait_measure_condition.wait_for(wait_lock, std::chrono::duration<unsigned long int, std::milli>(measure_interval));
		}
		catch (...) {
			Serial.println("ERROR: exception in measurement");
		}
}

/* *************************************************************************** / ************************************ */
/* WiFi */

// static DNSServer DNSd;

static void handle_WiFi_event(WiFiEvent_t const event) {
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

static signed int check_WiFi_status(void) {
	static signed int last_status = WL_NO_SHIELD;
	signed int status = WiFi.status();
	if (status != last_status) {
		last_status = status;
		Serial.println(status_message(status));
		if (status == WL_CONNECTED) {
			String const SSID = WiFi.SSID();
			Serial.print("WiFi SSID: ");
			Serial.println(WiFi.SSID());
			Serial.print("IP address: ");
			//	Serial.println(WiFi.localIP().toString());
			WiFi.localIP().printTo(Serial);
			Serial.println();
		}
		redraw_display();
	}
	return status;
}

static void wifi_thread(void) {
	for (;;)
		try {
			delay(WiFi_check_interval);
			check_WiFi_status();
		}
		catch (...) {
			Serial.println("ERROR: exception in WiFi checking");
		}
}

static void setup_WiFi(void) {
	WiFi.disconnect();
	WiFi.onEvent(handle_WiFi_event);
	WiFi.setHostname("WeatherStation");

	if (use_AP_mode) {
		/* WiFi access-point */
		WiFi.mode(WIFI_AP);
		// IPAddress my_IP_address = IPAddress(8, 8, 8, 8);
		// WiFi.softAPConfig(my_IP_address, my_IP_address, IPAddress(255, 255, 255, 0));
		while (!WiFi.softAP(AP_SSID.c_str(), AP_PASS, 1, 0, 4)) {
			Serial.println("ERROR: failed to create soft AP");
			Monitor.println("ERROR: WiFi AP");
			Monitor.display();
			delay(reinitialize_interval);
		}
		Serial.print("WiFi SSID: ");
		Serial.println(WiFi.softAPSSID());
		Serial.print("IP address: ");
		//	Serial.println(WiFi.softAPIP().toString());
		WiFi.softAPIP().printTo(Serial);
		Serial.println();

		/* DNS server */
		// static uint16_t const DNS_port = 53;
		// static String const DNS_domain("*");
		// while (!DNSd.start(DNS_port, DNS_domain, my_IP_address)) {
		// 	Serial.println("ERROR: failed to create DNS server");
		// 	Monitor.println("ERROR: DNS server");
		// 	delay(reinitialize_interval);
		// }
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
		while (millis() < WiFi_wait_time && WiFi.status() != WL_CONNECTED)
			delay(1);
		/* TODO: move to loop() */
		// std::thread(wifi_thread).detach();
	}
}

/* *************************************************************************** / ************************************ */
/* Web server */

static PsychicHttpServer HTTPd;
static PsychicHttpsServer HTTPSd;

static PROGMEM char const tls_key[] =
R"(-----BEGIN PRIVATE KEY-----
MIICdwIBADANBgkqhkiG9w0BAQEFAASCAmEwggJdAgEAAoGBAMNgxUb2U45aziNv
J+VWYP0CUOvjVwxe9pW8lrSevsX+thjYHiU3OOridQ/Q/GocJaZgCQDFAnL54FYN
pSmSIXtIuwjWEk+nYK7chDSwnkZ191dIQGTqI7BWglckqHc4y2auWD7JZCGIEFjK
yEyraHv2S2cAd0pwlWcUCICgte0PAgMBAAECgYEAicaI92SnQYCpUvWEtcX2+RQU
CnQzo2aoDqmBwPcc4rSepuBoSagqfACbuj6OcSlOJ4gbcS58bqXk2+odaTZCYtlC
+3ME9A+WNQCVjHm8qXugLyuw6LHHQkKZ0D5T4uiTCQCF4eGkfPwke3o8/H/0Dzqe
l49zwfT/xxHL16I4KgECQQDk1Fe0jVLjhVYbmODB3Zp81oktxo283oaTLnCMO09M
l9sM1WzZXS7ZiJe5jq6LY8ysiQMUMxl1TAxbGT+WZSUBAkEA2pOdXG78FhYuojpz
hwF8eMoQ3p+BfZ+4R5K+y2jeANoui4JrQ2XC23UJ9I51nzFO+7BlyKynvZXf0Ai4
tL7CDwJAAvbhP/yIs1vZ1revSbOmObHJyycEVQsI8UUrvhVSnKpm8w6cv2AeqEDF
vmijyDh9wUpxGMTksolOq6tzEG61AQJBAKRUhuKProcMdlMRjvnZbDOD99roIPrJ
skpdUYSsevw5DPVmQC6Tu0QzYiCzWkstTyx7GosdA5/Npk9Jv1RkdpECQCS/fBnl
nY7gZGKfoTbwiTBjAvRt+zfX7Ur0CUCYrZpTNTKtHNFh/8bImR+nnhGMqWV5DCUH
2yH9XHBPj+9AXdA=
-----END PRIVATE KEY-----
)";

static PROGMEM char const tls_cert[] =
R"(-----BEGIN CERTIFICATE-----
MIICAjCCAWsCFDEQA8xXkoJCKRZu7sn1fd0h9AsgMA0GCSqGSIb3DQEBCwUAMEAx
CzAJBgNVBAYTAkhLMQwwCgYDVQQKDANIS1UxDTALBgNVBAsMBEdlb2cxFDASBgNV
BAMMCzE5Mi4xNjguNC4xMB4XDTI0MDgwODA4MzEwM1oXDTI0MDkwNzA4MzEwM1ow
QDELMAkGA1UEBhMCSEsxDDAKBgNVBAoMA0hLVTENMAsGA1UECwwER2VvZzEUMBIG
A1UEAwwLMTkyLjE2OC40LjEwgZ8wDQYJKoZIhvcNAQEBBQADgY0AMIGJAoGBAMNg
xUb2U45aziNvJ+VWYP0CUOvjVwxe9pW8lrSevsX+thjYHiU3OOridQ/Q/GocJaZg
CQDFAnL54FYNpSmSIXtIuwjWEk+nYK7chDSwnkZ191dIQGTqI7BWglckqHc4y2au
WD7JZCGIEFjKyEyraHv2S2cAd0pwlWcUCICgte0PAgMBAAEwDQYJKoZIhvcNAQEL
BQADgYEAaruQki3Ot6X3wuu26HdA9V0S+ZwjrjOAaRPO1liO1qkkNyXKenL6yRpC
i2lVLW/CxmGgT6nT64MFUh4wIlZmbNuK+yTNdCaJI7I713YEjDAuomzS/myPyLDm
fPQsAPOfXW3x3SDYwVp+V8rcl/xegEyo1BQaKbTloCEKFmNfEAI=
-----END CERTIFICATE-----
)";

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

static PROGMEM char const web_home_html_1[] =
R"HTML(<html xmlns='http://www.w3.org/1999/xhtml'>
<head>
<meta content-type='application/xhtml+xml; charset=UTF-8' />
<meta charset='UTF-8' />
<meta name='viewport' content='width=device-width, initial-scale=1' />
<title>Weather data</title>
<link rel='stylesheet' type='text/css' href='style.css' />
</head>
<body>
<noscript>Javascript is required for this web page.</noscript>
<script type='text/javascript'>
	(function(p){document.readyState!=="loading"?p():document.addEventListener("DOMContentLoaded",p)})(function(p){
		window.Alone = {
			operator: location.pathname === "/operator",

)HTML";

static PROGMEM char const web_home_html_2[] =
R"HTML(
		};
		return import("./script.js").then(function(){}, p);
	}
	(function(SD_load_error){
		"use strict";
		console.log("Failed to load script from SD card:", SD_load_error);
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
		document.body.textContent = "";
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
			if (Alone.operator) {
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
			a_($a, "href", Alone.data_file);
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
			a_($a, "href", Alone.gps_file);
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
		if (Alone.operator) {
			var $report_auto;
			void function () {
				var $div, $label, $input;
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
			}();
		}
		var $data_list;
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
			Alone.data_fields.forEach(
				function (field) {
					var text = field.name[0].toUpperCase() + field.name.substring(1);
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
			Alone.gps_fields.forEach(
				function (field) {
					var text = field.name[0].toUpperCase() + field.name.substring(1);
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
							for (var j = 0; fields.length > j; ++j) {
								var $td = $E("td");
								s_($td, "border-style", "solid");
								s_($td, "border-width", "thin");
								s_($td, "text-align", "center");
								if (j === 0) c_($td, $T(string_from_Date(fields[j])));
								else c_($td, $T(fields[j]));
								c_($tr, $td);
							}
							c_($data_list, $tr);
						}
						return resolve();
					};
					xhr.open("GET", "data/recent.csv", true);
					xhr.send(null);
				}
			);
		}
		function GPS_load() {
			return new Promise(
				function (resolve, reject) {
					$GPS_list.textContent = null;
					$GPS_loading.hidden = false;
					var xhr = new XMLHttpRequest();
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
					xhr.send(null);
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
					refresh_timer = setInterval(data_load, Alone.measure_interval);
				}
				else {
					if (refresh_timer === null) return;
					clearInterval(refresh_timer);
					refresh_timer = null;
				}
			}
		);
		setTimeout(load_all, 3000);
		if (Alone.operator)
			void function () {
				if ("geolocation" in window.navigator) if (window.isSecureContext) {
					function make_body(timestamp, coords) {
						var body = new URLSearchParams;
						body.append("identity",  Alone.identity);
						body.append("time",      timestamp);
						body.append("latitude",  coords.latitude);
						body.append("longitude", coords.longitude);
						body.append("altitude",  coords.altitude);
						return body;
					}
					function upload_GPS(timestamp, coords) {
						var body = make_body(timestamp, coords);
						var xhr = new XMLHttpRequest();
						xhr.open("POST", "/gps/upload.exe", true);
						xhr.send(body);
					}
					function report_GPS(timestamp, coords) {
						var body = make_body(timestamp, coords);
						var xhr = new XMLHttpRequest();
						xhr.open("POST", Alone.report, true);
						xhr.send(body);
					}
					function record_GPS(spacetime) {
						if (spacetime === null || typeof spacetime === "undefined") return;
						var timestamp = string_from_Date(spacetime.timestamp, "T");
						var coords = spacetime.coords;
						if (Alone.operator) {
							upload_GPS(timestamp, coords);
							if ($report_auto.checked)
								report_GPS(timestamp, coords);
						}
					}
					function get_GPS() {
						navigator.geolocation.getCurrentPosition(
							record_GPS,
							function (error) {
								console.error("GeoLocationError: ", error.message);
							},
							{timeout: 15000, enableHighAccuracy: true}
						)
					}
					get_GPS();
					setInterval(get_GPS, Alone.measure_interval);
				}
			}();
	}));
</script>
</body>
</html>
)HTML";

static esp_err_t web_home_handle(PsychicRequest *const request) {
	PsychicStreamResponse response(request, "application/xhtml+xml; charset=UTF-8");
	response.addHeader("CONTENT-SECURITY-POLICY", "connect-src *");
	response.beginSend();
	response.write(reinterpret_cast<uint8_t const *>(web_home_html_1), sizeof web_home_html_1 - 1);
	response.print("\t\t\tidentity: '");
	response.print(javascript_escape(device_name));
	response.print("',\r\n\t\t\tmeasure_interval: '");
	response.print(measure_interval);
	response.print("',\r\n\t\t\tdata_file: '");
	response.print(javascript_escape(data_filename));
	response.print("',\r\n\t\t\tgps_file: '");
	response.print(javascript_escape(gps_filename));
	response.print("',\r\n\t\t\treport: '");
	response.print(javascript_escape(report_URL));
	response.print("',\r\n\t\t\tdata_fields: [");
	bool first = true;
	for (struct Field const field: data_fields) {
		if (first)
			first = false;
		else
			response.print(", ");
		response.print("{name:\'");
		response.print(javascript_escape(field.name));
		response.print("\',unit:\'");
		response.print(javascript_escape(field.unit));
		response.print("\'}");
	}
	response.print("],\r\n\t\t\tgps_fields: [");
	first = true;
	for (struct Field const field: gps_fields) {
		if (first)
			first = false;
		else
			response.print(", ");
		response.print("{name:\'");
		response.print(javascript_escape(field.name));
		response.print("\',unit:\'");
		response.print(javascript_escape(field.unit));
		response.print("\'}");
	}
	response.print("]\r\n");
	response.write(reinterpret_cast<uint8_t const *>(web_home_html_2), sizeof web_home_html_2 - 1);
	return response.endSend();
}

static PROGMEM char const web_icon_data[] = {
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

static esp_err_t web_icon_handle(PsychicRequest *const request) {
	return request->reply(200, "image/png", web_icon_data);
}

static esp_err_t web_data_recent_handle(PsychicRequest *const request) {
	PsychicStreamResponse response(request, "text/csv");
	response.addHeader("CONTENT-SECURITY-POLICY", "connect-src *");
	response.beginSend();
	response.println(data_header);
	for (struct Data const &record: data_records)
		response.println(CSV_Data(&record));
	return response.endSend();
}

static esp_err_t web_data_latest_handle(PsychicRequest *const request) {
	PsychicStreamResponse response(request, "text/csv");
	response.addHeader("CONTENT-SECURITY-POLICY", "connect-src *");
	response.beginSend();
	response.println(data_header);
	if (!data_records.empty())
		response.println(CSV_Data(&data_records.back()));
	return response.endSend();
}

static esp_err_t web_gps_recent_handle(PsychicRequest *const request) {
	PsychicStreamResponse response(request, "text/csv");
	response.addHeader("CONTENT-SECURITY-POLICY", "connect-src *");
	response.beginSend();
	response.println(gps_header);
	for (struct GPS const &record: gps_records)
		response.println(CSV_GPS(&record));
	return response.endSend();
}

static esp_err_t web_gps_latest_handle(PsychicRequest *const request) {
	PsychicStreamResponse response(request, "text/csv");
	response.addHeader("CONTENT-SECURITY-POLICY", "connect-src *");
	response.beginSend();
	response.println(data_header);
	if (!data_records.empty())
		response.println(CSV_Data(&data_records.back()));
	return response.endSend();
}

static esp_err_t web_gps_upload_handle(PsychicRequest *const request) {
	struct GPS gps = {.time = (uint32_t)0, .latitude = NAN, .longitude = NAN, .altitude = NAN};
	PsychicWebParameter *parameter;
	parameter = request->getParam("time");
	if (parameter != nullptr) {
		char const *const value = parameter->value().c_str();
		DateTime const datetime(value);
		if (datetime.isValid())
			gps.time = datetime;
		else {
			Serial.print("WARN: incorrect command time = ");
			Serial.println(value);
		}
	}
	parameter = request->getParam("latitude");
	if (parameter != nullptr) {
		char const *const value = parameter->value().c_str();
		char *end;
		double const x = strtod(value, &end);
		if (!*end)
			gps.latitude = x;
		else {
			Serial.print("WARN: incorrect GPS latitude = ");
			Serial.println(value);
		}
	}
	parameter = request->getParam("longitude");
	if (parameter != nullptr) {
		char const *const value = parameter->value().c_str();
		char *end;
		double const x = strtod(value, &end);
		if (!*end)
			gps.longitude = x;
		else {
			Serial.print("WARN: incorrect GPS longitude = ");
			Serial.println(value);
		}
	}
	parameter = request->getParam("altitude");
	if (parameter != nullptr) {
		char const *const value = parameter->value().c_str();
		char *end;
		double const x = strtod(value, &end);
		if (!*end)
			gps.altitude = x;
		else {
			Serial.print("WARN: incorrect GPS altitude = ");
			Serial.println(value);
		}
	}

	if (gps_records.size() >= records_max_size)
		gps_records.pop_front();
	gps_records.push_back(gps);

	if (has_SD_card) {
		SDCARD_LOCK(sdcard_lock)
		File file = SD.open(gps_filename, "a", true);
		try {
			file.println(CSV_GPS(&gps));
		}
		catch (...) {
			Serial.println("ERROR: failed to write GPS data into SD card");
		}
		file.close();
	}

	return request->reply(204, "text/plain", "");
}

static PROGMEM char const web_setting_html_1[] =
R"HTML(<html xmlns='http://www.w3.org/1999/xhtml'>
<head>
<meta content-type='application/xhtml+xml; charset=UTF-8' />
<meta charset='UTF-8' />
<meta name='viewport' content='width=device-width, initial-scale=1' />
<title>Settings</title>
<link rel='icon' type='image/png' href='favicon.ico' />
<link rel='stylesheet' type='text/css' href='style.css' />
</head>
<body>
<p><a href='operator'>&#x2190; Back</a></p>
)HTML";

static PROGMEM char const web_setting_html_2[] = R"HTML(
</body>
</html>
)HTML";

static PROGMEM char const web_setting_form_1[] = "<form\r\n\tid='";

static PROGMEM char const web_setting_form_2[] = R"HTML('
	action='setting.exe'
	method='POST'
	style='margin: 1ex; border: solid thin; padding: 1ex'
>
)HTML";

static String XML_escape(String const &string) {
	String result;
	for (char const c: string)
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

static void web_setting_form(PsychicStreamResponse *const response, char const *const id) {
	response->write(reinterpret_cast<uint8_t const *>(web_setting_form_1), sizeof web_setting_form_1 - 1);
	response->print(XML_escape(id));
	response->write(reinterpret_cast<uint8_t const *>(web_setting_form_2), sizeof web_setting_form_2 - 1);
}

static esp_err_t web_setting_handle(PsychicRequest *const request) {
	PsychicStreamResponse response(request, "application/xhtml+xml; charset=UTF-8");
	response.addHeader("CONTENT-SECURITY-POLICY", "connect-src *");
	response.beginSend();

	response.write(reinterpret_cast<uint8_t const *>(web_setting_html_1), sizeof web_setting_html_1 - 1);

	web_setting_form(&response, "set_time");
	response.print(
		"\t<label>\r\n"
		"\t\tCurrent time \r\n"
		"\t\t<input type='datetime-local' name='time' required='' />\r\n"
		"\t</label>\r\n"
		"\t<button type='submit'>Set</button>\r\n"
		"</form>\r\n"
	);

	web_setting_form(&response, "set_name");
	response.print(
		"\t<label>\r\n"
		"\t\tDevice ID\r\n"
		"\t\t<input type='text' name='name' required='' value='"
	);
	response.print(XML_escape(device_name));
	response.print(
		"' />\r\n"
		"\t</label>\r\n"
		"\t<button type='submit'>Set</button>\r\n"
		"</form>\r\n"
	);

	web_setting_form(&response, "set_interval");
	response.print(
		"\t<label>\r\n"
		"\t\tMeasure interval / seconds\r\n"
		"\t\t<input type='number' name='interval' min='10' max='900' required='' value='"
	);
	response.print(String(measure_interval / 1000));
	response.print(
		"' />"
		"\t</label>\r\n"
		"\t<button type='submit'>Set</button>\r\n"
		"</form>\r\n"
	);

	web_setting_form(&response, "set_wifi");
	response.print(
		"\t<label style='display: block'>\r\n"
		"\t\tProvide WiFi\r\n"
		"\t\t<select name='WiFi'>\r\n"
		"\t\t\t<option value='AP'"
	);
	if (use_AP_mode) response.print(" selected=''");
	response.print(
		">\r\n"
		"\t\t\t\tAccess point\r\n"
		"\t\t\t</option>\r\n"
		"\t\t\t<option value='STA'"
	);
	if (!use_AP_mode) response.print(" selected=''");
	response.print(
		">\r\n"
		"\t\t\t\tStation\r\n"
		"\t\t\t</option>\r\n"
		"\t\t</select>\r\n"
		"\t</label>\r\n"
		"\t<label style='display: block'>\r\n"
		"\t\tAP SSID\r\n"
		"\t\t<input name='APSSID' value='"
	);
	response.print(XML_escape(AP_SSID));
	response.print(
		"' />\r\n"
		"\t</label>\r\n"
		"\t<label style='display: block'>\r\n"
		"\t\tAP PASS\r\n"
		"\t\t<input name='APPASS' value='"
	);
	response.print(XML_escape(AP_PASS));
	response.print(
		"' />\r\n"
		"\t</label>\r\n"
		"\t<label style='display: block'>\r\n"
		"\t\tSTA SSID\r\n"
		"\t\t<input name='STASSID' value='"
	);
	response.print(XML_escape(STA_SSID));
	response.print(
		"' />\r\n"
		"\t</label>\r\n"
		"\t<label style='display: block'>\r\n"
		"\t\tSTA PASS\r\n"
		"\t\t<input name='STAPASS' value='"
	);
	response.print(XML_escape(STA_PASS));
	response.print(
		"' />\r\n"
		"\t</label>\r\n"
		"\t<button type='submit'>Set</button>\r\n\r\n"
		"</form>\r\n"
	);

	web_setting_form(&response, "set_report");
	response.print(
		"\t<label>\r\n"
		"\t\tReport URL\r\n"
		"\t\t<input type='text' name='report' required='' value='"
	);
	response.print(XML_escape(report_URL));
	response.print(
		"' />\r\n"
		"\t</label>\r\n"
		"\t<button type='submit'>Set</button>\r\n"
		"</form>\r\n"
	);

	web_setting_form(&response, "do_measure");
	response.print(
		"\t<label style='display: block'>\r\n"
		"\t\tConfirm\r\n"
		"\t\t<input type='checkbox' name='measure' />\r\n"
		"\t</label>\r\n"
		"\t<button type='submit'>Measure now</button>\r\n"
		"</form>\r\n"
	);

	web_setting_form(&response, "do_delete");
	response.print(
		"\t<label style='display: block'>\r\n"
		"\t\tConfirm\r\n"
		"\t\t<input type='checkbox' name='delete' />\r\n"
		"\t</label>\r\n"
		"\t<button type='submit'>Delete all data</button>\r\n"
		"</form>\r\n"
	);

	web_setting_form(&response, "do_reboot");
	response.print(
		"\t<label style='display: block'>Confirm \r\n"
		"\t\t<input type='checkbox' name='reboot' />\r\n"
		"\t</label>\r\n"
		"\t<button type='submit' name='reboot'>Reboot</button>\r\n"
		"</form>\r\n"
	);

	response.write(reinterpret_cast<uint8_t const *>(web_setting_html_2), sizeof web_setting_html_2 - 1);
	return response.endSend();
}

static PROGMEM char const web_command_html[] =
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

static esp_err_t web_command_handle(PsychicRequest *const request) {
	PsychicWebParameter *parameter;
	parameter = request->getParam("time");
	if (parameter != nullptr) {
		char const *const value = parameter->value().c_str();
		Serial.print("command time = ");
		Serial.println(value);
		DateTime const datetime(value);
		if (datetime.isValid())
			set_time(datetime);
		else {
			Serial.print("WARN: incorrect command time = ");
			Serial.println(value);
		}
	}
	parameter = request->getParam("name");
	if (parameter != nullptr) {
		char const *const value = parameter->value().c_str();
		Serial.print("command name = ");
		Serial.println(value);
		device_name = value;
		need_save = true;
	}
	parameter = request->getParam("interval");
	if (parameter != nullptr) {
		char const *const value = parameter->value().c_str();
		Serial.print("command interval = ");
		Serial.println(value);
		char *end;
		unsigned long int const x = strtoul(value, &end, 10);
		if (!*end && x >= 15 && x <= 900) {
			measure_interval = x * 1000;
			need_save = true;
		}
		else {
			Serial.print("WARN: incorrect command interval = ");
			Serial.println(value);
		}
	}
	parameter = request->getParam("WiFi");
	if (parameter != nullptr) {
		char const *const value = parameter->value().c_str();
		Serial.print("command WiFi = ");
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
			Serial.println("WARN: incorrect command WiFi = ");
			Serial.println(value);
		}
	}
	parameter = request->getParam("APSSID");
	if (parameter != nullptr) {
		char const *const value = parameter->value().c_str();
		Serial.print("command APSSID = ");
		Serial.println(value);
		AP_SSID = value;
		need_save = true;
	}
	parameter = request->getParam("APPASS");
	if (parameter != nullptr) {
		char const *const value = parameter->value().c_str();
		Serial.print("command APPASS = ");
		Serial.println(value);
		AP_PASS = value;
		need_save = true;
	}
	parameter = request->getParam("STASSID");
	if (parameter != nullptr) {
		char const *const value = parameter->value().c_str();
		Serial.print("command STASSID = ");
		Serial.println(value);
		STA_SSID = value;
		need_save = true;
	}
	parameter = request->getParam("STAPASS");
	if (parameter != nullptr) {
		char const *const value = parameter->value().c_str();
		Serial.print("command STAPASS = ");
		Serial.println(value);
		STA_PASS = value;
		need_save = true;
	}
	parameter = request->getParam("report");
	if (parameter != nullptr) {
		char const *const value = parameter->value().c_str();
		Serial.print("command report = ");
		Serial.println(value);
		report_URL = value;
		need_save = true;
	}
	if (request->hasParam("measure")) {
		Serial.println("command measure");
		wait_measure_condition.notify_all();
	}
	if (request->hasParam("delete")) {
		Serial.println("command delete");
		data_records.clear();
		SDCARD_LOCK(sdcard_lock)
		//	SD.remove(data_filename);
		File file = SD.open(data_filename, "w", true);
		try {
			file.println(data_header);
		}
		catch (...) {
			Serial.println("ERROR: failed to write header into data file");
		}
		file.close();
	}
	if (request->hasParam("reboot")) {
		Serial.println("command reboot");
		Serial.flush();
		need_reboot = true;
		need_save = false;
	}

	PsychicResponse response(request);
	response.setCode(303);
	response.setContentType("application/xhtml+xml; charset=UTF-8");
	response.addHeader("LOCATION", "/setting.html");
	response.setContent(reinterpret_cast<uint8_t const *>(web_command_html), sizeof web_command_html - 1);
	return response.send();
}

static void webserver_setup(void) {
	HTTPd.config.max_uri_handlers = 20;
	while (HTTPd.listen(HTTP_port) != ESP_OK) {
		Serial.println("ERROR: failed to start HTTP server");
		Monitor.println("Failed to start HTTPS server");
		Monitor.display();
		delay(reinitialize_interval);
	}
	Serial.println("HTTP server started");

	HTTPSd.config.max_uri_handlers = 20;
	while (HTTPSd.listen(HTTPS_port, tls_cert, tls_key) != ESP_OK) {
		Serial.println("ERROR: failed to start HTTPS server");
		Monitor.println("Failed to start HTTPS server");
		Monitor.display();
		delay(reinitialize_interval);
	}
	Serial.println("HTTPS server started");

	HTTPd .on("/",                HTTP_GET, web_home_handle);
	HTTPSd.on("/",                HTTP_GET, web_home_handle);
	HTTPd .on("/operator",        HTTP_GET, web_home_handle);
	HTTPSd.on("/operator",        HTTP_GET, web_home_handle);
	HTTPd .on("/favicon.ico",     HTTP_GET, web_icon_handle);
	HTTPSd.on("/favicon.ico",     HTTP_GET, web_icon_handle);
	HTTPd .on("/data/recent.csv", HTTP_GET, web_data_recent_handle);
	HTTPSd.on("/data/recent.csv", HTTP_GET, web_data_recent_handle);
	HTTPd .on("/data/latest.csv", HTTP_GET, web_data_latest_handle);
	HTTPSd.on("/data/latest.csv", HTTP_GET, web_data_latest_handle);
	HTTPd .on("/gps/recent.csv",  HTTP_GET, web_gps_recent_handle);
	HTTPSd.on("/gps/recent.csv",  HTTP_GET, web_gps_recent_handle);
	HTTPd .on("/gps/latest.csv",  HTTP_GET, web_gps_latest_handle);
	HTTPSd.on("/gps/latest.csv",  HTTP_GET, web_gps_latest_handle);
	HTTPd .on("/gps/upload.exe", HTTP_POST, web_gps_upload_handle);
	HTTPSd.on("/gps/upload.exe", HTTP_POST, web_gps_upload_handle);
	HTTPd .on("/setting.html",    HTTP_GET, web_setting_handle);
	HTTPSd.on("/setting.html",    HTTP_GET, web_setting_handle);
	HTTPd .on("/setting.exe",    HTTP_POST, web_command_handle);
	HTTPSd.on("/setting.exe",    HTTP_POST, web_command_handle);
	if (has_SD_card) {
		HTTPd .serveStatic("/", SD, "/");
		HTTPSd.serveStatic("/", SD, "/");
	};
}

/* *************************************************************************** / ************************************ */
/* Main procedures */

static void redraw_display(void) {
	Monitor.clearDisplay();
	Monitor.setCursor(0, FONT_OFFSET);
	if (has_SD_card)
		Monitor.println("SD card found");
	else
		Monitor.println("No SD card");
	if (use_AP_mode) {
		Monitor.println("WiFi SSID:");
		Monitor.println(WiFi.softAPSSID());
		Monitor.println("IP address:");
		//	Monitor.println(WiFi.softAPIP().toString());
		WiFi.softAPIP().printTo(Monitor);
		Monitor.println();
	}
	else {
		signed int status = WiFi.status();
		Monitor.println(status_message(status));
		if (WL_CONNECTED) {
			Monitor.println("WiFi SSID:");
			Monitor.println(WiFi.SSID());
			Monitor.println("IP address:");
			//	Monitor.println(WiFi.localIP().toString());
			WiFi.localIP().printTo(Monitor);
			Monitor.println();
		}
	}
	if (data_records.size())
		Monitor.println(pretty_Data(&data_records.back()));
	Monitor.display();
}

void loop(void) {
	delay(1000);

	if (need_save) {
		save_settings();
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
}

static void set_pthread_stack_size(size_t const stack_size) {
	static esp_pthread_cfg_t esp_pthread_cfg = esp_pthread_get_default_config();
	esp_pthread_cfg.stack_size = stack_size;
	esp_pthread_set_cfg(&esp_pthread_cfg);
}

void setup(void) {
	/* Constants*/
	data_header = data_fields[0].name;
	if (data_fields[0].unit)
		data_header = data_header + " (" + data_fields[0].unit + ')';
	for (unsigned int i = 1; i < sizeof data_fields / sizeof *data_fields; ++i) {
		data_header = data_header + ',' + data_fields[i].name;
		if (data_fields[i].unit)
			data_header = data_header + " (" + data_fields[i].unit + ')';
	}
	gps_header = gps_fields[0].name;
	if (gps_fields[0].unit)
		gps_header = gps_header + " (" + gps_fields[0].unit + ')';
	for (unsigned int i = 1; i < sizeof gps_fields / sizeof *gps_fields; ++i) {
		gps_header = gps_header + ',' + gps_fields[i].name;
		if (gps_fields[i].unit)
			gps_header = gps_header + " (" + gps_fields[i].unit + ')';
	}

	/* Reset pin */
	pinMode(reset_pin, INPUT);

	/* Serial port */
	Serial.begin(serial_baudrate);

	/* OLED display */
	Monitor.begin(SSD1306_SWITCHCAPVCC, 0x3C);
	Monitor.setFont(&TomThumb);
	Monitor.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
	Monitor.setRotation(3);
	Monitor.clearDisplay();
	Monitor.display();
	Monitor.setCursor(0, FONT_OFFSET);

	/* Start-up delay */
	delay(start_wait_time);

	/* SD */
	pinMode(SD_MISO, INPUT_PULLUP);
	SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
	has_SD_card = SD.begin(SD_CS, SPI);
	if (has_SD_card)
		load_settings();
	else {
		Serial.println("SD card not found");
		Monitor.println("SD card not found");
	}

	/* Clock */
	external_clock_available = external_clock.begin();
	if (external_clock_available) {
		Serial.println("Clock found");
		Monitor.println("Clock found");
	}
	else {
		Serial.println("Clock not found");
		Monitor.println("Clock not found");
	}

	/* Sensor */
	#if SENSOR == SENSOR_BME280
		while (!BME280.begin()) {
			Serial.println("ERROR: BME280 not found");
			Monitor.println("BME280 not found");
			Monitor.display();
			delay(reinitialize_interval);
		}
		Serial.println("BME280 found");
		Monitor.println("BME280 found");
		Monitor.display();
	#elif SENSOR == SENSOR_SHT40
		while (!SHT4x.begin()) {
			Serial.println("ERROR: SHT40 not found");
			Monitor.println("SHT40 not found");
			Monitor.display();
			delay(reinitialize_interval);
		}
		SHT4x.setPrecision(SHT4X_HIGH_PRECISION);
		SHT4x.setHeater(SHT4X_NO_HEATER);
		Serial.println("SHT40 found");
		Monitor.println("SHT40 found");
		Monitor.display();
	#endif

	/* WiFi */
	setup_WiFi();

	/* Web server */
	set_pthread_stack_size(32768);
	webserver_setup();

	/* Spawn measurement thread */
	//	set_pthread_stack_size(4096);
	std::thread(measure_thread).detach();
}

/* *************************************************************************** / ************************************ */
