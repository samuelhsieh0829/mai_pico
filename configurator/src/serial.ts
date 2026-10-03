import { parseCfgLine } from './protocol'
import type { CfgEvent, DeviceDump, DeviceInfo } from './types'

interface SerialPortLike {
  readable: ReadableStream<Uint8Array> | null
  writable: WritableStream<Uint8Array> | null
  open(options: { baudRate: number }): Promise<void>
  close(): Promise<void>
}

interface SerialNavigator extends Navigator {
  serial?: {
    requestPort(options?: { filters: Array<{ usbVendorId: number; usbProductId: number }> }): Promise<SerialPortLike>
  }
}

type EventListener = (event: CfgEvent) => void
type LineListener = (line: string) => void

const cfgErrorMessages: Record<string, string> = {
  PROFILE_INVALID: '設定檔驗證失敗；舊版韌體也可能因 MPR121 離線而拒絕保存。',
  PROFILE_EXTENSION: 'Flash 設定格式無效，請重新燒錄韌體或恢復預設值。',
  PROFILE_SENSORS: 'MPR121 未全部上線；此版韌體不應以此阻止按鍵設定。',
  PROFILE_LEDS: '每鍵 LED 數量超出範圍。',
  PROFILE_TOUCH_MAP: '觸控映射有重複或缺少區域。',
  PROFILE_THRESHOLDS: '觸控門檻不符合 1 ≤ Release < Touch ≤ 255。',
  PROFILE_BUTTONS: '按鍵 GPIO 有重複或不在安全清單中。',
}

const cfgErrorMessage = (code: string) => cfgErrorMessages[code] ?? `Pico 拒絕設定：${code}`

export class MaiPicoSerial {
  private port: SerialPortLike | null = null
  private reader: ReadableStreamDefaultReader<Uint8Array> | null = null
  private writer: WritableStreamDefaultWriter<Uint8Array> | null = null
  private eventListeners = new Set<EventListener>()
  private lineListeners = new Set<LineListener>()
  private pending = new Map<string, { resolve: () => void; reject: (error: Error) => void; timer: number }>()
  private decoder = new TextDecoder()
  private buffer = ''
  private closing = false

  get supported() { return Boolean((navigator as SerialNavigator).serial) }
  get connected() { return this.port !== null }

  onEvent(listener: EventListener) {
    this.eventListeners.add(listener)
    return () => this.eventListeners.delete(listener)
  }

  onLine(listener: LineListener) {
    this.lineListeners.add(listener)
    return () => this.lineListeners.delete(listener)
  }

  async connect(showAllPorts = false): Promise<DeviceInfo> {
    const serial = (navigator as SerialNavigator).serial
    if (!serial) throw new Error('這個瀏覽器不支援 Web Serial，請使用 Chrome 或 Edge。')
    // Open the chooser directly from the click, without waiting for getPorts().
    // A previously authorized port may also be a game I/O port rather than CLI.
    this.port = await serial.requestPort(showAllPorts ? undefined : {
      filters: [{ usbVendorId: 0x0ca3, usbProductId: 0x0021 }],
    })
    this.closing = false
    this.buffer = ''
    this.decoder = new TextDecoder()
    try {
      await this.port.open({ baudRate: 115200 })
      if (!this.port.readable || !this.port.writable) throw new Error('序列埠沒有可用的讀寫串流。')
      this.reader = this.port.readable.getReader()
      this.writer = this.port.writable.getWriter()
      void this.readLoop()
      return await this.requestInfo()
    } catch (error) {
      await this.disconnect()
      throw error
    }
  }

  async disconnect() {
    this.closing = true
    for (const pending of this.pending.values()) {
      window.clearTimeout(pending.timer)
      pending.reject(new Error('裝置已中斷'))
    }
    this.pending.clear()
    try { await this.reader?.cancel() } catch { /* port already closed */ }
    try { this.reader?.releaseLock() } catch { /* no lock */ }
    try { this.writer?.releaseLock() } catch { /* no lock */ }
    this.reader = null
    this.writer = null
    try { await this.port?.close() } catch { /* port already closed */ }
    this.port = null
    this.closing = false
  }

