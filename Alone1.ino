#include <stdlib.h>
#include <deque>
#include <chrono>
#include <thread>
#include <mutex>
#include <Arduino.h>
#include <esp_pthread.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <SD.h>
#include <RTClib.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_BME280.h>

#include "config.h"

static std::mutex mutex_1;
#define DISPLAY_MUTEX mutex_1
#define HTTP_MUTEX mutex_1
#define MEASURE_MUTEX mutex_1

static bool need_save = false;
static bool need_reboot = false;

/*****************************************************************************/
/* SD card */

static char const setting_filename[] = "/setting.txt";

// static SPIClass SPI_1(HSPI);
static bool has_SD_card;

static void save_settings(void) {
	if (!has_SD_card) return;
	File file = SD.open(setting_filename, "w", true);
	if (!file) {
		Serial.println("Failed to open setting file");
		return;
	}
	file.println(measure_interval / 1000);
	file.println(int(use_AP_mode));
	file.println(AP_SSID);
	file.println(AP_PASS);
	file.println(STA_SSID);
	file.println(STA_PASS);
	file.close();
}

static void load_settings(void) {
	char *e;
	String s;
	unsigned long int u;

	if (!has_SD_card) return;
	File file = SD.open(setting_filename, "r", false);
	if (!file) {
		Serial.println("Failed to open setting file");
		return;
	}

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

	file.close();
}

/*****************************************************************************/
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
		std::lock_guard<std::mutex> display_lock(DISPLAY_MUTEX);
		external_clock.adjust(datetime);
	}
	else {
		internal_clock.adjust(datetime);
		internal_clock_available = true;
	}
}

static DateTime get_time(void) {
	if (external_clock_available) {
		std::lock_guard<std::mutex> display_lock(DISPLAY_MUTEX);
		return external_clock.now();
	}
	else {
		return internal_clock.now();
	}
}

/*****************************************************************************/
/* Measurement */

Adafruit_BME280 BME280;

struct Data {
	DateTime time;
	float temperature;
	float pressure;
	float humidity;
};

static size_t const records_max_size = 60;
static std::deque<Data> records;

inline static String show_time(DateTime const datetime) {
	if (datetime.isValid())
		return datetime.timestamp();
	else
		return String("?");
}

static void measure(void) {
	Data data;
	if (clock_available())
		data.time = get_time();
	else
		data.time = DateTime(0, 0, 0);
	std::lock_guard<std::mutex> device_lock(MEASURE_MUTEX);
	data.temperature = BME280.readTemperature();
	data.pressure = BME280.readPressure();
	data.humidity = BME280.readHumidity();

	Serial.printf(
		"Measure %s,%f,%f,%f\r\n",
		show_time(data.time).c_str(), data.temperature, data.pressure, data.humidity
	);

	if (records.size() >= records_max_size) records.pop_back();
	records.push_front(data);

	if (has_SD_card) {
		File file = SD.open("/all.csv", "a", true);
		file.printf(
			"%s,%f,%f,%f\r\n",
			show_time(data.time).c_str(), data.temperature, data.pressure, data.humidity
		);
		file.close();
	}
}

static void measure_thread(void) {
	for (;;)
		try {
			measure();
			// delay(measure_interval);
			std::this_thread::sleep_for(std::chrono::duration<unsigned long int, std::milli>(measure_interval));
		}
		catch (...) {
			std::lock_guard<std::mutex> display_lock(DISPLAY_MUTEX);
			Serial.println("ERROR: exception in measurement");
		}
}

/*****************************************************************************/
/* WiFi */

// static DNSServer DNSd;
static WebServer HTTPd(80);
static Adafruit_SSD1306 Monitor(128, 64);
static IPAddress my_IP_address;

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
		String const message = status_message(status);
		Serial.println(message);
		Monitor.clearDisplay();
		Monitor.setCursor(0, 0);
		Monitor.println(message);
		if (status == WL_CONNECTED) {
			String const SSID = WiFi.SSID();
			Serial.print("WiFi SSID: ");
			Serial.println(SSID);
			Monitor.println("WiFi SSID:");
			Monitor.println(SSID);
			my_IP_address = WiFi.localIP();
			Serial.print("IP address: ");
			Serial.println(my_IP_address.toString());
			Monitor.println("IP address:");
			Monitor.println(my_IP_address.toString());
		}
		Monitor.display();
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
			std::lock_guard<std::mutex> display_lock(DISPLAY_MUTEX);
			Serial.println("ERROR: exception in WiFi checking");
		}
}

