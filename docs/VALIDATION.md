# Validation

The automated suite covers durable stream/remote identity, rename/restart/source changes, deletion unlinking, duplicate rejection, imported configuration preservation, command routing, origin checks, bounded discovery, shared media leases, worker crash replacement, and real loopback HLS generation/probing/transcoding.

The retained upstream suite covers SGS mappings, pairing state/credential handling, Hopper/Joey routing, RF fallback, verified MAC/IP recovery, atomic configuration, and serial behavior. OS keyrings and receiver hardware are isolated with test doubles; test success does not claim live hardware acceptance.

Run `python verify.py` after installing `requirements-dev.txt` and `requirements-ocr.txt`. The real media test skips only when FFmpeg/ffprobe are absent; inspect the reported skip count. It creates a local test-pattern HLS source, serves it on loopback, probes it, feeds it to the production playback manager, and checks generated media segments. It sends no commands to physical receivers and scans no lab hosts.

Deployment acceptance is the numbered checklist in README.md. Windows launch scripts are included, but Linux-container tests cannot establish a Windows laptop's keyring, firewall, serial-driver, or receiver reachability behavior.

## Execution in the development container

Python unit/API/upstream regression tests and the real FFmpeg/HLS smoke test were executed successfully. Actual receiver commands, LAN source discovery, and Windows execution remain deployment acceptance items.

A Playwright browser acceptance script is included in `tests/browser_flow.cjs`. Browser startup in the development container was blocked by the host's socket restrictions (`socket() failed: Operation not permitted`), so a successful browser run is **not** claimed. To run it on your laptop with Node.js installed:

```powershell
npm install --no-save playwright
npx playwright install chromium
$env:JV_PYTHON = (Resolve-Path .\.venv\Scripts\python.exe).Path
node tests/browser_flow.cjs
```

The fixture is loopback-only, uses a generated video test pattern, and replaces the hardware controller only inside the fixture process. It exercises UI CRUD, links, real browser HLS playback, command dispatch, two viewers, rename/reload, and mobile layout. Production `python -m vision` does not use this fixture.

A jsdom runtime integration test was executed against the real loopback Flask API: UI bootstrap, settop/stream/remote forms, persisted links, selected video-window creation, and controller dispatch passed. This checks JavaScript/DOM behavior; it is not a substitute for visual layout or browser video-decoder acceptance. The independent FFmpeg smoke test exercises actual media processing.

To repeat the DOM test: `npm install`, then `npm run test:dom` (set `JV_PYTHON` to the virtualenv Python as above). `npm run test:browser` runs the Playwright acceptance script after installing its browser.
