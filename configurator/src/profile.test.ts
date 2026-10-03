import { describe, expect, it } from 'vitest'
import { validateProfile } from './profile'
import { BUTTON_GPIOS, BUTTON_IDS, MPR121_ADDRESSES, TOUCH_ZONES } from './types'
import type { HardwareProfile } from './types'

const fixture = (): HardwareProfile => ({
  schema: 'mai-pico-hardware-profile',
  formatVersion: 1,
  createdAt: '2026-08-30T00:00:00.000Z',
  sourceDeviceSerial: 'test-pico',
  mpr121Addresses: [...MPR121_ADDRESSES],
  ledsPerButton: 4,
  touch: TOUCH_ZONES.map((zone, physical) => ({
    zone,
    sensor: Math.floor(physical / 12),
    channel: physical % 12,
    touchThreshold: 22,
    releaseThreshold: 15,
    baseline: 800,
    signal: 60,
    noise: 2,
    snr: 30,
  })),
  buttons: BUTTON_IDS.map((id, index) => ({ id, gpio: BUTTON_GPIOS[index], activeHigh: false })),
})

describe('hardware profile validation', () => {
  it('accepts a complete unique 34-zone / 12-button profile', () => {
    expect(validateProfile(fixture()).schema).toBe('mai-pico-hardware-profile')
  })

  it('rejects duplicate physical channels and GPIOs outside the fixed set', () => {
    const duplicate = fixture()
    duplicate.touch[1].sensor = duplicate.touch[0].sensor
    duplicate.touch[1].channel = duplicate.touch[0].channel
    expect(() => validateProfile(duplicate)).toThrow(/重複/)

    const gpio = fixture()
    gpio.buttons[0].gpio = 28
    expect(() => validateProfile(gpio)).toThrow(/固定的 12 個 GPIO/)
  })

  it('rejects malformed numeric fields and illegal thresholds', () => {
    const malformed = fixture()
    malformed.touch[0].sensor = Number.NaN
    expect(() => validateProfile(malformed)).toThrow(/不合法/)

    const thresholds = fixture()
    thresholds.touch[0].releaseThreshold = thresholds.touch[0].touchThreshold
    expect(() => validateProfile(thresholds)).toThrow(/不合法/)
  })
})
