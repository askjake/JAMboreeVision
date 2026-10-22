/* dart_version.h - single source of truth for the firmware identity banner. */
#ifndef DART_VERSION_H
#define DART_VERSION_H

#define DART_FW_NAME      "DART-BRIDGE"
#define DART_FW_VERSION   "2.0.0"
#define DART_FW_DATE      "2026-10-21"
/* Protocol revision of the host<->board serial contract (not the I2C contract). */
#define DART_SERIAL_PROTO "3"
/* I2C contract revision: 1 == byte-for-byte compatible with the legacy
 * DART_Dumb.ino / DART_16Remotes response layout (15 bytes, 0xAF/INT/EVT/KEY). */
#define DART_I2C_PROTO    "1"

#endif /* DART_VERSION_H */
