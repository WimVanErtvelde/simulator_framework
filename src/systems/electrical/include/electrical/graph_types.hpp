#pragma once

#include <string>
#include <vector>
#include <optional>

namespace elec_graph {

// ─── Enums ──────────────────────────────────────────────────────────

enum class NodeType { source, bus, junction, load };

enum class ConnectionType {
    wire,
    switch_,
    contactor,
    relay,
    circuit_breaker,
    potentiometer,
    selector
};

// ─── Source Parameters ──────────────────────────────────────────────

struct GcuParams {
    double overvolt_threshold = 32.0;
    double undervolt_threshold = 24.0;
    double trip_delay_ms = 500.0;
    std::string reset_mode = "manual";
};

struct BatteryParams {
    double capacity_ah = 40.0;
    double internal_resistance_ohm = 0.05;
    double charge_rate_max = 20.0;
    double initial_soc = 95.0;
    std::vector<std::pair<double, double>> soc_voltage_curve; // (soc_pct, voltage)
};

struct SourceParams {
    std::string subtype; // dc_generator, battery, external_power, apu_generator, starter_generator
    double nominal_voltage = 28.0;
    int engine_index = -1;
    double min_rpm_pct = 0.0;
    std::optional<GcuParams> gcu;
    std::optional<BatteryParams> battery;
};

// ─── Load Parameters ────────────────────────────────────────────────

struct LoadParams {
    std::string load_type; // avionics, motor, resistive, constant_current
    double nominal_current = 1.0;
    double inrush_current = 0.0;
    double inrush_duration_ms = 0.0;
};

// ─── Node (topology definition) ─────────────────────────────────────

struct Node {
    std::string id;
    NodeType type = NodeType::junction;
    std::string label;
    std::optional<SourceParams> source;
    std::optional<LoadParams> load;
};

// ─── Connection State (runtime) ─────────────────────────────────────

struct ConnectionState {
    bool closed = false;
    bool tripped = false;
    bool pulled = false;
    double pot_value = 0.0;
    int selector_position = 0;
    double current_through = 0.0;
    bool jammed = false;
    bool jammed_value = false;
};

// ─── Connection (topology definition) ───────────────────────────────

struct Connection {
    std::string id;
    ConnectionType type = ConnectionType::wire;
    std::string label;
    std::string from;
    std::string to;
    bool default_closed = true;
    bool pilot_controllable = true;
    double rating = 0.0;       // CB rating in amps
    std::string coil_bus;      // relay coil power source (node id)
};

// ─── Node State (runtime) ───────────────────────────────────────────

struct NodeState {
    bool powered = false;
    double voltage = 0.0;
    double current = 0.0;
    std::string power_source;

    // Source-specific runtime
    bool online = false;
    double battery_soc = -1.0;
    bool gcu_tripped = false;
    double gcu_trip_timer = 0.0;

    // Load-specific runtime
    double inrush_timer = 0.0;
};

// ─── FDM Inputs ─────────────────────────────────────────────────────

struct FdmInputs {
    std::vector<double> engine_n2_pct;
    bool apu_running = false;
    bool external_power_connected = false;
    bool on_ground = false;
    double ambient_temp_c = 15.0;
};

// ─── CAS Messages ───────────────────────────────────────────────────

struct CasMessageDef {
    std::string id;
    std::string text;
    std::string level;          // advisory, caution, warning
    std::string condition_type; // source_fail, low_voltage, low_soc, bus_dead, cb_trip
    std::string target_id;
    double threshold = 0.0;
};

struct CasMessage {
    std::string text;
    int level = 0; // 0=advisory, 1=caution, 2=warning
};

// ─── Topology ───────────────────────────────────────────────────────

struct Topology {
    std::string aircraft_type;
    std::string designation;
    std::string revision;
    double solver_rate_hz = 50.0;
    int propagation_passes = 4;

    std::vector<Node> nodes;
    std::vector<Connection> connections;
    std::vector<CasMessageDef> cas_messages;
};

} // namespace elec_graph
