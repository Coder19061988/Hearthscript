extends Node3D
## 3D view of the settlement: ground, buildings and blueprints, forest, fields, characters, sun, rain, snow.
## The simulation works in meters on a plane; SCALE converts them to scene units (x, z).
## Building meshes stand a little north of their point, so characters at the "door" stay visible.
## Every building has its own root node; it is rebuilt when the building changes (blueprint -> built).

const SCALE := 0.2
const BUILDING_OFFSET := Vector3(0.0, 0.0, -4.0)
const HOUSE_SIZE := Vector3(5.0, 3.2, 5.0)
const STORAGE_SIZE := Vector3(6.0, 4.0, 5.0)
const FOREST_TREES := 70
const FOREST_CLEARING := 3.0
const FOREST_RADIUS := 12.0
const FIELD_SIZE := 9.0
const FIELD_ROWS := 6
const FIELD_PLANTS := 8
const GRASS := Color(0.42, 0.58, 0.3)
const SNOW := Color(0.86, 0.89, 0.93)
const BLUEPRINT := Color(0.55, 0.78, 1.0, 0.28)
const PERSON_COLORS := [
	Color(0.85, 0.33, 0.3), Color(0.3, 0.55, 0.85), Color(0.95, 0.75, 0.25), Color(0.55, 0.35, 0.75),
	Color(0.3, 0.7, 0.55), Color(0.9, 0.5, 0.2), Color(0.6, 0.6, 0.6), Color(0.85, 0.45, 0.65),
]
const ACTION_SHORT := {
	"go_to": "іде", "go_home": "іде додому", "take": "бере інструмент", "put_back": "повертає інструмент",
	"chop_wood": "рубає", "drop": "складає", "pick_up": "бере зі складу", "eat": "їсть",
	"sleep": "спить", "rest": "відпочиває", "wait": "чекає", "heat": "топить піч",
	"wear": "переодягається", "sow": "сіє", "harvest": "жне", "grind": "меле", "bake": "пече",
	"deliver": "несе на будову", "build": "будує", "move_into": "переселяється",
}
const CARGO_COLORS := {"wood": Color(0.62, 0.42, 0.22), "grain": Color(0.9, 0.78, 0.4),
	"flour": Color(0.95, 0.93, 0.85), "bread": Color(0.75, 0.5, 0.25)}
const RESOURCE_NAMES := {"wood": "дрова", "bread": "хліб", "grain": "зерно", "flour": "борошно"}

var _env: Environment
var _sun: DirectionalLight3D
var _ground_material: StandardMaterial3D
var _precipitation: CPUParticles3D
var _precipitation_mode := ""
var _materials := {}
var _buildings := {}    # building id -> {key, root, label, ...kind-specific nodes}
var _people := {}       # character id -> {root, body, cargo, tool, hat, ring, label (2D tag)}
var _tags: CanvasLayer  # name tags of characters, drawn over the 3D view and under the UI
var _ghost: MeshInstance3D


func _ready() -> void:
	_env = Environment.new()
	_env.background_mode = Environment.BG_COLOR
	_env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	_env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	var world_env := WorldEnvironment.new()
	world_env.environment = _env
	add_child(world_env)

	_tags = CanvasLayer.new()
	_tags.layer = 0
	add_child(_tags)

	_sun = DirectionalLight3D.new()
	_sun.shadow_enabled = true
	add_child(_sun)

	var ground := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(300.0, 300.0)
	ground.mesh = plane
	_ground_material = StandardMaterial3D.new()
	_ground_material.albedo_color = GRASS
	ground.material_override = _ground_material
	ground.position = Vector3(6.0, 0.0, 20.0)
	add_child(ground)

	_precipitation = CPUParticles3D.new()
	_precipitation.amount = 1500
	_precipitation.emission_shape = CPUParticles3D.EMISSION_SHAPE_BOX
	_precipitation.emission_box_extents = Vector3(70.0, 0.5, 70.0)
	_precipitation.direction = Vector3(0.0, -1.0, 0.0)
	_precipitation.position = Vector3(6.0, 35.0, 20.0)
	_precipitation.emitting = false
	add_child(_precipitation)

	_ghost = MeshInstance3D.new()
	_ghost.mesh = BoxMesh.new()
	_ghost.visible = false
	add_child(_ghost)


## Forgets everything and builds the view of a new world (characters appear on the first update).
func build(buildings: Array) -> void:
	for entry in _buildings.values():
		entry["root"].queue_free()
	_buildings.clear()
	for p in _people.values():
		p["root"].queue_free()
		p["label"].queue_free()
	_people.clear()
	_sync_buildings(buildings)


