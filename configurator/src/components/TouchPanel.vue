<script setup lang="ts">
import { computed } from 'vue'

const props = defineProps<{
  pressedZones?: Set<string>
  intensities?: Record<string, number>
  values?: Record<string, number>
  pressThresholds?: Record<string, number>
  releaseThresholds?: Record<string, number>
  showValues?: boolean
}>()

type ZoneShape = { name: string; path: string; x: number; y: number }

const OUTER_RADIUS = 242
const OUTER_HALF_ANGLE = 11
const E_RADIUS = 148
const E_SIZE = 40

const point = (radius: number, angle: number) => ({
  x: 250 + Math.cos((angle - 90) * Math.PI / 180) * radius,
  y: 250 + Math.sin((angle - 90) * Math.PI / 180) * radius,
})

const outerArc = (start: number, end: number) => {
  const a = point(OUTER_RADIUS, start)
  const b = point(OUTER_RADIUS, end)
  return { a, b, command: `M ${a.x} ${a.y} A ${OUTER_RADIUS} ${OUTER_RADIUS} 0 0 1 ${b.x} ${b.y}` }
}

const eVertex = (angle: number, radial: number, tangential: number) => {
  const radians = angle * Math.PI / 180
  return {
    x: 250 + Math.sin(radians) * radial + Math.cos(radians) * tangential,
    y: 250 - Math.cos(radians) * radial + Math.sin(radians) * tangential,
  }
}

const aZone = (name: string, angle: number): ZoneShape => {
  const start = angle - OUTER_HALF_ANGLE
  const end = angle + OUTER_HALF_ANGLE
  const { command } = outerArc(start, end)
  const previousE = angle - 22.5
  const nextE = angle + 22.5
  const innerEnd = eVertex(nextE, E_RADIUS, -E_SIZE)
  const innerStart = eVertex(previousE, E_RADIUS, E_SIZE)
  const label = point(202, angle)
  return {
    name,
    path: `${command} L ${innerEnd.x} ${innerEnd.y} L ${innerStart.x} ${innerStart.y} Z`,
    x: label.x,
    y: label.y,
  }
}

const dZone = (name: string, angle: number): ZoneShape => {
  const start = angle - OUTER_HALF_ANGLE
  const end = angle + OUTER_HALF_ANGLE
  const { command } = outerArc(start, end)
  const innerEnd = eVertex(angle, E_RADIUS, E_SIZE)
  const pointEnd = point(E_RADIUS + E_SIZE, angle)
  const innerStart = eVertex(angle, E_RADIUS, -E_SIZE)
  const label = point(216, angle)
  return {
    name,
    path: `${command} L ${innerEnd.x} ${innerEnd.y} L ${pointEnd.x} ${pointEnd.y} L ${innerStart.x} ${innerStart.y} Z`,
    x: label.x,
    y: label.y,
  }
}

const polygon = (name: string, angle: number, radius: number, sides: number, size: number, rotation = 0): ZoneShape => {
  const center = point(radius, angle)
  const points = Array.from({ length: sides }, (_, index) => {
    const theta = (rotation - 90 + index * 360 / sides) * Math.PI / 180
    return `${center.x + Math.cos(theta) * size},${center.y + Math.sin(theta) * size}`
  })
  return { name, path: `M ${points.join(' L ')} Z`, x: center.x, y: center.y }
}

const zones = computed<ZoneShape[]>(() => {
  const result: ZoneShape[] = []
  // A zones bridge two adjacent E zones. D zones use the outward half of an E
  // zone as a pointed inner edge, matching the physical Mai touch sheet.
  for (let i = 0; i < 8; i++) {
    result.push(dZone(`D${i + 1}`, i * 45))
    result.push(aZone(`A${i + 1}`, i * 45 + 22.5))
  }
  // Every E zone has one point facing outward; its screen-space appearance
  // alternates naturally between a diamond and a square.
  for (let i = 0; i < 8; i++) {
    result.push(polygon(`E${i + 1}`, i * 45, E_RADIUS, 4, E_SIZE, i * 45))
  }
  // B1 sits between E1 and E2; the remaining B zones continue clockwise.
  for (let i = 0; i < 8; i++) {
    result.push(polygon(`B${i + 1}`, i * 45 + 22.5, 100, 8, 38, 22.5))
  }
  // The centre is deliberately inset from the B ring, then split vertically.
  result.push({ name: 'C2', path: 'M 250 208 L 222 208 L 210 220 L 210 280 L 222 292 L 250 292 Z', x: 230, y: 250 })
  result.push({ name: 'C1', path: 'M 250 208 L 278 208 L 290 220 L 290 280 L 278 292 L 250 292 Z', x: 270, y: 250 })
  return result
})
</script>

<template>
  <svg class="touch-panel" viewBox="0 0 500 500" role="img" aria-label="34 區 Mai 即時觸控面板">
    <g
      v-for="zone in zones"
      :key="zone.name"
      class="touch-zone"
      :class="{ pressed: pressedZones?.has(zone.name) }"
      :style="{ '--intensity': props.intensities?.[zone.name] ?? 0 }"
      :aria-label="`${zone.name}，delta ${props.values?.[zone.name] ?? 0}，按下門檻 ${props.pressThresholds?.[zone.name] ?? 0}，放開門檻 ${props.releaseThresholds?.[zone.name] ?? 0}`"
    >
      <title>{{ zone.name }} · Δ {{ props.values?.[zone.name] ?? 0 }} · 按下 ≥ {{ props.pressThresholds?.[zone.name] ?? 0 }} · 放開 &lt; {{ props.releaseThresholds?.[zone.name] ?? 0 }}</title>
      <path :d="zone.path" />
      <text
        v-if="props.showValues"
        class="zone-value"
        :x="zone.x"
        :y="zone.y - 7"
        dominant-baseline="middle"
        text-anchor="middle"
      >{{ props.values?.[zone.name] ?? 0 }}/{{ props.pressedZones?.has(zone.name) ? props.releaseThresholds?.[zone.name] : props.pressThresholds?.[zone.name] }}</text>
      <text
        class="zone-name"
        :x="zone.x"
        :y="zone.y + (props.showValues ? 7 : 0)"
        dominant-baseline="middle"
        text-anchor="middle"
      >{{ zone.name }}</text>
    </g>
  </svg>
</template>
