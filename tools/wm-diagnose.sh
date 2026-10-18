#!/bin/sh
# Prints what the system knows about the Digiface so names and ports can be checked.
echo "== USB"; lsusb -d 2a39:3fa0 2>/dev/null || echo "Digiface not seen by lsusb"
echo "== ALSA"; aplay -l 2>/dev/null | grep -i -A2 digiface
echo "== PipeWire nodes"; pw-cli ls Node 2>/dev/null | grep -i -B3 -A12 digiface
echo "== PipeWire ports"; pw-link -io 2>/dev/null | grep -i digiface
echo "== Node properties (names the apps use)"; pw-dump 2>/dev/null | python3 -c '
import json,sys
for o in json.load(sys.stdin):
    p=(o.get("info") or {}).get("props") or {}
    if "digiface" in json.dumps(p).lower() and o["type"].endswith("Node"):
        print({k:v for k,v in p.items() if k.startswith(("node.name","node.desc","node.nick","node.channel","audio.","media.class"))})
'