## Called every frame with fresh state from the simulation.
func update_world(buildings: Array, characters: Array, time_of_day: float, raining: bool, winter: bool,
		selected_id: int, camera: Camera3D) -> void:
	_update_sky(time_of_day, raining, winter)
	_sync_buildings(buildings)
	for b in buildings:
		var entry: Dictionary = _buildings[b["id"]]
		if not b.get("built", true):
			_update_blueprint(entry, b)
			continue
		match b["kind"]:
			"storage":
				_update_storage(entry, b)
			"house":
				_update_house(entry, b)
			"forest":
				_set_text(entry["label"], "%s\nдерев: %d" % [b["name"], b["trees_left"]])
				var trees: Array = entry["trees"]
				var visible_count := int(round(trees.size() * float(b["trees_left"]) / maxf(1.0, b["trees_total"])))
				for i in trees.size():
					trees[i].visible = i < visible_count
			"field":
				_update_field(entry, b)
	var groups := {}  # rounded position -> number of characters already placed there
	for i in characters.size():
		var c: Dictionary = characters[i]
		var key: Vector2i = Vector2i((c["position"] as Vector2).round())
		var slot: int = groups.get(key, 0)
		groups[key] = slot + 1
		_update_person(c, i, slot, selected_id, camera)


## Character id under the mouse (screen distance from the figure), or -1.
func pick_character(camera: Camera3D, screen_pos: Vector2) -> int:
	var best := -1
	var best_dist := 40.0
	for id in _people:
		var point: Vector3 = _people[id]["root"].position + Vector3(0, 1.0, 0)
		if camera.is_position_behind(point):
			continue
		var d := camera.unproject_position(point).distance_to(screen_pos)
		if d < best_dist:
			best_dist = d
			best = id
	return best


## Point on the ground under the mouse, in simulation meters (null if the ray misses the ground).
func ground_point(camera: Camera3D, screen_pos: Vector2) -> Variant:
	var from := camera.project_ray_origin(screen_pos)
	var dir := camera.project_ray_normal(screen_pos)
	if absf(dir.y) < 0.0001:
		return null
	var t := -from.y / dir.y
	if t < 0.0:
		return null
	var p := from + dir * t
	return Vector2(p.x, p.z) / SCALE


## Translucent preview of a blueprint while the player chooses its place (green — fits, red — too close).
func show_ghost(kind: String, position_m: Vector2, valid: bool) -> void:
	var size := _footprint(kind)
	(_ghost.mesh as BoxMesh).size = size
	_ghost.position = _to_scene(position_m) + _offset(kind) + Vector3(0, size.y * 0.5, 0)
	_ghost.material_override = _material(Color(0.3, 0.9, 0.4, 0.4) if valid else Color(0.95, 0.3, 0.3, 0.4), true)
	_ghost.visible = true


func hide_ghost() -> void:
	_ghost.visible = false


# ---------------------------------------------------------------- buildings

func _sync_buildings(buildings: Array) -> void:
	for b in buildings:
		var key := "%s/%s" % [b["kind"], b.get("built", true)]
		var entry = _buildings.get(b["id"])
		if entry != null and entry["key"] == key:
			continue
		if entry != null:
			entry["root"].queue_free()
		_buildings[b["id"]] = _create_building(b, key)


