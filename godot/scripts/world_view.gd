extends Node3D
## 3D view of the settlement: ground, buildings and blueprints, forest, fields, characters, sun, rain, snow.
## The simulation works in meters on a plane; SCALE converts them to scene units (x, z).
## Building meshes stand a little north of their point, so characters at the "door" stay visible.
## Every building has its own root node; it is rebuilt when the building changes (blueprint -> built).
## Walls, wood piles and tree trunks are "solid" for the first-person observer (keep_walkable).

const SCALE := 0.2
const GROUND_CENTER := Vector3(6.0, 0.0, 20.0)
const GROUND_SIZE := 300.0
const BUILDING_OFFSET := Vector3(0.0, 0.0, -4.0)
const HOUSE_SIZE := Vector3(5.0, 3.2, 5.0)
const STORAGE_SIZE := Vector3(6.0, 4.0, 5.0)
const BOARD_SIZE := Vector3(3.0, 2.6, 0.5)
const BARN_SIZE := Vector3(7.0, 3.6, 5.0)
const TOWN_HALL_SIZE := Vector3(6.0, 4.6, 5.0)
const WORKSHOP_SIZE := Vector3(5.0, 3.0, 4.5)
## Walls of every workshop type; a smithy also gets a chimney and an anvil.
const WORKSHOP_COLORS := {"smithy": Color(0.42, 0.4, 0.4), "tailor": Color(0.75, 0.62, 0.7),
	"sawmill": Color(0.62, 0.48, 0.3), "carpentry": Color(0.7, 0.55, 0.35), "kiln": Color(0.5, 0.36, 0.3)}
const CRAFT_SHORT := {"flour": "меле", "bread": "пече", "axe": "кує", "sickle": "кує", "scythe": "кує",
	"cloth": "тче", "clothes": "шиє", "warm_clothes": "шиє", "planks": "пиляє", "bed": "столярує",
	"charcoal": "палить", "iron": "плавить", "pickaxe": "кує", "water": "набирає воду"}
const TRADING_POST_SIZE := Vector3(5.0, 2.6, 3.0)
const MEADOW_SIZE := 12.0
const DEPOSIT_SIZE := 6.0  # a quarry or a mine
## Animals stand in a row in front of their barn; a slot per animal. Their names are listed on the barn's label.
const ANIMAL_SPACING := 1.0
const FOREST_TREES := 70
const FOREST_CLEARING := 3.0
const FOREST_RADIUS := 12.0
## The observer bumps into a tree this far from its trunk (× tree size): close enough to walk through the forest.
const TREE_SOLID_RADIUS := 0.5
const LABEL_PIXEL_SIZE := 0.02
const FIRST_PERSON_LABEL_PIXEL_SIZE := 0.0005
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
	"wear": "переодягається", "sow": "сіє", "harvest": "жне", "craft": "працює",
	"deliver": "несе на будову", "build": "будує", "move_into": "переселяється",
	"post": "пише на дошці", "read": "читає дошку",
	"mine": "добуває", "water": "поливає", "mow": "косить", "feed": "годує", "milk": "доїть", "collect_eggs": "збирає яйця", "slaughter": "забиває",
	"order": "замовляє", "dismantle": "розбирає"}
const CARGO_COLORS := {"wood": Color(0.62, 0.42, 0.22), "grain": Color(0.9, 0.78, 0.4),
	"flour": Color(0.95, 0.93, 0.85), "bread": Color(0.75, 0.5, 0.25), "hay": Color(0.8, 0.75, 0.35),
	"milk": Color(0.97, 0.97, 0.95), "eggs": Color(0.95, 0.9, 0.78), "meat": Color(0.7, 0.25, 0.22)}
const RESOURCE_NAMES := {"wood": "дрова", "bread": "хліб", "grain": "зерно", "flour": "борошно",
	"hay": "сіно", "milk": "молоко", "eggs": "яйця", "meat": "м'ясо", "clothes": "одяг",
	"warm_clothes": "теплий одяг", "axe": "сокира", "sickle": "серп", "scythe": "коса", "cow": "корова",
	"chicken": "курка", "scrap": "брухт", "flax": "льон", "cloth": "тканина", "rags": "ганчір'я",
	"planks": "дошки", "bed": "ліжко", "pickaxe": "кайло", "stone": "камінь", "ore": "руда",
	"charcoal": "вугілля", "iron": "залізо", "water": "вода"}

