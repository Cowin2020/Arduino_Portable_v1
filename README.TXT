Board
	Additional Boards Manager URLs:
		https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_dev_index.json
	Boards Manager: esp32 (3.1.3)
	Board: TTGo LoRa32-OLED
	Board Revision: TTGo LoRa32-OLED (1.6.1)

Libraries:
	incbin (0.1.2)
	RTClib (2.1.4)
		Adafruit BusIO (1.17.4)
	Adafruit SSD1306 (2.5.17)
		Adafruit GFX Library (1.12.6)
	Adafruit SHT4x Library (1.0.5) optional
		Adafruit SH11x Library (2.1.14)
		Adafruit Unified Sensor (1.1.15)
	Adafruit BME280 Library (2.3.0) optional
	PMS Library (1.1.0) optional
	PsychicHttp (3.1.2)
		UrlEncode (1.0.1)
		ArduinoJson (7.4.3)

Setup:
	Copy files in "SDCard" to SD card if SD card is used.
	Copy "config.h.example" to "config.h".
	Edit "config.h", and read "doc/CONFIG.TXT" for feather description.

More documentation:
	"doc/CONFIG.TXT"      - settings in "config.h".
	"doc/ADD_SENSOR.TXT"  - tutorial of adding a new model of sensor
	"doc/DATA_UPLOAD.TXT" - protocol and data format to upload data
