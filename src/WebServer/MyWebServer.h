#ifndef MYWEBSERVER_H
#define MYWEBSERVER_H

// #define WEBSERVER_H // https://github.com/me-no-dev/ESPAsyncWebServer/issues/418#issuecomment-667976368
#include "WebServer.h"
#include <ESPAsyncWebServer.h>


extern AsyncWebServer server;

void setupWebServer();
void handle_NotFound(AsyncWebServerRequest *request);
void drawGraph(AsyncWebServerRequest *request);
void handleRoot(AsyncWebServerRequest *request);

#endif