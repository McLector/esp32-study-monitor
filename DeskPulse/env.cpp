#include "env.h"
#include <DHT.h>

static DHT dht(PIN_DHT, DHT11);

void envInit() { dht.begin(); }

void envApply(float tempC, float humidity) {
  if (isnan(tempC) || isnan(humidity)) return;  // DHT11 misses reads now and then
  app.live.tempC = tempC;
  app.live.humidity = humidity;
}

void taskEnvironment() { envApply(dht.readTemperature(), dht.readHumidity()); }
