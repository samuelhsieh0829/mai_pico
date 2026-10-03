<script setup lang="ts">
import { computed, onBeforeUnmount, ref } from 'vue'
import LiveController from './components/LiveController.vue'
import { assignButtonGpio, assignTouchChannel, cloneDraft, draftFromDump, mappingChangeCount, mappingSnapshot, normalizedTouchIntensity, validateMappingDraft } from './mapping'
import type { MappingDraft } from './mapping'
import { downloadProfile, validateProfile } from './profile'
import { MaiPicoSerial } from './serial'
import { BUTTON_GPIOS, MPR121_ADDRESSES, TOUCH_ZONES } from './types'
import type { ButtonId, DeviceDump, DeviceInfo, HardwareProfile, LiveSample, TouchMapping } from './types'

type Tab = 'live' | 'settings' | 'terminal'

const serial = new MaiPicoSerial()
const tab = ref<Tab>('live')
const connected = ref(false)
const busy = ref(false)
const transactionActive = ref(false)
const deviceInfo = ref<DeviceInfo | null>(null)
const latestLive = ref<LiveSample | null>(null)
const saved = ref<MappingDraft | null>(null)
const draft = ref<MappingDraft | null>(null)
const errorMessage = ref('')
const notice = ref('')
const terminalLines = ref<string[]>([])
const terminalInput = ref('')
const liveMirrored = ref(false)
const showLiveValues = ref(false)

const touchGroups = [
  { name: 'A', zones: TOUCH_ZONES.filter((zone) => zone.startsWith('A')) },
  { name: 'B', zones: TOUCH_ZONES.filter((zone) => zone.startsWith('B')) },
  { name: 'C', zones: TOUCH_ZONES.filter((zone) => zone.startsWith('C')) },
  { name: 'D', zones: TOUCH_ZONES.filter((zone) => zone.startsWith('D')) },
  { name: 'E', zones: TOUCH_ZONES.filter((zone) => zone.startsWith('E')) },
]

const fourSensorsOnline = computed(() => (latestLive.value?.sensorMask ?? deviceInfo.value?.sensorMask) === 0x0f)
const pressedZones = computed(() => {
  const result = new Set<string>()
  const mask = latestLive.value?.touchMask ?? 0n
  TOUCH_ZONES.forEach((zone, index) => {
    if (mask & (1n << BigInt(index))) result.add(zone)
  })
  return result
})
const touchIntensity = computed<Record<string, number>>(() => {
  const result: Record<string, number> = {}
  const mapping = saved.value?.touch ?? []
  TOUCH_ZONES.forEach((zone, index) => {
    const threshold = latestLive.value?.pressThresholds[index] ||
      mapping.find((entry) => entry.zone === zone)?.touchThreshold || 22
    result[zone] = normalizedTouchIntensity(latestLive.value?.delta[index] ?? 0, threshold)
  })
  return result
})
const touchValues = computed<Record<string, number>>(() => {
  const result: Record<string, number> = {}
  TOUCH_ZONES.forEach((zone, index) => {
    result[zone] = latestLive.value?.delta[index] ?? 0
  })
  return result
})
const touchPressThresholds = computed<Record<string, number>>(() => {
  const result: Record<string, number> = {}
  TOUCH_ZONES.forEach((zone, index) => {
    result[zone] = latestLive.value?.pressThresholds[index] ||
      saved.value?.touch.find((entry) => entry.zone === zone)?.touchThreshold || 0
  })
  return result
})
const touchReleaseThresholds = computed<Record<string, number>>(() => {
  const result: Record<string, number> = {}
  TOUCH_ZONES.forEach((zone, index) => {
    result[zone] = latestLive.value?.releaseThresholds[index] ||
      saved.value?.touch.find((entry) => entry.zone === zone)?.releaseThreshold || 0
  })
  return result
})
const mappingErrors = computed(() => draft.value
  ? validateMappingDraft(draft.value)
  : { touch: {}, buttons: {}, general: ['尚未讀取裝置設定。'] })
const hasMappingErrors = computed(() =>
  Object.keys(mappingErrors.value.touch).length > 0 ||
  Object.keys(mappingErrors.value.buttons).length > 0 ||
  mappingErrors.value.general.length > 0)
const changeCount = computed(() => saved.value && draft.value ? mappingChangeCount(saved.value, draft.value) : 0)

function setError(error: unknown) {
  errorMessage.value = error instanceof Error ? error.message : String(error)
  notice.value = ''
}

