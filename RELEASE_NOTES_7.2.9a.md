# Tlgrm 7.2.9a

Base: Telegram Desktop **7.2.9** (upstream stable). This rebases the fork from
**7.2.8** onto upstream's stable **7.2.9**
(`AppVersion 700200901 = 7002009 * 100 + 1`).

## What changed upstream (7.2.8 → 7.2.9)

A small maintenance release — **stability/rendering fixes, no new features and
no API change**. The MTProto API layer is **229** on both, so the MCP tool
surface (404 tools) is unaffected:

- **Fix some tlottie incorrect renderings** (the headline of 7.2.9), on top of
  7.2.7/7.2.8's crash fixes for invalid lottie files.
- Assorted upstream build and stability fixes across the 63-commit range.

## Build note (toolchain)

Built with Apple clang 21 / macOS SDK 27. Two toolchain-compat fixes were
required and are independent of the upstream merge: range-v3's hand-rolled
`std::` forward declarations in `meta.hpp` are disabled via
`META_NO_STD_FORWARD_DECLARATIONS` (the new libc++ provides them), and the
desktop-app submodules were synced to their 7.2.9 pins.

## Everything Tlgrm carries forward

The fork's own work is unchanged and merged forward intact: the embedded MCP
tool surface (404 tools, four declaration sites verified by the build-time
cross-check), the gradual export / retention / archiver subsystems, the pinned
`updates.71grm.site` update origin, and the branding. The MCP `AppVersion`
scheme (`upstreamBase * 100 + index`) is described in
[7.0.9a](RELEASE_NOTES_7.0.9a.md).

## Signed and notarized

Developer ID signature + Apple notarization ticket; `spctl` reports
`accepted / source=Notarized Developer ID` for both the app and the disk image.
Camera, microphone and location entitlements are preserved through the
strip-and-re-sign step.