func _create_building(b: Dictionary, key: String) -> Dictionary:
	var root := Node3D.new()
	add_child(root)
	var entry := {"key": key, "root": root}
	var kind: String = b["kind"]
	var base := _to_scene(b["position"])
	if not b.get("built", true):
		if kind == "field":
			_add_box(root, base + Vector3(0, 0.03, 0), Vector3(FIELD_SIZE, 0.06, FIELD_SIZE), BLUEPRINT, true)
			var ploughed := _add_box(root, base + Vector3(0, 0.05, 0), Vector3(FIELD_SIZE, 0.1, FIELD_SIZE),
				Color(0.45, 0.32, 0.2))
			entry["progress"] = ploughed
		else:
			var size := _footprint(kind)
			var center := base + BUILDING_OFFSET
			_add_box(root, center + Vector3(0, size.y * 0.5, 0), size, BLUEPRINT, true)
			entry["progress"] = _add_box(root, center, Vector3(size.x * 0.96, size.y, size.z * 0.96), Color(0.72, 0.55, 0.32))
		entry["label"] = _add_label(root, base + BUILDING_OFFSET * 2.0 + Vector3(0, 7.0, 0), b["name"])
		return entry
	match kind:
		"storage":
			_add_box(root, base + BUILDING_OFFSET + Vector3(0, 2.0, 0), STORAGE_SIZE, Color(0.55, 0.38, 0.22))
			_add_box(root, base + BUILDING_OFFSET + Vector3(0, 4.3, 0), Vector3(6.6, 0.6, 5.6), Color(0.4, 0.27, 0.16))
			entry["pile"] = _add_box(root, base + BUILDING_OFFSET + Vector3(4.5, 0.0, 0.0), Vector3(2.0, 1.0, 3.0),
				Color(0.62, 0.42, 0.22))
			entry["label"] = _add_label(root, base + BUILDING_OFFSET * 2.0 + Vector3(0, 8.0, 0), b["name"])
		"house":
			_add_box(root, base + BUILDING_OFFSET + Vector3(0, 1.6, 0), HOUSE_SIZE, Color(0.86, 0.78, 0.6))
			var roof := MeshInstance3D.new()
			var prism := PrismMesh.new()
			prism.size = Vector3(5.8, 2.2, 5.8)
			roof.mesh = prism
			roof.material_override = _material(Color(0.6, 0.26, 0.18))
			roof.position = base + BUILDING_OFFSET + Vector3(0, 4.3, 0)
			root.add_child(roof)
			_add_box(root, base + BUILDING_OFFSET + Vector3(1.4, 5.0, 1.0), Vector3(0.6, 1.6, 0.6), Color(0.45, 0.42, 0.4))
			entry["smoke"] = _add_smoke(root, base + BUILDING_OFFSET + Vector3(1.4, 6.0, 1.0))
			entry["label"] = _add_label(root, base + BUILDING_OFFSET * 2.0 + Vector3(0, 8.5, 0), b["name"])
		"forest":
			entry["trees"] = _plant_forest(root, base)
			# Above the far edge of the forest, so it does not cover the characters in the clearing.
			entry["label"] = _add_label(root, base + Vector3(-FOREST_RADIUS - 2.0, 4.0, 0), b["name"])
		"field":
			entry["plants"] = _make_field(root, base)
			entry["label"] = _add_label(root, base + Vector3(-FIELD_SIZE * 0.5 - 2.0, 3.0, 0), b["name"])
	return entry


func _update_blueprint(entry: Dictionary, b: Dictionary) -> void:
	var progress: float = b["progress"]
	var needs: Dictionary = b["needs"]
	var parts := []
	for res in needs:
		parts.append("%s %d" % [RESOURCE_NAMES.get(res, res), needs[res]])
	var status := "потрібно: " + ", ".join(parts) if not parts.is_empty() else "матеріали є"
	_set_text(entry["label"], "%s · креслення\n%s · збудовано %d%%" % [b["name"], status, int(progress * 100.0)])
	var frame: MeshInstance3D = entry["progress"]
	if b["kind"] == "field":
		frame.scale.x = maxf(progress, 0.001)
		frame.visible = progress > 0.0
	else:
		var height := (_footprint(b["kind"]) as Vector3).y
		frame.scale.y = maxf(progress, 0.001)
		frame.position.y = height * progress * 0.5
		frame.visible = progress > 0.0


func _update_storage(entry: Dictionary, b: Dictionary) -> void:
	var res: Dictionary = b["resources"]
	var tools: Dictionary = b["tools"]
	var wood: int = res.get("wood", 0)
	_set_text(entry["label"], "%s\nдрова %d · хліб %d · зерно %d · борошно %d\nсокир %d · серпів %d" % [
		b["name"], wood, res.get("bread", 0), res.get("grain", 0), res.get("flour", 0),
		tools.get("axe", 0), tools.get("sickle", 0)])
	var pile: MeshInstance3D = entry["pile"]
	pile.visible = wood > 0
	pile.scale.y = clampf(wood / 60.0, 0.2, 5.0)
	pile.position.y = pile.scale.y * 0.5


func _update_house(entry: Dictionary, b: Dictionary) -> void:
	var heated: bool = b["heated"]
	var state := "у печі %.1f дров" % b["firewood"] if heated else "піч холодна"
	var residents: Array = b["residents"]
	_set_text(entry["label"], "%s · мешканців %d/%d\n%+.0f°C · %s" % [
		b["name"], residents.size(), b["capacity"], b["temperature"], state])
	entry["smoke"].emitting = heated