function setNotice(message: string) {
  notice.value = message
  errorMessage.value = ''
}

const stopEvent = serial.onEvent((event) => {
  if (event.type === 'info') deviceInfo.value = event.value
  if (event.type === 'live-sample') latestLive.value = event.sample
  if (event.type === 'ack' && event.action === 'BEGIN') transactionActive.value = true
  if (event.type === 'ack' && (event.action === 'ABORT' || event.action === 'COMMIT')) transactionActive.value = false
  if (event.type === 'disconnect' && connected.value) {
    connected.value = false
    transactionActive.value = false
    latestLive.value = null
    void serial.disconnect()
    setError('Mai Pico 已中斷；尚未保存的畫面草稿仍會保留。')
  }
})

const stopLine = serial.onLine((line) => {
  terminalLines.value.push(line)
  if (terminalLines.value.length > 300) terminalLines.value.splice(0, terminalLines.value.length - 300)
})

function applyDump(dump: DeviceDump) {
  const next = draftFromDump(dump)
  saved.value = next
  draft.value = cloneDraft(next)
}

async function startLive() {
  await serial.command('cfg live on', 'LIVE_ON')
}

async function stopLive() {
  await serial.command('cfg live off', 'LIVE_OFF')
  latestLive.value = null
}

async function connect(showAllPorts = false) {
  busy.value = true
  errorMessage.value = ''
  notice.value = '請在瀏覽器的通訊埠選單中選擇 Command Line Port；若選單空白，可改用「顯示所有通訊埠」或 Edge。'
  try {
    deviceInfo.value = await serial.connect(showAllPorts)
    connected.value = true
    if (deviceInfo.value.sensorCount !== 4 || deviceInfo.value.channels !== 48 ||
        deviceInfo.value.zones !== 34 || deviceInfo.value.buttons !== 12) {
      throw new Error('這個 Command Line Port 不是相容的四顆 MPR121 CFG/1 韌體。')
    }
    applyDump(await serial.requestDump())
    await startLive()
    setNotice(fourSensorsOnline.value
      ? '已連線；即時監看不會中斷正常遊戲輸入。'
      : `只偵測到 sensor mask 0x${deviceInfo.value.sensorMask.toString(16)}，請檢查四顆 MPR121。`)
  } catch (error) {
    await serial.disconnect()
    connected.value = false
    latestLive.value = null
    setError(error)
  } finally {
    busy.value = false
  }
}

async function disconnect() {
  busy.value = true
  try {
    if (transactionActive.value) await serial.command('cfg abort', 'ABORT').catch(() => undefined)
    else await stopLive().catch(() => undefined)
    await serial.disconnect()
  } finally {
    connected.value = false
    transactionActive.value = false
    latestLive.value = null
    busy.value = false
  }
}

function touchEntry(zone: string) {
  return draft.value?.touch.find((entry) => entry.zone === zone)
}

function changeTouchSensor(entry: TouchMapping, event: Event) {
  if (!draft.value) return
  assignTouchChannel(draft.value.touch, entry.zone, Number((event.target as HTMLSelectElement).value), entry.channel)
}

function changeTouchChannel(entry: TouchMapping, event: Event) {
  if (!draft.value) return
  assignTouchChannel(draft.value.touch, entry.zone, entry.sensor, Number((event.target as HTMLSelectElement).value))
}

function changeButtonGpio(id: ButtonId, event: Event) {
  if (!draft.value) return
  assignButtonGpio(draft.value.buttons, id, Number((event.target as HTMLSelectElement).value))
}

function resetDraft() {
  if (!saved.value) return
  draft.value = cloneDraft(saved.value)
  setNotice('已放棄尚未保存的修改。')
}

function profileFromDraft(): HardwareProfile {
  if (!draft.value) throw new Error('尚未讀取裝置設定。')
  return {
    schema: 'mai-pico-hardware-profile',
    formatVersion: 1,
    createdAt: new Date().toISOString(),
    sourceDeviceSerial: deviceInfo.value?.serial ?? 'unknown',
    mpr121Addresses: [...MPR121_ADDRESSES],
    ledsPerButton: draft.value.ledsPerButton,
    touch: draft.value.touch.map((entry) => ({ ...entry })),
    buttons: draft.value.buttons.map((entry) => ({ ...entry })),
  }
}

