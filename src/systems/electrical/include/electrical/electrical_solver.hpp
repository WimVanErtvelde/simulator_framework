#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

namespace elec_sys {

// ─── Topology Data Structures (loaded from JSON) ─────────────────

struct GcuParams {
    double overvolt_threshold = 32.0;
    double undervolt_threshold = 20.0;
    double overfreq_threshold = 420.0;
    double underfreq_threshold = 380.0;
    double trip_delay_ms = 500.0;
    std::string reset_mode = "manual"; // "manual" or "auto"
};

struct BatteryParams {
    double capacity_ah = 40.0;
    double internal_resistance_ohm = 0.05;
    double charge_rate_max = 20.0;
    std::vector<std::pair<double, double>> soc_voltage_curve; // (soc_pct, voltage)
};

struct TruParams {
    std::string input_bus;
    double output_voltage = 28.0;
    double max_current = 100.0;
};

struct InverterParams {
    std::string input_bus;
    double output_voltage = 115.0;
    double output_frequency = 400.0;
};

struct SourceDef {
    std::string id;
    std::string type; // ac_generator, dc_generator, starter_generator, apu_generator, external_power, battery, transformer_rectifier, inverter
    std::string label;
    double nominal_voltage = 28.0;
    double nominal_frequency = 0.0;
    int engine_index = -1;
    double min_rpm_pct = 0.0;
    double max_current = 200.0;
    std::optional<GcuParams> gcu;
    std::optional<BatteryParams> battery;
    std::optional<TruParams> tru;
    std::optional<InverterParams> inverter;
};

struct BusDef {
    std::string id;
    std::string label;
    std::string type; // "dc", "ac", "ac_wild"
    double nominal_voltage = 28.0;
    double nominal_frequency = 0.0;
    int shed_priority = 0;
};

struct SwitchDef {
    std::string id;
    std::string type; // contactor, relay, switch, diode, bus_tie, current_limiter
    std::string label;
    std::string source; // source or bus ID
    std::string target; // bus ID
    bool default_closed = false;
    std::string coil_bus; // for relays
    double switching_delay_ms = 80.0;
    bool pilot_controllable = true;
    double current_rating = 0.0;
};

struct CircuitBreakerDef {
    std::string id;
    double rating = 5.0;
    std::string trip_curve = "thermal_magnetic";
    std::string panel_location;
};

struct LoadDef {
    std::string id;
    std::string label;
    std::string bus;
    std::string type; // constant_power, constant_current, resistive, motor, avionics
    double nominal_current = 1.0;
    double inrush_current = 0.0;
    double inrush_duration_ms = 0.0;
    CircuitBreakerDef cb;
    bool essential = false;
    std::vector<std::string> affected_systems;
};

struct DirectConnection {
    std::string from;
    std::string to;
};

struct CasMessageDef {
    std::string id;
    std::string text;
    std::string level; // advisory, caution, warning
    std::string condition_type; // bus_dead, source_fail, cb_trip, low_voltage, low_soc, custom
    std::string target_id;
    double threshold = 0.0;
};

struct Topology {
    std::string aircraft_type;
    std::string designation;
    std::string revision;
    double solver_rate_hz = 50.0;
    int propagation_passes = 4;
    double contactor_delay_ms = 80.0;
    std::vector<SourceDef> sources;
    std::vector<BusDef> buses;
    std::vector<SwitchDef> switches;
    std::vector<LoadDef> loads;
    std::vector<DirectConnection> direct_connections;
    std::vector<CasMessageDef> cas_messages;
};

// ─── Runtime State ───────────────────────────────────────────────

struct SourceState {
    bool online = false;
    double voltage = 0.0;
    double current = 0.0;
    double frequency = 0.0;
    double battery_soc = 100.0; // only for batteries
    bool gcu_tripped = false;
    double gcu_trip_timer = 0.0;
};

struct BusState {
    bool powered = false;
    double voltage = 0.0;
    double current = 0.0;
    std::string source_id;
};

struct SwitchState {
    bool closed = false;
    bool tripped = false;
    double transition_timer = 0.0; // remaining delay in seconds
    bool commanded_position = false;
};

struct LoadState {
    bool powered = false;
    double current = 0.0;
    bool cb_closed = true;
    bool cb_tripped = false;
    bool cb_pulled = false;
    double inrush_timer = 0.0;
};

struct FdmInputs {
    std::vector<double> engine_n2_pct;
    bool apu_running = false;
    bool external_power_connected = false;
    bool on_ground = false;
    double ambient_temp_c = 15.0;
};

struct CasMessage {
    std::string text;
    int level = 0; // 0=advisory, 1=caution, 2=warning
};

// ─── Solver ──────────────────────────────────────────────────────

class ElectricalSolver {
public:
    ElectricalSolver();
    ~ElectricalSolver();

    /// Load topology from JSON file. Returns true on success.
    bool loadTopology(const std::string& json_path);

    /// Load topology from YAML file. Returns true on success.
    bool loadTopologyYaml(const std::string& yaml_path);

    /// Load topology from a pre-built Topology struct.
    void loadTopology(const Topology& topo);

    /// Access the loaded topology
    const Topology& getTopology() const { return topology_; }

    /// Reset all runtime state to initial conditions
    void reset();

    /// Run one solver step. dt is in seconds.
    void step(double dt);

    /// Set FDM inputs (call before step())
    void setFdmInputs(const FdmInputs& inputs);

    /// Command a switch or CB
    void commandSwitch(const std::string& id, int cmd); // 0=open, 1=close, 2=toggle
    void commandCircuitBreaker(const std::string& id, int cmd);

    /// Inject or clear a fault
    void injectFault(const std::string& target_id, const std::string& fault_type);
    void clearFault(const std::string& target_id);
    void clearAllFaults();

    /// Read state
    const std::unordered_map<std::string, SourceState>& getSourceStates() const { return source_states_; }
    const std::unordered_map<std::string, BusState>& getBusStates() const { return bus_states_; }
    const std::unordered_map<std::string, SwitchState>& getSwitchStates() const { return switch_states_; }
    const std::unordered_map<std::string, LoadState>& getLoadStates() const { return load_states_; }
    const std::vector<CasMessage>& getCasMessages() const { return active_cas_; }
    double getSimTime() const { return sim_time_; }

private:
    void updateSources(double dt);
    void resetBuses();
    void propagateDirectConnections();
    void propagatePowerFlow();
    void updateRelayCoils();
    void updateLoads(double dt);
    void updateBatterySoc(double dt);
    void evaluateCas();

    double interpolateSocVoltage(const BatteryParams& bp, double soc) const;

    Topology topology_;
    FdmInputs fdm_inputs_;

    std::unordered_map<std::string, SourceState> source_states_;
    std::unordered_map<std::string, BusState> bus_states_;
    std::unordered_map<std::string, SwitchState> switch_states_;
    std::unordered_map<std::string, LoadState> load_states_;
    std::unordered_map<std::string, std::string> faults_; // target_id -> fault_type

    std::vector<CasMessage> active_cas_;
    double sim_time_ = 0.0;
};

} // namespace elec_sys
