# Source provenance

JAMboreeVision combines behavior from repositories owned by askjake; neither source repository was modified.

- JAMboreeLite: commit `ec197967a8041836584c46a1cc6128e6e4eaae65` (main at intake). The `jamboree/` transport modules and relevant regression tests are retained. The transport change is optional `JAMBOREE_SGS_CERT`/`JAMBOREE_SGS_KEY` paths in `sgs_bridge.py`; private keys/certificates are not copied into this repository. Tests adapt root-relative file references and isolate the OS keyring. Obsolete upstream installer tests are excluded because Vision has its own launchers.
- Sling-viewer: commit `d8f3ddb1ac2dc9b1b0dc2c7fa28b6736088176e6` (main at intake). Its HLS grid, muted playback, reconnect approach, RTMP `live0`–`live8` sources, and FFmpeg-to-HLS workflow informed the integrated implementation. The source contains a fixed viewer/launchers, not general discovery; configurable discovery is new in Vision. Original launchers that kill global FFmpeg processes were not retained.
- HLS.js: version 1.6.13, bundled `dist/hls.min.js` from jsDelivr's versioned npm distribution; BSD-2-Clause license in `vision/static/HLS-LICENSE.txt`. No runtime CDN fetch is required. The license applies to HLS.js, not automatically to the rest of this repository.

`source-manifest.json` records the exact source commits. Source hashes can be compared against the upstream repositories independently.