  async write(command: string) {
    if (!this.writer) throw new Error('尚未連線')
    await this.writer.write(new TextEncoder().encode(`${command}\r\n`))
  }

  command(command: string, expectedAck: string, timeoutMs = 4000): Promise<void> {
    if (this.pending.has(expectedAck)) return Promise.reject(new Error(`已有 ${expectedAck} 指令等待回覆`))
    return new Promise((resolve, reject) => {
      const timer = window.setTimeout(() => {
        this.pending.delete(expectedAck)
        reject(new Error(`${expectedAck} 等待回覆逾時；請確認選到 Command Line Port。`))
      }, timeoutMs)
      this.pending.set(expectedAck, { resolve, reject, timer })
      void this.write(command).catch((error) => {
        window.clearTimeout(timer)
        this.pending.delete(expectedAck)
        reject(error)
      })
    })
  }

  requestInfo(timeoutMs = 4000): Promise<DeviceInfo> {
    return new Promise((resolve, reject) => {
      const off = this.onEvent((event) => {
        if (event.type === 'info') {
          window.clearTimeout(timer)
          off()
          resolve(event.value)
        }
      })
      const timer = window.setTimeout(() => {
        off()
        reject(new Error('沒有收到 CFG/1 回覆；請重新選擇 Mai Pico Command Line Port。'))
      }, timeoutMs)
      void this.write('cfg info').catch((error) => {
        window.clearTimeout(timer)
        off()
        reject(error)
      })
    })
  }

  requestDump(timeoutMs = 6000): Promise<DeviceDump> {
    return new Promise((resolve, reject) => {
      const touch: DeviceDump['touch'] = []
      const buttons: DeviceDump['buttons'] = []
      let ledsPerButton = -1
      const off = this.onEvent((event) => {
        if (event.type === 'touch-config') {
          touch.push({ ...event.value, baseline: 0, signal: 0, noise: 0, snr: 0 })
        } else if (event.type === 'button-config') {
          buttons.push(event.value)
        } else if (event.type === 'leds-config') {
          ledsPerButton = event.perButton
        } else if (event.type === 'end') {
          window.clearTimeout(timer)
          off()
          resolve({ touch, buttons, ledsPerButton })
        } else if (event.type === 'disconnect') {
          window.clearTimeout(timer)
          off()
          reject(new Error('讀取設定時裝置已中斷。'))
        }
      })
      const timer = window.setTimeout(() => {
        off()
        reject(new Error('cfg dump 等待回覆逾時。'))
      }, timeoutMs)
      void this.write('cfg dump').catch((error) => {
        window.clearTimeout(timer)
        off()
        reject(error)
      })
    })
  }

  private dispatch(event: CfgEvent) {
    if (event.type === 'ack') {
      const pending = this.pending.get(event.action)
      if (pending) {
        window.clearTimeout(pending.timer)
        this.pending.delete(event.action)
        pending.resolve()
      }
    } else if (event.type === 'error') {
      const first = this.pending.entries().next().value as [string, { reject: (error: Error) => void; timer: number }] | undefined
      if (first) {
        window.clearTimeout(first[1].timer)
        this.pending.delete(first[0])
        first[1].reject(new Error(cfgErrorMessage(event.code)))
      }
    } else if (event.type === 'disconnect') {
      for (const pending of this.pending.values()) {
        window.clearTimeout(pending.timer)
        pending.reject(new Error('裝置已中斷'))
      }
      this.pending.clear()
    }
    this.eventListeners.forEach((listener) => listener(event))
  }

  private async readLoop() {
    try {
      while (this.reader) {
        const { value, done } = await this.reader.read()
        if (done) break
        this.buffer += this.decoder.decode(value, { stream: true })
        const lines = this.buffer.split(/\r?\n/)
        this.buffer = lines.pop() ?? ''
        for (const line of lines) {
          this.lineListeners.forEach((listener) => listener(line))
          const event = parseCfgLine(line)
          if (event) this.dispatch(event)
        }
      }
    } catch (error) {
      this.lineListeners.forEach((listener) => listener(`讀取中斷：${String(error)}`))
    } finally {
      if (!this.closing && this.port) this.dispatch({ type: 'disconnect' })
    }
  }
}