func _update_field(entry: Dictionary, b: Dictionary) -> void:
	var state: String = b["state"]
	var growth: float = b["growth"]
	match state:
		"empty":
			_set_text(entry["label"], "%s\nпорожнє" % b["name"])
		"sown":
			_set_text(entry["label"], "%s\nросте: %d%%" % [b["name"], int(growth * 100.0)])
		"ripe":
			_set_text(entry["label"], "%s\nстигле: %d зерна" % [b["name"], b["grain_left"]])
	var plants: Array = entry["plants"]
	var standing := plants.size()
	if state == "ripe":
		standing = int(ceil(plants.size() * float(b["grain_left"]) / 80.0))
	var height := 0.0
	if state == "sown":
		height = lerpf(0.15, 1.2, growth)
	elif state == "ripe":
		height = 1.3
	var color := Color(0.85, 0.72, 0.3) if state == "ripe" else Color(0.35, 0.65, 0.25)
	for i in plants.size():
		var plant: MeshInstance3D = plants[i]
		plant.visible = height > 0.0 and i < standing
		plant.scale.y = maxf(height, 0.01)
		plant.position.y = height * 0.5
		plant.material_override = _material(color)


func _footprint(kind: String) -> Vector3:
	match kind:
		"house":
			return HOUSE_SIZE
		"storage":
			return STORAGE_SIZE
		_:
			return Vector3(FIELD_SIZE, 0.2, FIELD_SIZE)


func _offset(kind: String) -> Vector3:
	return Vector3.ZERO if kind == "field" else BUILDING_OFFSET


# ---------------------------------------------------------------- characters

func _update_person(c: Dictionary, index: int, slot: int, selected_id: int, camera: Camera3D) -> void:
	var id: int = c["id"]
	if not _people.has(id):
		_people[id] = _make_person()
	var p: Dictionary = _people[id]
	var root: Node3D = p["root"]
	# Characters standing at the same point are spread around it; their labels are stacked.
	var angle := slot * 2.4
	var radius := 0.0 if slot == 0 else 1.6
	root.position = _to_scene(c["position"]) + Vector3(cos(angle), 0.0, sin(angle)) * radius

	var alive: bool = c["alive"]
	var body: MeshInstance3D = p["body"]
	body.rotation_degrees.x = 0.0 if alive else 90.0
	body.position.y = 0.9 if alive else 0.45
	body.material_override = _material(PERSON_COLORS[index % PERSON_COLORS.size()] if alive else Color(0.35, 0.35, 0.35))
	var carrying: Dictionary = c["carrying"]
	var main_res := ""
	var units := 0
	for res in carrying:
		if carrying[res] > units:
			units = carrying[res]
			main_res = res
	p["cargo"].visible = alive and units > 0
	if units > 0:
		p["cargo"].scale = Vector3.ONE * (0.6 + minf(units, 20) * 0.04)
		p["cargo"].material_override = _material(CARGO_COLORS.get(main_res, Color(0.6, 0.5, 0.4)))
	var tool: String = c["tool"]
	p["tool"].visible = alive and tool != ""
	p["tool"].material_override = _material(Color(0.85, 0.75, 0.3) if tool == "sickle" else Color(0.75, 0.75, 0.8))
	p["hat"].visible = alive and c["clothes"] == "warm_clothes"
	p["ring"].visible = id == selected_id

	var status := "смерть" if not alive else _status_text(c)
	var tag: Label = p["label"]
	var text := "%s · %s" % [c["name"], status]
	if tag.text != text:
		tag.text = text
	tag.modulate = Color(1.0, 0.88, 0.35) if id == selected_id else Color.WHITE
	var head := root.position + Vector3(0.0, 2.4, 0.0)
	tag.visible = not camera.is_position_behind(head)
	if tag.visible:
		tag.size = tag.get_minimum_size()
		tag.position = camera.unproject_position(head) - Vector2(tag.size.x * 0.5, tag.size.y + slot * 20.0)


func _status_text(c: Dictionary) -> String:
	var action: String = c["action"]
	if action != "":
		return ACTION_SHORT.get(action, action)
	match c["state"]:
		"running":
			return "думає"
		"finished":
			return "без діла"
		"crashed":
			return "помилка в коді"
		_:
			return "без коду"