async function stageDraft(value: MappingDraft) {
  for (const mapping of value.touch) {
    await serial.command(
      `cfg set touch ${mapping.zone} ${mapping.sensor} ${mapping.channel} ${mapping.touchThreshold} ${mapping.releaseThreshold}`,
      'SET_TOUCH',
    )
  }
  for (const mapping of value.buttons) {
    await serial.command(`cfg set button ${mapping.id} ${mapping.gpio} ${mapping.activeHigh ? 1 : 0}`, 'SET_BUTTON')
  }
  await serial.command(`cfg set leds ${value.ledsPerButton}`, 'SET_LEDS')
}

async function reloadCommitted() {
  applyDump(await serial.requestDump())
}

async function saveAll() {
  if (!connected.value || !draft.value || busy.value) return
  if (hasMappingErrors.value) return setError('請先修正標示的映射錯誤。')
  const requested = cloneDraft(draft.value)
  busy.value = true
  errorMessage.value = ''
  try {
    await stopLive()
    await serial.command('cfg begin', 'BEGIN')
    await stageDraft(requested)
    await serial.command('cfg commit', 'COMMIT', 8000)
    transactionActive.value = false
    const actual = draftFromDump(await serial.requestDump())
    if (mappingSnapshot(actual) !== mappingSnapshot(requested)) throw new Error('Flash 讀回內容與送出的設定不同。')
    saved.value = actual
    draft.value = cloneDraft(actual)
    deviceInfo.value = await serial.requestInfo()
    setNotice('已寫入 Pico Flash 並讀回確認。')
  } catch (error) {
    if (transactionActive.value) {
      await serial.command('cfg abort', 'ABORT').catch(() => undefined)
      transactionActive.value = false
    }
    await reloadCommitted().catch(() => undefined)
    setError(error)
  } finally {
    if (connected.value) await startLive().catch((error) => setError(error))
    busy.value = false
  }
}

async function importProfile(event: Event) {
  const input = event.target as HTMLInputElement
  const file = input.files?.[0]
  if (!file) return
  try {
    const profile = validateProfile(JSON.parse(await file.text()))
    draft.value = draftFromDump({ touch: profile.touch, buttons: profile.buttons, ledsPerButton: profile.ledsPerButton })
    tab.value = 'settings'
    setNotice(`已載入 ${file.name} 到草稿；確認後請按「全部保存」。`)
  } catch (error) { setError(error) } finally { input.value = '' }
}

function downloadCurrentProfile() {
  try {
    downloadProfile(validateProfile(profileFromDraft()))
    setNotice('已下載目前畫面中的設定草稿。')
  } catch (error) { setError(error) }
}

async function sendTerminal() {
  const command = terminalInput.value.trim()
  if (!command) return
  terminalLines.value.push(`> ${command}`)
  terminalInput.value = ''
  try { await serial.write(command) } catch (error) { setError(error) }
}

onBeforeUnmount(() => {
  stopEvent()
  stopLine()
  void serial.disconnect()
})
</script>