var _env: Environment
var _sun: DirectionalLight3D
var _ground_material: StandardMaterial3D
var _precipitation: CPUParticles3D
var _precipitation_mode := ""
var _materials := {}
var _buildings := {}    # building id -> {key, root, label, solids, ...kind-specific nodes}
var _people := {}       # character id -> {root, body, cargo, tool, hat, ring, label (2D tag)}
var _animals := {}      # animal id -> {root, body}
var _tags: CanvasLayer  # name tags of characters, drawn over the 3D view and under the UI
var _ghost: MeshInstance3D
var _first_person := false


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
	plane.size = Vector2(GROUND_SIZE, GROUND_SIZE)
	ground.mesh = plane
	_ground_material = StandardMaterial3D.new()
	_ground_material.albedo_color = GRASS
	ground.material_override = _ground_material
	ground.position = GROUND_CENTER
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
	for a in _animals.values():
		a["root"].queue_free()
	_animals.clear()
	_sync_buildings(buildings)


## Called every frame with fresh state from the simulation.
func update_world(buildings: Array, characters: Array, animals: Array, time_of_day: float, raining: bool,
		winter: bool, selected_id: int, camera: Camera3D) -> void:
	_update_sky(time_of_day, raining, winter)
	_sync_buildings(buildings)
	var in_barn := _animal_lines(animals)
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
			"board":
				_set_text(entry["label"], "%s\nзаписів: %d" % [b["name"], b["notes"]])
				entry["paper"].visible = b["notes"] > 0
			"barn":
				var names: Array = in_barn.get(b["id"], [])
				_set_text(entry["label"], "%s · тварин %d/%d%s" % [b["name"], b["animals"], b["capacity"],
					"\n" + " · ".join(names) if not names.is_empty() else ""])
			"town_hall":
				_set_text(entry["label"], "%s · монет %d" % [b["name"], b["coins"]])
			"trading_post":
				_update_trading_post(entry, b)
			"deposit":
				_set_text(entry["label"], "%s\n%s: %d" % [b["name"], RESOURCE_NAMES.get(b["resource"], b["resource"]), b["left"]])
			"meadow":
				_set_text(entry["label"], "%s\nсіна: %d" % [b["name"], b["hay"]])
				var grass: MeshInstance3D = entry["grass"]
				grass.scale.y = maxf(0.05, float(b["hay"]) / maxf(1.0, b["hay_max"]))
				grass.position.y = grass.scale.y * 0.3
	_update_animals(animals)
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


## First person changes only how building labels are sized (see _size_label).
func set_first_person(on: bool) -> void:
	_first_person = on
	for entry in _buildings.values():
		if entry.has("label"):
			_size_label(entry["label"])


