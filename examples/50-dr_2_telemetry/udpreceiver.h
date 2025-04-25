#pragma once
#include <iostream>
#include <thread>
#include <mutex>
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <reshade.hpp>

typedef struct telemetry_data {
	float time;
	float speed;
	float throttle;
	float steer;
	float brake;
	float gforceLat;
	float gforceLon;
} TelemetryData;

void copyLatest(TelemetryData *pOutput);
void resetTelemetryData();

bool startReceiver();
void destroyReceiver();
