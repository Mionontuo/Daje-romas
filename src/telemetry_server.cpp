#include "../include/telemetry_server.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <utility>

namespace {
    std::string jsonEscape(const std::string &value) {
        std::ostringstream stream;
        for (const unsigned char character : value) {
            switch (character) {
            case '"': stream << "\\\""; break;
            case '\\': stream << "\\\\"; break;
            case '\b': stream << "\\b"; break;
            case '\f': stream << "\\f"; break;
            case '\n': stream << "\\n"; break;
            case '\r': stream << "\\r"; break;
            case '\t': stream << "\\t"; break;
            default:
                if (character < 0x20) {
                    stream << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(character);
                }
                else stream << character;
            }
        }

        return stream.str();
    }

    void writeNumber(std::ostringstream &stream, double value) {
        if (std::isfinite(value)) stream << value;
        else stream << "null";
    }

    bool sendAll(SOCKET socket, const std::string &response) {
        std::size_t sent = 0;
        while (sent < response.size()) {
            const int result = send(
                socket,
                response.data() + sent,
                static_cast<int>(std::min<std::size_t>(response.size() - sent, INT_MAX)),
                0);
            if (result <= 0) return false;
            sent += static_cast<std::size_t>(result);
        }

        return true;
    }

    bool jsonNumber(const std::string &body, const char *name, double &value) {
        const std::string key = std::string("\"") + name + "\"";
        std::size_t position = body.find(key);
        if (position == std::string::npos) return false;
        position = body.find(':', position + key.size());
        if (position == std::string::npos) return false;
        const char *start = body.c_str() + position + 1;
        char *end = nullptr;
        const double parsed = std::strtod(start, &end);
        if (end == start || !std::isfinite(parsed)) return false;
        value = parsed;
        return true;
    }

    bool jsonBoolean(const std::string &body, const char *name, bool &value) {
        const std::string key = std::string("\"") + name + "\"";
        std::size_t position = body.find(key);
        if (position == std::string::npos) return false;
        position = body.find(':', position + key.size());
        if (position == std::string::npos) return false;
        const std::size_t first = body.find_first_not_of(" \t\r\n", position + 1);
        if (first == std::string::npos) return false;
        if (body.compare(first, 4, "true") == 0) { value = true; return true; }
        if (body.compare(first, 5, "false") == 0) { value = false; return true; }
        return false;
    }
}

TelemetryServer::TelemetryServer()
    : m_running(false),
      m_listenSocket(static_cast<std::uintptr_t>(INVALID_SOCKET)),
      m_port(0) {
}

TelemetryServer::~TelemetryServer() {
    stop();
}

bool TelemetryServer::start(std::uint16_t port) {
    if (m_running.load()) return true;

    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;

    const SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }

    BOOL reuseAddress = TRUE;
    setsockopt(
        listenSocket,
        SOL_SOCKET,
        SO_REUSEADDR,
        reinterpret_cast<const char *>(&reuseAddress),
        sizeof(reuseAddress));

    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (bind(listenSocket, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) == SOCKET_ERROR
        || listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(listenSocket);
        WSACleanup();
        return false;
    }

    m_port = port;
    m_listenSocket.store(static_cast<std::uintptr_t>(listenSocket));
    m_running.store(true);
    m_thread = std::thread(&TelemetryServer::serve, this);
    return true;
}

void TelemetryServer::stop() {
    if (!m_running.exchange(false)) return;
    if (m_thread.joinable()) m_thread.join();
}

void TelemetryServer::publish(const Snapshot &snapshot) {
    std::lock_guard<std::mutex> lock(m_snapshotMutex);
    m_snapshot = snapshot;
}

TelemetryServer::ControlState TelemetryServer::getControlState() const {
    std::lock_guard<std::mutex> lock(m_controlMutex);
    return m_control;
}