func _make_person() -> Dictionary:
	var root := Node3D.new()
	add_child(root)
	var body := MeshInstance3D.new()
	var capsule := CapsuleMesh.new()
	capsule.radius = 0.45
	capsule.height = 1.8
	body.mesh = capsule
	body.position.y = 0.9
	root.add_child(body)

	var cargo := MeshInstance3D.new()
	var box := BoxMesh.new()
	box.size = Vector3(0.9, 0.45, 0.45)
	cargo.mesh = box
	cargo.position = Vector3(0.0, 2.2, 0.0)
	root.add_child(cargo)

	var tool := MeshInstance3D.new()
	var handle := BoxMesh.new()
	handle.size = Vector3(0.12, 1.0, 0.12)
	tool.mesh = handle
	tool.position = Vector3(0.6, 1.0, 0.0)
	tool.rotation_degrees.z = 25.0
	root.add_child(tool)

	var hat := MeshInstance3D.new()
	var sphere := SphereMesh.new()
	sphere.radius = 0.38
	sphere.height = 0.5
	hat.mesh = sphere
	hat.material_override = _material(Color(0.42, 0.3, 0.2))
	hat.position = Vector3(0.0, 1.85, 0.0)
	root.add_child(hat)

	var ring := MeshInstance3D.new()
	var torus := TorusMesh.new()
	torus.inner_radius = 0.75
	torus.outer_radius = 1.0
	ring.mesh = torus
	ring.material_override = _material(Color(1.0, 0.85, 0.2))
	ring.position.y = 0.05
	root.add_child(ring)

	var label := Label.new()
	label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	label.add_theme_font_size_override("font_size", 15)
	label.add_theme_constant_override("outline_size", 6)
	label.add_theme_color_override("font_outline_color", Color(0.0, 0.0, 0.0, 0.9))
	_tags.add_child(label)
	return {"root": root, "body": body, "cargo": cargo, "tool": tool, "hat": hat, "ring": ring, "label": label}


# ---------------------------------------------------------------- scenery

func _plant_forest(parent: Node3D, center: Vector3) -> Array:
	var rng := RandomNumberGenerator.new()
	rng.seed = 42
	var trees := []
	for i in FOREST_TREES:
		var angle := rng.randf() * TAU
		var radius := FOREST_CLEARING + sqrt(rng.randf()) * (FOREST_RADIUS - FOREST_CLEARING)
		var tree := Node3D.new()
		tree.position = center + Vector3(cos(angle) * radius, 0.0, sin(angle) * radius)
		var size := rng.randf_range(0.8, 1.3)
		var trunk := MeshInstance3D.new()
		var trunk_mesh := CylinderMesh.new()
		trunk_mesh.top_radius = 0.15 * size
		trunk_mesh.bottom_radius = 0.22 * size
		trunk_mesh.height = 1.4 * size
		trunk.mesh = trunk_mesh
		trunk.material_override = _material(Color(0.4, 0.27, 0.15))
		trunk.position.y = 0.7 * size
		tree.add_child(trunk)
		var crown := MeshInstance3D.new()
		var crown_mesh := CylinderMesh.new()
		crown_mesh.top_radius = 0.0
		crown_mesh.bottom_radius = 1.1 * size
		crown_mesh.height = 3.2 * size
		crown.mesh = crown_mesh
		crown.material_override = _material(Color(0.16, 0.38, 0.2))
		crown.position.y = (1.4 + 1.5) * size
		tree.add_child(crown)
		parent.add_child(tree)
		trees.append(tree)
	return trees


## A ploughed plot with rows of plants; the plants grow with the field.
func _make_field(parent: Node3D, center: Vector3) -> Array:
	_add_box(parent, center + Vector3(0, 0.05, 0), Vector3(FIELD_SIZE, 0.1, FIELD_SIZE), Color(0.45, 0.32, 0.2))
	var plants := []
	var step_x := FIELD_SIZE / FIELD_ROWS
	var step_z := FIELD_SIZE / FIELD_PLANTS
	for row in FIELD_ROWS:
		for k in FIELD_PLANTS:
			var plant := MeshInstance3D.new()
			var stalk := BoxMesh.new()
			stalk.size = Vector3(0.35, 1.0, 0.35)
			plant.mesh = stalk
			plant.position = center + Vector3((row + 0.5) * step_x - FIELD_SIZE * 0.5, 0.0,
				(k + 0.5) * step_z - FIELD_SIZE * 0.5)
			plant.visible = false
			parent.add_child(plant)
			plants.append(plant)
	return plants