static void setup_wifi(void) {
	WiFi.disconnect();
	WiFi.onEvent(handle_WiFi_event);

	if (use_AP_mode) {
		/* WiFi access-point */
		WiFi.disconnect();
		WiFi.mode(WIFI_AP);
		WiFi.setHostname("WeatherStation");
		// my_IP_address = IPAddress(8, 8, 8, 8);
		// WiFi.softAPConfig(my_IP_address, my_IP_address, IPAddress(255, 255, 255, 0));
		while (!WiFi.softAP(AP_SSID.c_str(), AP_PASS, 1, 0, 2)) {
			Serial.println("ERROR: failed to create soft AP");
			Monitor.println("ERROR: WiFi AP");
			Monitor.display();
			delay(reinitialize_interval);
		}
		my_IP_address = WiFi.softAPIP();
		String const SSID = WiFi.softAPSSID();
		Serial.print("WiFi SSID: ");
		Serial.println(SSID);
		Monitor.println("WiFi SSID:");
		Monitor.println(SSID);
		Serial.print("IP address: ");
		Serial.println(my_IP_address.toString());
		Monitor.println("IP address:");
		Monitor.println(my_IP_address.toString());
		Monitor.display();

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
			Monitor.println("ERROR: WiFi shield");
			Monitor.display();
			delay(reinitialize_interval);
		}
		while (millis() < WiFi_wait_time) {
			delay(2);
			if (WiFi.status() == WL_CONNECTED) {
				my_IP_address = WiFi.localIP();
				break;
			}
		}
		std::thread(wifi_thread).detach();
	}
}

/*****************************************************************************/
/* Web server */

static PROGMEM char const home_html[] =
R"HTML(<html xmlns='http://www.w3.org/1999/xhtml'>
<head>
<meta content-type='application/xhtml+xml; charset=UTF-8' />
<meta charset='UTF-8' />
<meta name='viewport' content='width=device-width, initial-scale=1' />
<title>Weather data</title>
<link rel='stylesheet' type='text/css' href='style.css' />
</head>
<body>
<noscript>Javascript is required for this webpage.</noscript>
<script type='text/javascript'>
	(function(p){document.readyState!=='loading'?p():document.addEventListener('DOMContentLoaded',p)})(function(){
		'use strict';
		var records = new Array();
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
		void function () {
			var $p, $a;
			$p = $E('p');
			s_($p, 'text-align', 'center');
			$a = $E('a');
			s_($a, 'margin', '1ex');
			s_($a, 'border', 'solid thin gray');
			s_($a, 'padding', '1ex');
			a_($a, 'href', 'setting.html');
			c_($a, $T('Settings'));
			c_($p, $a);
			$a = $E('a');
			s_($a, 'margin', '1ex');
			s_($a, 'border', 'solid thin gray');
			s_($a, 'padding', '1ex');
			a_($a, 'href', 'recent.csv');
			a_($a, 'download', '');
			c_($a, $T('Download recent data'));
			c_($p, $a);
			$a = $E('a');
			s_($a, 'margin', '1ex');
			s_($a, 'border', 'solid thin gray');
			s_($a, 'padding', '1ex');
			a_($a, 'href', 'all.csv');
			a_($a, 'download', '');
			c_($a, $T('Download all data'));
			c_($p, $a);
			c_(document.body, $p);
		}();
		var $reflesh, $auto;
		void function () {
			var $form, $button, $label, $input;
			$reflesh = $form = $E('form');
			s_($form, 'margin-top', '2ex');
			s_($form, 'margin-bottom', '2ex');
			$button = $E('button');
			a_($button, 'type', 'submit');
			c_($button, $T('Reflesh now'));
			c_($form, $button);
			$label = $E('label');
			s_($label, 'margin-left', '2ex');
			s_($label, 'padding', '1ex');
			c_($label, $T('Auto reflesh'));
			$auto = $input = $E('input');
			a_($input, 'type', 'checkbox');
			c_($label, $input);
			c_($form, $label);
			c_(document.body, $form);
		}();
		var $list;
		void function () {
			var $table, $thead, $tr, $th, $tbody;
			$table = $E('table');
			s_($table, 'width', '100%');
			s_($table, 'border-collapse', 'collapse');
			$thead = $E('thead');
			s_($thead, 'border-bottom-style', 'solid');
			$tr = $E('tr');
			$th = $E('th');
			c_($th, $T('Time'));
			c_($tr, $th);
			$th = $E('th');
			c_($th, $T('Temperature'));
			c_($tr, $th);
			$th = $E('th');
			c_($th, $T('Pressure'));
			c_($tr, $th);
			$th = $E('th');
			c_($th, $T('Humidity'));
			c_($tr, $th);
			c_($thead, $tr);
			c_($table, $thead);
			$list = $tbody = $E('tbody');
			c_($table, $tbody);
			c_(document.body, $table);
			return $tbody;
		}();
		function load() {
			$list.textContent = null;
			var $loading = $E('p');
			c_($loading, $T('Loading...'));
			c_(document.body, $loading);
			var xhr = new XMLHttpRequest();
			xhr.onloadend = function (event) {
				document.body.removeChild($loading);
				var text = xhr.responseText;
				if (text == null || xhr.status !== 200) {
					alert('Failed to load data');
					return;
				}
				var lines = text.split('\r\n');
				if (!lines || !(lines.length > 0)) return;
				for (var i = 1; lines.length > i; ++i) {
					if (!lines[i] || typeof lines[i] !== 'string') continue;
					var fields = lines[i].split(',');
					var $tr = $E('tr');
					for (var j = 0; fields.length > j; ++j) {
						var $td = $E('td');
						s_($td, 'border-style', 'solid');
						s_($td, 'border-width', 'thin');
						s_($td, 'text-align', 'center');
						c_($td, $T(fields[j]));
						c_($tr, $td);
					}
					c_($list, $tr);
				}
			};
			xhr.open('GET', '/recent.csv', true);
			xhr.send(null);
		}
		$reflesh.addEventListener(
			'submit',
			function (event) {
				event.preventDefault();
				load();
			}
		);
		var timer = null;
		$auto.addEventListener(
			'change',
			function (event) {
				if ($auto.checked) {
					if (timer !== null) return;
					timer = setInterval(load, 15000);
				}
				else {
					if (timer === null) return;
					clearInterval(timer);
					timer = null;
				}
			}
		);
		return new Promise(
			function (resolve) {
				return import('./script.js')
					.then(function () {}, load)
					.then(resolve);
			}
		);
	});
