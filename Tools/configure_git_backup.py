"""Initialize repository-local LFS and config filtering with this Python runtime."""
from pathlib import Path
import subprocess
import sys

project = Path(__file__).resolve().parent.parent
subprocess.run(["git", "lfs", "install", "--local"], cwd=project, check=True)
command = subprocess.list2cmdline([Path(sys.executable).as_posix(),
                                  (project / "Tools/git_config_clean.py").as_posix()])
record_command = subprocess.list2cmdline([Path(sys.executable).as_posix(),
                                         (project / "Tools/git_record_clean.py").as_posix()])
for key, value in (("filter.npr-config.clean", command),
                   ("filter.npr-config.smudge", "cat"),
                   ("filter.npr-config.required", "true"),
                   ("filter.npr-record.clean", record_command),
                   ("filter.npr-record.smudge", "cat"),
                   ("filter.npr-record.required", "true")):
    subprocess.run(["git","config","--local",key,value],cwd=project,check=True)
print("Repository-local LFS and machine-local config filtering initialized.")