func _add_smoke(parent: Node3D, pos: Vector3) -> CPUParticles3D:
	var smoke := CPUParticles3D.new()
	smoke.amount = 24
	smoke.lifetime = 3.0
	smoke.direction = Vector3(0.2, 1.0, 0.0)
	smoke.spread = 12.0
	smoke.gravity = Vector3.ZERO
	smoke.initial_velocity_min = 1.0
	smoke.initial_velocity_max = 1.6
	smoke.scale_amount_min = 0.6
	smoke.scale_amount_max = 1.2
	var puff := SphereMesh.new()
	puff.radius = 0.35
	puff.height = 0.7
	puff.material = _material(Color(0.75, 0.75, 0.75, 0.55), true)
	smoke.mesh = puff
	smoke.position = pos
	smoke.emitting = false
	parent.add_child(smoke)
	return smoke


func _update_sky(time_of_day: float, raining: bool, winter: bool) -> void:
	var hour := time_of_day / 3600.0
	var elevation := sin((hour - 6.0) / 12.0 * PI) * 65.0  # degrees above the horizon
	var day := clampf((elevation + 8.0) / 20.0, 0.0, 1.0)  # 0 — night, 1 — day, soft dusk
	_sun.rotation_degrees = Vector3(-maxf(elevation, 10.0), -30.0 + (hour - 12.0) * 12.0, 0.0)
	_sun.light_energy = lerpf(0.18, 1.1, day) * (0.55 if raining else 1.0)
	_sun.light_color = Color(0.6, 0.7, 1.0).lerp(Color(1.0, 0.95, 0.86), day)
	var sky_day := Color(0.45, 0.5, 0.56) if raining else (Color(0.7, 0.78, 0.86) if winter else Color(0.55, 0.72, 0.92))
	var sky := Color(0.03, 0.04, 0.09).lerp(sky_day, day)
	_env.background_color = sky
	_env.ambient_light_color = sky.lerp(Color.WHITE, 0.35)
	_env.ambient_light_energy = lerpf(0.4, 0.7, day)
	_ground_material.albedo_color = SNOW if winter else GRASS
	_set_precipitation("snow" if winter else ("rain" if raining else ""))


func _set_precipitation(mode: String) -> void:
	if mode == _precipitation_mode:
		return
	_precipitation_mode = mode
	_precipitation.emitting = mode != ""
	if mode == "rain":
		_precipitation.lifetime = 1.0
		_precipitation.spread = 4.0
		_precipitation.initial_velocity_min = 35.0
		_precipitation.initial_velocity_max = 40.0
		var drop := BoxMesh.new()
		drop.size = Vector3(0.05, 0.9, 0.05)
		drop.material = _material(Color(0.75, 0.82, 1.0, 0.55), true)
		_precipitation.mesh = drop
	elif mode == "snow":
		_precipitation.lifetime = 9.0
		_precipitation.spread = 25.0
		_precipitation.initial_velocity_min = 3.0
		_precipitation.initial_velocity_max = 4.5
		var flake := BoxMesh.new()
		flake.size = Vector3(0.18, 0.18, 0.18)
		flake.material = _material(Color(1.0, 1.0, 1.0, 0.9), true)
		_precipitation.mesh = flake


# ---------------------------------------------------------------- helpers

func _to_scene(p: Vector2) -> Vector3:
	return Vector3(p.x * SCALE, 0.0, p.y * SCALE)


func _add_box(parent: Node3D, pos: Vector3, size: Vector3, color: Color, transparent := false) -> MeshInstance3D:
	var mesh := MeshInstance3D.new()
	var box := BoxMesh.new()
	box.size = size
	mesh.mesh = box
	mesh.material_override = _material(color, transparent)
	mesh.position = pos
	parent.add_child(mesh)
	return mesh


func _add_label(parent: Node3D, pos: Vector3, text: String) -> Label3D:
	var label := Label3D.new()
	label.position = pos
	label.text = text
	label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	label.font_size = 64
	label.pixel_size = 0.02
	label.outline_size = 14
	label.outline_modulate = Color(0.0, 0.0, 0.0, 0.85)
	label.no_depth_test = true
	parent.add_child(label)
	return label


func _set_text(label: Label3D, text: String) -> void:
	if label.text != text:
		label.text = text


func _material(color: Color, transparent := false) -> StandardMaterial3D:
	var key := "%s/%s" % [color.to_html(), transparent]
	if not _materials.has(key):
		var m := StandardMaterial3D.new()
		m.albedo_color = color
		if transparent:
			m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
			m.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		_materials[key] = m
	return _materials[key]
