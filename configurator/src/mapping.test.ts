import { describe, expect, it } from 'vitest'
import { assignButtonGpio, assignTouchChannel, draftFromDump, mappingChangeCount, normalizedTouchIntensity, validateMappingDraft } from './mapping'
import { BUTTON_GPIOS, BUTTON_IDS, TOUCH_ZONES } from './types'
import type { DeviceDump } from './types'

function validDump(): DeviceDump {
  return {
    touch: TOUCH_ZONES.map((zone, index) => ({
      zone, sensor: Math.floor(index / 12), channel: index % 12,
      touchThreshold: 22, releaseThreshold: 11, baseline: 0, signal: 0, noise: 0, snr: 0,
    })),
    buttons: BUTTON_IDS.map((id, index) => ({ id, gpio: BUTTON_GPIOS[index], activeHigh: false })),
    ledsPerButton: 4,
  }
}

describe('mapping editor', () => {
  it('hydrates rows in logical display order', () => {
    const dump = validDump()
    dump.touch.reverse()
    const draft = draftFromDump(dump)
    expect(draft.touch[0].zone).toBe('A1')
    expect(draft.touch[33].zone).toBe('E8')
  })

  it('swaps occupied touch channels and button GPIOs', () => {
    const draft = draftFromDump(validDump())
    assignTouchChannel(draft.touch, 'A1', 0, 1)
    expect(draft.touch[0]).toMatchObject({ zone: 'A1', sensor: 0, channel: 1 })
    expect(draft.touch[1]).toMatchObject({ zone: 'A2', sensor: 0, channel: 0 })
    assignButtonGpio(draft.buttons, 'B1', BUTTON_GPIOS[1])
    expect(draft.buttons[0].gpio).toBe(BUTTON_GPIOS[1])
    expect(draft.buttons[1].gpio).toBe(BUTTON_GPIOS[0])
  })

  it('validates thresholds and detects changes', () => {
    const saved = draftFromDump(validDump())
    const draft = draftFromDump(validDump())
    draft.touch[0].releaseThreshold = draft.touch[0].touchThreshold
    expect(validateMappingDraft(draft).touch.A1).toContain('Release')
    expect(mappingChangeCount(saved, draft)).toBe(1)
  })

  it('normalizes live delta against the configured touch threshold', () => {
    expect(normalizedTouchIntensity(-2, 20)).toBe(0)
    expect(normalizedTouchIntensity(10, 20)).toBe(0.5)
    expect(normalizedTouchIntensity(40, 20)).toBe(1)
  })
})
