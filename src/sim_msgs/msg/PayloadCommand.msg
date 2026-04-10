# Payload station weight command from IOS
# Published by: ios_backend → /aircraft/payload/command
# Consumed by: flight_model_adapter (writes JSBSim pointmass properties)
std_msgs/Header header
uint8[] station_indices        # JSBSim pointmass indices
float32[] weights_lbs          # weight per station in lbs
