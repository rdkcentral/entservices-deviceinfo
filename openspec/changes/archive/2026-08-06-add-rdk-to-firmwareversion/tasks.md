## 1. Interface Update

- [x] 1.1 Add `rdk` string member to `FirmwareversionInfo` struct in `entservices-apis/apis/DeviceInfo/IDeviceInfo.h`
- [x] 1.2 Verify `FirmwareversionInfo` struct is initialised with `rdk = ""` as default

## 2. Core Implementation

- [x] 2.1 In `plugin/DeviceInfoImplementation.cpp` `FirmwareVersion()`: implement `std::regex_search` on `firmwareVersionInfo.imagename` using pattern `_(?:[A-Za-z][^_]*?)?(\d+\.\d+[^.\s_]+)(?:_|$)`
- [x] 2.2 Set `firmwareVersionInfo.rdk = mwMatch[1]` on regex match, else `firmwareVersionInfo.rdk = "0.0"`

## 3. L1 Test Updates

- [x] 3.1 Update `FirmwareVersion_Success` expected response: `rdk` → `"0.0"` (imagename `TEST_IMAGE_V1` has no version segment)
- [x] 3.2 Update `FirmwareVersion_Success_MissingOptionalFields` expected response: `rdk` → `"0.0"`
- [x] 3.3 Update `FirmwareVersion_Success_WithRdk`: use imagename `ELTE11MWR_8.3p9s1_DEV`, assert `rdk` = `"8.3p9s1"`
- [x] 3.4 Add `FirmwareVersion_Success_RdkDefaultWhenNoVersionInImageName`: imagename with no version segment, assert `rdk` = `"0.0"`
- [x] 3.5 Add `FirmwareVersion_Success_RdkFromEmbeddedVersion`: imagename `COESST11AEI_E032.031.00.8.6p99s2_DEV`, assert `rdk` = `"8.6p99s2"`
- [x] 3.6 Update `test_DeviceInfoJsonRpc.cpp` `firmwareversion` test expected `rdk` → `"0.0"` (imagename `CUSTOM5_VBN_2203_sprint_..._NG` has no version segment)

## 4. L2 Test Updates

- [x] 4.1 Update `DeviceInfo_L2_PropertyTest` firmwareversion section: update comment and assert `rdk` == `"22.03s"` (from default imagename `CUSTOM_VBN_22.03s_sprint_...`)
- [x] 4.2 Update `DeviceInfo_COMRPC_FirmwareVersion`: assert `rdk` == `"22.03s"`, update comment
- [x] 4.3 Rewrite `DeviceInfo_COMRPC_FirmwareVersion_WithRdk`: write imagename `ELTE11MWR_8.3p9s1_DEV` to `/version.txt`, assert `rdk` == `"8.3p9s1"`
- [x] 4.4 Rename `DeviceInfo_COMRPC_FirmwareVersion_RdkKeyNotInFile` → `DeviceInfo_COMRPC_FirmwareVersion_RdkDefaultWhenNoVersionInImageName`: write imagename with no version segment, assert `rdk` == `"0.0"`

## 5. Spec Update

- [x] 5.1 Add `rdk` to the `firmwareversion` response field list in `openspec/specs/DeviceInfo/spec.md`
- [x] 5.2 Add `rdk` row to the firmwareversion optional fields table with source description: "extracted from `imagename` via regex; defaults to `"0.0"` if no version segment found"
- [x] 5.3 Update the Requirements section: change "all other fields are optional and default to `""`" to reflect `rdk` defaults to `"0.0"`
- [x] 5.4 Update the Usage Examples section `firmwareversion` example response to include `rdk`
- [x] 5.5 Add Change History entry: `2026-08-06 - add-rdk-to-firmwareversion - Add rdk field to firmwareversion extracted from imagename`

## 6. Verification

- [ ] 6.1 Build the plugin and confirm no compilation errors
- [ ] 6.2 Run L1 tests — all pass
- [ ] 6.3 Run L2 tests — all pass
- [ ] 6.4 Re-run `openspec-coverage` and confirm score is maintained or improved
