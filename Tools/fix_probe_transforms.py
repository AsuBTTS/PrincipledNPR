"""Correct the owned bootstrap actors after a positional Rotator argument bug."""
import json
from pathlib import Path
import unreal

root = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
assert (root / 'PrincipledNPR.uproject').is_file(), 'Wrong project'
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert editor.get_editor_world().get_path_name().startswith('/Game/NPR/Maps/L_S58_ToonProbe.')
desired = {'KeyDirectional': (-40.0, -100.0, 0.0),
           'ProbeCamera': (-20.0, 130.0, 0.0), 'PlayerStart': (0.0, 130.0, 0.0)}
record = {'reason': 'Use explicit pitch/yaw/roll keywords; native MakeRotator takes roll/pitch/yaw.',
          'engine_version': unreal.SystemLibrary.get_engine_version(), 'actors': []}
for key, (pitch, yaw, roll) in desired.items():
    tag = 'PNPR_Bootstrap_' + key
    matches = [actor for actor in actors.get_all_level_actors()
               if tag in [str(t) for t in actor.get_editor_property('tags')]]
    assert len(matches) == 1, key
    actor = matches[0]
    before = actor.get_actor_rotation()
    assert actor.set_actor_rotation(unreal.Rotator(pitch=pitch, yaw=yaw, roll=roll), False)
    after = actor.get_actor_rotation()
    record['actors'].append({'key': key, 'before': {'pitch': before.pitch, 'yaw': before.yaw, 'roll': before.roll},
                            'after': {'pitch': after.pitch, 'yaw': after.yaw, 'roll': after.roll}})
editor.set_level_viewport_camera_info(unreal.Vector(700.0, -850.0, 500.0),
                                      unreal.Rotator(pitch=-20.0, yaw=130.0, roll=0.0))
assert levels.save_current_level(), 'Map save failed'
(root / 'Documentation/ProbeTransformCorrection.json').write_text(
    json.dumps(record, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.log('PrincipledNPR probe transform correction saved.')
