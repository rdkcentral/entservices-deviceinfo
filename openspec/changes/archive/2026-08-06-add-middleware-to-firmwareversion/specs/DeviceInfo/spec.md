## MODIFIED Requirements

### Requirement: firmwareversion middleware field
The `firmwareversion` property SHALL include a `middleware` field in its response.
The `middleware` value SHALL be extracted from the `imagename` field (read from `/version.txt`) using a regex that matches a version segment of the form `N.Nxxx` (two numeric parts plus an alphanumeric suffix with no further dots) delimited by underscores or end of string.
If no such segment is found in `imagename`, the `middleware` field SHALL default to `"0.0"`.
The `middleware` field SHALL always be present in the response — it is never absent.

#### Scenario: imagename contains a direct version segment
- **WHEN** `imagename` is `ELTE11MWR_8.6p1s2_PROD`
- **THEN** `middleware` is `"8.6p1s2"`

#### Scenario: imagename contains a version embedded in a letter-prefixed segment
- **WHEN** `imagename` is `COESST11AEI_E032.031.00.8.6p99s2_DEV`
- **THEN** `middleware` is `"8.6p99s2"`

#### Scenario: imagename contains a multi-dot numeric version (not a middleware version)
- **WHEN** `imagename` is `SKTL11MEIIT_DEV_rel-15567_20260805034710_8.5.3.7B1`
- **THEN** `middleware` is `"0.0"`

#### Scenario: imagename has no version segment
- **WHEN** `imagename` is `ELTE11MWR_DEV_develop_20260806042826_DPRCTN`
- **THEN** `middleware` is `"0.0"`

#### Scenario: firmwareversion response always includes middleware field
- **WHEN** `firmwareversion` is called and `imagename` is successfully read
- **THEN** the response SHALL contain a `middleware` key regardless of whether a version was found