<template>
  <main class="app-shell">
    <header class="topbar">
      <div><p class="eyebrow">MAI PICO / LIVE CFG·1</p><h1>即時輸入監看與配置</h1></div>
      <button class="connection-pill" :class="{ online: connected }" :disabled="busy" @click="connected ? disconnect() : connect()">
        <span></span>{{ connected ? `已連線 · ${deviceInfo?.serial ?? 'Mai Pico'}` : '連接 Command Line Port' }}
      </button>
    </header>

    <div v-if="errorMessage" class="banner error">{{ errorMessage }}</div>
    <div v-else-if="notice" class="banner notice">{{ notice }}</div>

    <nav class="tabs" aria-label="主要功能">
      <button :class="{ active: tab === 'live' }" @click="tab = 'live'">即時狀態</button>
      <button :class="{ active: tab === 'settings' }" @click="tab = 'settings'">分配設定 <span v-if="changeCount" class="dirty-dot">{{ changeCount }}</span></button>
      <button :class="{ active: tab === 'terminal' }" @click="tab = 'terminal'">進階終端</button>
    </nav>

    <section v-if="tab === 'live'" class="live-layout">
      <article class="panel live-visual-panel" :class="{ disconnected: !connected }">
        <div class="panel-heading">
          <div><p class="step-label">LIVE INPUT MAP</p><h2>Mai 控制器即時狀態</h2></div>
          <div class="live-heading-actions">
            <button
              class="mirror-toggle"
              :class="{ active: showLiveValues }"
              type="button"
              role="switch"
              :aria-checked="showLiveValues"
              @click="showLiveValues = !showLiveValues"
            >{{ showLiveValues ? '隱藏即時讀值' : '顯示即時讀值' }}</button>
            <button
              class="mirror-toggle"
              :class="{ active: liveMirrored }"
              type="button"
              :aria-pressed="liveMirrored"
              @click="liveMirrored = !liveMirrored"
            >{{ liveMirrored ? '已鏡像顯示' : '鏡像顯示' }}</button>
            <div class="live-badge" :class="{ active: connected && latestLive }"><span></span>{{ connected && latestLive ? 'LIVE' : 'OFFLINE' }}</div>
          </div>
        </div>
        <LiveController
          :pressed-zones="pressedZones"
          :intensities="touchIntensity"
          :values="touchValues"
          :press-thresholds="touchPressThresholds"
          :release-thresholds="touchReleaseThresholds"
          :show-values="showLiveValues"
          :button-mask="latestLive?.buttonMask ?? 0"
          :mirrored="liveMirrored"
        />
      </article>
      <aside class="side-stack">
        <article class="panel status-card live-status-card">
          <p class="step-label">DEVICE STATUS</p>
          <h2>{{ connected ? '資料接收中' : '尚未連接 Mai Pico' }}</h2>
          <p>{{ connected ? '監看模式不會暫停 HID、AIME 或燈光輸出。' : '請使用 Chrome 或 Edge 連接 Command Line Port。' }}</p>
          <template v-if="!connected">
            <button class="secondary-button" :disabled="busy" @click="connect(true)">顯示所有通訊埠</button>
            <p>一般連線選單空白時，可顯示全部再選擇 Command Line Port。若仍空白，請改用 Edge 測試。</p>
          </template>
          <div class="status-list">
            <div><span>MPR121</span><strong :class="{ good: fourSensorsOnline }">{{ fourSensorsOnline ? '4 / 4 ONLINE' : 'CHECK' }}</strong></div>
            <div><span>Sensor mask</span><strong>0x{{ (latestLive?.sensorMask ?? deviceInfo?.sensorMask ?? 0).toString(16).padStart(2, '0') }}</strong></div>
            <div><span>觸控判定</span><strong :class="{ good: deviceInfo?.touchDetection === 'direct_delta' }">{{ deviceInfo?.touchDetection === 'direct_delta' ? 'DIRECT Δ' : 'MPR STATUS' }}</strong></div>
            <div><span>Sequence</span><strong>{{ latestLive?.sequence ?? '—' }}</strong></div>
            <div><span>觸控按下</span><strong>{{ pressedZones.size }}</strong></div>
            <div><span>按鍵按下</span><strong>{{ ((latestLive?.buttonMask ?? 0).toString(2).match(/1/g) ?? []).length }}</strong></div>
          </div>
        </article>
        <article class="panel legend-card">
          <p class="step-label">DISPLAY GUIDE</p>
          <div><span class="legend-swatch idle"></span><p><strong>待機</strong><small>保留區塊輪廓</small></p></div>
          <div><span class="legend-swatch signal"></span><p><strong>接近按下門檻</strong><small>Δ = baseline − filtered，Δ 越高越亮</small></p></div>
          <div><span class="legend-swatch pressed"></span><p><strong>直接讀數判定按下</strong><small>顯示「Δ／目前門檻」；按下後改用放開門檻</small></p></div>
        </article>
      </aside>
    </section>

    <section v-else-if="tab === 'settings'" class="settings-stack">
      <article class="panel settings-header">
        <div><p class="step-label">MAPPING DRAFT</p><h2>觸控與按鍵分配</h2><p>欄位顯示目前 Flash 值；修改只留在畫面草稿，按下全部保存才會寫入裝置。</p></div>
        <div class="draft-summary" :class="{ dirty: changeCount > 0 }"><strong>{{ changeCount }}</strong><span>筆修改</span></div>
      </article>

      <article class="panel mapping-card">
        <div class="panel-heading"><div><p class="step-label">34 TOUCH ZONES</p><h2>觸控分配與讀數門檻</h2><p class="mapping-help">Δ = baseline − filtered 為診斷讀值；MPR121 會同步套用門檻與 debounce：未按下時 Δ ≥「按下」即觸發，並維持按住直到通過「放開」門檻。</p></div><span class="muted">MPR 0–3 · ID 0–11</span></div>
        <div v-if="draft" class="touch-mapping-groups">
          <section v-for="group in touchGroups" :key="group.name" class="mapping-group">
            <h3>{{ group.name }} 區</h3>
            <div v-for="zone in group.zones" :key="zone" class="mapping-row touch-mapping-row" :class="{ invalid: mappingErrors.touch[zone] }">
              <strong class="mapping-id">{{ zone }}</strong>
              <label><span>MPR</span><select :value="touchEntry(zone)?.sensor" @change="changeTouchSensor(touchEntry(zone)!, $event)"><option v-for="sensor in 4" :key="sensor - 1" :value="sensor - 1">{{ sensor - 1 }}</option></select></label>
              <label><span>ID</span><select :value="touchEntry(zone)?.channel" @change="changeTouchChannel(touchEntry(zone)!, $event)"><option v-for="channel in 12" :key="channel - 1" :value="channel - 1">{{ channel - 1 }}</option></select></label>
              <label><span>按下 Δ≥</span><input v-model.number="touchEntry(zone)!.touchThreshold" type="number" min="2" max="255" title="未按下時，delta 大於等於此值即按下"></label>
              <label><span>放開 Δ&lt;</span><input v-model.number="touchEntry(zone)!.releaseThreshold" type="number" min="1" max="254" title="按下後，delta 小於此值才放開"></label>
              <div class="mapping-live-reading" :class="{ pressed: pressedZones.has(zone) }">
                <span>即時 Δ</span>
                <strong>{{ connected && latestLive ? touchValues[zone] : '—' }}</strong>
                <small v-if="connected && latestLive">{{ pressedZones.has(zone) ? `按住 ≥ ${touchReleaseThresholds[zone]}` : `待按 ≥ ${touchPressThresholds[zone]}` }}</small>
              </div>
              <small v-if="mappingErrors.touch[zone]" class="field-error">{{ mappingErrors.touch[zone] }}</small>
            </div>
          </section>
        </div>
        <div v-else class="empty-state">連線後會讀取目前的 34 區設定。</div>
      </article>

      <article class="panel mapping-card">
        <div class="panel-heading"><div><p class="step-label">12 CABINET BUTTONS</p><h2>按鍵分配</h2></div><span class="muted">只使用固定安全 GPIO 清單</span></div>
        <div v-if="draft" class="button-mapping-grid">
          <div v-for="entry in draft.buttons" :key="entry.id" class="mapping-row button-mapping-row" :class="{ invalid: mappingErrors.buttons[entry.id] }">
            <strong class="mapping-id">{{ entry.id }}</strong>
            <label><span>GPIO</span><select :value="entry.gpio" @change="changeButtonGpio(entry.id, $event)"><option v-for="gpio in BUTTON_GPIOS" :key="gpio" :value="gpio">GP{{ gpio }}</option></select></label>
            <label><span>Active level</span><select v-model="entry.activeHigh"><option :value="false">LOW</option><option :value="true">HIGH</option></select></label>
            <small v-if="mappingErrors.buttons[entry.id]" class="field-error">{{ mappingErrors.buttons[entry.id] }}</small>
          </div>
        </div>
        <div v-else class="empty-state">連線後會讀取目前的 12 鍵設定。</div>
      </article>

      <article class="panel save-bar">
        <div><p class="step-label">FLASH TRANSACTION</p><strong>{{ changeCount ? `有 ${changeCount} 筆尚未保存` : '設定與裝置一致' }}</strong><small>保存期間輸入會短暫暫停，完成後自動恢復即時監看。</small></div>
        <div class="save-actions">
          <label class="file-button">匯入 JSON<input type="file" accept="application/json,.json" :disabled="busy" @change="importProfile"></label>
          <button class="secondary-button compact" :disabled="!draft || busy" @click="downloadCurrentProfile">下載 JSON</button>
          <button class="secondary-button compact" :disabled="!changeCount || busy" @click="resetDraft">放棄修改</button>
          <button class="primary-button compact" :disabled="busy || !connected || !changeCount || hasMappingErrors" @click="saveAll">{{ busy ? '保存中…' : '全部保存' }}</button>
        </div>
      </article>
    </section>

    <section v-else class="panel terminal-panel">
      <div class="panel-heading"><div><p class="step-label">ADVANCED TERMINAL</p><h2>原始 CLI</h2></div><button class="text-button" @click="terminalLines = []">清除</button></div>
      <pre>{{ terminalLines.join('\n') || '尚無輸出' }}</pre>
      <form class="terminal-form" @submit.prevent="sendTerminal"><input v-model="terminalInput" :disabled="!connected" placeholder="cfg info"><button class="primary-button" :disabled="!connected">送出</button></form>
    </section>
  </main>
</template>