void TelemetryServer::serve() {
    const SOCKET listenSocket = static_cast<SOCKET>(m_listenSocket.load());
    while (m_running.load()) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(listenSocket, &readSet);
        timeval timeout = { 0, 200000 };
        const int ready = select(0, &readSet, nullptr, nullptr, &timeout);
        if (ready <= 0 || !m_running.load()) continue;

        const SOCKET client = accept(listenSocket, nullptr, nullptr);
        if (client == INVALID_SOCKET) continue;

        DWORD receiveTimeout = 1000;
        setsockopt(
            client,
            SOL_SOCKET,
            SO_RCVTIMEO,
            reinterpret_cast<const char *>(&receiveTimeout),
            sizeof(receiveTimeout));

        char requestBuffer[4096] = {};
        const int received = recv(client, requestBuffer, sizeof(requestBuffer) - 1, 0);
        std::string request = received > 0 ? std::string(requestBuffer, received) : std::string();

        std::string body;
        std::string status = "200 OK";
        std::string contentType = "application/json; charset=utf-8";
        if (request.rfind("OPTIONS ", 0) == 0) {
            body = "{}";
        }
        else if (request.rfind("GET /telemetry", 0) == 0) {
            Snapshot snapshot;
            {
                std::lock_guard<std::mutex> lock(m_snapshotMutex);
                snapshot = m_snapshot;
            }
            body = serializeTelemetry(snapshot);
        }
        else if (request.rfind("GET /trace", 0) == 0) {
            int cylinder = 0;
            const std::size_t query = request.find("cylinder=");
            if (query != std::string::npos) {
                try { cylinder = std::stoi(request.substr(query + 9)); }
                catch (...) { cylinder = 0; }
            }

            Snapshot snapshot;
            {
                std::lock_guard<std::mutex> lock(m_snapshotMutex);
                snapshot = m_snapshot;
            }
            body = serializeTrace(snapshot, cylinder);
        }
        else if (request.rfind("GET /control", 0) == 0) {
            body = serializeControl(getControlState());
        }
        else if (request.rfind("POST /control", 0) == 0) {
            const std::size_t separator = request.find("\r\n\r\n");
            const std::string payload = separator == std::string::npos
                ? std::string()
                : request.substr(separator + 4);
            if (!updateControlFromJson(payload)) {
                status = "400 Bad Request";
                body = "{\"error\":\"invalid_control\"}";
            }
            else body = serializeControl(getControlState());
        }
        else if (request.rfind("GET /health", 0) == 0 || request.rfind("GET / ", 0) == 0) {
            body = "{\"status\":\"ok\",\"service\":\"engine-sim-telemetry\",\"version\":1}";
        }
        else {
            status = "404 Not Found";
            body = "{\"error\":\"not_found\"}";
        }

        std::ostringstream response;
        response << "HTTP/1.1 " << status << "\r\n"
                 << "Content-Type: " << contentType << "\r\n"
                 << "Content-Length: " << body.size() << "\r\n"
                 << "Access-Control-Allow-Origin: *\r\n"
                 << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                 << "Access-Control-Allow-Headers: Content-Type\r\n"
                 << "Cache-Control: no-store\r\n"
                 << "Connection: close\r\n\r\n"
                 << body;
        sendAll(client, response.str());
        shutdown(client, SD_BOTH);
        closesocket(client);
    }

    closesocket(listenSocket);
    m_listenSocket.store(static_cast<std::uintptr_t>(INVALID_SOCKET));
    WSACleanup();
}

std::string TelemetryServer::serializeTelemetry(const Snapshot &snapshot) const {
    std::ostringstream stream;
    stream << std::setprecision(10)
           << "{\"version\":1"
           << ",\"timestamp_ms\":" << snapshot.timestampMs
           << ",\"connected\":" << (snapshot.connected ? "true" : "false")
           << ",\"engine_name\":\"" << jsonEscape(snapshot.engineName) << "\""
           << ",\"rpm\":"; writeNumber(stream, snapshot.rpm);
    stream << ",\"crank_angle_deg\":"; writeNumber(stream, snapshot.crankAngleDegrees);
    stream << ",\"throttle\":"; writeNumber(stream, snapshot.throttle);
    stream << ",\"torque_nm\":"; writeNumber(stream, snapshot.torqueNm);
    stream << ",\"power_kw\":"; writeNumber(stream, snapshot.powerKw);
    stream << ",\"manifold_pressure_bar\":"; writeNumber(stream, snapshot.manifoldPressureBar);
    stream << ",\"intake_afr\":"; writeNumber(stream, snapshot.intakeAfr);
    stream << ",\"exhaust_o2_percent\":"; writeNumber(stream, snapshot.exhaustO2Percent);
    stream << ",\"vehicle_speed_kph\":"; writeNumber(stream, snapshot.vehicleSpeedKph);
    stream << ",\"volumetric_efficiency_percent\":"; writeNumber(stream, snapshot.volumetricEfficiencyPercent);
    stream << ",\"gear\":" << snapshot.gear
           << ",\"simulation_frequency\":" << snapshot.simulationFrequency
           << ",\"dyno_enabled\":" << (snapshot.dynoEnabled ? "true" : "false")
           << ",\"ignition_enabled\":" << (snapshot.ignitionEnabled ? "true" : "false")
           << ",\"paused\":" << (snapshot.paused ? "true" : "false")
           << ",\"displacement_l\":"; writeNumber(stream, snapshot.displacementLitres);
    stream << ",\"redline_rpm\":"; writeNumber(stream, snapshot.redlineRpm);
    stream << ",\"brake_percent\":"; writeNumber(stream, snapshot.brakePercent);
    stream << ",\"dyno_hold\":" << (snapshot.dynoHold ? "true" : "false");
    stream << ",\"dyno_target_rpm\":"; writeNumber(stream, snapshot.dynoTargetRpm);
    stream << ",\"rev_limiter_rpm\":"; writeNumber(stream, snapshot.revLimiterRpm);
    stream << ",\"ignition_advance_offset_deg\":"; writeNumber(stream, snapshot.ignitionAdvanceOffsetDegrees);
    stream << ",\"remote_control_enabled\":" << (snapshot.remoteControlEnabled ? "true" : "false")
           << ",\"cylinders\":[";
    for (std::size_t i = 0; i < snapshot.chambers.size(); ++i) {
        if (i != 0) stream << ',';
        const ChamberSample &chamber = snapshot.chambers[i];
        stream << "{\"index\":" << i << ",\"pressure_bar\":"; writeNumber(stream, chamber.pressureBar);
        stream << ",\"volume_cc\":"; writeNumber(stream, chamber.volumeCc);
        stream << ",\"temperature_c\":"; writeNumber(stream, chamber.temperatureC);
        stream << ",\"afr\":"; writeNumber(stream, chamber.afr);
        stream << ",\"lit\":" << (chamber.lit ? "true" : "false") << '}';
    }
    stream << "]}";
    return stream.str();
}

