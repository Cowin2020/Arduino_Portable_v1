Required libraries:
	RTClib
	Adafruit SSD1306
	Adafruit BME280
	Adafruit SHT4x Library
	ESP32_HTTPS_Server
		in file
			src/HTTPConnection.hpp
		replace
			#include <hwcrypto/sha.h>
		to
			#include <sha/sha_parallel_engine.h>
	PsychicHttp
		for 1.1.0
		in file
			src/PsychicHttpsServer.cpp
		replace at line 28
			this->ssl_config.cacert_pem = (uint8_t *)cert;
			this->ssl_config.cacert_len = strlen(cert)+1;
		to
		#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 2)
			this->ssl_config.servercert = (uint8_t *)cert;
			this->ssl_config.servercert_len = strlen(cert)+1;
		#else
			this->ssl_config.cacert_pem = (uint8_t *)cert;
			this->ssl_config.cacert_len = strlen(cert)+1;
		#endif

Setup:
	Copy files in "SDCard" to SD card if SD card is used
	Copy "config.h.example" to "config.h"
	Edit "config.h"

	SENSOR:
		either "SENSOR_BME280" or "SENSOR_SHT40"

	device_name:
		Identity of the device

	use_AP_mode:
		"true" to provide WiFi access point, or
		"false" to use an existing WiFi

	AP_SSID, AP_PASS:
		The SSID and password for the provided WiFi

	STA_SSID, STA_PASS:
		The SSID and password for the existing WiFi to connect

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

To modify elements:
	Edit "Alone1.ino":
		modify "measure" to fit how the values measured
		modify fields "struct Data", "data_fields[]" "CSV_Data()" and "pretty_Data()"
	Edit "SDCard/script.js":
		modify "data_fields"
