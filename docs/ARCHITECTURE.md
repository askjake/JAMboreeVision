# Architecture

A Waitress/Flask service serves one same-origin UI on port 5004. There are no iframes, external viewer servers, browser-side credentials, or runtime CDN dependencies.

The `jamboree/` package retains JAMboreeLite's SGS command mapping, Hopper/Joey routing, manual/automatic pairing implementation, serial DART control, verified MAC learning, durable configuration, and background IP recovery. Vision reuses its controller and routes in a separate Flask app, replacing the UI. Legacy command endpoints accept POST; state-changing GETs are disabled. JSON mutation requests are checked for same-origin browser use. Commands are serialized to avoid interleaved presses; the UI supports explicit duration-based taps/holds.

The existing settop store remains authoritative for mutable transport fields. Vision's SQLite registry stores independent remote UUIDs, unique names, the target settop alias, and a stream UUID foreign key. Settop aliases remain stable because the original credential/topology layer uses them as keys. Source URLs and remote display names may change without changing IDs. Source URL uniqueness prevents duplicate workers for duplicate saved URLs. References ending in `-stream` are computed from current remote names and resolved to the linked stream, not stored as fragile duplicated strings.

`vision/media.py` owns subprocess handles and shares sessions by stream ID. No shell command interpolation or global process killing occurs. Generation-specific HLS directories prevent stale segments being mistaken for a changed stream. A reader lease keeps the worker running; explicit close or lease expiration releases it. Selected windows persist per browser in localStorage, while actual asset links persist server-side. Video selections and stream associations are separate concepts.

Discovery expands an explicit host or IPv4 CIDR and a source URL template. ffprobe must return a video track before a result is listed. Found sources are not auto-linked to remotes, since a reachable video URL does not prove which receiver it depicts. The operator explicitly saves and links each source.

The process lock enforces one Vision server/importer per data directory. SQLite handles concurrent asset writes; the existing base store handles configuration persistence. API deletion checks protect remote→settop and Joey→Hopper relationships. Imported rows preserve unknown legacy fields. Production requests never use mocked transports; test doubles exist only in the test suite.

Scope limits: this release is a trusted LAN application, not a multi-user authenticated service. Automatic OCR pairing remains available through the inherited API when its frame-provider prerequisites are configured; a stream link alone does not configure OCR. Network/video and physical remote acceptance must be run on the deployment network.
