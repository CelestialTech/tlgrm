#!/usr/bin/env -S uv run --quiet --script
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
"""Cut a Tlgrm release. One command that owns the order.

    tools/release.py 7.0.9b
    tools/release.py 7.0.9b --dry-run     # print the plan, touch nothing

Every step here already existed as a separate script. What did not exist was
anything that knew they are *ordered*, and every ordering hazard in AGENTS.md
is there because it was hit in production:

  - stripping after the DMG           -> shipped a 1.56 GB app
  - signing after packaging           -> shipped an ad-hoc build that Gatekeeper
                                         refuses the moment it is downloaded
  - packing before signing            -> update packages carrying an unsigned app

So this does not reimplement anything. It calls the same tools in the only
order that is correct, and refuses to continue when a step's output is not what
the next step needs.

**Steps are idempotent, not resumable.** Rather than a --from flag, each step
skips when its output already exists for the target version, so re-running
after a failure is cheap and safe. That is simpler than resume and covers the
same need: the failure modes seen so far were all "one late step broke", never
"restart from the middle with different inputs".

Release notes are not generated. `RELEASE_NOTES_<version>.md` must exist before
this runs; the check is first so a 15-minute build is not wasted discovering it.

Publishing is the end of this script. Replacing the copy in /Applications is
not — that is the operator's own machine, and the command is printed instead.
"""

from __future__ import annotations

import argparse
import functools
import re
import subprocess
import sys
from pathlib import Path

print = functools.partial(print, flush=True)  # noqa: A001

REPO = Path(__file__).resolve().parent.parent
TDESKTOP = REPO / "tdesktop"
APP = TDESKTOP / "out/Release/Tlgrm.app"
SKILL = Path.home() / ".claude/skills/macos-codesign"
IDENTITY = "Developer ID Application: Rodion Nazarov (LGAQBC2VM2)"
API_ID = "2040"
API_HASH = "b18441a1ff607e10a989891a5462e627"


class Failed(RuntimeError):
    pass


def run(argv: list[str], cwd: Path | None = None, quiet: bool = False) -> str:
    """Run a command, fail loudly, return stdout."""
    proc = subprocess.run(
        argv, cwd=cwd, capture_output=True, text=True)
    if proc.returncode != 0:
        tail = "\n".join(
            (proc.stdout + proc.stderr).strip().splitlines()[-20:])
        raise Failed(f"{argv[0]} failed:\n{tail}")
    if not quiet and proc.stdout.strip():
        print("   " + proc.stdout.strip().splitlines()[-1])
    return proc.stdout


def plist(key: str, path: Path) -> str:
    proc = subprocess.run(
        ["/usr/libexec/PlistBuddy", "-c", f"Print {key}", str(path)],
        capture_output=True, text=True)
    return proc.stdout.strip() if proc.returncode == 0 else ""


def app_version_str() -> str:
    return plist("CFBundleShortVersionString", APP / "Contents/Info.plist")


def app_version_int() -> int:
    """The comparable integer, read from the header the build compiles in."""
    header = (TDESKTOP / "Telegram/SourceFiles/core/version.h").read_text()
    match = re.search(r"constexpr auto AppVersion = (\d+);", header)
    if not match:
        raise Failed("could not read AppVersion from core/version.h")
    return int(match.group(1))


def dmg_path(version: str) -> Path:
    return REPO / "dmg_build" / f"Tlgrm_{version}.dmg"


# --- the steps ----------------------------------------------------------------

def step_version(version: str, dry: bool) -> None:
    """set_version.py derives all three fields; editing them by hand does not.

    AppVersionOriginal has to stay plain upstream numbering for the shared
    cmake parser, AppVersionStr carries the fork letter, and AppVersion is the
    integer the updater compares. The parser's regex is unanchored and cmake
    caches, so a hand edit leaves a stale bundle version and says nothing.
    """
    if dry:
        print(f"   would run: build/set_version.py {version}")
        return
    run(["python3", "build/set_version.py", version], cwd=TDESKTOP / "Telegram")


def step_build(version: str, dry: bool) -> None:
    """Configure and build universal.

    `-destination 'generic/platform=macOS'` is not optional: a plain -scheme
    build silently emits only the host architecture.
    """
    if app_version_str() == version and not dry:
        print("   already built at this version, skipping")
        return
    if dry:
        print("   would configure and build universal (~15 min)")
        return
    run(["./configure.sh", "-D", f"TDESKTOP_API_ID={API_ID}",
         "-D", f"TDESKTOP_API_HASH={API_HASH}"], cwd=TDESKTOP / "Telegram")
    run(["xcodebuild", "-project", "Telegram.xcodeproj", "-scheme", "Telegram",
         "-configuration", "Release",
         "-destination", "generic/platform=macOS",
         "build", "-jobs", "24"], cwd=TDESKTOP / "out", quiet=True)


