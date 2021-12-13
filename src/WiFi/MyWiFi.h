#ifndef MYWIFI_H
#define MYWIFI_H

#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager

void wifiSettingsSavedCallback();
void setupWiFiServer();
void resetWiFiSettings();

#endif