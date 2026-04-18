import { useEffect, useRef, useCallback } from 'react'
import { useSimStore } from '../store/useSimStore'

const AXIS_STEP = 0.01
const TRIM_STEP = 0.005

function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)) }

export default function useKeyboardControls() {
  const keysDown = useRef(new Set())
  const state = useRef({
    aileron: 0, elevator: 0, rudder: 0,
    throttle: 0, mixture: 1.0,
    trimElevator: 0, brakeHeld: false, parkingBrake: false,
    magnetoLeft: false, magnetoRight: false, starter: false,
  })

  const setThrottle = useCallback((v) => { state.current.throttle = clamp(v, 0, 1) }, [])
  const setMixture = useCallback((v) => { state.current.mixture = clamp(v, 0, 1) }, [])
  const setMagneto = useCallback((pos) => {
    const s = state.current
    s.magnetoLeft = pos === 2 || pos === 3 || pos === 4
    s.magnetoRight = pos === 1 || pos === 3 || pos === 4
    s.starter = pos === 4
  }, [])

  useEffect(() => {
    const onKeyDown = (e) => {
      const k = e.key.toLowerCase()
      if (keysDown.current.has(k)) return
      keysDown.current.add(k)
      if (k === ' ') { state.current.aileron = 0; state.current.elevator = 0; state.current.rudder = 0; e.preventDefault() }
      if (k === 'b') state.current.brakeHeld = true
      if (k === 'p') state.current.parkingBrake = !state.current.parkingBrake  // toggle on press
    }
    const onKeyUp = (e) => {
      const k = e.key.toLowerCase()
      keysDown.current.delete(k)
      if (k === 'b') state.current.brakeHeld = false
    }
    const onBlur = () => { keysDown.current.clear(); state.current.brakeHeld = false }

    window.addEventListener('keydown', onKeyDown)
    window.addEventListener('keyup', onKeyUp)
    window.addEventListener('blur', onBlur)

    // 20 Hz tick
    const interval = setInterval(() => {
      const s = state.current
      const keys = keysDown.current
      if (keys.has('w')) s.elevator = clamp(s.elevator + AXIS_STEP, -1, 1)
      if (keys.has('s')) s.elevator = clamp(s.elevator - AXIS_STEP, -1, 1)
      if (keys.has('a')) s.aileron = clamp(s.aileron - AXIS_STEP, -1, 1)
      if (keys.has('d')) s.aileron = clamp(s.aileron + AXIS_STEP, -1, 1)
      if (keys.has('q')) s.rudder = clamp(s.rudder + AXIS_STEP, -1, 1)
      if (keys.has('e')) s.rudder = clamp(s.rudder - AXIS_STEP, -1, 1)
      if (keys.has('r')) s.throttle = clamp(s.throttle + AXIS_STEP, 0, 1)
      if (keys.has('f')) s.throttle = clamp(s.throttle - AXIS_STEP, 0, 1)
      if (keys.has('t')) s.trimElevator = clamp(s.trimElevator + TRIM_STEP, -1, 1)
      if (keys.has('g')) s.trimElevator = clamp(s.trimElevator - TRIM_STEP, -1, 1)
      // Mixture: shift+r / shift+f
      if (keys.has('shift') && keys.has('r')) s.mixture = clamp(s.mixture + AXIS_STEP, 0, 1)
      if (keys.has('shift') && keys.has('f')) s.mixture = clamp(s.mixture - AXIS_STEP, 0, 1)

      const ws = useSimStore.getState().ws
      if (!ws || ws.readyState !== WebSocket.OPEN) return

      // Virtual cockpit publishes to VIRTUAL priority (non-sticky, below
      // INSTRUCTOR) so IOS overrides still work and the cockpit doesn't
      // permanently take the channel on first keypress.
      ws.send(JSON.stringify({
        type: 'set_virtual_flight_controls',
        data: {
          aileron_norm: s.aileron, elevator_norm: s.elevator, rudder_norm: s.rudder,
          collective_norm: 0, trim_aileron_norm: 0, trim_elevator_norm: s.trimElevator, trim_rudder_norm: 0,
          brake_left_norm: s.brakeHeld ? 1.0 : 0.0,
          brake_right_norm: s.brakeHeld ? 1.0 : 0.0,
          parking_brake: s.parkingBrake,
        }
      }))
      ws.send(JSON.stringify({
        type: 'set_virtual_engine_controls',
        data: {
          throttle_norm: [s.throttle], mixture_norm: [s.mixture],
          magneto_left: [s.magnetoLeft], magneto_right: [s.magnetoRight],
          starter: s.starter,
        }
      }))
      // Starter engage switch for electrical solver load gating
      ws.send(JSON.stringify({
        type: 'set_virtual_panel',
        data: { switch_ids: ['sw_starter_engage'], switch_states: [s.starter] }
      }))
    }, 50) // 20 Hz

    return () => {
      clearInterval(interval)
      window.removeEventListener('keydown', onKeyDown)
      window.removeEventListener('keyup', onKeyUp)
      window.removeEventListener('blur', onBlur)
    }
  }, [])

  return { state: state.current, setThrottle, setMixture, setMagneto }
}