def step_verify_universal(dry: bool) -> None:
    if dry:
        print("   would check lipo reports x86_64 and arm64")
        return
    binary = APP / "Contents/MacOS/Tlgrm"
    info = run(["lipo", "-info", str(binary)], quiet=True)
    missing = [a for a in ("x86_64", "arm64") if a not in info]
    if missing:
        raise Failed(
            f"not universal, missing {', '.join(missing)}: {info.strip()}\n"
            "Rebuild with -destination 'generic/platform=macOS'.")
    print("   universal: x86_64 arm64")


def step_strip(dry: bool) -> None:
    """Strip before signing, so the signature covers the final bytes.

    xcodebuild does not strip (that needs DEPLOYMENT_POSTPROCESSING), and
    Packer refuses any payload over 1 GB with "Bad result len", which reads
    like a compression fault rather than "too big to ship".
    """
    binary = APP / "Contents/MacOS/Tlgrm"
    size = binary.stat().st_size
    if size < 600 * 1024 * 1024:
        print(f"   already stripped ({size / 1e6:.0f} MB)")
        return
    if dry:
        print(f"   would strip ({size / 1e6:.0f} MB -> ~490 MB)")
        return
    run(["strip", "-x", str(binary)], quiet=True)
    print(f"   {size / 1e6:.0f} MB -> {binary.stat().st_size / 1e6:.0f} MB")


def step_sign(dry: bool) -> None:
    """Developer ID sign, preserving entitlements minus get-task-allow.

    Re-signing REPLACES entitlements rather than inheriting them, so they are
    read back out of the bundle first. get-task-allow must go: notarization
    rejects it.
    """
    if dry:
        print("   would Developer ID sign with preserved entitlements")
        return
    current = subprocess.run(
        ["codesign", "-dv", str(APP)], capture_output=True, text=True)
    if "Developer ID Application" in (current.stdout + current.stderr):
        print("   already Developer ID signed, skipping")
        return
    ents = subprocess.run(
        ["codesign", "-d", "--entitlements", ":-", str(APP)],
        capture_output=True, text=True).stdout
    path = APP.parent / "release.entitlements"
    path.write_text(ents)
    subprocess.run(
        ["/usr/libexec/PlistBuddy", "-c",
         "Delete :com.apple.security.get-task-allow", str(path)],
        capture_output=True)
    try:
        run([str(SKILL / "sign.sh"), "--app", str(APP),
             "--entitlements", str(path)], quiet=True)
    finally:
        path.unlink(missing_ok=True)
    after = run(["codesign", "-dv", str(APP)], quiet=True)
    if "Developer ID Application" not in after:
        raise Failed("signing did not produce a Developer ID signature")
    print("   signed: Developer ID")


def step_dmg(version: str, dry: bool) -> None:
    """Build the DMG from the signed bundle, then sign and notarize it.

    The disk image needs its own signature: a stapled ticket alone still
    assesses as "no usable signature".
    """
    target = dmg_path(version)
    if dry:
        print(f"   would build, sign and notarize {target.name}")
        return
    if not target.exists():
        run(["./create_dmg.sh"], cwd=REPO, quiet=True)
        # create_dmg names from CFBundleShortVersionString, which is what we
        # asked for, but be explicit rather than assume.
        produced = REPO / "dmg_build" / f"Tlgrm_{app_version_str()}.dmg"
        if produced != target and produced.exists():
            produced.rename(target)
    if not target.exists():
        raise Failed(f"create_dmg.sh produced no {target.name}")
    run(["codesign", "--force", "--sign", IDENTITY, "--timestamp",
         str(target)], quiet=True)
    run([str(SKILL / "notarize.sh"), "--target", str(target)], quiet=True)
    print(f"   {target.name} signed and notarized")


def step_verify_gatekeeper(version: str, dry: bool) -> None:
    """spctl, not codesign --verify.

    `codesign --verify` passes on an ad-hoc signature and says nothing about
    Gatekeeper. 7.0.9 shipped ad-hoc precisely because that was the check used.
    """
    if dry:
        print("   would check spctl accepts the DMG and the app inside it")
        return
    verdict = subprocess.run(
        ["spctl", "-a", "-vvv", "--type", "install", str(dmg_path(version))],
        capture_output=True, text=True)
    combined = verdict.stdout + verdict.stderr
    if "accepted" not in combined or "Notarized Developer ID" not in combined:
        raise Failed(
            "Gatekeeper rejects the DMG:\n" + combined.strip() +
            "\nA downloaded copy would refuse to launch.")
    print("   spctl: accepted / Notarized Developer ID")


