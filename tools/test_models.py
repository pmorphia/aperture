"""Writes the small models in tests/models/ that the tests load.

    python3 tools/test_models.py

- bend.gltf: a 1 x 2 strip skinned to two bones, with buffers embedded as base64. Its "Bend"
  animation turns the upper bone 90 degrees around z over one second; "Lift" moves the root up
  by 1 with a step.
- tiles.glb: a binary glTF with a parent node (moved 2 along x) holding a 1 x 1 quad textured by
  an embedded 2 x 2 PNG (red, green, blue, white), and an emissive material.
- crate.obj, crate.mtl, crate.png: an OBJ cube with two materials, one textured.
"""
import base64
import json
import math
import os
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "tests", "models")


def png(width, height, pixels):
    """RGBA rows as a PNG file."""
    raw = b"".join(b"\0" + bytes(pixels[y * width * 4:(y + 1) * width * 4]) for y in range(height))

    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data +
                struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF))

    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


class Buffer:
    def __init__(self):
        self.data = bytearray()
        self.views = []
        self.accessors = []

    def add(self, fmt, values, kind, component, count, extra=None, target=None):
        while len(self.data) % 4:
            self.data.append(0)
        offset = len(self.data)
        self.data += struct.pack("<" + fmt * len(values), *values)
        view = {"buffer": 0, "byteOffset": offset, "byteLength": len(self.data) - offset}
        if target:
            view["target"] = target
        self.views.append(view)
        accessor = {"bufferView": len(self.views) - 1, "componentType": component,
                    "count": count, "type": kind}
        if extra:
            accessor.update(extra)
        self.accessors.append(accessor)
        return len(self.accessors) - 1

    def floats(self, values, kind, width, bounds=False):
        extra = None
        if bounds:
            columns = [values[i::width] for i in range(width)]
            extra = {"min": [min(c) for c in columns], "max": [max(c) for c in columns]}
        return self.add("f", values, kind, 5126, len(values) // width, extra)

    def raw_view(self, data):
        while len(self.data) % 4:
            self.data.append(0)
        offset = len(self.data)
        self.data += data
        self.views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(data)})
        return len(self.views) - 1


def bend():
    b = Buffer()
    # Three rows of two vertices at y = 0, 1, 2; the middle row is shared by both bones.
    positions, normals, joints, weights = [], [], [], []
    for row in range(3):
        for x in (-0.5, 0.5):
            positions += [x, float(row), 0.0]
            normals += [0.0, 0.0, 1.0]
            joints += [0, 1, 0, 0]
            w = {0: [1, 0], 1: [0.5, 0.5], 2: [0, 1]}[row]
            weights += [w[0], w[1], 0.0, 0.0]
    indices = [0, 1, 3, 0, 3, 2, 2, 3, 5, 2, 5, 4]
    position = b.floats(positions, "VEC3", 3, bounds=True)
    normal = b.floats(normals, "VEC3", 3)
    joint = b.add("H", joints, "VEC4", 5123, 6)
    weight = b.floats(weights, "VEC4", 4)
    index = b.add("H", indices, "SCALAR", 5123, len(indices))
    # Inverse bind matrices: bone 0 at the origin, bone 1 at y = 1.
    inverse = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
               1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 1]
    inverse_bind = b.floats([float(v) for v in inverse], "MAT4", 16)
    times = b.floats([0.0, 1.0], "SCALAR", 1, bounds=True)
    s = math.sin(math.pi / 4)
    rotations = b.floats([0.0, 0.0, 0.0, 1.0, 0.0, 0.0, s, s], "VEC4", 4)
    lift_times = b.floats([0.0, 0.5], "SCALAR", 1, bounds=True)
    lifts = b.floats([0.0, 0.0, 0.0, 0.0, 1.0, 0.0], "VEC3", 3)
    gltf = {
        "asset": {"version": "2.0", "generator": "aperture tools/test_models.py"},
        "scene": 0,
        "scenes": [{"nodes": [0, 3]}],
        "nodes": [
            {"name": "root", "children": [1]},
            {"name": "lower", "children": [2]},
            {"name": "upper", "translation": [0, 1, 0]},
            {"name": "strip", "mesh": 0, "skin": 0},
        ],
        "meshes": [{"primitives": [{
            "attributes": {"POSITION": position, "NORMAL": normal, "JOINTS_0": joint,
                           "WEIGHTS_0": weight},
            "indices": index, "material": 0}]}],
        "materials": [{"name": "red", "doubleSided": True, "pbrMetallicRoughness": {
            "baseColorFactor": [1, 0, 0, 1], "metallicFactor": 0, "roughnessFactor": 1}}],
        "skins": [{"joints": [1, 2], "inverseBindMatrices": inverse_bind}],
        "animations": [
            {"name": "Bend", "channels": [{"sampler": 0, "target": {"node": 2, "path": "rotation"}}],
             "samplers": [{"input": times, "output": rotations}]},
            {"name": "Lift", "channels": [{"sampler": 0, "target": {"node": 0, "path": "translation"}}],
             "samplers": [{"input": lift_times, "output": lifts, "interpolation": "STEP"}]},
        ],
        "accessors": b.accessors,
        "bufferViews": b.views,
        "buffers": [{"byteLength": len(b.data),
                     "uri": "data:application/octet-stream;base64," +
                            base64.b64encode(bytes(b.data)).decode()}],
    }
    with open(os.path.join(OUT, "bend.gltf"), "w") as f:
        json.dump(gltf, f, indent=1)


