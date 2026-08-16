'''
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
'''
import sys, os, re, subprocess, io

def finish(code):
    global executePath
    os.chdir(executePath)
    sys.exit(code)

if sys.platform == 'win32' and not 'COMSPEC' in os.environ:
    print('[ERROR] COMSPEC environment variable is not set.')
    finish(1)

executePath = os.getcwd()
scriptPath = os.path.dirname(os.path.realpath(__file__))

inputVersion = ''
versionOriginal = ''
versionMajor = ''
versionMinor = ''
versionPatch = ''
versionAlpha = '0'
versionBeta = False
# Tlgrm's own release letter on a given upstream base: 7.0.9a, 7.0.9b, ...
# Empty means a plain upstream-numbered build.
versionFork = ''
for arg in sys.argv:
  # Trailing lowercase letter = this fork's release index on that base.
  match = re.match(r'^\s*(\d+)\.(\d+)(\.(\d+)([a-z])?(\.(\d+|beta))?)?\s*$', arg)
  if match:
    inputVersion = arg
    versionOriginal = inputVersion
    versionMajor = match.group(1)
    versionMinor = match.group(2)
    versionPatch = match.group(4) if match.group(4) else '0'
    versionFork = match.group(5) if match.group(5) else ''
    versionAlphaBeta = match.group(6) if match.group(6) else ''
    if len(versionAlphaBeta) > 0:
      if match.group(7) == 'beta':
        versionBeta = True
      else:
        versionAlpha = match.group(7)

if not len(versionMajor):
  print("Wrong version parameter")
  finish(1)

def checkVersionPart(part):
  cleared = int(part) % 1000 if len(part) > 0 else 0
  if str(cleared) != part:
    print("Bad version part: " + part)
    finish(1)

checkVersionPart(versionMajor)
checkVersionPart(versionMinor)
checkVersionPart(versionPatch)
checkVersionPart(versionAlpha)

versionUpstream = int(versionMajor) * 1000000 + int(versionMinor) * 1000 + int(versionPatch)

# One upstream base carries several Tlgrm releases, so the comparable integer
# is `upstreamBase * 100 + index` -- 7.0.9a is 700000901, 7.0.9b is 700000902.
# A newer upstream base always outranks every letter on an older one, and the
# base stays readable in the number and in package filenames. Packer refuses
# anything over 999999999 (packer.cpp), which this reaches at upstream major
# 10; the scheme needs revisiting before then.
# cmake/version.cmake (a shared upstream submodule, not ours to change) reads
# ONLY AppVersionOriginal, splits it on '.' and does integer math on the parts,
# so that field has to stay numeric -- it feeds the bundle's
# CFBundleShortVersionString. Fold the fork index into the patch component so
# it stays both parseable and unique per release: 7.0.9a -> 7.0.901.
if versionFork:
  forkIndex = ord(versionFork) - ord('a') + 1
  versionOriginal = (versionMajor + '.' + versionMinor + '.'
    + str(int(versionPatch) * 100 + forkIndex))
  versionFull = str(versionUpstream * 100 + forkIndex)
  if int(versionFull) > 999999999:
    print("Version too large for the packer: " + versionFull)
    finish(1)
else:
  versionFull = str(versionUpstream)
versionFullAlpha = '0'
if versionAlpha != '0':
  versionFullAlpha = str(int(versionFull) * 1000 + int(versionAlpha))

versionStr = versionMajor + '.' + versionMinor + '.' + versionPatch + versionFork
versionStrSmall = versionStr if versionPatch != '0' else versionMajor + '.' + versionMinor + versionFork

if versionBeta:
  print('Setting version: ' + versionStr + ' beta')
elif versionAlpha != '0':
  print('Setting version: ' + versionStr + '.' + versionAlpha + ' closed alpha')
else:
  print('Setting version: ' + versionStr + ' stable')

#def replaceInFile(path, replaces):

