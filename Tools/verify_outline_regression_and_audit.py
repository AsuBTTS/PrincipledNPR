"""Run existing real-RHI Create acceptance and read-only binary asset audit."""
import runpy
from pathlib import Path
import unreal
root = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
runpy.run_path(str(root / "Tools/audit_outline_assets.py"))
runpy.run_path(str(root / "Tools/verify_outline_workflow.py"))
