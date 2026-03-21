import { useSimStore } from '../../store/useSimStore'
import { SectionHeader, FullWidthBtn } from './PanelUtils'

export default function ScenariosPanel() {
  const { sendCommand } = useSimStore()

  const saveScenario = () => {
    const name = prompt('Scenario name:')
    if (name) sendCommand(15, { name })
  }

  return (
    <div>
      <FullWidthBtn
        label="SAVE CURRENT STATE"
        style={{ marginBottom: 12 }}
        onClick={saveScenario}
      />
      <SectionHeader title="SAVED SCENARIOS" />
      <div style={{ color: '#64748b', fontSize: 12 }}>No scenarios saved yet</div>
    </div>
  )
}
