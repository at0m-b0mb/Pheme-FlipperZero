# Changelog

## v1.0 — 2026-08-12

First release.

### Protocol

- A complete POCSAG receiver written from scratch; the Flipper firmware has no
  POCSAG decoder to build on.
- 512, 1200 and 2400 bps, detected by racing three decoders rather than by
  measuring run lengths — misreading the rate by a factor of two is exactly the
  mistake a histogram makes.
- Preamble detection, sync-codeword hunting on every bit in every state (so a
  glitch costs one batch rather than the rest of the transmission), batch
  framing, and capcode reconstruction from frame position.
- BCH(31,21) with overall parity: a 1024-entry syndrome table built at startup,
  correcting one and two bit errors and refusing three.
- Alphanumeric and numeric alphabets, both re-renderable from the stored payload
  because paging terminals do not reliably set the function bits.
- An unrecoverable codeword leaves twenty placeholder bits rather than a
  five-bit alignment shift, and the page is labelled damaged.

### Privacy

- Fourteen detectors producing character spans: names, dates of birth, patient
  and record numbers, email, telephone, extensions and bleeps, street addresses,
  postcodes, wards and bays, clinical and incident vocabulary, door codes and
  vehicle plates.
- Grades A+ to F with floors that no page can duck under, applied as a
  monotonic compression rather than a clip. A+ and A are unreachable by
  construction: every POCSAG page is cleartext.
- Messages redacted on screen by default, drawn as a skyline of blocks that are
  tall wherever a person's data sits. Plain text needs a setting that ships off
  plus a long press, and moving to another page re-hides it.
- Per-capcode roster with an inferred role, showing what following one pager
  assembles that no single page gave away.
- Redacted session reports written to the SD card.

### Radio and UI

- Eight paging channels the CC1101 can actually tune, each labelled with what it
  is used for.
- Scan counts POCSAG synchronisation rather than signal strength, so a loud
  carrier that is not paging correctly reads quiet.
- Demo mode generates a paging channel in software and feeds it through the real
  decoder — nothing is pasted into the UI.
- Five animated lessons on how paging is framed, why it was never encrypted, and
  which paging bands this radio physically cannot hear.
- Optional narrow receive filter (58 kHz) as an opt-in setting.

### Testing

- 53 million host checks under ASan and UBSan over the shipping sources.
- README screenshots rendered from the real engine's own output, so they cannot
  drift from the app's behaviour. Building them this way surfaced eleven layout
  overruns before any of them reached hardware.

### Known limits

- Never tested against a live paging transmitter.
- The narrow filter preset is unverified on hardware, which is why it is not the
  default.
- FLEX is not decoded. VHF paging, 466 MHz commercial paging and US 929–932 MHz
  paging are outside the CC1101's tuning range.
