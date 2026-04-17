import { create } from 'zustand'

const WS_URL = `${window.location.protocol === 'https:' ? 'wss:' : 'ws:'}//${window.location.host}/ws`

// Round float32 values to avoid display artefacts (e.g. 114.5999984741211 → 114.60)
const r2 = (v) => v != null ? Math.round(v * 100) / 100 : v   // 2 decimal (MHz)
const r3 = (v) => v != null ? Math.round(v * 1000) / 1000 : v // 3 decimal (MHz fine)
const r0 = (v) => v != null ? Math.round(v) : v                // integer (kHz, deg)

// Clamp untuned frequencies (0 from input_arbitrator) to minimum valid value per type
const comMin = (v) => { const r = r3(v); return (r != null && r > 0) ? r : 118.0 }
const navMin = (v) => { const r = r2(v); return (r != null && r > 0) ? r : 108.0 }
const adfMin = (v) => { const r = r0(v); return (r != null && r > 0) ? r : 190 }
const xpdrDef = (v) => { const r = r0(v); return (r != null && r > 0) ? r : 7000 }

// SimCommand enum values — must match backend SimCommand.msg
const CMD = {
  RUN: 1,
  FREEZE: 2,
  RESET_FLIGHT: 3,
  RESET_AIRCRAFT: 4,
  RESET_FAILURES: 5,
  SHUTDOWN: 6,
  RELOAD_NODE: 7,
  DEACTIVATE_NODE: 8,
  ACTIVATE_NODE: 9,
  RESET_NODE: 10,
}

export { CMD }