</script>
</body>
</html>
)HTML";

static void respond_home_html(void) {
	HTTPd.send(200, "application/xhtml+xml", home_html);
}

static PROGMEM char const web_icon[] = {
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
	/* checksum */
	0x37, 0x6E, 0xF9, 0x24
};

static void respond_web_icon(void) {
	HTTPd.send(200, "image/png", web_icon);
}

static void respond_data(void) {
	HTTPd.setContentLength(CONTENT_LENGTH_UNKNOWN);
	HTTPd.send(200, "text/csv", "time,temperature,pressure,humidity\r\n");
	for (Data data: records)
		HTTPd.sendContent(
			show_time(data.time) + ","
				+ data.temperature + ","
				+ data.pressure + ","
				+ data.humidity + "\r\n"
		);
}

static PROGMEM char const web_setting_head[] =
R"HTML(<html xmlns='http://www.w3.org/1999/xhtml'>
	<head>
		<meta content-type='application/xhtml+xml; charset=UTF-8' />
		<meta charset='UTF-8' />
		<meta name='viewport' content='width=device-width, initial-scale=1' />
		<title>Settings</title>
		<link rel='stylesheet' type='text/css' href='style.css' />
	</head>
	<body>
		<p><a href='./'>&#x2190; Back</a></p>
)HTML";

static PROGMEM char const web_setting_tail[] =
R"HTML(
	</body>
</html>
)HTML";

static void respond_setting_html(void) {
	static char const form_start[] =
		"\t\t<form action='command.exe' method='POST' style='margin: 1ex; border: solid thin; padding: 1ex'>";
	HTTPd.setContentLength(CONTENT_LENGTH_UNKNOWN);
	HTTPd.send(200, "application/xhtml+xml", web_setting_head);

	HTTPd.sendContent(form_start);
	HTTPd.sendContent("<label>Current time \
<input type='datetime-local' name='time' required='' /></label>\
<button type='submit'>Set</button></form>\r\n");

	HTTPd.sendContent(form_start);
	HTTPd.sendContent("<label>Measure interval / seconds \
<input type='number' name='measure' min='15' max='900' required='' value='");
	HTTPd.sendContent(String(measure_interval / 1000));
	HTTPd.sendContent("' /></label><button type='submit'>Set</button></form>\r\n");

	HTTPd.sendContent(form_start);
	HTTPd.sendContent("\r\n\t\t\t<label style='display: block'>Provide WiFi \
<input type='checkbox' name='useAP'");
	if (use_AP_mode) HTTPd.sendContent(" checked");
	HTTPd.sendContent(" /></label>\r\n\
\t\t\t<button type='submit'>Set</button>\r\n\
\t\t</form>\r\n");

	HTTPd.sendContent(form_start);
	HTTPd.sendContent("\r\n\t\t\t<label style='display: block'>Confirm \
<input type='checkbox' name='delete' /></label>\r\n\
\t\t\t<button type='submit'>Delete all data</button>\r\n\
\t\t</form>\r\n");

	HTTPd.sendContent(form_start);
	HTTPd.sendContent("\r\n\t\t\t<label style='display: block'>Confirm \
<input type='checkbox' name='reboot' /></label>\r\n\
\t\t\t<button type='submit' name='reboot'>Reboot</button>\r\n\
\t\t</form>\r\n");

	HTTPd.sendContent(web_setting_tail);
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
		<p>Command received. Redirect to <a href='./'>homepage.</a></p>
	</body>
</html>
)HTML";

