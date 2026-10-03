import { BUTTON_GPIOS, BUTTON_IDS, MPR121_ADDRESSES, TOUCH_ZONES } from './types'
import type { HardwareProfile } from './types'

export function validateProfile(value: unknown): HardwareProfile {
  if (!value || typeof value !== 'object') throw new Error('設定檔不是 JSON 物件。')
  const profile = value as Partial<HardwareProfile>
  if (profile.schema !== 'mai-pico-hardware-profile' || profile.formatVersion !== 1) {
    throw new Error('不支援的設定檔格式或版本。')
  }
  if (profile.ledsPerButton !== 4) throw new Error('這個硬體設定檔必須使用每按鍵 4 顆串接 LED。')
  if (JSON.stringify(profile.mpr121Addresses) !== JSON.stringify(MPR121_ADDRESSES)) {
    throw new Error('MPR121 位址必須為 0x5A、0x5B、0x5C、0x5D。')
  }
  if (!Array.isArray(profile.touch) || profile.touch.length !== 34) throw new Error('設定檔必須包含 34 個觸控區。')
  if (!Array.isArray(profile.buttons) || profile.buttons.length !== 12) throw new Error('設定檔必須包含 12 顆按鍵。')

  const isInteger = (entry: unknown) => typeof entry === 'number' && Number.isInteger(entry)
  const isFiniteNumber = (entry: unknown) => typeof entry === 'number' && Number.isFinite(entry)

  const zones = new Set(profile.touch.map((entry) => entry.zone))
  const physical = new Set(profile.touch.map((entry) => entry.sensor * 12 + entry.channel))
  if (TOUCH_ZONES.some((zone) => !zones.has(zone)) || physical.size !== 34) throw new Error('觸控區名稱或實體通道有缺漏／重複。')
  for (const entry of profile.touch) {
    if (!TOUCH_ZONES.includes(entry.zone) || !isInteger(entry.sensor) || !isInteger(entry.channel) ||
        !isInteger(entry.touchThreshold) || !isInteger(entry.releaseThreshold) ||
        !isFiniteNumber(entry.baseline) || !isFiniteNumber(entry.signal) ||
        !isFiniteNumber(entry.noise) || !isFiniteNumber(entry.snr) ||
        entry.sensor < 0 || entry.sensor > 3 || entry.channel < 0 || entry.channel > 11 ||
        entry.releaseThreshold < 1 || entry.releaseThreshold >= entry.touchThreshold || entry.touchThreshold > 255) {
      throw new Error(`觸控區 ${entry.zone} 的映射或 threshold 不合法。`)
    }
  }

  const ids = new Set(profile.buttons.map((entry) => entry.id))
  const gpios = new Set(profile.buttons.map((entry) => entry.gpio))
  if (BUTTON_IDS.some((id) => !ids.has(id)) || gpios.size !== 12 ||
      profile.buttons.some((entry) => !BUTTON_IDS.includes(entry.id) || !isInteger(entry.gpio) ||
        typeof entry.activeHigh !== 'boolean' || !BUTTON_GPIOS.includes(entry.gpio as typeof BUTTON_GPIOS[number]))) {
    throw new Error('按鍵必須一對一使用固定的 12 個 GPIO。')
  }
  if (typeof profile.createdAt !== 'string' || typeof profile.sourceDeviceSerial !== 'string') {
    throw new Error('設定檔缺少建立時間或來源裝置序號。')
  }
  return profile as HardwareProfile
}

export function downloadProfile(profile: HardwareProfile) {
  const blob = new Blob([JSON.stringify(profile, null, 2)], { type: 'application/json' })
  const url = URL.createObjectURL(blob)
  const link = document.createElement('a')
  link.href = url
  link.download = `mai-pico-${profile.sourceDeviceSerial || 'profile'}.json`
  document.body.append(link)
  link.click()
  link.remove()
  URL.revokeObjectURL(url)
}
