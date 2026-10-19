"""Read-only binary material/MaterialFunction graph audit through the editor API."""
import json
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
OUT = ROOT / "Documentation/Outline/MultiScope/AssetAudit"
OUT.mkdir(parents=True, exist_ok=True)
assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
registry = unreal.AssetRegistryHelpers.get_asset_registry()
paths = sorted(set(assets.list_assets("/Game/EndfieldRender/Character", True, False) +
                   assets.list_assets("/PrincipledNPR/Outline", True, False)))
report = []
for path in paths:
    if "outline" not in path.lower():
        continue
    obj = assets.load_asset(path)
    if not isinstance(obj, unreal.MaterialInterface):
        continue
    dump = unreal.NPROutlineEditorLibrary.audit_material(obj)
    filename = path.replace("/", "_").split(".")[0] + ".txt"
    (OUT / filename).write_text(dump, encoding="utf-8")
    report.append({"asset": path, "class": obj.get_class().get_name(), "dump": filename,
                   "referencers": [str(n) for n in registry.get_referencers(path.split(".")[0], unreal.AssetRegistryDependencyOptions(include_hard_package_references=True, include_soft_package_references=True))],
                   "has_vertex_normal_node": "MaterialExpressionVertexNormalWS" in dump,
                   "has_vertex_color_node": "MaterialExpressionVertexColor" in dump,
                   "has_custom_node": "MaterialExpressionCustom" in dump,
                   "has_scene_texture_node": "MaterialExpressionSceneTexture" in dump})
(OUT / "Index.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
unreal.log("Outline material graph audit: %d assets" % len(report))
