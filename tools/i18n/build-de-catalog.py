#!/usr/bin/env python3
"""Compile the German catalogue the iPad build ships.

Orca generates resources/i18n/<lang>/OrcaSlicer.mo from localization/i18n/,
but only through the `gettext_po_to_mo` CMake target, which is not part of
`all` and which no workflow here builds -- so resources/i18n/ went into the
IPA holding nothing but placeholder.txt and every launch came up in English,
however firmly patch 0412/0415 asked for German.

So the .mo is built here and committed under orca-overlay/resources/, which
every workflow copies into the Orca tree before it bundles resources/. No
gettext on the runner, no CMake target, nothing to remember in CI.

Run this after editing the .po:

    python3 tools/i18n/build-de-catalog.py

It writes both de/ and de_DE/: patch 0415 loads the catalogue itself with
SetLanguage("de"), while Orca's own path asks for the config value "de_DE".
Whichever runs first, the catalogue is where it looks.

--check compiles to memory and reports whether the committed .mo files are
what the .po produces, without writing anything (used by CI).
"""

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pofmt

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PO = os.path.join(REPO, 'orca-overlay/localization/i18n/de/OrcaSlicer_de.po')
OUT_DIRS = ['orca-overlay/resources/i18n/de', 'orca-overlay/resources/i18n/de_DE']
MAGIC = 0x950412de


def catalogue(po_path):
    """{msgid: msgstr} in GNU gettext's key form, header included."""
    out = {}
    for e in pofmt.parse(po_path):
        if e.msgid is None:
            continue
        msgid = pofmt._un(e.msgid)
        if e.msgid_plural is not None:
            plural = pofmt._un(e.msgid_plural)
            strs = [pofmt._un(e.msgstr[i]) for i in sorted(e.msgstr)]
            if not any(strs):
                continue
            key = msgid + '\x00' + plural
            out[key] = '\x00'.join(strs)
            continue
        value = pofmt._un(e.msgstr.get(0, ''))
        if msgid and not value:
            # untranslated: leave it out, gettext falls back to the msgid
            continue
        if e.msgctxt is not None:
            msgid = pofmt._un(e.msgctxt) + '\x04' + msgid
        out[msgid] = value
    return out


def compile_mo(entries):
    """The MO layout of gettext's spec: two sorted string tables and an index."""
    items = sorted((k.encode('utf-8'), v.encode('utf-8')) for k, v in entries.items())
    n = len(items)
    keystart = 7 * 4 + 16 * n          # header, then both index tables
    offsets = []
    ids = b''
    strs = b''
    for key, value in items:
        offsets.append((len(ids), len(key), len(strs), len(value)))
        ids += key + b'\x00'
        strs += value + b'\x00'
    valuestart = keystart + len(ids)
    koffsets = []
    voffsets = []
    for o1, l1, o2, l2 in offsets:
        koffsets += [l1, o1 + keystart]
        voffsets += [l2, o2 + valuestart]
    output = struct.pack('Iiiiiii', MAGIC, 0, n, 7 * 4, 7 * 4 + n * 8, 0, 0)
    output += struct.pack('i' * len(koffsets), *koffsets)
    output += struct.pack('i' * len(voffsets), *voffsets)
    output += ids
    output += strs
    return output


def main():
    check = '--check' in sys.argv
    entries = catalogue(PO)
    blob = compile_mo(entries)
    # the header entry carries no msgid, everything else is a real message
    print('%s: %d messages' % (os.path.relpath(PO, REPO), len(entries) - 1))
    status = 0
    for d in OUT_DIRS:
        path = os.path.join(REPO, d, 'OrcaSlicer.mo')
        if check:
            have = open(path, 'rb').read() if os.path.exists(path) else None
            if have != blob:
                print('::error::%s is not what %s compiles to; run '
                      'tools/i18n/build-de-catalog.py' % (d + '/OrcaSlicer.mo', 'the .po'))
                status = 1
            else:
                print('%s/OrcaSlicer.mo: up to date (%d bytes)' % (d, len(blob)))
            continue
        os.makedirs(os.path.join(REPO, d), exist_ok=True)
        with open(path, 'wb') as f:
            f.write(blob)
        print('%s/OrcaSlicer.mo: %d bytes' % (d, len(blob)))
    return status


if __name__ == '__main__':
    sys.exit(main())
