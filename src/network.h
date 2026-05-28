#pragma once
#include <NTPClient.h>
#include <WebServer.h>

extern NTPClient ntp;
extern WebServer server;
extern bool      refreshRequested;

void connectWiFi();
void setupServer();