def tiles():
    b = Buffer()
    positions = [-0.5, -0.5, 0, 0.5, -0.5, 0, 0.5, 0.5, 0, -0.5, 0.5, 0]
    uvs = [0, 1, 1, 1, 1, 0, 0, 0]
    position = b.floats([float(v) for v in positions], "VEC3", 3, bounds=True)
    uv = b.floats([float(v) for v in uvs], "VEC2", 2)
    index = b.add("B", [0, 1, 2, 0, 2, 3], "SCALAR", 5121, 6)  # unsigned byte indices
    image = b.raw_view(png(2, 2, [255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255,
                                  255, 255, 255, 255]))
    gltf = {
        "asset": {"version": "2.0"},
        "nodes": [{"name": "holder", "translation": [2, 0, 0], "children": [1]},
                  {"name": "tile", "mesh": 0}],
        "scenes": [{"nodes": [0]}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": position, "TEXCOORD_0": uv},
                                    "indices": index, "material": 0}]}],
        "materials": [{"name": "tiles", "pbrMetallicRoughness": {
            "baseColorTexture": {"index": 0}, "metallicFactor": 0},
            "emissiveFactor": [0, 0, 0]}],
        "textures": [{"source": 0, "sampler": 0}],
        "samplers": [{"magFilter": 9728, "minFilter": 9728}],
        "images": [{"bufferView": image, "mimeType": "image/png"}],
        "accessors": b.accessors,
        "bufferViews": b.views,
        "buffers": [{"byteLength": len(b.data)}],
    }
    text = json.dumps(gltf).encode()
    text += b" " * (-len(text) % 4)
    binary = bytes(b.data) + b"\0" * (-len(b.data) % 4)
    body = (struct.pack("<II", len(text), 0x4E4F534A) + text +
            struct.pack("<II", len(binary), 0x004E4942) + binary)
    with open(os.path.join(OUT, "tiles.glb"), "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, 12 + len(body)) + body)


def crate():
    with open(os.path.join(OUT, "crate.png"), "wb") as f:
        f.write(png(2, 2, [0, 200, 255, 255] * 4))
    with open(os.path.join(OUT, "crate.mtl"), "w") as f:
        f.write("# two materials\nnewmtl wood\nKd 1 1 1\nmap_Kd crate.png\nNs 10\n\n"
                "newmtl metal\nKd 1 0.5 0\nPm 1\nPr 0.4\n")
    corners = [(x, y, z) for x in (-0.5, 0.5) for y in (-0.5, 0.5) for z in (-0.5, 0.5)]
    lines = ["mtllib crate.mtl"]
    lines += ["v %g %g %g" % c for c in corners]
    lines += ["vt 0 0", "vt 1 0", "vt 1 1", "vt 0 1"]
    faces = {  # counterclockwise from outside, 1-based corner numbers
        "wood": [(2, 4, 8, 6), (1, 5, 7, 3), (5, 6, 8, 7), (1, 3, 4, 2)],
        "metal": [(3, 7, 8, 4), (1, 2, 6, 5)],
    }
    for material, quads in faces.items():
        lines.append("usemtl " + material)
        for q in quads:
            lines.append("f " + " ".join("%d/%d" % (v, i + 1) for i, v in enumerate(q)))
    with open(os.path.join(OUT, "crate.obj"), "w") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    bend()
    tiles()
    crate()
