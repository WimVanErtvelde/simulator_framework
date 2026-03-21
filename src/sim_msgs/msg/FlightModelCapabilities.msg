# FlightModelCapabilities.msg
# Published by: flight_model_adapter -> /sim/flight_model/capabilities
# QoS: transient_local (latched), published once at startup
# Consumed by: system nodes to gate solver vs defer to FDM

uint8 FDM_NATIVE=0
uint8 EXTERNAL_COUPLED=1
uint8 EXTERNAL_DECOUPLED=2

std_msgs/Header header

uint8 electrical
uint8 hydraulic
uint8 gear_retract
uint8 fuel_quantities
uint8 fuel_pump_pressure
uint8 fuel_crossfeed
uint8 prop_governor
uint8 beta_range
uint8 reverse_thrust
uint8 fadec
uint8 autothrust
uint8 starter
uint8 engine_fire
uint8 pneumatic
uint8 bleed_air
uint8 apu
uint8 fuel_control_unit
uint8 ignition_sequence