export const useSimStore = create((set, get) => ({
  // Connection
  ws: null,
  wsConnected: false,
  wsReconnectCount: 0,

  // Sim state
  simState: 'UNKNOWN',
  simTimeSec: 0,
  aircraftId: 'c172',
  freezePosition: false,
  freezeFuel: false,

  // FDM
  fdm: {
    lat: 51.5074, lon: -0.1278,
    altFtMsl: 0, iasKt: 0, gndSpeedKt: 0,
    hdgTrueDeg: 0, trackDeg: 0, vsFpm: 0,
    pitchDeg: 0, rollDeg: 0, isHelicopter: false,
    cgXIn: 0, cgYIn: 0, totalMassKg: 0,
  },

  // Atmosphere
  atmosphere: {
    qnhHpa: 1013.25, oatCelsius: 15.0,
    windDirDeg: 0, windSpeedKt: 0, visibilityM: 9999,
    windNorthMs: 0, windEastMs: 0, windDownMs: 0,
    visibleMoisture: false, turbulenceIntensity: 0,
  },

  // Active microbursts (from backend)
  microbursts: [],

  // Fuel
  fuel: {
    tanks: [], totalFuelKg: 0, totalFuelLiters: 0, totalFuelPct: 0,
    engineFuelFlowKgs: [], engineFuelFlowLph: [],
    lowFuelWarning: false,
  },

  // Fuel config (from aircraft YAML — drives tank display)
  fuelConfig: {
    fuelType: 'AVGAS_100LL', densityKgPerLiter: 0.72, displayUnit: 'L',
    tankCount: 0, tanks: [],
  },

  // Avionics (what pilot has dialled in — from /aircraft/controls/avionics)
  avionics: {
    com1Mhz: 118.0, com2Mhz: 118.0, com3Mhz: 118.0,
    nav1Mhz: 108.0, nav2Mhz: 108.0,
    adf1Khz: 190, adf2Khz: 190,
    xpdrCode: 7000, xpdrMode: 'ALT',
    obs1Deg: 0, obs2Deg: 0, dmeSource: 0,
    tacanChannel: 0, tacanBand: 0, gpsSource: 0,
  },

  // Navigation state (from sim_navigation node)
  nav: {
    gps1Valid: false, gps1LatDeg: 0, gps1LonDeg: 0, gps1AltFt: 0, gps1GsKt: 0, gps1TrackDeg: 0,
    gps2Valid: false, gps2LatDeg: 0, gps2LonDeg: 0, gps2AltFt: 0, gps2GsKt: 0, gps2TrackDeg: 0,
    activeGpsSource: 0,
    nav1Valid: false, nav1Ident: '', nav1Type: 'NONE', nav1ObsDeg: 0, nav1CdiDots: 0,
    nav1BearingDeg: 0, nav1RadialDeg: 0, nav1DistanceNm: 0, nav1Signal: 0, nav1ToFrom: 'OFF',
    nav1GsValid: false, nav1GsDots: 0,
    nav2Valid: false, nav2Ident: '', nav2Type: 'NONE', nav2ObsDeg: 0, nav2CdiDots: 0,
    nav2BearingDeg: 0, nav2RadialDeg: 0, nav2DistanceNm: 0, nav2Signal: 0, nav2ToFrom: 'OFF',
    nav2GsValid: false, nav2GsDots: 0,
    adf1Valid: false, adf1Ident: '', adf1RelBearingDeg: 0, adf1Signal: 0,
    adf2Valid: false, adf2Ident: '', adf2RelBearingDeg: 0, adf2Signal: 0,
    dmeSource: 'NAV1', dmeValid: false, dmeDistanceNm: 0, dmeGsKt: 0,
    tacanValid: false, tacanIdent: '', tacanBearingDeg: 0, tacanDistanceNm: 0,
    markerOuter: false, markerMiddle: false, markerInner: false,
    xpdrCode: 2000, xpdrMode: 'OFF', xpdrIdentActive: false,
    com1Mhz: 0, com2Mhz: 0, com3Mhz: 0, adf1Khz: 0, adf2Khz: 0,
    hdgMagDeg: 0, magVariationDeg: 0,
  },

  // Electrical state
  electrical: {
    busNames: [], busVoltages: [], busPowered: [],
    sourceNames: [], sourceActive: [], sourceVoltages: [], sourceCurrents: [],
    loadNames: [], loadPowered: [], loadCurrents: [],
    switchIds: [], switchLabels: [], switchClosed: [],
    cbNames: [], cbClosed: [], cbTripped: [],
    totalLoadAmps: 0, batterySocPct: 0,
    masterBusVoltage: 0, avionicsBusPowered: false, essentialBusPowered: false,
  },

  // Air data (from sim_air_data pitot-static model)
  airData: {
    iasKt: 0, casKt: 0, mach: 0,
    altIndicatedFt: 0, altPressureFt: 0, vsFpm: 0,
    satC: 0, tatC: 0,
    pitotHealthy: true, staticHealthy: true,
    pitotHeatOn: false, pitotIcePct: 0,
  },

  // Avionics config (from aircraft YAML — drives dynamic A/C page layout)
  avionicsConfig: { radios: [], displays: [] },

  // Electrical config (from aircraft electrical.yaml — drives dynamic IOS/cockpit panels)
  electricalConfig: null,

  // Weight & balance config (from aircraft weight.yaml)
  weightConfig: null,

  // Per-switch force state (from ArbitrationState.forced_switch_ids)
  forcedSwitchIds: [],
  forcedSelectorIds: [],

  // Engine state (from sim_engine_systems node)
  engines: {
    engineCount: 0, rpm: [], egtDegc: [], chtDegc: [],
    oilPressurePsi: [], oilTempDegc: [], manifoldPressureInhg: [],
    fuelFlowGph: [], n1Pct: [], n2Pct: [], tgtDegc: [], torquePct: [],
    engineRunning: [], engineFailed: [], starterEngaged: [],
    lowOilPressureWarning: [], highEgtWarning: [], highChtWarning: [],
  },

  // Engine config (from aircraft YAML — drives dynamic engine instrument layout)
  engineConfig: { engineCount: 0, engines: [] },

  // Arbitration state (per-channel input source)
  arbitration: {
    flightSource: 'FROZEN', engineSource: 'FROZEN',
    avionicsSource: 'FROZEN', panelSource: 'FROZEN',
    hwFlightHealthy: false, hwEngineHealthy: false,
    hwAvionicsHealthy: false, hwPanelHealthy: false,
  },

  // Gear state
  gear: {
    gearCount: 0, gearType: '', retractable: false,
    onGround: true, gearHandleDown: true, gearUnsafe: false, gearWarning: false,
    legNames: [], positionNorm: [], weightOnWheels: [],
    brakeLeftNorm: 0, brakeRightNorm: 0, parkingBrake: false, nosewheelAngleDeg: 0,
  },

  // Terrain source
  terrainSource: { source: 'UNKNOWN', description: '' },

  // Airport search (POS page)
  airportSearchResults: [],
  runwayResults: null,  // full airport object with runways

  // Node health
  nodes: {},

  // Alerts (last N alerts for display)
  alerts: [],
  alertsMax: 50,

  // Failure state
  armedFailures: 0,
  activeFailures: 0,
  activeFailureIds: [],
  armedFailureIds: [],
  failuresCatalog: [],

  // Session
  session: { name: 'Session 1', pilotName: '', instructorName: '' },

  // Flight track — thinned to prevent memory leaks on long sessions
  track: [],
  trackMaxPoints: 10000,

  // Topic forwarder (raw ROS2 topics for State Inspector)
  topicTree: {},       // { "/aircraft/fdm/state": { type: "...", has_data: true }, ... }
  topicValues: {},     // { "/aircraft/fdm/state": { latitude_deg: 50.9, ... }, ... }

  // UI
  activeTab: 'map',
  sidePanelOpen: false,
  ctrOnAircraft: true,
  hdgUp: false,

  // Pending confirm (for destructive actions)
  icConfiguration: 'cold_and_dark',  // IC preset: cold_and_dark / ready_for_takeoff / airborne_clean
  pendingAction: null,   // { type: string, expiresAt: number }

  // ─── ACTIONS ─────────────────────────────────────────────────────

  setActiveTab: (tab) => set({
    activeTab: tab,
    sidePanelOpen: tab !== 'map',
  }),

  closeSidePanel: () => set({ activeTab: 'map', sidePanelOpen: false }),

  sendCommand: (cmdType, payload = {}) => {
    const { ws, wsConnected } = get()
    if (!wsConnected || !ws) return false
    ws.send(JSON.stringify({ type: 'command', cmd: cmdType, ...payload }))
    return true
  },

  // Confirm pattern: first call arms, second call within 3s fires
  requestAction: (actionType, onConfirm) => {
    const { pendingAction } = get()
    const now = Date.now()

    if (pendingAction?.type === actionType && now < pendingAction.expiresAt) {
      // Second tap — execute
      set({ pendingAction: null })
      onConfirm()
    } else {
      // First tap — arm
      set({ pendingAction: { type: actionType, expiresAt: now + 3000 } })
      // Auto-cancel after 3s
      setTimeout(() => {
        set((s) => s.pendingAction?.type === actionType ? { pendingAction: null } : {})
      }, 3000)
    }
  },

  appendTrackPoint: (lat, lon) => set((s) => {
    const t = s.track
    if (t.length >= s.trackMaxPoints) {
      // Thin: keep every 2nd point in old data, full res in last 1000
      const cutoff = t.length - 1000
      const thinned = t.filter((_, i) => i % 2 === 0 || i >= cutoff)
      thinned.push([lat, lon])
      return { track: thinned }
    }
    // Mutate in place + bump reference — avoids full copy every frame
    t.push([lat, lon])
    return { track: t }
  }),

  clearTrack: () => set({ track: [] }),

  sendToggle: (type) => {
    const { ws, wsConnected } = get()
    if (!wsConnected || !ws) return false
    ws.send(JSON.stringify({ type }))
    return true
  },

  sendAvionics: (data) => {
    const { ws, wsConnected } = get()
    if (!wsConnected || !ws) return false
    ws.send(JSON.stringify({ type: 'set_avionics', ...data }))
    return true
  },

  sendVirtualAvionics: (data) => {
    const { ws, wsConnected } = get()
    if (!wsConnected || !ws) return
    ws.send(JSON.stringify({ type: 'set_virtual_avionics', data }))
  },

  sendPayload: (stations) => {
    const { ws, wsConnected } = get()
    if (!wsConnected || !ws) return
    ws.send(JSON.stringify({ type: 'set_payload', data: { stations } }))
  },

  sendFuelLoading: (tanks) => {
    const { ws, wsConnected } = get()
    if (!wsConnected || !ws) return
    ws.send(JSON.stringify({ type: 'set_fuel_loading', data: { tanks } }))
  },

  injectFailure: (failureId, paramsOverrideJson = '') => {
    const { ws, wsConnected } = get()
    if (!wsConnected || !ws) return false
    ws.send(JSON.stringify({
      type: 'failure_command',
      action: 'inject',
      failure_id: failureId,
      params_override_json: paramsOverrideJson,
    }))
    return true
  },

  clearFailure: (failureId) => {
    const { ws, wsConnected } = get()
    if (!wsConnected || !ws) return false
    ws.send(JSON.stringify({
      type: 'failure_command',
      action: 'clear',
      failure_id: failureId,
    }))
    return true
  },

  clearAllFailures: () => {
    const { ws, wsConnected } = get()
    if (!wsConnected || !ws) return false
    ws.send(JSON.stringify({
      type: 'failure_command',
      action: 'clear_all',
      failure_id: '',
    }))
    return true
  },

  sendPanel: (switchIds, switchStates, selectorIds, selectorValues, switchForced, selectorForced) => {
    const { ws, wsConnected } = get()
    if (!wsConnected || !ws) return false
    const data = { switch_ids: switchIds, switch_states: switchStates }
    if (switchForced?.length) data.switch_forced = switchForced
    if (selectorIds?.length) {
      data.selector_ids = selectorIds
      data.selector_values = selectorValues
      if (selectorForced?.length) data.selector_forced = selectorForced
    }
    ws.send(JSON.stringify({ type: 'set_panel', data }))
    return true
  },

  // ─── AIRPORT SEARCH ─────────────────────────────────────────────

  searchAirports: (query) => {
    const { ws, wsConnected } = get()
    if (!wsConnected || !ws || query.length < 2) return
    ws.send(JSON.stringify({ type: 'search_airports', query, max_results: 8 }))
  },

  getRunways: (icao) => {
    const { ws, wsConnected } = get()
    if (!wsConnected || !ws || !icao) return
    ws.send(JSON.stringify({ type: 'get_runways', icao }))
  },

  setDeparture: (data) => {
    const { ws, wsConnected, icConfiguration } = get()
    if (!wsConnected || !ws) return false
    ws.send(JSON.stringify({ type: 'set_departure', configuration: icConfiguration, ...data }))
    return true
  },

  setIcConfiguration: (config) => set({ icConfiguration: config }),

  // ─── WEBSOCKET ───────────────────────────────────────────────────

  connectWS: () => {
    const state = get()
    if (state.ws) state.ws.close()

    const ws = new WebSocket(WS_URL)

    ws.onopen = () => {
      set({ wsConnected: true, wsReconnectCount: 0 })
      console.log('[WS] connected')
    }

    ws.onclose = () => {
      set({ wsConnected: false, ws: null })
      const count = get().wsReconnectCount + 1
      const delay = Math.min(1000 * Math.pow(2, count - 1), 30000)
      console.log(`[WS] disconnected, reconnecting in ${delay}ms (attempt ${count})`)
      set({ wsReconnectCount: count })
      setTimeout(() => get().connectWS(), delay)
    }

    ws.onerror = (e) => console.error('[WS] error', e)

    ws.onmessage = (event) => {
      try {
        const msg = JSON.parse(event.data)
        const s = get()

        switch (msg.type) {
          case 'sim_state': {
            const baseState = msg.state ?? s.simState
            const newState = msg.reposition_active ? 'REPOSITIONING' : baseState
            const updates = {
              simState: newState,
              simTimeSec: msg.sim_time_sec ?? s.simTimeSec,
              aircraftId: msg.aircraft_id ?? s.aircraftId,
              freezePosition: msg.freeze_position ?? s.freezePosition,
              freezeFuel: msg.freeze_fuel ?? s.freezeFuel,
            }
            // Clear track on reset
            if (newState === 'RESETTING' || newState === 'READY') {
              updates.track = []
            }
            set(updates)
            break
          }

          case 'flight_model_state':
          case 'fdm_state':
          case 'fdm': {
            const fdm = {
              lat: msg.lat ?? s.fdm.lat,
              lon: msg.lon ?? s.fdm.lon,
              altFtMsl: msg.alt_ft_msl ?? (msg.altitude_m_msl ? msg.altitude_m_msl * 3.28084 : s.fdm.altFtMsl),
              iasKt: msg.ias_kt ?? s.fdm.iasKt,
              gndSpeedKt: msg.gnd_speed_kt ?? s.fdm.gndSpeedKt,
              hdgTrueDeg: msg.hdg_true_deg ?? s.fdm.hdgTrueDeg,
              trackDeg: msg.track_deg ?? s.fdm.trackDeg,
              vsFpm: msg.vs_fpm ?? s.fdm.vsFpm,
              pitchDeg: msg.pitch_deg ?? s.fdm.pitchDeg,
              rollDeg: msg.roll_deg ?? s.fdm.rollDeg,
              isHelicopter: msg.is_helicopter ?? s.fdm.isHelicopter,
              cgXIn: msg.cg_x_in ?? s.fdm.cgXIn,
              cgYIn: msg.cg_y_in ?? s.fdm.cgYIn,
              totalMassKg: msg.total_mass_kg ?? s.fdm.totalMassKg,
            }
            set({ fdm })
            // Append track point if sim is running
            if (s.simState === 'RUNNING') {
              get().appendTrackPoint(fdm.lat, fdm.lon)
            }
            break
          }

          case 'atmosphere':
            set({
              atmosphere: {
                qnhHpa: msg.qnh_hpa ?? s.atmosphere.qnhHpa,
                oatCelsius: msg.oat_celsius ?? s.atmosphere.oatCelsius,
                windDirDeg: msg.wind_dir_deg ?? s.atmosphere.windDirDeg,
                windSpeedKt: msg.wind_speed_kt ?? s.atmosphere.windSpeedKt,
                visibilityM: msg.visibility_m ?? s.atmosphere.visibilityM,
                windNorthMs: msg.wind_north_ms ?? s.atmosphere.windNorthMs,
                windEastMs: msg.wind_east_ms ?? s.atmosphere.windEastMs,
                windDownMs: msg.wind_down_ms ?? s.atmosphere.windDownMs,
                visibleMoisture: msg.visible_moisture ?? s.atmosphere.visibleMoisture,
                turbulenceIntensity: msg.turbulence_intensity ?? s.atmosphere.turbulenceIntensity,
              }
            })
            break

          case 'microbursts':
            set({ microbursts: msg.hazards ?? [] })
            break

          case 'fuel_state':
          case 'fuel': {
            const tc = msg.tank_count ?? (msg.tank_quantity_kg?.length ?? 0)
            const tanks = (msg.tank_quantity_kg ?? []).map((kg, i) => ({
              id: i,
              massKg: kg,
              liters: msg.tank_quantity_liters?.[i] ?? 0,
              pct: msg.tank_quantity_norm?.[i] ?? 0,
              usableKg: msg.tank_usable_kg?.[i] ?? 0,
              selected: msg.tank_selected?.[i] ?? false,
              boostPumpOn: msg.boost_pump_on?.[i] ?? false,
            })).slice(0, tc)
            set({
              fuel: {
                tanks,
                totalFuelKg: msg.total_fuel_kg ?? 0,
                totalFuelLiters: msg.total_fuel_liters ?? 0,
                totalFuelPct: msg.total_fuel_norm ?? 0,
                engineFuelFlowKgs: msg.engine_fuel_flow_kgs ?? [],
                engineFuelFlowLph: msg.engine_fuel_flow_lph ?? [],
                lowFuelWarning: msg.low_fuel_warning ?? false,
              },
            })
            break
          }

          case 'avionics':
            set({
              avionics: {
                com1Mhz: comMin(msg.com1_mhz) ?? s.avionics.com1Mhz,
                com2Mhz: comMin(msg.com2_mhz) ?? s.avionics.com2Mhz,
                com3Mhz: comMin(msg.com3_mhz) ?? s.avionics.com3Mhz,
                nav1Mhz: navMin(msg.nav1_mhz) ?? s.avionics.nav1Mhz,
                nav2Mhz: navMin(msg.nav2_mhz) ?? s.avionics.nav2Mhz,
                adf1Khz: adfMin(msg.adf1_khz) ?? s.avionics.adf1Khz,
                adf2Khz: adfMin(msg.adf2_khz) ?? s.avionics.adf2Khz,
                xpdrCode: xpdrDef(msg.xpdr_code) ?? s.avionics.xpdrCode,
                xpdrMode: msg.xpdr_mode ?? s.avionics.xpdrMode,
                obs1Deg: r0(msg.obs1_deg) ?? s.avionics.obs1Deg,
                obs2Deg: r0(msg.obs2_deg) ?? s.avionics.obs2Deg,
                dmeSource: msg.dme_source ?? s.avionics.dmeSource,
                tacanChannel: msg.tacan_channel ?? s.avionics.tacanChannel,
                tacanBand: msg.tacan_band ?? s.avionics.tacanBand,
                gpsSource: msg.gps_source ?? s.avionics.gpsSource,
              }
            })
            break

          case 'nav_state':
            set({
              nav: {
                gps1Valid: msg.gps1_valid ?? s.nav.gps1Valid,
                gps1LatDeg: msg.gps1_lat_deg ?? s.nav.gps1LatDeg,
                gps1LonDeg: msg.gps1_lon_deg ?? s.nav.gps1LonDeg,
                gps1AltFt: msg.gps1_alt_ft ?? s.nav.gps1AltFt,
                gps1GsKt: msg.gps1_gs_kt ?? s.nav.gps1GsKt,
                gps1TrackDeg: msg.gps1_track_deg ?? s.nav.gps1TrackDeg,
                gps2Valid: msg.gps2_valid ?? s.nav.gps2Valid,
                gps2LatDeg: msg.gps2_lat_deg ?? s.nav.gps2LatDeg,
                gps2LonDeg: msg.gps2_lon_deg ?? s.nav.gps2LonDeg,
                gps2AltFt: msg.gps2_alt_ft ?? s.nav.gps2AltFt,
                gps2GsKt: msg.gps2_gs_kt ?? s.nav.gps2GsKt,
                gps2TrackDeg: msg.gps2_track_deg ?? s.nav.gps2TrackDeg,
                activeGpsSource: msg.active_gps_source ?? s.nav.activeGpsSource,
                nav1Valid: msg.nav1_valid ?? s.nav.nav1Valid,
                nav1Ident: msg.nav1_ident ?? s.nav.nav1Ident,
                nav1Type: msg.nav1_type ?? s.nav.nav1Type,
                nav1ObsDeg: msg.nav1_obs_deg ?? s.nav.nav1ObsDeg,
                nav1CdiDots: msg.nav1_cdi_dots ?? s.nav.nav1CdiDots,
                nav1BearingDeg: msg.nav1_bearing_deg ?? s.nav.nav1BearingDeg,
                nav1RadialDeg: msg.nav1_radial_deg ?? s.nav.nav1RadialDeg,
                nav1DistanceNm: msg.nav1_distance_nm ?? s.nav.nav1DistanceNm,
                nav1Signal: msg.nav1_signal ?? s.nav.nav1Signal,
                nav1ToFrom: msg.nav1_to_from ?? s.nav.nav1ToFrom,
                nav1GsValid: msg.nav1_gs_valid ?? s.nav.nav1GsValid,
                nav1GsDots: msg.nav1_gs_dots ?? s.nav.nav1GsDots,
                nav2Valid: msg.nav2_valid ?? s.nav.nav2Valid,
                nav2Ident: msg.nav2_ident ?? s.nav.nav2Ident,
                nav2Type: msg.nav2_type ?? s.nav.nav2Type,
                nav2ObsDeg: msg.nav2_obs_deg ?? s.nav.nav2ObsDeg,
                nav2CdiDots: msg.nav2_cdi_dots ?? s.nav.nav2CdiDots,
                nav2BearingDeg: msg.nav2_bearing_deg ?? s.nav.nav2BearingDeg,
                nav2RadialDeg: msg.nav2_radial_deg ?? s.nav.nav2RadialDeg,
                nav2DistanceNm: msg.nav2_distance_nm ?? s.nav.nav2DistanceNm,
                nav2Signal: msg.nav2_signal ?? s.nav.nav2Signal,
                nav2ToFrom: msg.nav2_to_from ?? s.nav.nav2ToFrom,
                nav2GsValid: msg.nav2_gs_valid ?? s.nav.nav2GsValid,
                nav2GsDots: msg.nav2_gs_dots ?? s.nav.nav2GsDots,
                adf1Valid: msg.adf1_valid ?? s.nav.adf1Valid,
                adf1Ident: msg.adf1_ident ?? s.nav.adf1Ident,
                adf1RelBearingDeg: msg.adf1_rel_bearing_deg ?? s.nav.adf1RelBearingDeg,
                adf1Signal: msg.adf1_signal ?? s.nav.adf1Signal,
                adf2Valid: msg.adf2_valid ?? s.nav.adf2Valid,
                adf2Ident: msg.adf2_ident ?? s.nav.adf2Ident,
                adf2RelBearingDeg: msg.adf2_rel_bearing_deg ?? s.nav.adf2RelBearingDeg,
                adf2Signal: msg.adf2_signal ?? s.nav.adf2Signal,
                dmeSource: msg.dme_source ?? s.nav.dmeSource,
                dmeValid: msg.dme_valid ?? s.nav.dmeValid,
                dmeDistanceNm: msg.dme_distance_nm ?? s.nav.dmeDistanceNm,
                dmeGsKt: msg.dme_gs_kt ?? s.nav.dmeGsKt,
                tacanValid: msg.tacan_valid ?? s.nav.tacanValid,
                tacanIdent: msg.tacan_ident ?? s.nav.tacanIdent,
                tacanBearingDeg: msg.tacan_bearing_deg ?? s.nav.tacanBearingDeg,
                tacanDistanceNm: msg.tacan_distance_nm ?? s.nav.tacanDistanceNm,
                markerOuter: msg.marker_outer ?? s.nav.markerOuter,
                markerMiddle: msg.marker_middle ?? s.nav.markerMiddle,
                markerInner: msg.marker_inner ?? s.nav.markerInner,
                xpdrCode: xpdrDef(msg.xpdr_code) ?? s.nav.xpdrCode,
                xpdrMode: msg.xpdr_mode ?? s.nav.xpdrMode,
                xpdrIdentActive: msg.xpdr_ident_active ?? s.nav.xpdrIdentActive,
                com1Mhz: comMin(msg.com1_mhz) ?? s.nav.com1Mhz,
                com2Mhz: comMin(msg.com2_mhz) ?? s.nav.com2Mhz,
                com3Mhz: comMin(msg.com3_mhz) ?? s.nav.com3Mhz,
                adf1Khz: adfMin(msg.adf1_khz) ?? s.nav.adf1Khz,
                adf2Khz: adfMin(msg.adf2_khz) ?? s.nav.adf2Khz,
                hdgMagDeg: msg.hdg_mag_deg ?? s.nav.hdgMagDeg,
                magVariationDeg: msg.mag_variation_deg ?? s.nav.magVariationDeg,
              }
            })
            break

          case 'electrical_state':
            set({
              electrical: {
                busNames: msg.bus_names ?? s.electrical.busNames,
                busVoltages: msg.bus_voltages_v ?? s.electrical.busVoltages,
                busPowered: msg.bus_powered ?? s.electrical.busPowered,
                sourceNames: msg.source_names ?? s.electrical.sourceNames,
                sourceActive: msg.source_active ?? s.electrical.sourceActive,
                sourceVoltages: msg.source_voltages_v ?? s.electrical.sourceVoltages,
                sourceCurrents: msg.source_currents_a ?? s.electrical.sourceCurrents,
                loadNames: msg.load_names ?? s.electrical.loadNames,
                loadPowered: msg.load_powered ?? s.electrical.loadPowered,
                loadCurrents: msg.load_currents_a ?? s.electrical.loadCurrents,
                switchIds: msg.switch_ids ?? s.electrical.switchIds,
                switchLabels: msg.switch_labels ?? s.electrical.switchLabels,
                switchClosed: msg.switch_closed ?? s.electrical.switchClosed,
                cbNames: msg.cb_names ?? s.electrical.cbNames,
                cbClosed: msg.cb_closed ?? s.electrical.cbClosed,
                cbTripped: msg.cb_tripped ?? s.electrical.cbTripped,
                totalLoadAmps: msg.total_load_a ?? s.electrical.totalLoadAmps,
                batterySocPct: msg.battery_soc_pct ?? s.electrical.batterySocPct,
                masterBusVoltage: msg.master_bus_voltage_v ?? s.electrical.masterBusVoltage,
                avionicsBusPowered: msg.avionics_bus_powered ?? s.electrical.avionicsBusPowered,
                essentialBusPowered: msg.essential_bus_powered ?? s.electrical.essentialBusPowered,
              }
            })
            break

          case 'avionics_config':
            set({
              avionicsConfig: {
                radios: msg.radios ?? [],
                displays: msg.displays ?? [],
              },
            })
            break

          case 'electrical_config':
            set({ electricalConfig: msg })
            break

          case 'engines_state':
            set({
              engines: {
                engineCount: msg.engine_count ?? 0,
                rpm: msg.rpm ?? [],
                egtDegc: msg.egt_degc ?? [],
                chtDegc: msg.cht_degc ?? [],
                oilPressurePsi: msg.oil_pressure_psi ?? [],
                oilTempDegc: msg.oil_temp_degc ?? [],
                manifoldPressureInhg: msg.manifold_pressure_inhg ?? [],
                fuelFlowGph: msg.fuel_flow_gph ?? [],
                n1Pct: msg.n1_pct ?? [],
                n2Pct: msg.n2_pct ?? [],
                tgtDegc: msg.tgt_degc ?? [],
                torquePct: msg.torque_pct ?? [],
                engineRunning: msg.engine_running ?? [],
                engineFailed: msg.engine_failed ?? [],
                starterEngaged: msg.starter_engaged ?? [],
                lowOilPressureWarning: msg.low_oil_pressure_warning ?? [],
                highEgtWarning: msg.high_egt_warning ?? [],
                highChtWarning: msg.high_cht_warning ?? [],
              },
            })
            break

          case 'engine_config':
            set({
              engineConfig: {
                engineCount: msg.engine_count ?? 0,
                engines: msg.engines ?? [],
              },
            })
            break

          case 'fuel_config':
            set({
              fuelConfig: {
                fuelType: msg.fuel_type ?? 'AVGAS_100LL',
                densityKgPerLiter: msg.density_kg_per_liter ?? 0.72,
                displayUnit: msg.display_unit ?? 'L',
                tankCount: msg.tank_count ?? 0,
                tanks: msg.tanks ?? [],
              },
            })
            break

          case 'sim_alert': {
            const alert = {
              severity: msg.severity,
              source: msg.source,
              message: msg.message,
              time: Date.now(),
            }
            const alerts = [...s.alerts, alert].slice(-s.alertsMax)
            set({ alerts })
            break
          }

          case 'terrain_source':
            set({
              terrainSource: {
                source: msg.source ?? 'UNKNOWN',
                description: msg.description ?? '',
              }
            })
            break

          case 'airport_search_results':
            set({ airportSearchResults: msg.airports ?? [] })
            break

          case 'runway_results':
            if (msg.found && msg.airport) {
              set({ runwayResults: msg.airport })
            }
            break

          case 'node_health':
            set({ nodes: msg.nodes ?? s.nodes })
            break

          case 'failure_state':
            set({
              activeFailureIds: msg.active_failure_ids ?? s.activeFailureIds,
              armedFailureIds: msg.armed_failure_ids ?? s.armedFailureIds,
              activeFailures: (msg.active_failure_ids ?? s.activeFailureIds).length,
              armedFailures: (msg.armed_failure_ids ?? s.armedFailureIds).length,
            })
            break

          case 'failures_config':
            set({
              failuresCatalog: msg.catalog ?? s.failuresCatalog,
            })
            break

          case 'air_data_state':
            set({
              airData: {
                iasKt: msg.ias_kt ?? s.airData.iasKt,
                casKt: msg.cas_kt ?? s.airData.casKt,
                mach: msg.mach ?? s.airData.mach,
                altIndicatedFt: msg.alt_indicated_ft ?? s.airData.altIndicatedFt,
                altPressureFt: msg.alt_pressure_ft ?? s.airData.altPressureFt,
                vsFpm: msg.vs_fpm ?? s.airData.vsFpm,
                satC: msg.sat_c ?? s.airData.satC,
                tatC: msg.tat_c ?? s.airData.tatC,
                pitotHealthy: msg.pitot_healthy ?? s.airData.pitotHealthy,
                staticHealthy: msg.static_healthy ?? s.airData.staticHealthy,
                pitotHeatOn: msg.pitot_heat_on ?? s.airData.pitotHeatOn,
                pitotIcePct: msg.pitot_ice_norm ?? s.airData.pitotIcePct,
              },
            })
            break

          case 'arbitration_state':
            set({
              arbitration: {
                flightSource: msg.flight_source ?? s.arbitration.flightSource,
                engineSource: msg.engine_source ?? s.arbitration.engineSource,
                avionicsSource: msg.avionics_source ?? s.arbitration.avionicsSource,
                panelSource: msg.panel_source ?? s.arbitration.panelSource,
                hwFlightHealthy: msg.hw_flight_healthy ?? s.arbitration.hwFlightHealthy,
                hwEngineHealthy: msg.hw_engine_healthy ?? s.arbitration.hwEngineHealthy,
                hwAvionicsHealthy: msg.hw_avionics_healthy ?? s.arbitration.hwAvionicsHealthy,
                hwPanelHealthy: msg.hw_panel_healthy ?? s.arbitration.hwPanelHealthy,
              },
              forcedSwitchIds: msg.forced_switch_ids ?? s.forcedSwitchIds,
              forcedSelectorIds: msg.forced_selector_ids ?? s.forcedSelectorIds,
            })
            break

          case 'gear_state':
            set({
              gear: {
                gearCount: msg.gear_count ?? s.gear.gearCount,
                gearType: msg.gear_type ?? s.gear.gearType,
                retractable: msg.retractable ?? s.gear.retractable,
                onGround: msg.on_ground ?? s.gear.onGround,
                gearHandleDown: msg.gear_handle_down ?? s.gear.gearHandleDown,
                gearUnsafe: msg.gear_unsafe ?? s.gear.gearUnsafe,
                gearWarning: msg.gear_warning ?? s.gear.gearWarning,
                legNames: msg.leg_names ?? s.gear.legNames,
                positionNorm: msg.position_norm ?? s.gear.positionNorm,
                weightOnWheels: msg.weight_on_wheels ?? s.gear.weightOnWheels,
                brakeLeftNorm: msg.brake_left_norm ?? s.gear.brakeLeftNorm,
                brakeRightNorm: msg.brake_right_norm ?? s.gear.brakeRightNorm,
                parkingBrake: msg.parking_brake ?? s.gear.parkingBrake,
                nosewheelAngleDeg: msg.nosewheel_angle_deg ?? s.gear.nosewheelAngleDeg,
              },
            })
            break

          case 'failure_count':
            set({
              armedFailures: msg.armed ?? s.armedFailures,
              activeFailures: msg.active ?? s.activeFailures,
            })
            break

          case 'weight_config':
            set({ weightConfig: msg })
            break

          case 'topic_tree':
            set({ topicTree: msg.topics ?? {} })
            break

          case 'topic_update':
            set({ topicValues: msg.topics ?? {} })
            break
        }
      } catch (e) {
        console.error('[WS] parse error', e)
      }
    }

    set({ ws })
  },
}))
