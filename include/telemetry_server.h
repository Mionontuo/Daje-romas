#ifndef ATG_ENGINE_SIM_TELEMETRY_SERVER_H
#define ATG_ENGINE_SIM_TELEMETRY_SERVER_H

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class TelemetryServer {
public:
    struct ControlState {
        bool enabled = false;
        double throttle = 0.0;
        double brakePercent = 0.0;
        bool dynoHold = false;
        double dynoTargetRpm = 2500.0;
        bool ignitionEnabled = false;
        bool paused = false;
        double revLimiterRpm = 6500.0;
        double ignitionAdvanceOffsetDegrees = 0.0;
    };

    struct ChamberSample {
        double pressureBar = 0.0;
        double volumeCc = 0.0;
        double temperatureC = 0.0;
        double afr = 0.0;
        bool lit = false;
        std::vector<double> pressureTraceBar;
        std::vector<double> volumeTraceCc;
    };

    struct Snapshot {
        std::uint64_t timestampMs = 0;
        bool connected = false;
        std::string engineName;
        double rpm = 0.0;
        double crankAngleDegrees = 0.0;
        double throttle = 0.0;
        double torqueNm = 0.0;
        double powerKw = 0.0;
        double manifoldPressureBar = 0.0;
        double intakeAfr = 0.0;
        double exhaustO2Percent = 0.0;
        double vehicleSpeedKph = 0.0;
        double volumetricEfficiencyPercent = 0.0;
        int gear = 0;
        int simulationFrequency = 0;
        bool dynoEnabled = false;
        bool ignitionEnabled = false;
        bool paused = false;
        double displacementLitres = 0.0;
        double redlineRpm = 0.0;
        double brakePercent = 0.0;
        bool dynoHold = false;
        double dynoTargetRpm = 0.0;
        double revLimiterRpm = 0.0;
        double ignitionAdvanceOffsetDegrees = 0.0;
        bool remoteControlEnabled = false;
        std::vector<ChamberSample> chambers;
    };

public:
    TelemetryServer();
    ~TelemetryServer();

    bool start(std::uint16_t port = 8765);
    void stop();
    void publish(const Snapshot &snapshot);
    ControlState getControlState() const;

    bool isRunning() const { return m_running.load(); }
    std::uint16_t getPort() const { return m_port; }

private:
    void serve();
    std::string serializeTelemetry(const Snapshot &snapshot) const;
    std::string serializeTrace(const Snapshot &snapshot, int cylinder) const;
    std::string serializeControl(const ControlState &control) const;
    bool updateControlFromJson(const std::string &body);

private:
    std::atomic<bool> m_running;
    std::atomic<std::uintptr_t> m_listenSocket;
    std::uint16_t m_port;
    std::thread m_thread;
    mutable std::mutex m_snapshotMutex;
    Snapshot m_snapshot;
    mutable std::mutex m_controlMutex;
    ControlState m_control;
};

#endif /* ATG_ENGINE_SIM_TELEMETRY_SERVER_H */