static void respond_command(void) {
	if (HTTPd.hasArg("time")) {
		String const arg = HTTPd.arg("time");
		Serial.print("command time = ");
		Serial.println(arg);
		DateTime const datetime(arg.c_str());
		if (datetime.isValid()) {
			set_time(datetime);
			need_save = true;
		} else {
			Serial.print("WARN: incorrect command time = ");
			Serial.println(arg);
		}
	}
	if (HTTPd.hasArg("measure")) {
		String const arg = HTTPd.arg("measure");
		Serial.print("command measure = ");
		Serial.println(arg);
		char *end;
		unsigned long int value = strtoul(arg.c_str(), &end, 10);
		if (*end == 0 && value >= 15 && value <= 900) {
			measure_interval = value * 1000;
			need_save = true;
		} else {
			Serial.print("WARN: incorrect command measure = ");
			Serial.println(arg);
		}
	}
	if (HTTPd.hasArg("delete")) {
		Serial.println("command delete");
		SD.remove(setting_filename);
		records.clear();
	}
	if (HTTPd.hasArg("reboot")) {
		Serial.println("command reboot");
		Serial.flush();
		need_reboot = true;
		need_save = false;
	}
	HTTPd.sendHeader("Location", "/");
	HTTPd.send(303, "application/xhtml+xml", command_html);
}

// static void respond_web_not_found(void) {
// 	HTTPd.sendHeader("Location", String("http://") + my_IP_address.toString() + "/");
// 	HTTPd.send(302, "text/plain", "Redirect...");
// }

static void setup_webserver(void) {
	HTTPd.enableCORS(true);
	HTTPd.enableCrossOrigin(true);
	HTTPd.on("/", HTTP_GET, respond_home_html);
	HTTPd.on("/favicon.ico", HTTP_GET, respond_web_icon);
	HTTPd.on("/recent.csv", respond_data);
	HTTPd.on("/setting.html", HTTP_GET, respond_setting_html);
	HTTPd.on("/command.exe", HTTP_POST, respond_command);
	if (has_SD_card) HTTPd.serveStatic("/", SD, "/");
	// HTTPd.onNotFound(respond_web_not_found);
	HTTPd.begin();
}

/*****************************************************************************/
/* Main procedures */

void loop(void) {
	delay(2);
	// if (use_AP_mode) DNSd.processNextRequest();
	std::lock_guard<std::mutex> lock(HTTP_MUTEX);
	HTTPd.handleClient();
	if (need_save) {
		need_save = false;
		save_settings();
	}
	if (need_reboot) {
		need_reboot = false;
		delay(1000);
		esp_restart();
	}
}

void setup(void) {
	/* Serial port */
	Serial.begin(serial_baudrate);
	delay(start_wait_time);

	/* OLED display */
	Monitor.begin(SSD1306_SWITCHCAPVCC, 0x3C);
	Monitor.setRotation(2);
	Monitor.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
	Monitor.clearDisplay();
	Monitor.display();
	Monitor.setCursor(0, 0);

	/* SD */
	pinMode(SD_MISO, INPUT_PULLUP);
	SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
	has_SD_card = SD.begin(SD_CS, SPI);
	if (has_SD_card)
		load_settings();
	else {
		Serial.println("WARN: SD card not found");
		Monitor.println("No SD card");
		Monitor.display();
	}

	/* Clock */
	external_clock_available = external_clock.begin();

	/* Sensor */
	while (!BME280.begin()) {
		Serial.println("ERROR: BME280 not found");
		Monitor.println("ERROR: BME280");
		Monitor.display();
		delay(reinitialize_interval);
	}

	/* WiFi */
	setup_wifi();

	/* Web server */
	setup_webserver();

	/* Spawn measurement thread */
	static esp_pthread_cfg_t esp_pthread_cfg = esp_pthread_get_default_config();
	esp_pthread_cfg.stack_size = 4096;
	esp_pthread_cfg.inherit_cfg = true;
	esp_pthread_set_cfg(&esp_pthread_cfg);
	std::thread(measure_thread).detach();
}

/*****************************************************************************/
