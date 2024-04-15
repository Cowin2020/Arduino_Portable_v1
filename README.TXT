Required libraries:
	RTClib
	Adafruit SSD1306
	Adafruit BME280
	ESP32_HTTPS_Server
		in file
			src/HTTPConnection.hpp
		replace
			#include <hwcrypto/sha.h>
		to
			#include <sha/sha_parallel_engine.h>

Set up:
Copy files in "SDCard" to SD card if SD card is used
Copy "config.h.example" to "config.h"
Edit "config.h"

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
