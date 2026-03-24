---
name: Auto-route to domain agents
description: Automatically delegate work to the appropriate custom agent (core, systems, ios, aircraft, messages) based on task context — don't wait for the user to explicitly invoke an agent.
type: feedback
---

When the user gives a task, automatically route it to the appropriate custom agent based on domain:
- **core** — sim_manager, flight_model_adapter, input_arbitrator, atmosphere, cigi_bridge, navaid_sim, sim_interfaces
- **systems** — electrical, fuel, engines, hydraulic, gear, failures, navigation, ice_protection, pressurization
- **ios** — IOS backend (FastAPI/rclpy), IOS frontend (React/Zustand/WebSocket), virtual cockpit pages
- **aircraft** — config YAMLs, pluginlib plugins, plugins.xml, adding/modifying aircraft types
- **messages** — sim_msgs definitions, topic naming, field conventions

**Why:** User prefers not to manually invoke agents — Claude should infer the right domain from context.

**How to apply:** Analyze the task, pick the right agent(s), and launch them. For cross-cutting work (e.g., new message + node + frontend display), launch multiple agents in parallel. Use the main conversation for orchestration and tasks that span all domains.
