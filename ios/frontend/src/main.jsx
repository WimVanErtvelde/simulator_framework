import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import { BrowserRouter, Routes, Route } from 'react-router-dom'
import './index.css'
import App from './App.jsx'
import CockpitElectrical from './cockpit/CockpitElectrical.jsx'
import CockpitAvionics from './cockpit/CockpitAvionics.jsx'
import C172Panel from './cockpit/C172Panel.jsx'
import { useSimStore } from './store/useSimStore'

// Connect WebSocket before rendering
useSimStore.getState().connectWS()

createRoot(document.getElementById('root')).render(
  <StrictMode>
    <BrowserRouter>
      <Routes>
        <Route path="/" element={<App />} />
        <Route path="/cockpit/c172/electrical" element={<CockpitElectrical />} />
        <Route path="/cockpit/c172/avionics" element={<CockpitAvionics />} />
        <Route path="/cockpit/c172/panel" element={<C172Panel />} />
      </Routes>
    </BrowserRouter>
  </StrictMode>,
)
