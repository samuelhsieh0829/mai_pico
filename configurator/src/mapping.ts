import { BUTTON_GPIOS, BUTTON_IDS, TOUCH_ZONES } from './types'
import type { ButtonMapping, DeviceDump, TouchMapping, TouchZone } from './types'

export interface MappingDraft {
  touch: TouchMapping[]
  buttons: ButtonMapping[]
  ledsPerButton: number
}

export interface MappingErrors {
  touch: Record<string, string>
  buttons: Record<string, string>
  general: string[]
}

const cloneTouch = (entry: TouchMapping): TouchMapping => ({ ...entry })
const cloneButton = (entry: ButtonMapping): ButtonMapping => ({ ...entry })

export function draftFromDump(dump: DeviceDump): MappingDraft {
  const byZone = new Map(dump.touch.map((entry) => [entry.zone, entry]))
  const byButton = new Map(dump.buttons.map((entry) => [entry.id, entry]))
  return {
    touch: TOUCH_ZONES.map((zone) => cloneTouch(byZone.get(zone) ?? {
      zone, sensor: -1, channel: -1, touchThreshold: 22, releaseThreshold: 11,
      baseline: 0, signal: 0, noise: 0, snr: 0,
    })),
    buttons: BUTTON_IDS.map((id, index) => cloneButton(byButton.get(id) ?? {
      id, gpio: BUTTON_GPIOS[index], activeHigh: false,
    })),
    ledsPerButton: dump.ledsPerButton,
  }
}

export function cloneDraft(draft: MappingDraft): MappingDraft {
  return {
    touch: draft.touch.map(cloneTouch),
    buttons: draft.buttons.map(cloneButton),
    ledsPerButton: draft.ledsPerButton,
  }
}

export function assignTouchChannel(entries: TouchMapping[], zone: TouchZone, sensor: number, channel: number) {
  const target = entries.find((entry) => entry.zone === zone)
  if (!target || (target.sensor === sensor && target.channel === channel)) return
  const oldSensor = target.sensor
  const oldChannel = target.channel
  const occupied = entries.find((entry) => entry !== target && entry.sensor === sensor && entry.channel === channel)
  target.sensor = sensor
  target.channel = channel
  if (occupied) {
    occupied.sensor = oldSensor
    occupied.channel = oldChannel
  }
}

export function assignButtonGpio(entries: ButtonMapping[], id: ButtonMapping['id'], gpio: number) {
  const target = entries.find((entry) => entry.id === id)
  if (!target || target.gpio === gpio) return
  const oldGpio = target.gpio
  const occupied = entries.find((entry) => entry !== target && entry.gpio === gpio)
  target.gpio = gpio
  if (occupied) occupied.gpio = oldGpio
}

export function validateMappingDraft(draft: MappingDraft): MappingErrors {
  const errors: MappingErrors = { touch: {}, buttons: {}, general: [] }
  const physical = new Map<number, string>()
  for (const entry of draft.touch) {
    if (!Number.isInteger(entry.sensor) || entry.sensor < 0 || entry.sensor > 3 ||
        !Number.isInteger(entry.channel) || entry.channel < 0 || entry.channel > 11) {
      errors.touch[entry.zone] = '請選擇有效的 MPR 與 ID。'
      continue
    }
    if (!Number.isInteger(entry.touchThreshold) || entry.touchThreshold < 2 || entry.touchThreshold > 255 ||
        !Number.isInteger(entry.releaseThreshold) || entry.releaseThreshold < 1 || entry.releaseThreshold >= entry.touchThreshold) {
      errors.touch[entry.zone] = '需符合 1 ≤ Release < Touch ≤ 255。'
    }
    const key = entry.sensor * 12 + entry.channel
    const owner = physical.get(key)
    if (owner) {
      errors.touch[entry.zone] = `與 ${owner} 使用相同通道。`
      errors.touch[owner] = `與 ${entry.zone} 使用相同通道。`
    } else physical.set(key, entry.zone)
  }

  const gpios = new Map<number, string>()
  for (const entry of draft.buttons) {
    if (!BUTTON_GPIOS.includes(entry.gpio as typeof BUTTON_GPIOS[number])) {
      errors.buttons[entry.id] = 'GPIO 不在允許清單中。'
      continue
    }
    const owner = gpios.get(entry.gpio)
    if (owner) {
      errors.buttons[entry.id] = `與 ${owner} 使用相同 GPIO。`
      errors.buttons[owner] = `與 ${entry.id} 使用相同 GPIO。`
    } else gpios.set(entry.gpio, entry.id)
  }

  if (draft.touch.length !== TOUCH_ZONES.length) errors.general.push('觸控區數量不完整。')
  if (draft.buttons.length !== BUTTON_IDS.length) errors.general.push('按鍵數量不完整。')
  return errors
}

export function mappingSnapshot(draft: MappingDraft) {
  return JSON.stringify({
    touch: draft.touch.map(({ zone, sensor, channel, touchThreshold, releaseThreshold }) =>
      ({ zone, sensor, channel, touchThreshold, releaseThreshold })),
    buttons: draft.buttons.map(({ id, gpio, activeHigh }) => ({ id, gpio, activeHigh })),
    ledsPerButton: draft.ledsPerButton,
  })
}

export function mappingChangeCount(saved: MappingDraft, draft: MappingDraft) {
  let count = saved.ledsPerButton === draft.ledsPerButton ? 0 : 1
  draft.touch.forEach((entry, index) => {
    const before = saved.touch[index]
    if (!before || entry.zone !== before.zone || entry.sensor !== before.sensor || entry.channel !== before.channel ||
        entry.touchThreshold !== before.touchThreshold || entry.releaseThreshold !== before.releaseThreshold) count++
  })
  draft.buttons.forEach((entry, index) => {
    const before = saved.buttons[index]
    if (!before || entry.id !== before.id || entry.gpio !== before.gpio || entry.activeHigh !== before.activeHigh) count++
  })
  return count
}

export function normalizedTouchIntensity(delta: number, threshold: number) {
  if (!Number.isFinite(delta) || !Number.isFinite(threshold) || threshold <= 0) return 0
  return Math.max(0, Math.min(1, delta / threshold))
}