## The first-person observer (camera_rig.gd) at `pos` with `radius`: kept on the ground and moved out of walls,
## wood piles and trees. Blueprints before construction starts, fields, meadows, characters and animals
## let the observer through.
func keep_walkable(pos: Vector3, radius: float) -> Vector3:
	var half := GROUND_SIZE * 0.5 - radius
	var p := Vector2(clampf(pos.x, GROUND_CENTER.x - half, GROUND_CENTER.x + half),
		clampf(pos.z, GROUND_CENTER.z - half, GROUND_CENTER.z + half))
	for entry in _buildings.values():
		for solid in entry["solids"]:
			if not (solid["node"] as Node3D).is_visible_in_tree():
				continue  # a chopped tree, an empty wood pile, a blueprint not started yet
			if solid.has("rect"):
				p = _push_out_of_rect(p, solid["rect"], radius)
			else:
				p = _push_out_of_circle(p, solid["center"], solid["radius"] + radius)
	return Vector3(p.x, pos.y, p.y)


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
	var entry := {"key": key, "root": root, "solids": []}
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
			entry["progress"] = _block(entry, _add_box(root, center, Vector3(size.x * 0.96, size.y, size.z * 0.96),
				Color(0.72, 0.55, 0.32)))
		entry["label"] = _add_label(root, base + BUILDING_OFFSET * 2.0 + Vector3(0, 7.0, 0), b["name"])
		return entry
	match kind:
		"storage":
			_block(entry, _add_box(root, base + BUILDING_OFFSET + Vector3(0, 2.0, 0), STORAGE_SIZE, Color(0.55, 0.38, 0.22)))
			_add_box(root, base + BUILDING_OFFSET + Vector3(0, 4.3, 0), Vector3(6.6, 0.6, 5.6), Color(0.4, 0.27, 0.16))
			entry["pile"] = _block(entry, _add_box(root, base + BUILDING_OFFSET + Vector3(4.5, 0.0, 0.0),
				Vector3(2.0, 1.0, 3.0), Color(0.62, 0.42, 0.22)))
			entry["label"] = _add_label(root, base + BUILDING_OFFSET * 2.0 + Vector3(0, 8.0, 0), b["name"])
		"house":
			_block(entry, _add_box(root, base + BUILDING_OFFSET + Vector3(0, 1.6, 0), HOUSE_SIZE, Color(0.86, 0.78, 0.6)))
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
			entry["trees"] = _plant_forest(root, base, entry["solids"])
			# Above the far edge of the forest, so it does not cover the characters in the clearing.
			entry["label"] = _add_label(root, base + Vector3(-FOREST_RADIUS - 2.0, 4.0, 0), b["name"])
		"field":
			entry["plants"] = _make_field(root, base)
			entry["label"] = _add_label(root, base + Vector3(-FIELD_SIZE * 0.5 - 2.0, 3.0, 0), b["name"])
		"town_hall":
			_block(entry, _add_box(root, base + BUILDING_OFFSET + Vector3(0, TOWN_HALL_SIZE.y * 0.5, 0), TOWN_HALL_SIZE,
				Color(0.78, 0.74, 0.66)))
			_add_box(root, base + BUILDING_OFFSET + Vector3(0, TOWN_HALL_SIZE.y + 0.3, 0),
				Vector3(TOWN_HALL_SIZE.x + 0.4, 0.6, TOWN_HALL_SIZE.z + 0.4), Color(0.3, 0.32, 0.38))
			_add_box(root, base + BUILDING_OFFSET + Vector3(2.4, TOWN_HALL_SIZE.y + 1.8, 0), Vector3(0.12, 2.4, 0.12),
				Color(0.4, 0.3, 0.2))
			_add_box(root, base + BUILDING_OFFSET + Vector3(2.9, TOWN_HALL_SIZE.y + 2.6, 0), Vector3(0.9, 0.6, 0.05),
				Color(0.85, 0.25, 0.2))
			entry["label"] = _add_label(root, base + BUILDING_OFFSET + Vector3(0, TOWN_HALL_SIZE.y + 3.8, 0), b["name"])
		"workshop":
			var type: String = b["workshop"]
			_block(entry, _add_box(root, base + BUILDING_OFFSET + Vector3(0, WORKSHOP_SIZE.y * 0.5, 0), WORKSHOP_SIZE,
				WORKSHOP_COLORS.get(type, Color(0.6, 0.55, 0.5))))
			_add_box(root, base + BUILDING_OFFSET + Vector3(0, WORKSHOP_SIZE.y + 0.25, 0),
				Vector3(WORKSHOP_SIZE.x + 0.4, 0.5, WORKSHOP_SIZE.z + 0.4), Color(0.28, 0.26, 0.26))
			if type == "smithy" or type == "kiln":
				_add_box(root, base + BUILDING_OFFSET + Vector3(-1.6, WORKSHOP_SIZE.y + 1.2, 1.2), Vector3(0.8, 2.0, 0.8),
					Color(0.3, 0.3, 0.3))
			if type == "smithy":
				_add_box(root, base + BUILDING_OFFSET + Vector3(0, 0.5, WORKSHOP_SIZE.z * 0.5 + 0.6), Vector3(0.9, 1.0, 0.6),
					Color(0.2, 0.2, 0.22))  # the anvil at the door
			entry["label"] = _add_label(root, base + BUILDING_OFFSET + Vector3(0, WORKSHOP_SIZE.y + 3.0, 0), b["name"])
		"trading_post":
			var timber := Color(0.55, 0.38, 0.22)
			_block(entry, _add_box(root, base + BUILDING_OFFSET + Vector3(0, 0.5, 0),
				Vector3(TRADING_POST_SIZE.x, 1.0, TRADING_POST_SIZE.z), timber))
			for x in [-2.3, 2.3]:
				_add_box(root, base + BUILDING_OFFSET + Vector3(x, 1.3, -1.3), Vector3(0.2, 2.6, 0.2), timber)
			_add_box(root, base + BUILDING_OFFSET + Vector3(0, 2.7, -0.4), Vector3(TRADING_POST_SIZE.x + 0.6, 0.15, 2.6),
				Color(0.85, 0.75, 0.4))
			# The merchant's cart: shown while he stands at the post.
			var cart := Node3D.new()
			root.add_child(cart)
			_add_box(cart, base + BUILDING_OFFSET + Vector3(0, 0.9, 3.2), Vector3(3.2, 0.9, 1.8), Color(0.45, 0.3, 0.18))
			_add_box(cart, base + BUILDING_OFFSET + Vector3(0, 1.7, 3.2), Vector3(3.0, 0.8, 1.7), Color(0.92, 0.9, 0.82))
			cart.visible = false
			entry["cart"] = cart
			entry["label"] = _add_label(root, base + BUILDING_OFFSET + Vector3(0, 4.2, 0), b["name"])
		"barn":
			_block(entry, _add_box(root, base + BUILDING_OFFSET + Vector3(0, BARN_SIZE.y * 0.5, 0), BARN_SIZE,
				Color(0.55, 0.22, 0.17)))
			var roof := MeshInstance3D.new()
			var prism := PrismMesh.new()
			prism.size = Vector3(BARN_SIZE.x + 0.6, 2.0, BARN_SIZE.z + 0.6)
			roof.mesh = prism
			roof.material_override = _material(Color(0.35, 0.3, 0.28))
			roof.position = base + BUILDING_OFFSET + Vector3(0, BARN_SIZE.y + 1.0, 0)
			root.add_child(roof)
			entry["label"] = _add_label(root, base + BUILDING_OFFSET + Vector3(0, BARN_SIZE.y + 3.2, 0), b["name"])
		"deposit":
			# A pit with rocks: grey stone or rusty ore.
			var rock := Color(0.58, 0.57, 0.55) if b["resource"] == "stone" else Color(0.52, 0.32, 0.22)
			_add_box(root, base + Vector3(0, 0.02, 0), Vector3(DEPOSIT_SIZE, 0.04, DEPOSIT_SIZE), Color(0.5, 0.45, 0.38))
			for r in [[-1.6, -1.0, 1.6, 1.2], [1.4, -0.6, 1.2, 0.9], [0.0, 1.5, 1.8, 1.5], [-0.8, 0.6, 0.9, 0.7],
					[1.7, 1.6, 0.8, 0.6]]:
				_block(entry, _add_box(root, base + Vector3(r[0], r[3] * 0.5, r[1]), Vector3(r[2], r[3], r[2] * 0.8), rock))
			entry["label"] = _add_label(root, base + Vector3(-DEPOSIT_SIZE * 0.5 - 2.0, 3.0, 0), b["name"])
		"meadow":
			_add_box(root, base + Vector3(0, 0.02, 0), Vector3(MEADOW_SIZE, 0.04, MEADOW_SIZE), Color(0.5, 0.68, 0.3))
			entry["grass"] = _add_box(root, base + Vector3(0, 0.3, 0), Vector3(MEADOW_SIZE * 0.9, 0.6, MEADOW_SIZE * 0.9),
				Color(0.45, 0.72, 0.28))
			entry["label"] = _add_label(root, base + Vector3(-MEADOW_SIZE * 0.5 - 2.0, 2.5, 0), b["name"])
		"board":
			var wood := Color(0.5, 0.35, 0.2)
			for x in [-1.2, 1.2]:
				_block(entry, _add_box(root, base + BUILDING_OFFSET + Vector3(x, 1.3, 0), Vector3(0.25, 2.6, 0.25), wood))
			_block(entry, _add_box(root, base + BUILDING_OFFSET + Vector3(0, 1.8, 0.05), Vector3(2.8, 1.4, 0.15),
				Color(0.72, 0.56, 0.36)))
			entry["paper"] = _add_box(root, base + BUILDING_OFFSET + Vector3(0, 1.8, 0.15), Vector3(2.2, 1.0, 0.04),
				Color(0.95, 0.93, 0.85))
			entry["label"] = _add_label(root, base + BUILDING_OFFSET + Vector3(0, 4.0, 0), b["name"])
	return entry