def step_packages(version_int: int, dry: bool) -> None:
    """Pack from the signed bundle. --no-strip so the signature survives."""
    if dry:
        print(f"   would pack tarmacupd{version_int} and tmacupd{version_int}")
        return
    packages = [REPO / "dmg_build" / f"{p}{version_int}"
                for p in ("tarmacupd", "tmacupd")]
    if all(p.exists() for p in packages):
        print("   packages already built for this version, skipping")
        return
    run(["uv", "run", "--python", "3.14", "tools/publish_update.py",
         "--version", str(version_int), "--pack-only", "--no-strip"],
        cwd=REPO, quiet=True)
    for p in packages:
        if not p.exists():
            raise Failed(f"packing produced no {p.name}")
    print("   packed both platform keys")


def step_publish(version_int: int, dry: bool) -> None:
    """Both update paths. Needs the client running for the MTProto half."""
    if dry:
        print("   would copy to the HTTP origin and post to @updates71grm")
        return
    run(["uv", "run", "--python", "3.14", "tools/publish_update.py",
         "--version", str(version_int), "--no-strip"], cwd=REPO, quiet=True)
    print("   published to both update paths")


def step_github(version: str, notes: Path, dry: bool) -> None:
    existing = subprocess.run(
        ["gh", "release", "view", f"v{version}", "--repo",
         "CelestialTech/tlgrm"], capture_output=True, text=True)
    if dry:
        # Say which of the two it would actually do. "would create" when the
        # release already exists is the kind of plan that misleads.
        if existing.returncode == 0:
            print(f"   v{version} exists; would replace its DMG asset")
        else:
            print(f"   would create release v{version} with {notes.name}")
        return
    if existing.returncode == 0:
        run(["gh", "release", "upload", f"v{version}", str(dmg_path(version)),
             "--repo", "CelestialTech/tlgrm", "--clobber"], quiet=True)
        print(f"   v{version} existed; asset replaced")
    else:
        run(["gh", "release", "create", f"v{version}",
             "--repo", "CelestialTech/tlgrm",
             "--title", f"Tlgrm v{version}",
             "--notes-file", str(notes), str(dmg_path(version))], quiet=True)
        print(f"   created release v{version}")


def main() -> None:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("version", help="e.g. 7.0.9b — the fork letter is the "
                                    "release index on that upstream base")
    ap.add_argument("--dry-run", action="store_true",
                    help="print the plan and touch nothing")
    args = ap.parse_args()
    dry = args.dry_run

    if not re.fullmatch(r"\d+\.\d+\.\d+[a-z]?", args.version):
        sys.exit(f"error: '{args.version}' is not a version like 7.0.9b")

    notes = REPO / f"RELEASE_NOTES_{args.version}.md"
    if not notes.is_file():
        sys.exit(
            f"error: {notes.name} does not exist.\n"
            "Release notes are written by a person, and this is checked first "
            "so a 15-minute build is not spent discovering it is missing.")

    steps = [
        ("version", lambda: step_version(args.version, dry)),
        ("build", lambda: step_build(args.version, dry)),
        ("verify universal", lambda: step_verify_universal(dry)),
        ("strip", lambda: step_strip(dry)),
        ("sign", lambda: step_sign(dry)),
        ("dmg + notarize", lambda: step_dmg(args.version, dry)),
        ("verify gatekeeper", lambda: step_verify_gatekeeper(args.version, dry)),
        ("packages", lambda: step_packages(app_version_int(), dry)),
        ("publish", lambda: step_publish(app_version_int(), dry)),
        ("github release", lambda: step_github(args.version, notes, dry)),
    ]

    print(f"{'Planning' if dry else 'Cutting'} release {args.version}"
          f" ({len(steps)} steps)\n")
    for index, (name, fn) in enumerate(steps, 1):
        print(f"{index}/{len(steps)} {name}")
        try:
            fn()
        except Failed as error:
            sys.exit(f"\nstopped at step {index} ({name}):\n{error}")

    if dry:
        print("\nDry run: nothing was touched.")
        return
    print(f"\nReleased {args.version}. To use it on this machine:\n"
          f"  hdiutil attach {dmg_path(args.version)}\n"
          f"  rm -rf /Applications/Tlgrm.app && "
          f"cp -R /Volumes/Tlgrm/Tlgrm.app /Applications/\n"
          f"  hdiutil detach /Volumes/Tlgrm")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit("interrupted")
