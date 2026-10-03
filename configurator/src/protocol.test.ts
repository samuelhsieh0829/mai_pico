import { describe, expect, it } from 'vitest'
import { parseCfgLine } from './protocol'

describe('CFG/1 framing', () => {
  it('parses INFO even when the CLI prompt precedes the marker', () => {
    expect(parseCfgLine('mai_pico>@CFG/1 INFO serial=abc123 sensors=0f sensor_count=4 channels=48 zones=34 buttons=12 leds_per_button=4 touch_detection=direct_delta')).toEqual({
      type: 'info',
      value: { serial: 'abc123', sensorMask: 15, sensorCount: 4, channels: 48, zones: 34, buttons: 12, ledsPerButton: 4, touchDetection: 'direct_delta' },
    })
  })

  it('identifies old status-bit firmware when INFO has no detection field', () => {
    const event = parseCfgLine('@CFG/1 INFO serial=old sensors=0f sensor_count=4 channels=48 zones=34 buttons=12 leds_per_button=4')
    expect(event?.type).toBe('info')
    if (event?.type !== 'info') return
    expect(event.value.touchDetection).toBe('mpr121_status')
  })

  it('parses exactly 48 baseline/delta channel pairs', () => {
    const payload = Array.from({ length: 48 }, (_, index) => `${700 + index}:${index - 3}`).join(',')
    const event = parseCfgLine(`@CFG/1 DATA TOUCH 91 0f ${payload}`)
    expect(event?.type).toBe('touch-sample')
    if (event?.type !== 'touch-sample') return
    expect(event.sample.sequence).toBe(91)
    expect(event.sample.baseline).toHaveLength(48)
    expect(event.sample.delta[47]).toBe(44)
  })

  it('rejects truncated touch frames and parses GPIO bitmaps', () => {
    expect(parseCfgLine('@CFG/1 DATA TOUCH 1 0f 700:1')).toBeNull()
    expect(parseCfgLine('@CFG/1 DATA GPIO 2 a5f')).toEqual({
      type: 'gpio-sample', sample: { sequence: 2, levels: 0xa5f },
    })
  })

  it('parses live state with diagnostic deltas and active thresholds', () => {
    const delta = Array.from({ length: 34 }, (_, index) => index - 4).join(',')
    const press = Array.from({ length: 34 }, (_, index) => 20 + index % 3).join(',')
    const release = Array.from({ length: 34 }, (_, index) => 12 + index % 3).join(',')
    expect(parseCfgLine(`@CFG/1 DATA LIVE 25 0f 200000001 805 ${delta} ${press} ${release}`)).toEqual({
      type: 'live-sample',
      sample: {
        sequence: 25,
        sensorMask: 0x0f,
        touchMask: 0x200000001n,
        buttonMask: 0x805,
        delta: Array.from({ length: 34 }, (_, index) => index - 4),
        pressThresholds: Array.from({ length: 34 }, (_, index) => 20 + index % 3),
        releaseThresholds: Array.from({ length: 34 }, (_, index) => 12 + index % 3),
      },
    })
  })

  it('keeps accepting live frames from older firmware', () => {
    const delta = Array.from({ length: 34 }, () => 0).join(',')
    const event = parseCfgLine(`@CFG/1 DATA LIVE 1 0f 0 0 ${delta}`)
    expect(event?.type).toBe('live-sample')
    if (event?.type !== 'live-sample') return
    expect(event.sample.pressThresholds).toEqual([])
    expect(event.sample.releaseThresholds).toEqual([])
  })

  it('rejects malformed live frames', () => {
    expect(parseCfgLine('@CFG/1 DATA LIVE 1 0f 1 1 1,2,3')).toBeNull()
    expect(parseCfgLine('@CFG/1 DATA LIVE 1 0f nope 1 0')).toBeNull()
  })
})