## A trading post: what waits for the merchant, what he left, the orders; his cart while he is here.
func _update_trading_post(entry: Dictionary, b: Dictionary) -> void:
	var lines := [b["name"] + (" · купець тут" if b["merchant_here"] else "")]
	if not (b["for_sale"] as Dictionary).is_empty():
		lines.append("на продаж: " + _goods_text(b["for_sale"]))
	if not (b["received"] as Dictionary).is_empty():
		lines.append("куплене: " + _goods_text(b["received"]))
	var orders := []
	for o in b["orders"]:
		orders.append("%s ×%d" % [RESOURCE_NAMES.get(o[0], o[0]), o[1]])
	if not orders.is_empty():
		lines.append("замовлено: " + ", ".join(orders))
	_set_text(entry["label"], "\n".join(lines))
	entry["cart"].visible = b["merchant_here"]


func _goods_text(goods: Dictionary) -> String:
	var parts := []
	for item in goods:
		parts.append("%s %d" % [RESOURCE_NAMES.get(item, item), goods[item]])
	return " · ".join(parts)


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
	var broken: Dictionary = b["broken_tools"]
	var broken_count := 0
	for kind in broken:
		broken_count += broken[kind]
	var farm := []  # products of the meadow and the barn, when there are any
	for r in ["hay", "milk", "eggs", "meat", "scrap", "flax", "cloth", "rags", "planks", "bed", "stone", "ore", "charcoal",
			"iron"]:
		if res.get(r, 0) > 0:
			farm.append("%s %d" % [RESOURCE_NAMES[r], res[r]])
	_set_text(entry["label"], "%s\nдрова %d · хліб %d · зерно %d · борошно %d%s\nсокир %d · серпів %d · кіс %d%s%s" % [
		b["name"], wood, res.get("bread", 0), res.get("grain", 0), res.get("flour", 0),
		"\n" + " · ".join(farm) if not farm.is_empty() else "",
		tools.get("axe", 0), tools.get("sickle", 0), tools.get("scythe", 0),
		" · кайл %d" % tools["pickaxe"] if tools.get("pickaxe", 0) > 0 else "",
		" · зламаних %d" % broken_count if broken_count > 0 else ""])
	var pile: MeshInstance3D = entry["pile"]
	pile.visible = wood > 0
	pile.scale.y = clampf(wood / 60.0, 0.2, 5.0)
	pile.position.y = pile.scale.y * 0.5


