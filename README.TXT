Board
	Additional Boards Manager URLs:
		https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_dev_index.json
	Boards Manager: esp32 (3.1.0)
	Board: TTGo LoRa32-OLED
	Board Revision: TTGo LoRa32-OLED (1.6.1)

Libraries:
	RTClib (2.1.4)
	Adafruit BusIO (1.17.4)
	Adafruit SSD1306 (2.5.16)
		Adafruit GFX Library (1.12.4)
	Adafruit BME280 (2.3.0) optional
	Adafruit SHT4x Library (1.0.5) optional
		Adafruit SH11x Library (2.1.14)
		Adafruit Unified Sensor (1.1.15)
	PsychicHttp (2.1.1)
		UrlEncode (1.0.1)
		ArduinoJson (7.4.2)

Modified library:
	For ESP32 (3.1.X) and PsychicHttp (1.2.1),
	patch file "src/async_worker.cpp" in library "PsychicHttp" to add
	"__attribute__((weak))" to functions "httpd_req_async_handler_begin"
	and "httpd_req_async_handler_complete".

Setup:
	Copy files in "SDCard" to SD card if SD card is used
	Copy "config.h.example" to "config.h"
	Edit "config.h"

	SENSOR:
		Currently only "SENSOR_SHT40" is supported

	organisation_name, campaign_name, device_name:
		Identities of the organisation, campaign and device respectively

	use_AP_mode:
		"true" to provide WiFi access point, or
		"false" to use an existing WiFi

	AP_SSID, AP_PASS:
		The SSID and password for the provided WiFi

	STA_SSID, STA_PASS:
		The SSID and password for the existing WiFi to connect

	monitor_URL:
		URL of monitor server

	upload_username, upload_password:
		Username and password for uploading data to data server

	start_wait_time:
		Milliseconds delay after device boot-up

	reboot_wait_time:
		Milliseconds delay before reboot

	WiFi_wait_time:
		Milliseconds for connecting existed WiFi

	WiFi_check_interval:
		Interval in milliseconds to update WiFi state

	serial_baudrate:
		Baud rate of the serial port

	reinitialize_interval:
		Milliseconds delay to re-try after error on start up

	reset_pin:
		Pin to control the loading of settings

To modify elements:
	Edit "Arduino_Portable_v1.ino":
		modify "SENSOR" for different sensors
		modify fields "struct Data", "data_fields[]" "CSV_Data()" and "pretty_Data()"
		modify "measure" to fit how the values measured
	Edit "SDCard/script.js":
		modify "data_fields"
