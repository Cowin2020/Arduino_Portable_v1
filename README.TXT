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