func _update_house(entry: Dictionary, b: Dictionary) -> void:
	var heated: bool = b["heated"]
	var state := "у печі %.1f дров" % b["firewood"] if heated else "піч холодна"
	var residents: Array = b["residents"]
	var beds: int = b["beds"]
	_set_text(entry["label"], "%s · мешканців %d/%d%s\n%+.0f°C · %s" % [
		b["name"], residents.size(), b["capacity"], " · ліжок %d" % beds if beds > 0 else "", b["temperature"], state])
	entry["smoke"].emitting = heated


func _update_field(entry: Dictionary, b: Dictionary) -> void:
	var state: String = b["state"]
	var growth: float = b["growth"]
	match state:
		"empty":
			_set_text(entry["label"], "%s\nпорожнє" % b["name"])
		"sown":
			_set_text(entry["label"], "%s\n%s росте: %d%%%s" % [b["name"], RESOURCE_NAMES.get(b["crop"], b["crop"]),
				int(growth * 100.0), " · полите" if b["watered"] else ""])
		"ripe":
			_set_text(entry["label"], "%s\nстигле: %s %d" % [b["name"], RESOURCE_NAMES.get(b["crop"], b["crop"]),
				b["grain_left"]])
	var plants: Array = entry["plants"]
	var standing := plants.size()
	if state == "ripe":
		standing = int(ceil(plants.size() * float(b["grain_left"]) / (40.0 if b["crop"] == "flax" else 80.0)))
	var height := 0.0
	if state == "sown":
		height = lerpf(0.15, 1.2, growth)
	elif state == "ripe":
		height = 1.3
	var color := Color(0.35, 0.65, 0.25)
	if state == "ripe":
		color = Color(0.45, 0.55, 0.85) if b["crop"] == "flax" else Color(0.85, 0.72, 0.3)  # flax flowers blue
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
		"board":
			return BOARD_SIZE
		"barn":
			return BARN_SIZE
		"town_hall":
			return TOWN_HALL_SIZE
		"field":
			return Vector3(FIELD_SIZE, 0.2, FIELD_SIZE)
		"trading_post":
			return TRADING_POST_SIZE
		"deposit":
			return Vector3(DEPOSIT_SIZE, 1.5, DEPOSIT_SIZE)
		_:
			return WORKSHOP_SIZE  # any workshop type


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


