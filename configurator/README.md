# Mai Pico hardware configurator

Vue 3 + TypeScript Web Serial configurator for the firmware's `CFG/1` protocol. It runs locally and does not need a backend or an Internet connection.

## Run

```text
npm install
npm run dev
```

On Windows, after installing dependencies, double-click `start-local.cmd` to start the configurator at `http://127.0.0.1:5173/`. Keep its terminal window open while using the page; closing it stops the local website. If port 5173 is already in use, use the existing configurator server or stop it before starting another copy.

Open the localhost URL in Chrome or Edge and select the Mai Pico **Command Line Port**. Every connection opens the chooser directly without first waiting for `getPorts()` or automatically reusing a previously authorized game I/O port. The default chooser is filtered to USB VID `0x0CA3`, PID `0x0021`; **Show all serial ports** bypasses this filter if the list is empty. Both paths require a valid `cfg info` response before loading settings. If even the unfiltered list is empty, test Edge to distinguish a Chrome enumeration problem from a Windows device problem.

The home page starts the firmware's non-transactional 25 Hz live stream. It visualises all 34 touch zones and 12 cabinet buttons. Each touch zone can show its live diagnostic `delta / active threshold`; the active threshold changes from Press to Release while held. Normal game input comes from the MPR121's coherent, debounced touch status rather than from these separately sampled diagnostic values. Monitoring does not suspend HID, AIME, lighting, or the normal game I/O path.

The mapping page loads the current Flash configuration with `cfg dump`. Every touch zone exposes MPR index, electrode ID, direct-delta Press and Release thresholds, plus its live delta; every button exposes its allowed GPIO and active level. Choosing an occupied channel swaps the two assignments so the draft stays one-to-one.

Edits remain in a browser-side draft until **Save all** starts one validated transaction. The app stops live monitoring, stages all 34 touch and 12 button mappings, commits to Flash, reads the result back, and resumes monitoring. JSON import loads a draft for review; JSON export uses the existing `mai-pico-hardware-profile`, `formatVersion: 1` format.

## Checks

```text
npm test
npm run build
```

The tests cover CFG/live framing, 34-bit touch state, mapping swaps, threshold validation, intensity normalisation, and profile validation.
