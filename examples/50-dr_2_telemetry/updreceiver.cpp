#include "udpreceiver.h"

using namespace std;

static const USHORT PORT = 20777;
static const char *ADDRESS = "127.0.0.1";
static const int RECEIVER_TIMEOUT = 60 * 60 * 1000;	// 1 hour
static const int DIRT_RALLY_PACKET_SIZE = 264;

static std::mutex startStopMutex; 
static bool started = false;
static std::thread* pReceiverThread = NULL;
static SOCKET receiverSocket = INVALID_SOCKET;

static std::mutex copyDataMutex;
static TelemetryData latestTelemetryData {};

using namespace reshade::log;

void copyLatest(TelemetryData *pDest) {
	std::lock_guard<std::mutex> lock(copyDataMutex);
	*pDest = latestTelemetryData;
}

void resetTelemetryData() {
	std::lock_guard<std::mutex> lock(copyDataMutex);
	latestTelemetryData = {};
}


static float readFloatLE(char *buffer, int offset) {
	return *((float*) (buffer + offset));
}


static void fillTelemetryFrame(char *buffer, TelemetryData *pDst) {
	std::lock_guard<std::mutex> lock(copyDataMutex);
	pDst->time = readFloatLE(buffer, 0);
	pDst->speed = readFloatLE(buffer, 28);
	pDst->throttle = readFloatLE(buffer, 116);
	pDst->steer = readFloatLE(buffer, 120);
	pDst->brake = readFloatLE(buffer, 124);
	/* 
	pDst->clutch = readFloatLE(buffer, 128);
	pDst->gear = readFloatLE(buffer, 132);
	*/
	pDst->gforceLat = readFloatLE(buffer, 130);
	pDst->gforceLon = readFloatLE(buffer, 140);
}

static bool initSocket() {
	message(level::info, "Inside initSocket()");
	if (receiverSocket != INVALID_SOCKET) {
		message(level::error, "receiverSocket not set to INVALID_SOCKET(!!) ");
	}
	WSADATA wsaStartupData = {};
	int error = WSAStartup(MAKEWORD(2, 2), &wsaStartupData);
	if (error != 0) {
		message(level::error, "Could not find a usable version of Winsock.dll.");
		WSACleanup();
		return false;
	}

	receiverSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (receiverSocket == INVALID_SOCKET)
	{
		message(level::error, "Could not create udp socket.");
		WSACleanup();
		return false;
	}

	int receiverTimeoutVal = RECEIVER_TIMEOUT;
	if (0 != setsockopt(receiverSocket, SOL_SOCKET, SO_RCVTIMEO, (char *)&receiverTimeoutVal, sizeof(receiverTimeoutVal))) {
		message(level::error, "Could not set SO_RCVTIMEO on socket.");
		closesocket(receiverSocket);
		receiverSocket = INVALID_SOCKET;
		WSACleanup();
		return false;
	}

	sockaddr_in receiverAddress;
	receiverAddress.sin_family = AF_INET;
	receiverAddress.sin_port = htons(PORT);
	//inet_pton(AF_INET, ADDRESS, &(receiverAddress.sin_addr));	// TODO: Make ip address configurable
	receiverAddress.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	
	if (SOCKET_ERROR == bind(receiverSocket, (sockaddr *)&receiverAddress, sizeof(receiverAddress))) {
		message(level::error, "Could not bind upd socket");
		closesocket(receiverSocket);
		receiverSocket = INVALID_SOCKET;
		WSACleanup();
		return false;
	}

	return true;
}

#define MAX_INVALID_SIZE_PACKETS 10

static void receivePackets() {
	message(level::info, "Starting to receive packets.");
	char receiveBuffer[512] {};
	int bytesRead = 1;
	int numInvalid = 0;
	while (bytesRead > 0 && numInvalid < MAX_INVALID_SIZE_PACKETS) {
		bytesRead = recv(receiverSocket, receiveBuffer, sizeof(receiveBuffer), 0);
		if (bytesRead > 0) {
			if (bytesRead == DIRT_RALLY_PACKET_SIZE) {
				numInvalid = 0;
				fillTelemetryFrame(receiveBuffer, &latestTelemetryData);
			} else {
				numInvalid++;
				if (numInvalid < MAX_INVALID_SIZE_PACKETS) {
					message(level::error, "Invalid udp packet size received for Dirt Rally 2.0. Dropping ...");
					// drop packet
				}
			}
		}
	}

	if (numInvalid >= MAX_INVALID_SIZE_PACKETS) {
		message(level::error, "Received 10+ successive udp packets with invalid size for Dirt Rally 2.0. Exiting udp receiver thread.");
	} else if (bytesRead == 0) {
		message(level::info, "Shutdown of udp receiver thread, because socket was closed.");
	}
	else if (bytesRead < 0) {
		int lastWsaError = WSAGetLastError();
		switch (lastWsaError) {
		case WSAEINTR:
			// nop - this is expected behaviour if 'destroyReceiver is called
			break;
		default:
			message(level::warning, "Shutdown of udp receiver thrread because of SOCKET_ERROR");
			break;
		}
	}
}

bool startReceiver() {
	message(level::info, "Inside startReceiver()");
	std::lock_guard<std::mutex> lock(startStopMutex);

	if (started) {
		message(level::warning, "Dirt Rally 2.0 udp receiver already started. NOP");
		return false;
	}
	
	if (receiverSocket != INVALID_SOCKET) {
		message(level::error, "Dirt Rally 2.0 udp receiver is connected. NOP");
		return false;
	}

	initSocket();
	if (receiverSocket == INVALID_SOCKET) {
		message(level::error, "Could not initialize receiver socket.");
		return false;
	}

	resetTelemetryData();

	message(level::info, "Init of udp socket completed");
	message(level::warning, "Starting receiver thread...");
	pReceiverThread = new thread(receivePackets);
	message(level::warning, "Started receiver thread.");

	started = true;
	return true;
}

void destroyReceiver() {
	std::lock_guard<std::mutex> lock(startStopMutex);
	if (!started) {
		message(level::error, "Udp receiver already set to NOT started. NOP");
		return;
	}
	started = false;
	if (receiverSocket != INVALID_SOCKET) {
		closesocket(receiverSocket);
		if (pReceiverThread != NULL) {
			if (pReceiverThread->get_id() != std::this_thread::get_id()
					&& pReceiverThread->joinable()) {
				message(level::warning, "Joining receiver thread");
				pReceiverThread->join();
			}
			else {
				message(level::warning, "Receiver thread shutdown by itself.");
			}
			pReceiverThread = NULL;
		}
	}
	receiverSocket = INVALID_SOCKET;
	WSACleanup();
}
