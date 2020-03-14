#pragma GCC optimize ("-Ofast")
#pragma GCC push_options

#include "User_Setup.h"
#define USER_SETUP_LOADED

#include <Ticker.h>
#include <SPI.h>
#include <Wire.h>

#include <Adafruit_MCP23017.h>
#include <Adafruit_MCP4725.h>
#include <ESP8266WiFi.h>
#include <TFT_eSPI.h>
#include <sigma_delta.h>

#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <ArduinoJson.h>

#include <arduino.h>
#include "OTACore.h"

extern void zx_setup();
extern void zx_loop();

void setup() {

	if (!OTASetup())
	{
		zx_setup();
	}
}

void loop()
{
	if (!OTALoop())
	{
		zx_loop();
	}
}