def checkChangelog():
  global scriptPath, versionStr, versionStrSmall

  count = 0
  with io.open(scriptPath + '/../../changelog.txt', encoding='utf-8') as f:
    for line in f:
      if line.startswith(versionStr + ' ') or line.startswith(versionStrSmall + ' '):
        count = count + 1
  if count == 0:
    print('Changelog entry not found!')
    finish(1)
  elif count != 1:
    print('Wrong changelog entries count found: ' + count)
    finish(1)

checkChangelog()

def replaceInFile(path, replacements):
  content = ''
  foundReplacements = {}
  updated = False
  with open(path, 'r') as f:
    for line in f:
      for replacement in replacements:
        if re.search(replacement[0], line):
          changed = re.sub(replacement[0], replacement[1], line)
          if changed != line:
            line = changed
            updated = True
          foundReplacements[replacement[0]] = True
      content = content + line
  for replacement in replacements:
    if not replacement[0] in foundReplacements:
      print('Could not find "' + replacement[0] + '" in "' + path + '".')
      finish(1)
  if updated:
    with open(path, 'w') as f:
      f.write(content)

print('Patching build/version...')
replaceInFile(scriptPath + '/version', [
  [ r'(AppVersion\s+)\d+', r'\g<1>' + versionFull ],
  [ r'(AppVersionStrMajor\s+)\d[\d\.]*', r'\g<1>' + versionMajor + '.' + versionMinor ],
  [ r'(AppVersionStrSmall\s+)\d[\d\.]*', r'\g<1>' + versionStrSmall ],
  [ r'(AppVersionStr\s+)\d[\d\.]*', r'\g<1>' + versionStr ],
  [ r'(BetaChannel\s+)\d', r'\g<1>' + ('1' if versionBeta else '0') ],
  [ r'(AlphaVersion\s+)\d+', r'\g<1>' + versionFullAlpha ],
  [ r'(AppVersionOriginal\s+)\d[\d\.beta]*', r'\g<1>' + versionOriginal ],
])

print('Patching core/version.h...')
replaceInFile(scriptPath + '/../SourceFiles/core/version.h', [
  [ r'(TDESKTOP_REQUESTED_ALPHA_VERSION\s+)\(\d+ULL\)', r'\g<1>(' + versionFullAlpha + 'ULL)' ],
  [ r'(AppVersion\s+=\s+)\d+', r'\g<1>' + versionFull ],
  [ r'(AppVersionStr\s+=\s+)[^;]+', r'\g<1>"' + versionStrSmall + '"' ],
  [ r'(AppBetaVersion\s+=\s+)[a-z]+', r'\g<1>' + ('true' if versionBeta else 'false') ],
])

parts = [versionMajor, versionMinor, versionPatch, versionAlpha]
withcomma = ','.join(parts)
withdot = '.'.join(parts)
rcReplaces = [
  [ r'(FILEVERSION\s+)\d+,\d+,\d+,\d+', r'\g<1>' + withcomma ],
  [ r'(PRODUCTVERSION\s+)\d+,\d+,\d+,\d+', r'\g<1>' + withcomma ],
  [ r'("FileVersion",\s+)"\d+\.\d+\.\d+\.\d+"', r'\g<1>"' + withdot + '"' ],
  [ r'("ProductVersion",\s+)"\d+\.\d+\.\d+\.\d+"', r'\g<1>"' + withdot + '"' ],
]

print('Patching Telegram.rc...')
replaceInFile(scriptPath + '/../Resources/winrc/Telegram.rc', rcReplaces)

print('Patching Updater.rc...')
replaceInFile(scriptPath + '/../Resources/winrc/Updater.rc', rcReplaces)

print('Patching appxmanifest.xml...')
replaceInFile(scriptPath + '/../Resources/uwp/AppX/AppxManifest.xml', [
  [ r'( Version=)"\d+\.\d+\.\d+\.\d+"', r'\g<1>"' + withdot + '"' ],
])
