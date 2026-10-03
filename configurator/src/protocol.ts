import type { ButtonId, CfgEvent, TouchZone } from './types'

const keyValues = (text: string) => Object.fromEntries(
  [...text.matchAll(/([a-z_]+)=([^\s]+)/gi)].map((match) => [match[1], match[2]]),
)

export function parseCfgLine(input: string): CfgEvent | null {
  const marker = input.indexOf('@CFG/1 ')
  if (marker < 0) return null
  const line = input.slice(marker + 7).trim()

  if (line.startsWith('INFO ')) {
    const fields = keyValues(line.slice(5))
    return {
      type: 'info',
      value: {
        serial: fields.serial,
        sensorMask: Number.parseInt(fields.sensors, 16),
        sensorCount: Number(fields.sensor_count),
        channels: Number(fields.channels),
        zones: Number(fields.zones),
        buttons: Number(fields.buttons),
        ledsPerButton: Number(fields.leds_per_button),
        touchDetection: fields.touch_detection === 'direct_delta' ? 'direct_delta' : 'mpr121_status',
      },
    }
  }
  if (line.startsWith('GPIO_LIST ')) {
    return { type: 'gpio-list', gpios: line.slice(10).split(',').map(Number) }
  }
  if (line.startsWith('DATA TOUCH ')) {
    const match = /^DATA TOUCH (\d+) ([0-9a-f]+) (.+)$/i.exec(line)
    if (!match) return null
    const pairs = match[3].split(',').map((pair) => pair.split(':').map(Number))
    if (pairs.length !== 48 || pairs.some((pair) => pair.length !== 2 || pair.some(Number.isNaN))) return null
    return {
      type: 'touch-sample',
      sample: {
        sequence: Number(match[1]),
        sensorMask: Number.parseInt(match[2], 16),
        baseline: pairs.map((pair) => pair[0]),
        delta: pairs.map((pair) => pair[1]),
      },
    }
  }
  if (line.startsWith('DATA GPIO ')) {
    const match = /^DATA GPIO (\d+) ([0-9a-f]+)$/i.exec(line)
    return match ? {
      type: 'gpio-sample',
      sample: { sequence: Number(match[1]), levels: Number.parseInt(match[2], 16) },
    } : null
  }
  if (line.startsWith('DATA LIVE ')) {
    const match = /^DATA LIVE (\d+) ([0-9a-f]+) ([0-9a-f]+) ([0-9a-f]+) (.+)$/i.exec(line)
    if (!match) return null
    const payloads = match[5].trim().split(/\s+/)
    if (payloads.length !== 1 && payloads.length !== 3) return null
    const [delta, pressThresholds = [], releaseThresholds = []] = payloads.map((payload) => payload.split(',').map(Number))
    if (delta.length !== 34 || delta.some(Number.isNaN) ||
        (payloads.length === 3 && (pressThresholds.length !== 34 || releaseThresholds.length !== 34 ||
          pressThresholds.some(Number.isNaN) || releaseThresholds.some(Number.isNaN)))) return null
    return {
      type: 'live-sample',
      sample: {
        sequence: Number(match[1]),
        sensorMask: Number.parseInt(match[2], 16),
        touchMask: BigInt(`0x${match[3]}`),
        buttonMask: Number.parseInt(match[4], 16),
        delta,
        pressThresholds,
        releaseThresholds,
      },
    }
  }
  if (line.startsWith('TOUCH ')) {
    const fields = keyValues(line.slice(6))
    return {
      type: 'touch-config',
      value: {
        zone: fields.zone as TouchZone,
        sensor: Number(fields.sensor),
        channel: Number(fields.channel),
        touchThreshold: Number(fields.touch),
        releaseThreshold: Number(fields.release),
      },
    }
  }
  if (line.startsWith('BUTTON ')) {
    const fields = keyValues(line.slice(7))
    return {
      type: 'button-config',
      value: {
        id: fields.id as ButtonId,
        gpio: Number(fields.gpio),
        activeHigh: fields.active_high === '1',
      },
    }
  }
  if (line.startsWith('LEDS ')) {
    const fields = keyValues(line.slice(5))
    return { type: 'leds-config', perButton: Number(fields.per_button) }
  }
  if (line.startsWith('ACK ')) return { type: 'ack', action: line.slice(4).trim() }
  if (line.startsWith('ERR ')) return { type: 'error', code: line.slice(4).trim() }
  if (line === 'END') return { type: 'end' }
  return null
}
