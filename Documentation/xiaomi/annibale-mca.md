# Annibale MCA foundation (R5)

This batch adds four experimental components for Redmi K90 / annibale:
`mca_parse_dts`, `mca_hwid`, `mca_workqueue`, and `mca_adsp_glink`. It provides
common helpers and communication with ADSP firmware, **not a complete charging
stack or a flashable kernel**. All MCA options stay disabled in the normal board
fragment. No charging current, voltage, thermal table or battery identity is
invented or copied from another phone.

## Source and stock evidence

The common helpers derive from the GPL community reconstruction in
[xiaomi-dada/android_kernel_xiaomi_sm8750](https://github.com/xiaomi-dada/android_kernel_xiaomi_sm8750/tree/a7a6375b13e490d187c2b348d961cec39a414ad2/drivers/power/supply/mca),
introduced by commit
[`7145ffb9`](https://github.com/xiaomi-dada/android_kernel_xiaomi_sm8750/commit/7145ffb9459fa49eb8b82a6addf7c977f1f9ed8a).
That project describes reconstruction from Dada stock modules. This is **not MCA
source newly published by MiCode**. Copyright/license notices of the imported
helpers are retained. The transport is rewritten around the reviewed wire layout
and the common HWID helper is changed to an initialized, board-gated snapshot.

K90 reference binaries were read from
[xiaomi-annibale's fixed stock snapshot](https://github.com/xiaomi-annibale/android_device_xiaomi_annibale-kernel/tree/df318c6b1c81e17d4fdc0fc9333c00c4d90e928a/modules/vendor_boot).
The source/binary hashes and checked layouts are in
[annibale-mca-sources.json](annibale-mca-sources.json). No proprietary binary is
redistributed in this change. Static checks found:

- Owners `0x800a` (charging) and `0x8009` (QBG), request/response type 1,
  notification type 2, read opcode 1 and write opcode 2.
- Request: 276 bytes; property at 12, sequence at 16, data at 20.
  Response: 280 bytes; property at 12, return code at 16, sequence at 20,
  256-byte data at 24. A call accepts a non-NULL buffer and 0..255 bytes.
- Notifications: ID at 12, 256-byte data at 16. The new implementation checks
  bounds and zero-pads short notification payloads before dispatch.
- The 40-byte ARM64 MCA HWID layout, including build ADC at 20, product ADC at
  24, raw HWID at 28 and product-name pointer at 32.
- Stock workqueue wrappers use kernel queue/cancel APIs and flags `0x2a`
  (`WQ_UNBOUND | WQ_MEM_RECLAIM | WQ_CPU_INTENSIVE`). Cancellation is non-sync.

This verifies selected interfaces, not firmware property meanings, KMI CRCs,
prebuilt-module interoperability or hardware operation. The source header omits
Dada property/notification enums: individual IDs and units need Annibale evidence
before any charging policy uses them.

## Implementation and integration contract

| Component | Changes and limits |
| --- | --- |
| DT parser | Reject NULL outputs, negative indices and zero array counts; check table shape without overflowing `max_rows * row_size`; preserve scalar fallback/error behavior. String-array failures may leave already parsed elements written: callers must discard the table on error. |
| HWID bridge | Initialize once from the real HWID provider, reject missing identity or non-Annibale product, preserve ADC/region fields, remove the unrelated project-7 region quirk. Returned data must not be changed or freed by consumers. |
| Workqueue | Dedicated real kernel queue, retaining the public queue/cancel interface; standard kernel logging. Consumers must stop producers and synchronously cancel their own delayed/active work before unloading; the non-sync wrappers do not provide teardown synchronization. |
| ADSP transport | Both owners, serialized transactions, full response identity matching including error replies, bounded waits, no automatic write retry, unchanged failed read buffers, link-loss completion and explicit listener unregister. |

The transport uses the published `mca,adsp_glink` node already in the Annibale
DT repository. No board wiring or policy DT was changed. `XIAOMI_MCA_GLINK`
requires `XIAOMI_MCA_CORE`, `QTI_PMIC_GLINK`, and **QTI_BATTERY_CHARGER=n**: the
standard battery client conflicts with the charging owner. Core helpers require
EXPERT, OF and the real `MI_HARDWARE_ID` provider. The normal board configuration
continues to use its existing battery-client configuration; the test fragment
must not be installed as a finished charging configuration.

Forward the actual `hwid.project`, `hwid.hwid_value`, `hwid.project_adc` and
`hwid.build_adc` module parameters as described in
[annibale-peripherals.md](annibale-peripherals.md). Never substitute constants.
Consumers must be rebuilt together: preserved symbol spellings and layouts do
not establish compatibility with stock modules. The new GPL unregister exports
are required for any listener that can be removed. Callbacks execute on a separate
ordered worker so they can request ADSP properties without blocking the Glink
receive worker. They must not register/unregister listeners from inside callbacks,
or wait for a task that needs that registration lock. Registrations copy callback
function pointers but do not own the client data; unregister waits out dispatch.
Registrations do not replay old events: a late client must explicitly read/sync
its initial state and handle an unavailable transport.

Notifications are bounded to 64 outstanding items and may be dropped with a
rate-limited warning on overflow/allocation failure. Restart events use a separate
embedded work item, require no allocation, and coalesce DOWN before the latest UP;
intermediate rapid transitions are not individually preserved. These events are
for resynchronization, not an exact event log. No guarantee is made for a listener
that blocks indefinitely. State is rechecked by every property transaction.

The Qualcomm provider now holds SRCU through receive callback dispatch and waits
for those callbacks after removing a client from its IDR. Previously, a client
could be freed between lookup and callback. Unregister must be outside message
and state callbacks and follow cessation of client writes. Header validation also
rejects negative RPMSG lengths. The MCA remove path stops publication, waits out
its serialized request, unregisters both channels and drains its worker without
holding the request mutex. This is source-level lifetime protection, not proof
from concurrent kernel stress testing.

Matching responses with malformed success length return `EPROTO`; unmatched or
header-truncated responses do not acknowledge a request. Error responses with a
complete 24-byte identity and length no greater than 280 propagate their firmware
code. Exact error-packet lengths still need live firmware verification. Sequence
numbers persist across platform rebind while this module is loaded, but this does
not claim replay protection across module reloads or 32-bit wraparound.

## Reproduce validation

Use the clang and host-tool environment in [annibale-port.md](annibale-port.md).
Use a dedicated output directory; the `mca` phase intentionally disables the
standard battery client in that directory.

```sh
bash scripts/xiaomi/compile-annibale-board.sh mca
python3 scripts/xiaomi/tests/test_annibale_mca.py
# Restore the normal board configuration afterwards:
bash scripts/xiaomi/compile-annibale-board.sh all
```

The explicit test fragment is `arch/arm64/configs/vendor/annibale_mca_test.config`.
The phase builds the four MCA objects and `qti_pmic_glink.o`, in addition to the
existing board smoke check. `mca-objects.log` records the object step. The test
configuration is not a release or ROM deployment configuration.

Validation on 2026-11-15: four MCA objects and the modified provider compiled as
AArch64 ELF objects with Android clang r510928. Four host suites run actual source
functions with mocks and UBSan: DT boundaries/defaults, HWID mapping, protocol and
request failure injection, and restart event ordering. They cover stale errors,
wrong owner/opcode/property/sequence, truncated/malformed replies, timeout,
link loss, failed-read buffer preservation, duplicate state events and teardown
gating. They are deterministic host tests, not kernel scheduling/race tests.
The four existing peripheral host suites also pass. A conflicting Kconfig check
confirmed that enabling the standard battery client deselects the MCA transport.
The normal board object check passes after restoring the default configuration
with MCA and fingerprint off and QTI_BATTERY_CHARGER=m.

Existing duplicate DT-target Make recipe warnings remain. No complete Image,
final module modpost/link, KMI checks, signed module partition, actual ADSP request,
battery reporting, fast charging, suspend, or device test has been completed.
MCA logging/telemetry, event/voter framework, charging interface, fuel gauge,
charger policies and their power-supply/HAL integration remain unfinished.
