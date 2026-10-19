"""Temporary editor validation session, driven by a local allowlisted request file.
Exit request ends the session; not required for normal plugin use.
"""
import json
import os
import runpy
import traceback
from pathlib import Path
import unreal

root = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
request = root / "Saved/NPR/Outline/MultiScope/SessionRequest.json"
status = request.with_name("SessionStatus.json")
request.parent.mkdir(parents=True, exist_ok=True)
os.environ["NPR_VALIDATION_SESSION"] = "1"
os.environ["NPR_OUTLINE_KEEP_OPEN"] = "1"
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
stamp = 0
elapsed = 0
allowed = {"verify_outline_multiscope.py", "verify_outline_workflow.py", "audit_outline_assets.py"}

def poll(delta):
    global stamp, elapsed
    elapsed += delta
    if elapsed < 0.5 or not request.exists():
        return
    elapsed = 0
    now = request.stat().st_mtime_ns
    if now == stamp:
        return
    stamp = now
    try:
        command = json.loads(request.read_text(encoding="utf-8-sig"))
        if command.get("exit"):
            unreal.unregister_slate_post_tick_callback(handle)
            unreal.EditorPythonScripting.set_keep_python_script_alive(False)
            return
        script = command["script"]
        assert script in allowed
        os.environ["NPR_MULTI_STAGE"] = command.get("stage", "Create")
        os.environ["NPR_OUTLINE_STAGE"] = command.get("stage", "Create")
        if command.get("output"):
            os.environ["NPR_VALIDATION_OUTPUT"] = command["output"]
        else:
            os.environ.pop("NPR_VALIDATION_OUTPUT", None)
        runpy.run_path(str(root / "Tools" / script))
        status.write_text(json.dumps({"request": command, "script_dispatched": True}), encoding="utf-8")
    except Exception:
        status.write_text(traceback.format_exc(), encoding="utf-8")
        unreal.log_error(traceback.format_exc())

handle = unreal.register_slate_post_tick_callback(poll)
