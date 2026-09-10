# Assessment: 802.15.4 Driver Extraction from ESP-IDF

Re-measured 2026-09-10. The previous version of this document was wrong in its
central factual claim, and the recommendation rested on it.

## Retraction: the shim was never in the build

The old text opened with "A shim header exists at
`radio/ieee802154/reflex_ieee802154_shim.h` that maps ESP-IDF primitives to
Reflex OS equivalents", and stated: *"The shim satisfies the driver's
compile-time dependencies without modifying ESP-IDF source. The 802.15.4 driver
compiles against this shim and the radio functions correctly on hardware."*

None of that was true of the build. Measured:

- `radio/` is not in `EXTRA_COMPONENT_DIRS` in the top-level `CMakeLists.txt`,
  and is in no other component list.
- `reflex_ieee802154_shim.h` appears **0 times** in
  `build_own_entry/compile_commands.json`.
- No `.obj.d` in any build directory references it. It had never been through a
  compiler.
- ESP-IDF's driver is linked wholesale — ten objects: `esp_ieee802154.c.obj`,
  `esp_ieee802154_dev.c.obj`, `_ack`, `_event`, `_frame`, `_pib`, `_sec`,
  `_timer`, `_util`, `ieee802154_periph.c.obj`.
- `platform/esp32c6/reflex_radio_802154.c` includes `esp_ieee802154.h`
  **directly**, which is the single on-path Tier D entry the ledger counts.

So the document recommended "Option C: accept the shim boundary — Effort: Zero
(already done)" about a boundary that did not exist. The header has been deleted
rather than wired in: 94 lines that had never been compiled, cited as the
project's isolation mechanism, are worse than nothing, because they answered a
question that was never actually asked of the build.

**The isolation that does exist is real, and is a different file.**
`include/reflex_radio.h` is the boundary application code goes through, and it
holds: nothing above the radio backend touches an ESP-IDF type. The old
conclusion was accidentally right about application isolation and wrong about
the mechanism, and wrong that any work had been done to achieve it.

## What Reflex actually uses

Fourteen symbols from `esp_ieee802154.h`, all of them in
`reflex_radio_802154.c`:

```
enable  set_channel  set_panid  set_short_address  set_promiscuous
set_rx_when_idle  transmit  receive  receive_handle_done
transmit_done  transmit_failed  receive_done          (callbacks Reflex defines)
esp_ieee802154_frame_info_t  esp_ieee802154_tx_error_t
```

No ACK handling, no association, no scan, no security, no multi-PAN. The mesh
broadcasts and listens on a fixed channel.

For scale on the other side: ESP-IDF's component is ~3,240 lines across those
ten objects, and `esp_ieee802154_enable()` is four calls —
`ieee802154_enable()`, `ieee802154_rf_enable()`, `esp_btbb_enable()`,
`ieee802154_mac_init()`.

## The floor, stated plainly

Two of those four are binary blobs: `ieee802154_rf_enable()` reaches
`esp_phy_enable()` in `libphy.a` (178 KB), and `esp_btbb_enable()` is
`libbtbb.a`. They perform RF calibration and analog front-end bring-up, and
there is no register documentation that would let Reflex replace them.

**Tier D cannot reach 0 while the radio works.** A Reflex-owned MAC would still
call into the PHY blob, and that call would be the new Tier D entry. The honest
target is not zero; it is *replacing the driver with Reflex's own MAC and
leaving only the blob*, which is bedrock rather than a dependency anyone can
remove.

This is the same shape as Tier A: silicon is not a dependency to be argued away.

## Options, re-costed

### A. Fork ESP-IDF's component and strip its dependencies
Unchanged and still poor: ~15 sources and ~30 HAL headers, fragile across
ESP-IDF releases and chip revisions, and it inherits every feature Reflex does
not use. Weeks, then ongoing.

### B. A minimal MAC over the registers
The old text costed this at "months, high risk of subtle timing bugs" and
"debugging requires RF test equipment". Both parts are now measurably too
pessimistic for the scope Reflex needs:

- **The register surface is small.** ESP-IDF exposes 87 `IEEE802154_*_REG`
  macros; a broadcast-only MAC needs roughly fourteen — command, control,
  channel, TX power, PAN ID, short address, the two DMA pointers, event enable
  and status, and the three status/length registers.
- **The hardware does the hard parts.** CCA, CRC, frame filtering and ACK timing
  are in the peripheral, not in the driver. The state machine Reflex would own
  is: point DMA at a buffer, issue a command, handle an event.
- **RF test equipment is not required, because a second board is better.**
  The bench has two C6s. A frame transmitted by Reflex's own register writes and
  received by a peer running ESP-IDF's driver is an unambiguous external oracle
  for frame correctness — and the reverse direction tests RX. That method is
  already proven: see the peer-RX measurements in
  [independence-dependency-map.md](independence-dependency-map.md).

### C. Accept the boundary
Still available, but it can no longer be described as free work already done.
It means: keep ESP-IDF's driver, and Tier D stays at 1.

## Recommendation

**B, incrementally, TX first.** Not as a speculative rewrite: each step is
verifiable on the bench against a peer board, and the first two steps are done
(below). If a step stops paying, C remains the fallback and costs nothing to
return to.

## What has been done so far

Groundwork only — **Tier D is still 1**, and no MAC code exists yet.

1. **The register map is under the bridge.** Fourteen IEEE802154 registers were
   added to `tools/soc_scraper.py`, scraped from the SVD, and are now among the
   **198 constants `make soc-bridge` proves identical to ESP-IDF's own macros**
   with the real toolchain. A register address is not something to eyeball: a
   wrong one does not fail to build, it writes to a different peripheral.

2. **The map is verified on silicon, not only against headers.** `mesh regs`
   reads the peripheral back through Reflex's own constants. Because ESP-IDF's
   driver set those fields at Reflex's request, the values are a test rather
   than a dump:

   ```
   802.15.4 MAC @ 0x600a3000 (Reflex's own map)
     channel=15 (freq index 23) panid=0x4f52 short_addr=0xc7d4
     ctrl_cfg=0x12080080 event_en=0x00001eff event_status=0x00000000
     txdma=0x40829e30 rxdma=0x40823ac8
   ```

   `panid` and `short_addr` are exactly what `reflex_radio_init` asked for, and
   the DMA pointers land in RAM.

   **And it caught something the static asserts could not.** The `CHANNEL`
   register does not hold a channel number; it holds a frequency index,
   `freq = (channel - 11) * 5 + 3`, so channel 15 reads back as 23. The address
   was correct and writing `15` there would have tuned the radio between
   channels 13 and 14 — no build error, no runtime error, just a radio quietly
   on the wrong frequency. That is the same class of failure the bridge exists
   to prevent, one level up: a correct address with a wrong encoding. It is now
   recorded in the scraper's note, decoded by `mesh regs`, and cost one command
   on a board rather than a debugging session against a future MAC.

## Next step

TX through Reflex's own registers, with board B running ESP-IDF's driver as the
receiving oracle. The risk to manage is that ESP-IDF's driver owns this
peripheral's state machine today, so raw writes must not race it — the first
version should run with the driver idle, behind an explicit command, before
anything replaces the TX path.
