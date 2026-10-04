"""Remove machine-local file-server credentials from versioned UE config.

Git's clean filter affects committed content only; local UE files stay intact.
"""
import json
import re
import sys

data = sys.stdin.buffer.read()
sanitized = re.sub(rb"(?im)^[ \t]*SecurityToken[ \t]*=[^\r\n]*(?:\r?\n|$)", b"", data)
if sanitized.lstrip().startswith(b"{"):
    descriptor = json.loads(sanitized.decode("utf-8-sig"))
    association = descriptor.get("EngineAssociation")
    if isinstance(association,str) and association.startswith("{"):
        descriptor["EngineAssociation"] = "5.8"
    sanitized = json.dumps(descriptor,ensure_ascii=False,indent=2).encode("utf-8")
if sanitized:
    sanitized = sanitized.rstrip(b"\r\n") + b"\n"
sys.stdout.buffer.write(sanitized)
