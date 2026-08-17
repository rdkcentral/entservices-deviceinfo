## Context

The `firmwareversion` JSON-RPC property of the DeviceInfo plugin currently returns `imagename`, `sdk`, `mediarite`, `yocto`, and `pdri` fields. The `imagename` value (read from `/version.txt`) encodes the middleware version as a segment in the image name string (e.g. `ELTE11MWR_8.6p1s2_PROD` contains `8.6p1s2`).

Two image name patterns exist in the field:
1. **Direct**: `<MODEL>_<VERSION>_<VARIANT>` — version directly after first underscore (e.g. `ELTE11MWR_8.6p1s2_PROD`)
2. **Embedded**: `<MODEL>_<LETTER-PREFIX>.<MAJOR>.<MINOR>.<VERSION>_<VARIANT>` — version is the final `N.Nxxx` segment inside a letter-prefixed block (e.g. `COESST11AEI_E032.031.00.8.6p99s2_DEV`)

Multi-dot numeric versions like `8.5.3.7B1` or `8.7.1.0` do NOT represent middleware versions and must return the default `"0.0"`.

## Goals / Non-Goals

**Goals:**
- Expose the `middleware` version string in the `firmwareversion` response.
- Parse `middleware` from the already-read `imagename` — no new file reads.
- Default to `"0.0"` when no valid version segment is found.
- Handle both image name patterns with a single regex.
- Keep the change fully additive (non-breaking).

**Non-Goals:**
- Caching or pre-computing middleware version at plugin startup.
- Supporting image name formats not present in the field.
- Exposing middleware version as a standalone property.

## Decisions

### D1: Parse from `imagename`, not a separate file

**Decision**: Extract `middleware` from the `imagename` string already read from `/version.txt`.

**Rationale**: The middleware version is encoded in the image name by convention. Using `imagename` as the source is reliable, requires no new I/O, and is consistent with how `releaseversion` is also derived from `imagename`.

### D2: Single regex with optional letter-prefix group

**Decision**: Use `_(?:[A-Za-z][^_]*?)?(\d+\.\d+[^.\s_]+)(?:_|$)` as the single extraction regex.

**Rationale**:
- `_` — anchors to underscore boundary, preventing matches inside numeric-only segments
- `(?:[A-Za-z][^_]*?)?` — optionally skips a letter-prefixed segment (handles embedded pattern)
- `(\d+\.\d+[^.\s_]+)` — matches exactly two numeric parts plus alphanumeric suffix with no further dots; this correctly excludes `8.5.3.7B1` and `8.7.1.0`
- `(?:_|$)` — version must be followed by underscore or end of string

### D3: Default to `"0.0"` on no match

**Decision**: Use `"0.0"` as the default, not `""`.

**Rationale**: An empty string is ambiguous — it could mean "field not supported" or "parsing failed". `"0.0"` is a clearly invalid version that unambiguously signals "not determined", consistent with how `releaseversion` defaults to `"99.99.0.0"`.

## Risks / Trade-offs

| Risk | Mitigation |
|------|-----------|
| New image name format not matching regex | The default `"0.0"` is returned — no crash, no error. New formats can be handled by extending the regex. |
| `std::regex_search` performance on long imagename strings | imagename is a short string (< 100 chars); regex cost is negligible. |
| `FirmwareversionInfo` struct ABI change | `middleware` is added as a new string member; COM-RPC clients using the interface will need to be rebuilt — this is expected for any interface struct change. |

## Migration Plan

1. Add `middleware` member to `FirmwareversionInfo` in `IDeviceInfo.h`.
2. Update `DeviceInfoImplementation::FirmwareVersion()` to populate `middleware` via regex on `imagename`.
3. Update L1 and L2 tests to assert the new field.
4. Update `openspec/specs/DeviceInfo/spec.md` to document the new field.
5. No rollback complexity — change is additive; older clients ignore unknown fields in JSON-RPC responses.
