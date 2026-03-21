#include <sim_interfaces/i_electrical_model.hpp>
#include <electrical/electrical_solver.hpp>
#include <pluginlib/class_list_macros.hpp>
#include <string>

namespace aircraft_ec135
{

class ElectricalModel : public sim_interfaces::IElectricalModel
{
public:
  void configure(const std::string & yaml_path) override
  {
    solver_.loadTopologyYaml(yaml_path);
  }

  void update(double dt_sec) override
  {
    solver_.step(dt_sec);
  }

  void apply_failure(const std::string & failure_id, bool active) override
  {
    auto sep = failure_id.find('/');
    if (sep == std::string::npos) {
      if (active) solver_.injectFault(failure_id, "fail");
      else        solver_.clearFault(failure_id);
      return;
    }
    std::string target = failure_id.substr(0, sep);
    std::string fault  = failure_id.substr(sep + 1);
    if (active) solver_.injectFault(target, fault);
    else        solver_.clearFault(target);
  }

  void command_switch(const std::string & id, int cmd) override
  {
    solver_.commandSwitch(id, cmd);
  }

  void set_engine_n2(const std::vector<double> & n2_pct) override
  {
    fdm_inputs_.engine_n2_pct = n2_pct;
    solver_.setFdmInputs(fdm_inputs_);
  }

  void set_ground_state(bool on_ground, bool external_power_connected) override
  {
    fdm_inputs_.on_ground = on_ground;
    fdm_inputs_.external_power_connected = external_power_connected;
    solver_.setFdmInputs(fdm_inputs_);
  }

  void reset() override
  {
    solver_.reset();
    fdm_inputs_ = elec_sys::FdmInputs{};
  }

  sim_interfaces::ElectricalSnapshot get_snapshot() const override
  {
    sim_interfaces::ElectricalSnapshot snap;
    const auto & topo = solver_.getTopology();

    for (const auto & [id, bs] : solver_.getBusStates()) {
      std::string label = id;
      for (const auto & bd : topo.buses) {
        if (bd.id == id) { label = bd.label; break; }
      }
      snap.buses.push_back({label, (float)bs.voltage, bs.powered});
    }

    for (const auto & [id, ss] : solver_.getSourceStates()) {
      std::string label = id;
      for (const auto & sd : topo.sources) {
        if (sd.id == id) { label = sd.label; break; }
      }
      snap.sources.push_back({label, ss.online, (float)ss.voltage,
                               (float)ss.current, (float)ss.battery_soc});
    }

    // Switches
    for (const auto & [id, sw] : solver_.getSwitchStates()) {
      std::string label = id;
      for (const auto & sd : topo.switches) {
        if (sd.id == id) { label = sd.label; break; }
      }
      snap.switches.push_back({id, label, sw.closed});
    }

    float total_amps = 0.0f;
    for (const auto & [id, ls] : solver_.getLoadStates()) {
      std::string label = id;
      std::string cb_id;
      for (const auto & ld : topo.loads) {
        if (ld.id == id) { label = ld.label; cb_id = ld.cb.id; break; }
      }
      snap.loads.push_back({label, ls.powered, (float)ls.current});
      if (!cb_id.empty()) {
        snap.cbs.push_back({cb_id, ls.cb_closed, ls.cb_tripped});
      }
      if (ls.powered) total_amps += (float)ls.current;
    }
    snap.total_load_amps = total_amps;

    // Battery SOC — first battery
    for (const auto & [id, ss] : solver_.getSourceStates()) {
      for (const auto & sd : topo.sources) {
        if (sd.id == id && sd.type == "battery") {
          snap.battery_soc_pct = (float)ss.battery_soc;
          break;
        }
      }
    }

    // EC135: main_dc_bus is the master bus
    auto bus_it = solver_.getBusStates().find("main_dc_bus");
    if (bus_it != solver_.getBusStates().end()) {
      snap.master_bus_voltage = (float)bus_it->second.voltage;
    }

    auto av_it = solver_.getBusStates().find("avionics_bus");
    if (av_it != solver_.getBusStates().end()) {
      snap.avionics_bus_powered = av_it->second.powered;
    }

    auto es_it = solver_.getBusStates().find("ess_bus");
    if (es_it != solver_.getBusStates().end()) {
      snap.essential_bus_powered = es_it->second.powered;
    }

    return snap;
  }

  elec_sys::ElectricalSolver & solver() { return solver_; }

private:
  elec_sys::ElectricalSolver solver_;
  elec_sys::FdmInputs fdm_inputs_;
};

}  // namespace aircraft_ec135

PLUGINLIB_EXPORT_CLASS(aircraft_ec135::ElectricalModel, sim_interfaces::IElectricalModel)
