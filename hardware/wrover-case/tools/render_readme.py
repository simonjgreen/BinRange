"""Render the accepted v7.1.0 STL geometry for the public hardware gallery.

Run from any directory with Blender 4.5:
    blender --background --python hardware/wrover-case/tools/render_readme.py
Renders go to /tmp/binrange-case-renders; publish metadata-free JPEG copies.
Materials and lighting are illustrative. The printable meshes are never modified.
"""
from pathlib import Path
import math
import bpy
from mathutils import Vector

CASE = Path(__file__).resolve().parents[1]
EXPORTS = CASE / 'versions/v7.1.0/exports'
OUT = Path('/tmp/binrange-case-renders')
OUT.mkdir(exist_ok=True)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
scene = bpy.context.scene
scene.render.engine = 'CYCLES'
scene.cycles.samples = 64
scene.cycles.use_denoising = True
scene.render.resolution_x = 1600
scene.render.resolution_y = 1000
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = 'PNG'
scene.world.color = (0.35, 0.35, 0.35)
scene.view_settings.view_transform = 'AgX'


def material(name, color, roughness):
    mat = bpy.data.materials.new(name)
    mat.diffuse_color = (*color, 1)
    mat.use_nodes = True
    shader = mat.node_tree.nodes.get('Principled BSDF')
    shader.inputs['Base Color'].default_value = (*color, 1)
    shader.inputs['Roughness'].default_value = roughness
    return mat


base_mat = material('Deep petrol polymer', (0.035, 0.115, 0.145), 0.36)
lid_mat = material('Warm chalk polymer', (0.73, 0.69, 0.58), 0.43)
floor_mat = material('Warm studio floor', (0.68, 0.70, 0.69), 0.8)


def mesh(part, mat):
    bpy.ops.wm.stl_import(filepath=str(EXPORTS / f'wrover-case-v7.1.0-{part}.stl'))
    obj = bpy.context.object
    obj.name = part
    obj.data.materials.append(mat)
    # Preserve flat faces and the source mesh's precise edges and dimensions.
    return obj


base = mesh('base', base_mat)
lid = mesh('lid', lid_mat)
bpy.ops.mesh.primitive_plane_add(size=2000, location=(0, 0, -0.04))
bpy.context.object.data.materials.append(floor_mat)


def aim(obj, point):
    obj.rotation_euler = (Vector(point) - obj.location).to_track_quat('-Z', 'Y').to_euler()


for name, pos, power, size in [
    ('Large key', (-50, -100, 160), 650000, 120),
    ('Soft fill', (150, -10, 100), 250000, 100),
    ('Rim', (35, 130, 150), 850000, 100),
]:
    data = bpy.data.lights.new(name, 'AREA')
    data.energy, data.shape, data.size = power, 'DISK', size
    light = bpy.data.objects.new(name, data)
    scene.collection.objects.link(light)
    light.location = pos
    aim(light, (43, 23, 0))

bpy.ops.object.camera_add()
camera = bpy.context.object
scene.camera = camera
camera.data.type = 'ORTHO'
camera.data.lens = 50
camera.data.clip_end = 3000

# As in lid_assembled() in the SCAD source: flip around X, then translate.
lid.rotation_euler.x = math.pi
lid.location = (0, 46.6, 17.6)
camera.location = (-100, -150, 155)
aim(camera, (43, 23.3, 8))
camera.data.ortho_scale = 126
scene.render.filepath = str(OUT / 'wrover-case-v7.1.0-assembled.png')
bpy.ops.render.render(write_still=True)

# Open parts show the actual PCB locating cones and spring geometry.
lid.rotation_euler = (0, 0, 0)
lid.location = (0, 60, 0)
camera.location = (-110, -150, 200)
aim(camera, (43, 53.3, 5))
camera.data.ortho_scale = 166
scene.render.filepath = str(OUT / 'wrover-case-v7.1.0-open.png')
bpy.ops.render.render(write_still=True)