std::string TelemetryServer::serializeControl(const ControlState &control) const {
    std::ostringstream stream;
    stream << std::setprecision(10)
           << "{\"version\":1"
           << ",\"enabled\":" << (control.enabled ? "true" : "false")
           << ",\"throttle\":"; writeNumber(stream, control.throttle);
    stream << ",\"brake_percent\":"; writeNumber(stream, control.brakePercent);
    stream << ",\"dyno_hold\":" << (control.dynoHold ? "true" : "false")
           << ",\"dyno_target_rpm\":"; writeNumber(stream, control.dynoTargetRpm);
    stream << ",\"ignition_enabled\":" << (control.ignitionEnabled ? "true" : "false")
           << ",\"paused\":" << (control.paused ? "true" : "false")
           << ",\"rev_limiter_rpm\":"; writeNumber(stream, control.revLimiterRpm);
    stream << ",\"ignition_advance_offset_deg\":"; writeNumber(stream, control.ignitionAdvanceOffsetDegrees);
    stream << '}';
    return stream.str();
}

bool TelemetryServer::updateControlFromJson(const std::string &body) {
    if (body.empty()) return false;
    std::lock_guard<std::mutex> lock(m_controlMutex);
    bool changed = false;
    bool booleanValue = false;
    double numberValue = 0.0;

    if (jsonBoolean(body, "enabled", booleanValue)) { m_control.enabled = booleanValue; changed = true; }
    if (jsonNumber(body, "throttle", numberValue)) { m_control.throttle = std::max(0.0, std::min(1.0, numberValue)); changed = true; }
    if (jsonNumber(body, "brake_percent", numberValue)) { m_control.brakePercent = std::max(0.0, std::min(100.0, numberValue)); changed = true; }
    if (jsonBoolean(body, "dyno_hold", booleanValue)) { m_control.dynoHold = booleanValue; changed = true; }
    if (jsonNumber(body, "dyno_target_rpm", numberValue)) { m_control.dynoTargetRpm = std::max(500.0, std::min(20000.0, numberValue)); changed = true; }
    if (jsonBoolean(body, "ignition_enabled", booleanValue)) { m_control.ignitionEnabled = booleanValue; changed = true; }
    if (jsonBoolean(body, "paused", booleanValue)) { m_control.paused = booleanValue; changed = true; }
    if (jsonNumber(body, "rev_limiter_rpm", numberValue)) { m_control.revLimiterRpm = std::max(1000.0, std::min(20000.0, numberValue)); changed = true; }
    if (jsonNumber(body, "ignition_advance_offset_deg", numberValue)) { m_control.ignitionAdvanceOffsetDegrees = std::max(-15.0, std::min(15.0, numberValue)); changed = true; }
    return changed;
}

std::string TelemetryServer::serializeTrace(const Snapshot &snapshot, int cylinder) const {
    std::ostringstream stream;
    stream << std::setprecision(8) << "{\"version\":1,\"timestamp_ms\":" << snapshot.timestampMs;
    if (snapshot.chambers.empty()) {
        stream << ",\"cylinder\":0,\"pressure_bar\":[],\"volume_cc\":[]}";
        return stream.str();
    }

    cylinder = std::max(0, std::min(cylinder, static_cast<int>(snapshot.chambers.size()) - 1));
    const ChamberSample &chamber = snapshot.chambers[cylinder];
    stream << ",\"cylinder\":" << cylinder << ",\"pressure_bar\":[";
    for (std::size_t i = 0; i < chamber.pressureTraceBar.size(); ++i) {
        if (i != 0) stream << ',';
        writeNumber(stream, chamber.pressureTraceBar[i]);
    }
    stream << "],\"volume_cc\":[";
    for (std::size_t i = 0; i < chamber.volumeTraceCc.size(); ++i) {
        if (i != 0) stream << ',';
        writeNumber(stream, chamber.volumeTraceCc[i]);
    }
    stream << "]}";
    return stream.str();
}
