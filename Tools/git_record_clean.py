"""Keep regenerated research JSON free of host-specific filesystem locations."""
import json
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parent.parent.as_posix()
def clean(value):
    if isinstance(value,dict):
        result = {key:clean(item) for key,item in value.items() if key != "GPU"}
        for key in ("engine_association", "EngineAssociation"):
            if isinstance(result.get(key),str) and result[key].startswith("{"):
                result[key] = "5.8"
        return result
    if isinstance(value,list):
        return [clean(item) for item in value]
    if isinstance(value,str):
        normalized = value.replace("\\","/")
        if normalized.casefold().startswith(root.casefold()+"/"):
            return normalized[len(root)+1:]
        if re.match(r"^[A-Za-z]:[/\\]",value):
            return "LOCAL_ONLY/" + normalized.rsplit("/",1)[-1]
    return value
data = json.loads(sys.stdin.buffer.read().decode("utf-8-sig"))
sys.stdout.buffer.write((json.dumps(clean(data),ensure_ascii=False,indent=2)+"\n").encode("utf-8"))
