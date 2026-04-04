#pragma once

#include <string>
#include <vector>
#include <optional>

namespace fuel_graph {

// ─── Enums ──────────────────────────────────────────────────────────

enum class NodeType { tank, pump, junction, engine_inlet };

enum class ConnectionType { line, valve, selector, check_valve };

enum class PumpSource { engine, electrical };

// ─── Tank Parameters ────────────────────────────────────────────────

struct TankParams {
    double capacity_kg = 0.0;
    double unusable_kg = 0.0;
    double initial_quantity_kg = 0.0;  // defaults to capacity_kg if 0
    double arm_m = 0.0;               // longitudinal CG arm
    // Layer 2 (optional — activates flow physics when set)
    std::optional<std::vector<double>> position_m;  // [x, y, z] body frame
};

// ─── Pump Parameters ────────────────────────────────────────────────

struct PumpParams {
    PumpSource source = PumpSource::engine;
    int engine_index = 0;          // for engine-driven pumps
    double min_rpm_pct = 0.0;      // engine RPM threshold
    std::string switch_id;         // for electrical pumps: panel switch ID
};

// ─── Node ───────────────────────────────────────────────────────────

struct Node {
    std::string id;
    NodeType type = NodeType::junction;
    std::string label;
    std::optional<TankParams> tank;
    std::optional<PumpParams> pump;
    int engine_index = 0;  // for engine_inlet nodes
};

// ─── Selector Definition ────────────────────────────────────────────

struct SelectorDef {
    std::string id;
    std::string switch_id;                 // panel control ID
    std::vector<std::string> positions;    // named positions: [BOTH, LEFT, RIGHT, OFF]
    std::string default_position;
};

// ─── Selector Reference (on a connection) ───────────────────────────

struct SelectorRef {
    std::string group;                     // references SelectorDef.id
    std::vector<std::string> open_in;      // position names where this connection is open
};

// ─── Connection ─────────────────────────────────────────────────────

struct Connection {
    std::string id;
    ConnectionType type = ConnectionType::line;
    std::string from;
    std::string to;
    std::string switch_id;                 // for valve type
    bool default_open = true;              // for valve type
    std::optional<SelectorRef> selector;   // for selector type
};

// ─── Node State (runtime) ───────────────────────────────────────────

struct NodeState {
    // Tank state
    double quantity_kg = 0.0;
    bool selected = false;        // tank is in active feed path

    // Pump state
    bool running = false;
    bool jammed = false;
    bool jammed_running = false;  // captured state when jammed

    // Engine inlet state
    bool fed = false;
    double fuel_flow_kgs = 0.0;
};

// ─── Connection State (runtime) ─────────────────────────────────────

struct ConnectionState {
    bool open = true;
    bool jammed = false;
    bool jammed_open = false;     // captured state when jammed
    double leak_rate_lph = 0.0;   // active leak rate (failure effect)
};

// ─── FDM Inputs ─────────────────────────────────────────────────────

struct FdmInputs {
    std::vector<double> engine_rpm_pct;
    std::vector<double> engine_fuel_demand_kgs;
    bool on_ground = false;
    double pitch_rad = 0.0;       // Layer 2 (reserved)
    double roll_rad = 0.0;        // Layer 2 (reserved)
    double accel_x = 0.0;         // Layer 2 (reserved)
    double accel_y = 0.0;         // Layer 2 (reserved)
    double accel_z = 0.0;         // Layer 2 (reserved)
};

// ─── CAS Messages ───────────────────────────────────────────────────

struct CasMessageDef {
    std::string id;
    std::string text;
    std::string level;            // advisory, caution, warning
    std::string condition_type;   // low_fuel, fuel_press, pump_fail
    std::string target_id;
    double threshold = 0.0;
};

struct CasMessage {
    std::string text;
    int level = 0;  // 0=advisory, 1=caution, 2=warning
};

// ─── Fuel Snapshot (solver output) ──────────────────────────────────

struct TankSnapshot {
    std::string id;
    double quantity_kg = 0.0;
    double capacity_kg = 0.0;
    double unusable_kg = 0.0;
    double quantity_norm = 0.0;   // quantity / capacity
    bool selected = false;
};

struct EngineSnapshot {
    int engine_index = 0;
    bool fed = false;
    double fuel_flow_kgs = 0.0;
};

struct FuelSnapshot {
    std::vector<TankSnapshot> tanks;
    std::vector<EngineSnapshot> engines;
    double total_fuel_kg = 0.0;
    double total_fuel_norm = 0.0;
    bool low_fuel_warning = false;
    double cg_contribution_m = 0.0;
    std::vector<CasMessage> cas_messages;
};

// ─── Topology ───────────────────────────────────────────────────────

struct Topology {
    std::string aircraft_type;
    std::string fuel_type;
    double density_kg_per_liter = 0.72;
    double low_fuel_threshold_norm = 0.10;

    std::vector<Node> nodes;
    std::vector<Connection> connections;
    std::vector<SelectorDef> selectors;
    std::vector<CasMessageDef> cas_messages;
};

} // namespace fuel_graph
