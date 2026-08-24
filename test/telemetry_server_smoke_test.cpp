#include "../include/telemetry_server.h"

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#include <iostream>
#include <string>

namespace {
    std::string get(const char *path) {
        const SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (client == INVALID_SOCKET) return {};

        sockaddr_in address = {};
        address.sin_family = AF_INET;
        address.sin_port = htons(18765);
        inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
        if (connect(client, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) == SOCKET_ERROR) {
            closesocket(client);
            return {};
        }

        const std::string request = std::string("GET ") + path
            + " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
        if (send(client, request.data(), static_cast<int>(request.size()), 0) <= 0) {
            closesocket(client);
            return {};
        }

        std::string response;
        char buffer[4096];
        for (;;) {
            const int received = recv(client, buffer, sizeof(buffer), 0);
            if (received <= 0) break;
            response.append(buffer, received);
        }
        closesocket(client);
        return response;
    }

    bool contains(const std::string &text, const char *expected) {
        if (text.find(expected) != std::string::npos) return true;
        std::cerr << "Missing response fragment: " << expected << '\n';
        return false;
    }
}

int main() {
    TelemetryServer server;
    if (!server.start(18765)) {
        std::cerr << "Could not start the loopback telemetry server\n";
        return 1;
    }

    TelemetryServer::Snapshot snapshot;
    snapshot.timestampMs = 123456;
    snapshot.connected = true;
    snapshot.engineName = "CI \"Telemetry\"";
    snapshot.rpm = 4321.5;
    snapshot.torqueNm = 350.2;

    TelemetryServer::ChamberSample chamber;
    chamber.pressureBar = 42.5;
    chamber.volumeCc = 55.0;
    chamber.pressureTraceBar = { 1.0, 12.0, 42.5, 8.0 };
    chamber.volumeTraceCc = { 500.0, 180.0, 55.0, 320.0 };
    snapshot.chambers.push_back(chamber);
    server.publish(snapshot);

    const std::string health = get("/health");
    const std::string telemetry = get("/telemetry");
    const std::string trace = get("/trace?cylinder=0");
    server.stop();

    const bool passed =
        contains(health, "200 OK")
        && contains(health, "engine-sim-telemetry")
        && contains(telemetry, "CI \\\"Telemetry\\\"")
        && contains(telemetry, "\"rpm\":4321.5")
        && contains(telemetry, "\"pressure_bar\":42.5")
        && contains(trace, "\"pressure_bar\":[1,12,42.5,8]")
        && contains(trace, "\"volume_cc\":[500,180,55,320]");

    if (!passed) return 2;
    std::cout << "Telemetry loopback smoke test passed\n";
    return 0;
}
