"""Create original sample MatCap textures and a beveled calibration mesh in UE."""

from pathlib import Path
import math
import struct
import zlib

import unreal


DESTINATION = "/Game/NPR/MatCapDemo"
SOURCE = Path(unreal.Paths.project_saved_dir()).resolve() / "NPR" / "MatCap" / "Inputs"
SOURCE.mkdir(parents=True, exist_ok=True)
ASSETS = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)


def srgb(value):
    value = max(0.0, min(1.0, value))
    return round(255 * (value * 12.92 if value <= 0.0031308 else 1.055 * value ** (1 / 2.4) - 0.055))


def png_chunk(tag, data):
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))


def paint_texture(name, hue, accent):
    size = 256
    pixels = bytearray()
    for row in range(size):
        pixels.append(0)
        for col in range(size):
            x, y = (col + 0.5) / size * 2 - 1, 1 - (row + 0.5) / size * 2
            z = math.sqrt(max(0, 1 - x * x - y * y))
            broad = max(0, -0.45 * x + 0.65 * y + 0.61 * z) ** 10
            stripe = math.exp(-((y + 0.45 + 0.2 * x) / 0.09) ** 2) * (0.35 + 0.65 * z)
            for h, a in zip(hue, accent):
                pixels.append(srgb(h * (0.06 + 0.75 * broad) + a * 0.42 * stripe))
    header = struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0)
    path = SOURCE / (name + ".png")
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", header) + png_chunk(b"IDAT", zlib.compress(pixels)) + png_chunk(b"IEND", b""))
    return path


def bevel_mesh():
    # A chamfered cube consists of six inset squares, twelve bevel strips and eight triangles.
    outer, inner = 41, 30
    vertices = []
    for axis in range(3):
        for sign in (-1, 1):
            for a in (-inner, inner):
                for b in (-inner, inner):
                    point = [a, b]
                    point.insert(axis, sign * outer)
                    vertices.append(tuple(point))

    def dot(a, b):
        return sum(x * y for x, y in zip(a, b))

    def cross(a, b):
        return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])

    text = ["o MatCapBevel"] + [f"v {x} {z} {-y}" for x, y, z in vertices]
    faces = []
    for normal in ((x, y, z) for x in (-1, 0, 1) for y in (-1, 0, 1) for z in (-1, 0, 1) if (x, y, z) != (0, 0, 0)):
        plane = max(dot(normal, p) for p in vertices)
        face = [i for i, p in enumerate(vertices) if dot(normal, p) == plane]
        center = tuple(sum(vertices[i][a] for i in face) / len(face) for a in range(3))
        u = cross(normal, (0, 0, 1) if normal[0] or normal[1] else (0, 1, 0))
        v = cross(normal, u)
        face.sort(key=lambda i: math.atan2(dot(tuple(a - b for a, b in zip(vertices[i], center)), v),
                                           dot(tuple(a - b for a, b in zip(vertices[i], center)), u) * math.sqrt(dot(normal, normal))))
        faces.append("f " + " ".join(str(i + 1) for i in face))
    path = SOURCE / "SM_MatCapBevel.obj"
    path.write_text("\n".join(text + faces) + "\n", encoding="ascii")
    return path


def import_once(source, mesh=False):
    if ASSETS.does_asset_exist(f"{DESTINATION}/{source.stem}"):
        return
    task = unreal.AssetImportTask()
    task.filename = str(source)
    task.destination_path = DESTINATION
    task.destination_name = source.stem
    task.automated = True
    task.save = True
    if mesh:
        options = unreal.FbxImportUI()
        options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
        options.set_editor_property("automated_import_should_detect_type", False)
        options.set_editor_property("import_materials", False)
        options.set_editor_property("import_textures", False)
        task.options = options
        task.factory = unreal.FbxFactory()
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    if not ASSETS.does_asset_exist(f"{DESTINATION}/{source.stem}"):
        raise RuntimeError(f"Import failed: {source.name}: {task.imported_object_paths}")


import_once(paint_texture("T_MatCapCool", (0.24, 0.68, 1.0), (0.08, 0.22, 0.55)))
import_once(paint_texture("T_MatCapWarm", (1.0, 0.58, 0.23), (0.65, 0.16, 0.04)))
import_once(bevel_mesh(), mesh=True)
