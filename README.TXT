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
	Copy files in "SDCard" to SD card if SD card is used
	Copy "config.h.example" to "config.h"
	Edit "config.h"

	EMBED_FILE_PATH
		The path of SDCard directory

		It can be absolute path or relatived to the working path of
		the working directory of on compilation.

	ENABLE_SENSOR_SHT40
		If defined, SHT4x is supported.

	ENABLE_SENSOR_BME280
		If defined, BME280 is supported.

		SENSOR_BME280_I2C
			If defined, BME280 on I2C

		SENSOR_BME280_VSPI
			If defined, VSPI is used for BME280.
			VSPI is a hardware SPI in ESP32 chip.
			However, software SPI is suggested and you should
			normally turn off this option.

	ENABLE_SENSOR_PMS5003
		If defined, PMS5003 is supported.

		SENSOR_PMS5003_RX
		SENSOR_PMS5003_TX
		SENSOR_PMS5003_SET
		SENSOR_PMS5003_RESET
			The setting of the pin numbers of the sensor.

	DEFAULT_SENSORS
		Array of default sensor settings

		If the corresponding value is "std::nullopt",
		the sensor will not be used.
		If the corresponding value is "std::optional()",
		the sensor will be used and the value in the parentheses
		is available for corresponding device procedures.

	campaign_name
	device_name
		Identities of the campaign and device respectively

	use_AP_mode
		"true" to provide WiFi access point, or
		"false" to use an existing WiFi

	AP_SSID
	AP_PASS
		The SSID and password for the provided WiFi

	STA_SSID
	STA_PASS
		The SSID and password for the existing WiFi to connect

	monitor_URL
		URL of monitor server
		It traces the location of the device.

	upload_URL
		The URL of the data server

	upload_username
	upload_password
		Username and password for uploading data to data server

	measure_interval
		The time period in milliseconds to measure data

	timezone
		The difference in minutes between local time zone and UTC

		Positive number indicates time zones at the east of UTC.

	calibration_slope
	calibration_intercept
		Values for the calibration of the device

	display_refresh_interval
		Seconds of the display to refresh

	start_wait_time
		Milliseconds delay after device boot-up

	reboot_wait_time
		Milliseconds delay before reboot

	WiFi_wait_time
		Milliseconds for connecting existed WiFi

	WiFi_check_interval
		Interval in milliseconds to update WiFi state

	serial_baudrate
		Baud rate of the serial port

	reinitialize_interval
		Milliseconds delay to re-try after error on start up

	reset_pin
		The pin that controls the loading of settings on start up

	measure_interval_lowerbound
	measure_interval_upperbound
	calibration_slop_lowerbound
	calibration_slop_upperbound
	calibration_intercept_lowerbound
	calibration_intercept_upperbound
		The lower and upper bounds of the parameters

		They limit the parameters set by users either on setting
		page or from SD card.
