<script setup lang="ts">
import TouchPanel from './TouchPanel.vue'
import { BUTTON_IDS } from '../types'

const props = defineProps<{
  pressedZones: Set<string>
  intensities: Record<string, number>
  values: Record<string, number>
  pressThresholds: Record<string, number>
  releaseThresholds: Record<string, number>
  showValues: boolean
  buttonMask: number
  mirrored: boolean
}>()

const mainButtons = BUTTON_IDS.slice(0, 8)
const serviceButtons = BUTTON_IDS.slice(8)
const isPressed = (index: number) => Boolean(props.buttonMask & (1 << index))
</script>

<template>
  <div class="live-controller" :class="{ mirrored: props.mirrored }" aria-label="Mai Pico 即時輸入狀態">
    <div class="live-button-ring">
      <button
        v-for="(button, index) in mainButtons"
        :key="button"
        class="live-cabinet-button"
        :class="{ pressed: isPressed(index) }"
        :style="{ '--button-index': index }"
        type="button"
        tabindex="-1"
        :aria-pressed="isPressed(index)"
      ><span>{{ button }}</span></button>
      <div class="live-touch-disc">
        <TouchPanel
          :pressed-zones="pressedZones"
          :intensities="intensities"
          :values="values"
          :press-thresholds="pressThresholds"
          :release-thresholds="releaseThresholds"
          :show-values="showValues"
        />
      </div>
    </div>
    <div class="live-service-row">
      <button
        v-for="(button, offset) in serviceButtons"
        :key="button"
        type="button"
        tabindex="-1"
        :class="{ pressed: isPressed(offset + 8) }"
        :aria-pressed="isPressed(offset + 8)"
      ><span class="state-dot"></span>{{ button }}</button>
    </div>
  </div>
</template>
