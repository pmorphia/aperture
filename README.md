# Aperture

2D and 3D games in [Flywheel](https://github.com/pmorphia/flywheel): a window, input, shapes,
sprites, text, cameras, meshes and glTF models with skeletal animation, physically based
materials, shadows, instancing, render targets, shaders, collision worlds with walking
bodies, sound placed in space and streamed music, and what
fighting games and other frame-exact games need: fixed-step ticks, input history with motion
inputs, frame data, hitstop, camera shake, and debug drawing. Drawn with OpenGL through SDL3.

Aperture is currently in its **beta** phase: version **2.5 (beta-5)**.

```flywheel
import aperture as ap

ap.open("Hello, Aperture", 800, 450)
var position = ap.center()
while ap.frame() do
    position = position + ap.steer() * (240 * ap.delta())
    ap.clear(ap.NAVY)
    ap.circle(position.x, position.y, 24, ap.ORANGE)
    ap.text("Arrow keys or WASD move the circle", 20, 20)
end
ap.close()
```

```sh
pulley add polymorphia.aperture     # in your project
flywheel game.fw                # or: flywheel --build game.fw -O3 -o game
```

## Requirements

- Flywheel 2.26 (beta-26) or newer: Aperture uses structs, native handles, `// flywheel:`
  build directives, the built-in `vecmath` module, and the `hot` module.
- SDL 3.2 or newer with its development files, found through `pkg-config sdl3` (Arch:
  `sdl3`; Debian/Ubuntu: `libsdl3-dev`; Fedora: `SDL3-devel`). Images (PNG, JPEG, BMP, TGA)
  load with any of them; PNG screenshots need SDL 3.4 (older versions save BMP).
- An OpenGL 3.3 driver.
- Only for `import aperture.ttf`: FreeType (`pkg-config freetype2`).

The native part is C compiled by Flywheel the first time a program imports Aperture, and
cached. Developed and tested on Linux (X11, Wayland, and SDL's offscreen driver); the code
uses nothing Linux-specific, but Windows has not been run.

## The game loop

`ap.frame()` starts a frame: it shows what the previous one drew, reads input, advances the
clock, and returns `false` when the window should close. Everything is drawn between two
calls of it.

| | |
|---|---|
| `open(title, width, height, options = 0)` | open the window; options add up: `RESIZABLE`, `FULLSCREEN`, `NO_VSYNC`, `HIDDEN`, `MSAA`, `BORDERLESS` |
| `frame()`, `quit()`, `close()` | next frame; ask the loop to end; close and free everything |
| `delta()`, `time()`, `fps()`, `frame_count()` | seconds for the last frame; seconds since opening; frame rate; frames so far |
| `target_fps(rate)`, `vsync(on)` | limit the frame rate |
| `width()`, `height()`, `size()`, `center()` | the drawing area |
| `virtual_size(width, height)` | draw in a fixed coordinate space scaled to fit the window (letterboxed) |
| `set_title`, `set_size`, `fullscreen`, `show_cursor`, `capture_mouse` | the window |
| `collector_time(seconds)` | how long each `frame()` may spend collecting garbage cycles (default 1 ms) |

Multiply speeds by `delta()` so the game runs the same at any frame rate.

Run a game with `flywheel --hot game.fw` and edit its code while it plays: `frame()` switches
to the new functions within a frame of the recompile, keeping the game's state (see
[hot reload](https://github.com/pmorphia/flywheel/blob/main/docs/tooling.md#hot-reload)).

### Fixed steps

Fighting games, rhythm games, and anything with replays or netcode count time in ticks, not
seconds. `tick(rate)` runs game logic at exactly `rate` ticks a second whatever the frame
rate: a frame runs it as many times as the time that passed needs (none when frames come
faster, several when they come slower):

```flywheel
while ap.frame() do
    while ap.tick(60) do
        update()            # exactly 1/60 s of game time, every time
    end
    draw(ap.alpha())        # 0..1 between the last tick and the next, for smooth drawing
end
```

Inside a tick, `delta()` is exactly the step, and `key_pressed`, `pad_pressed`,
`mouse_pressed`, and their `released` twins compare with the previous tick, so a press lands
on exactly one tick. `tick_count()`, `step()`, and `ticking()` tell where you are. A frame runs
at most 8 ticks; a longer stall is dropped instead of caught up. `fixed_delta(seconds)` makes
every frame count as exactly that long, for recording video and deterministic tests.

### Hitstop and shake

| | |
|---|---|
| `hitstop(ticks)`, `frozen()` | freeze the action on impact: while `frozen()`, skip movement and animation but keep reading input, so it buffers |
| `shake(strength, ticks = 12)` | shake the camera by up to `strength` pixels, fading out; `begin_camera` and `begin_3d` apply it themselves |
| `shake_offset()`, `shaking(on)` | the current offset, for drawing without a camera; turn shaking off (for players who want less motion) |

Both count down in ticks (per frame in a game that does not use ticks). The shake pattern
depends only on the tick count, so replays shake the same way.
## Values

`Vec2`, `Vec3`, `Color`, `Rect`, `Camera2D`, `Camera3D`, and `Ray` are structs: they are
copied like numbers and cost nothing to create, so `position + velocity * dt` in a loop over
ten thousand things allocates nothing, and an array of them is one contiguous block.

`Vec2`, `Vec3`, `Vec4`, `Mat4`, and `Quat` are Flywheel's built-in
[vecmath](https://github.com/pmorphia/flywheel/blob/main/stdlib/README.md#vecmath) types,
re-exported: `ap.Vec2` and `vecmath.Vec2` are the same type, so vectors from other libraries
that use vecmath pass straight in.

- `Vec2(x, y)`, `Vec3(x, y, z)`: `+ - * /`, unary `-`, `==`, `length`, `normalized`, `dot`,
  `cross`, `distance`, `lerp`, `scaled`; `Vec2` also has `angle`, `rotated`, `toward`,
  `limited`, and `Vec2.from_angle(degrees)`.
- `Color(r, g, b, a = 255)`: `fade(opacity)`, `lerp`, `brightness`, `packed`. `hex("#ff8800")`
  and `hsv(hue, saturation, value)` make colors; `WHITE`, `BLACK`, `RED`, `ORANGE`, `YELLOW`,
  `GREEN`, `LIME`, `CYAN`, `BLUE`, `NAVY`, `PURPLE`, `PINK`, `BROWN`, `SKY`, `GRAY`,
  `LIGHT_GRAY`, `DARK_GRAY`, and `TRANSPARENT` are ready.
- `Rect(x, y, w, h)`: `contains`, `overlaps`, `intersection`, `center`, `moved`, `grown`,
  `right`, `bottom`, and `Rect.centered(center, w, h)`.

Whole numbers can be written without `.0` wherever a number is expected: `ap.rect(10, 10, 50,
50)`.

## Input

| | |
|---|---|
| `key_down(key)`, `key_pressed(key)`, `key_released(key)` | held; went down this frame; went up this frame. Keys are `KEY_A` … `KEY_Z`, `KEY_0` … `KEY_9`, `KEY_SPACE`, `KEY_ENTER`, `KEY_ESCAPE`, `KEY_LEFT`, `KEY_F1`, … |
| `any_key()`, `key_named("Space")`, `key_name(key)`, `typed()` | any key pressed; keys by name; text typed this frame |
| `mouse()`, `mouse_delta()`, `wheel()` | position, motion, and scrolling as `Vec2` |
| `mouse_down(button = MOUSE_LEFT)`, `mouse_pressed`, `mouse_released` | buttons |
| `pad_connected(pad = 0)`, `pad_axis(axis)`, `pad_stick(right = false)`, `pad_down(button)`, `pad_pressed(button)`, `pad_rumble(low, high, seconds)` | gamepads: `PAD_SOUTH`, `PAD_START`, `AXIS_LEFT_X`, … |
| `steer()` | the direction from arrow keys, WASD, the d-pad, or the left stick, at most 1 long |
| `send_key(key, down)`, `send_mouse(position, button, down)` | synthetic input for tests and replays |

### Input history

`Inputs` is one player's controls with a history of the last ticks (30 by default), for
fighting-game input. Directions use numpad notation as seen by a player facing right (6
forward, 4 back, 2 down, 3 down-forward, 5 neutral); `face(-1)` mirrors them for a player
facing left, so `"236"` is always toward the opponent.

```flywheel
var p1 = ap.Inputs(60)                                  # 60 ticks of history
p1.directions(ap.KEY_W, ap.KEY_S, ap.KEY_A, ap.KEY_D)   # the d-pad and left stick count too
p1.button("punch", ap.KEY_J, ap.PAD_WEST)
p1.button("kick", ap.KEY_K, ap.PAD_SOUTH)
while ap.frame() do
    while ap.tick(60) do
        p1.face(1 if me.x < them.x else -1)
        p1.update()
        if p1.motion("236") and p1.buffered("punch") do
            p1.consume("punch")
            fireball()
        else if p1.double_tap("6") do
            dash()
        end
    end
end
```

| | |
|---|---|
| `update()` | read the devices for this tick (once per tick) |
| `direction(ago = 0)`, `holding("4")` | numpad direction; whether one is held ("4" counts 1, 4, 7) |
| `held(name)`, `pressed(name)`, `released(name)` | a button this tick |
| `buffered(name, window = 5)`, `consume(name)` | a press within the last ticks that has not been used, so a press slightly early still counts; use it up |
| `motion("236", window = 15)` | a motion, in order, with other directions allowed between: `"236"`, `"214"`, `"623"`, `"41236"`, ... |
| `charged("4", ticks = 45, grace = 8)` | held back (or down) long enough, released no more than `grace` ticks ago |
| `double_tap("6", window = 12)` | the second tap of a dash, on its tick |
| `held_for("2")` | ticks in a row a direction has been held |
| `code()`, `feed(direction, buttons)`, `feed_code(code)` | one tick as a number, for netcode and replays: send `code()`, and `feed_code` the other side's instead of `update()` |
| `lines(count)`, `draw(x, y)` | the history as text, newest first, and an input display |
| `face(facing)`, `clear()` | which way the player faces; forget the history (a new round) |

Left and right together cancel; up wins over down. Up to 16 buttons.
## Drawing in 2D

Positions are in pixels from the top left; angles are degrees, clockwise.

| | |
|---|---|
| `clear(color)` | fill the drawing area |
| `rect(x, y, w, h, color)`, `fill(rect, color)`, `rect_lines(x, y, w, h, thickness, color)`, `outline(rect, thickness, color)` | rectangles |
| `gradient(x, y, w, h, top, bottom)`, `gradient_across(…, left, right)` | shaded rectangles |
| `line(x1, y1, x2, y2, thickness, color)`, `segment(from, to, thickness, color)` | lines |
| `circle(x, y, radius, color)`, `circle_lines(…, thickness, color)`, `ellipse`, `sector`, `arc` | round things |
| `triangle(a, b, c, color)`, `polygon(points, color)`, `path(points, thickness, color, closed)` | polygons |
| `push()`, `translate(x, y)`, `rotate(degrees)`, `scale(x, y)`, `pop()` | transform what is drawn until `pop()` |
| `begin_camera(camera)`, `end_camera()`, `camera_at(target, zoom)`, `to_world(camera, point)`, `to_screen(camera, point)` | a scrolling, zooming, rotating view |
| `blend(BLEND_ADD)`, `clip(rect)`, `unclip()` | glows; drawing limited to a rectangle |

Shapes and sprites are batched: thousands of them per frame are one or a few draw calls.

### Textures and sprites

```flywheel
var ship = ap.load_texture("ship.png", false)        # false keeps pixel art crisp
ap.sprite(ship, 100, 80)                             # top left at (100, 80)
ap.sprite_at(ship, position, angle, 2)               # centered, rotated, twice the size
ap.sprite_part(sheet, sheet.cell(3, 16, 16), ap.Rect(x, y, 32, 32))   # one cell of a sheet
```

`make_texture(width, height, function(int x, int y) returns Color …)` paints a texture in
code. Textures have `width`, `height`, `size()`, `bounds()`, `cell(index, w, h)`,
`smooth(on)`, `repeat(on)`, and `free()` (they are also freed when no longer referenced).

`Animation(sheet, cell_w, cell_h, frames, rate)` plays cells of a sheet (`update()`,
`draw(position, scale, flip)`), and `Tilemap(columns, rows, tile_size)` holds a grid of tiles
with `get`, `set`, `at(position)`, `blocked(rect)`, `slide(body, motion)` for movement that
stops at walls, and `draw(sheet, cell_size)`.

### Text

`text(content, x, y, size = 16, color = WHITE, font = null)` draws with the built-in 5x7
pixel font (with descenders on g j p q y), which needs no file. `text_centered` and `measure` do what they say.
`import aperture.ttf` adds `ttf.load_font("font.ttf", 48)` for TrueType and OpenType fonts,
and `Font.from_grid(texture, cell_w, cell_h)` makes a font from a sheet of characters. Fonts
cover ASCII and Latin-1.

### Canvases and shaders

A `Canvas(width, height)` is an off-screen drawing area: `draw_to(canvas)` … `draw_to_window()`,
then draw `canvas.texture` like any other. Draw a small canvas scaled up for a pixel-perfect
low-resolution game, or run the whole frame through a shader:

```flywheel
var waves = ap.Shader("vec4 effect(vec4 color, sampler2D image, vec2 uv) { uv.x += sin(uv.y * 40.0 + u_time * 4.0) * 0.01; return texture(image, uv) * color; }")
ap.use_shader(waves)
ap.sprite(canvas.texture, 0, 0)
ap.no_shader()
```

A shader defines `effect` in GLSL; `u_time` and `u_resolution` are provided, and its own
uniforms are set with `set`, `set_vec2`, `set_vec3`, and `set_color`. (In Flywheel strings a
brace with a space beside it is a literal brace.)

`screenshot("shot.png")` saves the drawing area and `read_pixel(x, y)` reads one pixel.

## Drawing in 3D

Y is up, and one unit is whatever you decide. 3D drawing happens between `begin_3d(camera)`
and `end_3d()`; 2D drawing after it lands on top, for the HUD.

```flywheel
var crate = ap.cube(1, 1, 1)
var camera = ap.Camera3D(ap.Vec3(4, 3, 6), ap.Vec3(0, 0.5, 0))
ap.sun(ap.Vec3(-0.5, -1, -0.3))

while ap.frame() do
    ap.clear(ap.SKY)
    ap.begin_3d(camera)
    ap.grid(10, 1)
    ap.draw(crate, ap.Vec3(0, 0.5, 0), ap.ORANGE, ap.Vec3(0, ap.time() * 45, 0))
    ap.end_3d()
    ap.text("{int(ap.fps())} fps", 10, 10)
end
```

| | |
|---|---|
| `Camera3D(position, target, up, fov = 60, near, far)` | a view; `fov <= 0` is a flat (orthographic) view `-fov` units tall. `orbit(target, yaw, pitch, distance)` and `look(position, yaw, pitch)` build cameras for orbiting and first-person views |
| `cube`, `sphere`, `plane`, `cylinder`, `cone`, `torus` | built-in meshes |
| `load_mesh("model.obj")` | the geometry of a Wavefront OBJ file as one mesh (`load_model` keeps its materials) |
| `MeshBuilder()` | your own meshes: `vertex(position, normal, uv)`, `triangle`, `quad`, `color`, `smooth`, `build` |
| `draw(mesh, position, tint, rotation, size, texture, material)` | draw a mesh; rotation is degrees around x, y, z |
| `draw_material(mesh, material, position, rotation, size, tint)` | draw a mesh with a material |
| `draw_many(mesh, transforms, material, colors, tint, texture)`, `transform(position, rotation, size)` | instancing: one call draws a mesh at every `Mat4` in a list, each tinted by its own color when `colors` has one per transform |
| `push_3d()`, `translate_3d`, `rotate_3d(degrees, axis)`, `scale_3d`, `pop_3d()` | nested transforms |
| `ambient(color)`, `sun(direction, color)`, `light(index, position, color, range)`, `light_off(index)`, `lighting(on)`, `shine(strength, tightness)` | lights: ambient, one directional, eight point lights, highlights |
| `fog(color, start, finish)`, `no_fog()` | distance fog |
| `line_3d`, `grid`, `box_lines`, `billboard(texture, position, height, tint)` | lines and camera-facing sprites |
| `wireframe(on)`, `cull(on)`, `depth_write(on)` | render state |
| `project(point)`, `visible(point)`, `screen_ray(position)` | where a point appears on screen; the ray under the mouse |
| `Ray.hits_sphere`, `hits_box`, `hits_plane`; `boxes_overlap`, `spheres_overlap` | picking and simple overlap tests (see [Collision](#collision) for levels) |

### Models and animation

`load_model(path)` reads glTF 2.0 (`.gltf` with its buffers and images, or a single `.glb`)
and Wavefront OBJ with its MTL materials. A model keeps its node tree, meshes, materials, and
textures; glTF models also keep their skins (skeletal meshes, up to 256 joints and four per
vertex) and animations.

```flywheel
var hero = ap.load_model("hero.glb")
print(hero.animations())                   # ["Idle", "Walk", "Run"]

while ap.frame() do
    hero.play("Walk", ap.time())           # pose it as the clip is that far in
    hero.blend("Run", ap.time(), speed)    # and mix another in (0 to 1)
    ap.begin_3d(camera)
    ap.draw_model(hero, position, ap.WHITE, ap.Vec3(0, heading, 0))
    var hand = hero.node_transform("hand_r")
    ap.draw(sword, position + hand.transform_point(ap.Vec3(0, 0, 0)))
    ap.end_3d()
end
```

| | |
|---|---|
| `draw_model(model, position, tint, rotation, size, material)` | draw in the current pose; a material replaces the model's own |
| `draw_model_many(model, transforms, colors, tint, material)` | instancing, all in the same pose |
| `play(name, time, loop = true)`, `blend(name, time, weight, loop)`, `rest()` | pose from animations: linear, step, and cubic spline keys for translation, rotation, and scale |
| `animations()`, `has_animation(name)`, `length(name)` | its clips |
| `nodes()`, `node_transform(name)`, `node_position(name)`, `node_rotation(name)` | its parts and bones, in the current pose, for attaching things |
| `turn_node(name, quat)`, `move_node(name, position)` | pose a node by hand after `play`: a head that looks at the player |
| `materials()`, `triangles()`, `low()`, `high()`, `size()` | its materials (to change) and its size |

glTF features read: every accessor type (normalized and sparse included), interleaved
buffers, data URIs, triangle strips and fans, node matrices or translation, rotation, and
scale, the default scene, metallic-roughness materials with all five texture maps, samplers,
alpha modes, double-sided materials, `KHR_materials_emissive_strength`, and
`KHR_materials_unlit`. Not read: morph targets, cameras, lights, texture coordinate sets
other than the first, compressed meshes (Draco, meshopt), and KTX2/WebP textures. A file
that requires an extension that is not read fails to load with its name.

### Materials

A `Material` lights a surface physically (metallic-roughness, in linear color): a base color
and texture, `metallic` 0 to 1, `roughness` 0 to 1, and optional maps.

```flywheel
var gold = ap.Material(ap.hex("#ffc850"), 1, 0.3)            # color, metallic, roughness
var bricks = ap.Material(ap.WHITE, 0, 0.85, ap.load_texture("bricks.jpg"))
bricks.normal_map(ap.load_texture("bricks_normal.png"))
ap.draw_material(ap.cube(), bricks, ap.Vec3(0, 0.5, 0))
```

| | |
|---|---|
| `color(c)`, `surface(metallic, roughness)`, `texture(t)` | the base |
| `normal_map(t, strength)` | tangent-space normal map (OpenGL convention, green up); tangents come from the model or are computed |
| `surface_map(t)` | roughness in green, metallic in blue (glTF's layout) |
| `glow(color, strength)`, `glow_map(t)` | light of its own |
| `occlusion_map(t)` | ambient occlusion in red |
| `cutoff(alpha)`, `transparent(on)`, `two_sided(on)`, `unlit(on)` | cut-outs, see-through surfaces (drawn after opaque ones), back faces, no lighting |

Meshes drawn without a material keep the simpler lighting (`shine` sets their highlights).
Materials from MTL files use `Kd`, `d`/`Tr`, `Ns` (as roughness), `Ke`, the PBR extension's
`Pr` and `Pm`, and the `map_Kd`, `map_Bump`/`norm`, `map_Ke`, and `map_Pr` textures.

### Shadows

The sun casts shadows through a shadow map. `scene_3d(camera, scene, radius)` draws a scene
function twice, into the shadow map around the camera's target and then through the camera:

```flywheel
ap.shadows(true)                            # resolution 2048, strength 0.75
while ap.frame() do
    ap.clear(ap.SKY)
    ap.scene_3d(camera, function()
        if not ap.casting_shadows() do      # the ground casts none
            ap.draw(ground, ap.Vec3(0, 0, 0))
        end
        ap.draw_model(hero, position)
    end, 20)
end
```

`begin_shadows(center, radius)` ... `end_shadows()` draws the shadow map by hand, before
`begin_3d`. Shadows fall within `radius` of the center: smaller is sharper. Lines and
billboards cast no shadows, and cut-out materials leave holes in them.

## Collision

A `World` holds solid, unmoving geometry: boxes, meshes, models, and loose triangles, in a
bounding volume hierarchy, so tests stay cheap however big the level is. A `Body` walks
through it.

```flywheel
var level = ap.World()
var chapel = ap.load_model("chapel.glb")
level.add_model(chapel)                                       # every triangle, as drawn
level.add_box(ap.Vec3(-20, -1, -20), ap.Vec3(20, 0, 20))      # a floor
var door = level.add_box(ap.Vec3(2, 0, -0.1), ap.Vec3(3, 2.2, 0.1))

var player = ap.Body(ap.Vec3(0, 0, 0))                        # feet at the origin
while ap.frame() do
    while ap.tick(60) do
        var wish = ...                                        # from input, along the ground
        player.velocity = ap.Vec3(wish.x * 6, player.velocity.y, wish.z * 6)
        if player.grounded and jumped do
            player.velocity.y = 7
        end
        player.update(level)                                  # gravity, then move and slide
    end
    ...
end
```

| `World` | |
|---|---|
| `add_box(low, high, group)`, `add_cube(center, size, rotation, group)` | boxes along the axes, or turned |
| `add_mesh(mesh, position, rotation, size, group)`, `add_model(model, position, rotation, size, group)` | triangles placed as `draw` and `draw_model` place them (a model in its current pose) |
| `add_triangles(points, group)` | three `Vec3` per triangle |
| `group()`, `enable(group, on)`, `remove(group)`, `clear()`, `triangles()` | each add makes a group unless given one; switch a group off (an open door) or take it out |
| `raycast(ray, reach)` | a `Hit`: `found`, `distance`, `point`, `normal` (facing the ray), `group` |
| `sees(from, to)`, `overlaps(center, radius)` | line of sight; whether a sphere touches anything |
| `sweep(from, to, radius)` | how far (0 to 1) a sphere gets before touching: keeps a following camera out of walls |
| `draw(color)`, `draw_debug(color)` | the triangles' edges (`draw_debug` only while debug drawing is on) |

A `Body` is an upright capsule standing on `position`: `radius` (0.35) wide and `height` (1.8)
tall. `move(world, motion)` slides it along walls, lets it stand still on slopes up to `slope`
degrees (50) instead of sliding down, climbs stairs and ledges up to `step` (0.3) high
smoothly, and keeps it on the ground going down slopes and stairs up to `snap` (0.3) below
instead of launching off them; it does none of that while jumping (moving up). After a move,
`grounded`, `ground_normal`, `ground_group`, `hit_wall`, `wall_normal`, and `hit_ceiling` say
what it touched. `update(world, gravity = 20)` adds gravity to `velocity`, moves by it for
`delta()`, and takes out what went into the ground, a ceiling, or a wall. `center()` and
`top()` give points on it, `overlaps(other)` tests two bodies, and `push_apart(world, other,
this_one_stays)` separates them side to side without pushing either into a wall, so fighters
and crowds cannot walk through each other.

Boxes and triangles need no window, so a game server can build the same `World` as the
players. Meshes and models are read back from the graphics card when added; a skinned model
collides in its bind pose. Worlds are freed when no longer referenced, or by `free()`.

## Sound

```flywheel
var jump = ap.load_sound("jump.wav")
var blip = ap.tone(ap.WAVE_SQUARE, 880, 0.1)             # no file needed
var voice = jump.play(0.8, 1.2)                          # volume, pitch
var music = ap.load_sound("theme.wav").loop(0.5)
ap.stop(music)
```

`tone(wave, frequency, seconds, slide_to, volume)` synthesizes effects (`WAVE_SINE`,
`WAVE_SQUARE`, `WAVE_SAW`, `WAVE_TRIANGLE`, `WAVE_NOISE`). Up to 32 sounds play at once;
`set_volume`, `set_pitch`, `pause`, `playing`, `stop_all`, and `master_volume` control them.
Sound files are WAV or Ogg Vorbis (`.ogg`).

Music streams from its file instead of loading whole: an Ogg track is decoded a little at a
time on the audio thread, so a long track costs almost no memory and keeps playing while a
frame takes long.

```flywheel
ap.play_music("theme.ogg", 0.6)            # volume; loops unless you pass false
ap.music_volume(0.3)
ap.pause_music()
ap.pause_music(false)
print ap.music_position(), ap.music_length()
ap.seek_music(30)
ap.stop_music()
```

One track plays at a time; `play_music` replaces it. `music_playing()` turns false after a
track that does not loop ends.

### Sound in space

`play_at` and `loop_at` put a sound at a point, heard from a listener: louder in the ear on
its side (never only one ear), quieter and muffled behind so front and back can be told
apart, full volume within `near` and falling off with distance beyond it, and silent from
`far` on (0, the default, is never silent).

```flywheel
var step = ap.load_sound("footstep.ogg")
var fire = ap.load_sound("fire.ogg").loop_at(ap.Vec3(4, 0, -2), 0.8, 1, 2, 40)   # volume, pitch, near, far
while ap.frame() do
    ap.listen_from(camera)                     # or ap.listener(head, facing)
    ap.move_voice(fire, torch.position)        # it follows what makes it
    if landed do
        step.play_at(enemy.position)
    end
end
```

The listener can move and turn each frame and every sound follows at once, gliding over about
10 ms so nothing clicks. Stereo files are mixed to mono to be placed. `ears(voice)` tells how
a voice is heard (x and y the ear gains, z how muffled, 0 in front to 1 behind), and
`in_cone(origin, forward, point, degrees)` whether a point is within a cone of view (along
the ground unless `flat` is false), for checks such as "is the player facing the sound".
`set_volume`, `set_pitch`, `pause`, and `stop` work on voices in space too.

## Frame data

Hitboxes, hurtboxes, cancel windows, and other per-frame facts of a move live in a TOML (or
JSON) file next to the game, or in a glTF model's animations. Frames count from 1, one per
tick.

```toml
[jab]
frames = 18
damage = 30                                    # any number: move.value("damage")
hitbox = [{ from = 5, to = 7, box = [20, -60, 40, 20], hitstun = 12, hitstop = 6 }]
hurtbox = [{ from = 1, to = 18, box = [-20, -90, 40, 90] }]
cancel = [{ from = 6, to = 12, into = ["special"] }]
invincible = [1, 3]                            # a named window: first and last frame
markers = { sound = 5, step = [3, 9] }
```

```flywheel
var data = ap.load_frames("fighter.frames.toml")
var jab = data.move("jab")
if jab.active(frame) do ... end                          # a hitbox is out
for hit in jab.hitboxes(frame) do
    if hit.placed(me.position, me.facing).overlaps(target) do
        ap.hitstop(int(hit.value("hitstop", 6)))
    end
end
jab.can_cancel(frame, "special"); jab.in_window("invincible", frame); jab.at("sound", frame)
jab.draw_debug(frame, me.position, me.facing)            # boxes, when debug drawing is on
```

Boxes are x, y, width, height from the character's origin for a character facing right;
`placed(origin, facing)` puts one in the world, mirrored for facing left. Any list of tables
with a `box` is a box of that kind (`pushbox`, `throwbox`, ...), and the numbers given with a
box are its `value`s. A `Move` also has `boxes_at(frame, kind)`, `startup()`, `marker(name)`,
`next(frame)` and `done(frame)` (for looping and one-shot moves), and `seconds(frame)` for
syncing an animation.

The file is watched while the game runs: save it and the moves change on the next frame, in
place (a `Move` you hold sees the new numbers). A file with a mistake prints the error and
keeps the last good data.

`model.frame_data()` reads the same tables from a glTF model's animation `extras` (in Blender:
custom properties on the action, exported with Include > Custom Properties), one move per
animation, named after it.

## Debug drawing

`debug(on)` turns debug drawing on, and F1 toggles it during play (`debug_toggle_key` changes
the key). While it is on, `Move.draw_debug` shows boxes (hitboxes red, hurtboxes blue, others
green) with the move's frame number, `debug_rect`, `debug_point`, and `debug_line_3d` draw,
and a panel in the corner shows the frame rate, frame and tick numbers, hitstop, every
`debug_text(line)` of the frame, and input displays from `debug_inputs(label, inputs)`. When
it is off, all of these do nothing.

## Helpers

`lerp`, `approach`, `remap`, `smooth`, `ease_in`, `ease_out`, `pulse(period)`; `random(low,
high)`, `chance(probability)`, `random_in(rect)`, `random_direction()`; `circles_overlap`,
`circle_overlaps_rect`; `Timer(interval)` with `tick()`; and `Particles()` with `burst`,
`update`, and `draw`.

## Examples

```sh
flywheel examples/hello.fw        # a window, input, text
flywheel examples/sparkwell.fw    # a small arcade game
flywheel examples/platformer.fw   # a tile map, jumps with coyote time, a following camera
flywheel examples/shooter.fw      # a top-down shooter: waves, aiming, many bullets
flywheel examples/gallery.fw      # a lit 3D scene with picking
flywheel examples/showroom.fw     # shadows, materials, a forest of instances; add -- model.glb
flywheel examples/skyhop.fw       # a 3D platformer: islands, moving platforms, crystals to find
flywheel --hot examples/fighter.fw   # a fighting-game training stage: ticks, motions, frame data
flywheel examples/crypt.fw        # walking a 3D room with stairs and slopes; a sound to turn toward
```

`flywheel --new --game NAME` (or `pulley new NAME --game`) starts a project with Aperture as a
dependency and a small playable game.

To run a game unattended (tests, CI, thumbnails), `APERTURE_FRAMES=n` ends it after n frames
and `APERTURE_SCREENSHOT=path` saves the last one. Every frame of such a run counts as exactly
1/`target_fps` (or 1/60) seconds, however fast it really draws, so the run is the same every
time:

```sh
SDL_VIDEODRIVER=offscreen APERTURE_FRAMES=120 APERTURE_SCREENSHOT=shot.png flywheel game.fw
```

## Tests

```sh
flywheel --test tests/
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy flywheel --test tests/    # with no display
```

The tests open a hidden window, draw, and read the pixels back. `tests/models/` holds small
glTF, GLB, and OBJ files written by `tools/test_models.py`, and `tests/audio/beep.ogg` is half
a second of a 440 Hz tone.

## Notes

- One window at a time; drawing calls belong to the thread that opened it.
- A file in your project named like a module Aperture imports (`math.fw`, `gc.fw`, `fs.fw`,
  `json.fw`, `toml.fw`, `hot.fw`) takes its place, as for any Flywheel import; rename the file.
- `native/backend.c` is the renderer; `native/model.h` reads glTF and OBJ models;
  `native/stb_image.h` (v2.32, public domain) decodes images; `native/stb_vorbis.h`
  (stb_vorbis v1.24, public domain) decodes Ogg Vorbis; `native/font5x7.h` is generated
  by `tools/font.py`.
