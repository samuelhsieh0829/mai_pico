export const TOUCH_ZONES = [
  'A1', 'A2', 'A3', 'A4', 'A5', 'A6', 'A7', 'A8',
  'B1', 'B2', 'B3', 'B4', 'B5', 'B6', 'B7', 'B8',
  'C1', 'C2',
  'D1', 'D2', 'D3', 'D4', 'D5', 'D6', 'D7', 'D8',
  'E1', 'E2', 'E3', 'E4', 'E5', 'E6', 'E7', 'E8',
] as const

export const BUTTON_IDS = [
  'B1', 'B2', 'B3', 'B4', 'B5', 'B6', 'B7', 'B8',
  'Test', 'Service', 'Navigate', 'Coin',
] as const

export const BUTTON_GPIOS = [1, 0, 4, 5, 8, 9, 3, 2, 12, 11, 10, 14] as const
export const MPR121_ADDRESSES = ['0x5A', '0x5B', '0x5C', '0x5D'] as const

export type TouchZone = typeof TOUCH_ZONES[number]
export type ButtonId = typeof BUTTON_IDS[number]

export interface DeviceInfo {
  serial: string
  sensorMask: number
  sensorCount: number
  channels: number
  zones: number
  buttons: number
  ledsPerButton: number
  touchDetection: 'direct_delta' | 'mpr121_status'
}

export interface TouchSample {
  sequence: number
  sensorMask: number
  baseline: number[]
  delta: number[]
}

export interface GpioSample {
  sequence: number
  levels: number
}

export interface LiveSample {
  sequence: number
  sensorMask: number
  touchMask: bigint
  buttonMask: number
  delta: number[]
  pressThresholds: number[]
  releaseThresholds: number[]
}

export interface TouchQuality {
  baseline: number
  signal: number
  noise: number
  snr: number
}

export interface TouchMapping extends TouchQuality {
  zone: TouchZone
  sensor: number
  channel: number
  touchThreshold: number
  releaseThreshold: number
}

export interface ButtonMapping {
  id: ButtonId
  gpio: number
  activeHigh: boolean
}

export interface HardwareProfile {
  schema: 'mai-pico-hardware-profile'
  formatVersion: 1
  createdAt: string
  sourceDeviceSerial: string
  mpr121Addresses: string[]
  ledsPerButton: number
  touch: TouchMapping[]
  buttons: ButtonMapping[]
}

export interface DeviceDump {
  touch: TouchMapping[]
  buttons: ButtonMapping[]
  ledsPerButton: number
}

export type CfgEvent =
  | { type: 'info'; value: DeviceInfo }
  | { type: 'gpio-list'; gpios: number[] }
  | { type: 'touch-sample'; sample: TouchSample }
  | { type: 'gpio-sample'; sample: GpioSample }
  | { type: 'live-sample'; sample: LiveSample }
  | { type: 'touch-config'; value: Pick<TouchMapping, 'zone' | 'sensor' | 'channel' | 'touchThreshold' | 'releaseThreshold'> }
  | { type: 'button-config'; value: ButtonMapping }
  | { type: 'leds-config'; perButton: number }
  | { type: 'ack'; action: string }
  | { type: 'error'; code: string }
  | { type: 'disconnect' }
  | { type: 'end' }
