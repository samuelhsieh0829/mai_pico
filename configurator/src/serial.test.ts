import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { MaiPicoSerial } from './serial'

function makePort(reply = '@CFG/1 INFO serial=test sensors=0f sensor_count=4 channels=48 zones=34 buttons=12 leds_per_button=4\r\n') {
  let controller: ReadableStreamDefaultController<Uint8Array>
  const readable = new ReadableStream<Uint8Array>({
    start(value) { controller = value },
  })
  const commands: string[] = []
  const writable = new WritableStream<Uint8Array>({
    write(bytes) {
      commands.push(new TextDecoder().decode(bytes))
      if (reply) controller.enqueue(new TextEncoder().encode(reply))
    },
  })
  return { readable, writable, commands, open: vi.fn().mockResolvedValue(undefined), close: vi.fn().mockResolvedValue(undefined) }
}

describe('serial connection and retry', () => {
  let client: MaiPicoSerial

  beforeEach(() => {
    client = new MaiPicoSerial()
    vi.stubGlobal('window', { setTimeout, clearTimeout })
  })

  afterEach(async () => {
    await client.disconnect()
    vi.useRealTimers()
    vi.unstubAllGlobals()
  })

  it('opens the chooser immediately even when the authorized-port query would hang', async () => {
    const port = makePort()
    const getPorts = vi.fn(() => new Promise(() => {}))
    const requestPort = vi.fn().mockResolvedValue(port)
    vi.stubGlobal('navigator', { serial: { getPorts, requestPort } })

    const connection = client.connect()
    expect(requestPort).toHaveBeenCalledTimes(1)
    expect(await connection).toMatchObject({ sensorMask: 15, sensorCount: 4 })
    expect(getPorts).not.toHaveBeenCalled()
    expect(port.commands).toEqual(['cfg info\r\n'])
  })

  it('lets the user select CLI rather than reusing the sole authorized game port', async () => {
    const gamePort = makePort('')
    const cliPort = makePort()
    const getPorts = vi.fn().mockResolvedValue([gamePort])
    vi.stubGlobal('navigator', { serial: { getPorts, requestPort: vi.fn().mockResolvedValue(cliPort) } })

    await client.connect()
    expect(gamePort.open).not.toHaveBeenCalled()
    expect(cliPort.open).toHaveBeenCalledWith({ baudRate: 115200 })
  })

  it('allows unfiltered selection and still requires a CFG info response', async () => {
    const port = makePort()
    const requestPort = vi.fn().mockResolvedValue(port)
    vi.stubGlobal('navigator', { serial: { requestPort } })

    expect(await client.connect(true)).toMatchObject({ serial: 'test', zones: 34 })
    expect(requestPort).toHaveBeenCalledWith(undefined)
    expect(port.commands).toEqual(['cfg info\r\n'])
  })

  it('releases a port that does not answer CFG and allows a fresh selection', async () => {
    vi.useFakeTimers()
    const wrongPort = makePort('')
    const cliPort = makePort()
    const requestPort = vi.fn().mockResolvedValueOnce(wrongPort).mockResolvedValueOnce(cliPort)
    vi.stubGlobal('navigator', { serial: { requestPort } })

    const failure = expect(client.connect()).rejects.toThrow('沒有收到 CFG/1 回覆')
    await vi.advanceTimersByTimeAsync(4000)
    await failure
    expect(client.connected).toBe(false)
    expect(wrongPort.close).toHaveBeenCalledTimes(1)
    expect(wrongPort.readable.locked).toBe(false)
    expect(wrongPort.writable.locked).toBe(false)
    expect(await client.connect()).toMatchObject({ serial: 'test' })
  })

  it('clears a failed open so a busy port can be retried', async () => {
    const busyPort = makePort()
    busyPort.open.mockRejectedValue(new Error('Port is busy'))
    const cliPort = makePort()
    const requestPort = vi.fn().mockResolvedValueOnce(busyPort).mockResolvedValueOnce(cliPort)
    vi.stubGlobal('navigator', { serial: { requestPort } })

    await expect(client.connect()).rejects.toThrow('Port is busy')
    expect(client.connected).toBe(false)
    expect(await client.connect()).toMatchObject({ serial: 'test' })
  })
})