## Animals in a row in front of their barn; dead ones lie on their side; slaughtered ones disappear.
func _update_animals(animals: Array) -> void:
	var seen := {}
	var slots := {}  # barn id -> next slot
	for a in animals:
		var id: int = a["id"]
		seen[id] = true
		if not _animals.has(id):
			_animals[id] = _make_animal(a["species"])
		var entry: Dictionary = _animals[id]
		var slot: int = slots.get(a["home"], 0)
		slots[a["home"]] = slot + 1
		var root: Node3D = entry["root"]
		root.position = _to_scene(a["position"]) + Vector3(1.5, 0, (slot - 2) * ANIMAL_SPACING)
		root.rotation.z = 0.0 if a["alive"] else PI * 0.5
	for id in _animals.keys():
		if not seen.has(id):
			_animals[id]["root"].queue_free()
			_animals.erase(id)


func _make_animal(species: String) -> Dictionary:
	var root := Node3D.new()
	add_child(root)
	var cow := species == "cow"
	var body_size := Vector3(0.6, 0.6, 1.2) if cow else Vector3(0.25, 0.25, 0.3)
	var color := Color(0.45, 0.3, 0.2) if cow else Color(0.95, 0.93, 0.88)
	var body := _add_box(root, Vector3(0, body_size.y * 0.5 + (0.35 if cow else 0.1), 0), body_size, color)
	var head := Vector3(0.35, 0.35, 0.4) if cow else Vector3(0.14, 0.14, 0.14)
	_add_box(root, Vector3(0, body_size.y + (0.5 if cow else 0.18), body_size.z * 0.55), head, color.darkened(0.15))
	return {"root": root, "body": body}


## Barn id -> names of its animals for the barn's label, with "голодна" / "мертва" where it matters.
func _animal_lines(animals: Array) -> Dictionary:
	var lines := {}
	for a in animals:
		var text: String = a["name"]
		if not a["alive"]:
			text += " (мертва)"
		elif a["hunger"] >= 80.0:
			text += " (голодна)"
		if not lines.has(a["home"]):
			lines[a["home"]] = []
		lines[a["home"]].append(text)
	return lines


func _status_text(c: Dictionary) -> String:
	var action: String = c["action"]
	if action == "craft":
		return CRAFT_SHORT.get(c["action_item"], "працює")
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

## Trees around a clearing; each tree is also added to `solids` (it stops blocking once chopped and hidden).
func _plant_forest(parent: Node3D, center: Vector3, solids: Array) -> Array:
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
		solids.append({"node": tree, "center": Vector2(tree.position.x, tree.position.z),
			"radius": TREE_SOLID_RADIUS * size})
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


## Makes a box solid for the first-person observer (while it is visible). The box's root stands at the origin,
## so its position is a scene position; only its height may be scaled.
func _block(entry: Dictionary, box: MeshInstance3D) -> MeshInstance3D:
	var size: Vector3 = (box.mesh as BoxMesh).size
	entry["solids"].append({"node": box,
		"rect": Rect2(box.position.x - size.x * 0.5, box.position.z - size.z * 0.5, size.x, size.z)})
	return box


## A circle of `radius` at `p` (ground plane x, z) moved out of `rect`: to the closest point, or through
## the nearest side when its center is already inside.
func _push_out_of_rect(p: Vector2, rect: Rect2, radius: float) -> Vector2:
	var closest := p.clamp(rect.position, rect.end)
	var away := p - closest
	if away.length_squared() >= radius * radius:
		return p
	if away != Vector2.ZERO:
		return closest + away.normalized() * radius
	var exits := [Vector2(rect.position.x - radius, p.y), Vector2(rect.end.x + radius, p.y),
		Vector2(p.x, rect.position.y - radius), Vector2(p.x, rect.end.y + radius)]
	var best: Vector2 = exits[0]
	for e: Vector2 in exits:
		if p.distance_squared_to(e) < p.distance_squared_to(best):
			best = e
	return best


func _push_out_of_circle(p: Vector2, center: Vector2, distance: float) -> Vector2:
	var away := p - center
	if away.length_squared() >= distance * distance:
		return p
	return center + (away.normalized() if away != Vector2.ZERO else Vector2.RIGHT) * distance


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
	_size_label(label)
	label.outline_size = 14
	label.outline_modulate = Color(0.0, 0.0, 0.0, 0.85)
	label.no_depth_test = true
	parent.add_child(label)
	return label


## Overview: labels are sized in the world, for a look from ~100 units. First person: the same size on screen at
## any distance (~18 px a line in a 900 px high window), otherwise a near label would cover half the view.
func _size_label(label: Label3D) -> void:
	label.fixed_size = _first_person
	label.pixel_size = FIRST_PERSON_LABEL_PIXEL_SIZE if _first_person else LABEL_PIXEL_SIZE


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
