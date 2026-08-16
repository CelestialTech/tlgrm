# Tlgrm 7.0.9a

Base: Telegram Desktop **7.0.9**, same as [7.0.9](RELEASE_NOTES_7.0.9.md).
Nothing upstream changed; everything here is Tlgrm's own.

This is the first release under the fork's own versioning, which is what the
`a` means.

## Versioning — several fork releases per upstream base

The updater compares `AppVersion` with a strict `>`, so every release we ship
costs a number. Tracking upstream's numbering meant a fix to 7.0.9 had to be
called 7.0.10 — spending a number that belongs to a real upstream-tracking
release. `AppVersion` now carries both:

```
AppVersion = upstreamBase * 100 + index        index 1..99, shown as a letter

7.0.9a  = 7000009 * 100 + 1 = 700000901
7.0.9b  = 7000009 * 100 + 2 = 700000902
7.0.12a = 7000012 * 100 + 1 = 700001201
```

Ordering holds on both axes: a newer upstream base outranks every letter on an
older one (`700001200 > 700000999`), so a rebase never collides with what has
already shipped. Installs on 7.0.9 (`7000009`) sit strictly below `700000901`
and are offered this release normally.

Bounds were checked rather than assumed — Packer refuses anything over
`999999999` and tdata stores the value as `qint32`, so the widest case this
scheme can express, `9.999.999z` → `999999926`, clears both. It needs
revisiting at upstream major 10.

## Signed and notarized

7.0.9 shipped ad-hoc signed. That runs fine from a build directory and is
refused by Gatekeeper the moment it is downloaded, because the download is
quarantined and an ad-hoc signature is not usable — the app appeared with a
prohibited-sign icon and would not launch.

This build carries a **Developer ID signature and an Apple notarization
ticket**. `spctl` reports `accepted / source=Notarized Developer ID` for both
the app and the disk image. `codesign --verify`, which passes on an ad-hoc
signature and says nothing about Gatekeeper, is no longer used to check.

Camera, microphone and location entitlements are preserved through the
strip-and-re-sign step; an earlier packaging pass dropped them while leaving
the hardened runtime enforcing them, which would have broken voice messages
and calls on the shipped build while working for whoever built it.

## Everything else

The MCP contract work, the pinned update origin, and the publisher repairs are
described in the [7.0.9 notes](RELEASE_NOTES_7.0.9.md) — they are in this
build too.
