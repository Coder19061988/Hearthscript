extends Node3D
## Kindlescript: entry point. Owns the simulation (GDExtension class KindleSimulation),
## drives game time and connects the 3D view with the interface.

const WorldView := preload("res://scripts/world_view.gd")
const CameraRig := preload("res://scripts/camera_rig.gd")
const GameUI := preload("res://scripts/game_ui.gd")

## Start of the MVP game: 4 adults (MVP.md §3). proto/tests.cpp (mvp_opts) checks the same numbers.
const START_OPTIONS := {"characters": 4, "axes": 2, "sickles": 2, "bread": 40, "grain": 30, "trees": 200,
	"random_rain": true, "seed": 3}
const BALANCE_PATH := "res://data/balance.json"
## The reference settlement script (scripts/mvp/settlement.py): one script, roles by name.
const DEFAULT_CODE_PATH := "res://data/settlement.py"
const FALLBACK_CODE := "# Код персонажа. F1 — довідка.\nwhile True:\n    me.wait_until(time(6, 0))\n    me.sleep(until=time(6, 0))\n"
## A frame never advances more than this much real time (a hitch must not skip hours of game time).
const MAX_FRAME_SECONDS := 0.25
const BUILD_PREFIX := {"house": "Будинок", "storage": "Склад", "field": "Поле"}
## Keep in sync with blueprint_min_distance in config/balance.json (the core makes the final check).
const BLUEPRINT_MIN_DISTANCE := 25.0
const FOREST_RADIUS_M := 60.0

var sim = null  # KindleSimulation; untyped so this script still loads when the DLL is missing
var world_view: WorldView
var camera_rig: CameraRig
var ui: GameUI
var speed := 1.0
var last_speed := 1.0
var selected_id := -1
var default_code := ""
var globals := {}  # the player's global variables; survive "Почати знову"
var placing_kind := ""  # blueprint kind while the player chooses a place
var placing_pos := Vector2.ZERO
var naming := false  # the name dialog is open


func _ready() -> void:
	world_view = WorldView.new()
	add_child(world_view)
	camera_rig = CameraRig.new()
	add_child(camera_rig)
	ui = GameUI.new()
	add_child(ui)
	ui.speed_selected.connect(_set_speed)
	ui.code_applied.connect(_on_code_applied)
	ui.character_selected.connect(select_character)
	ui.restart_requested.connect(start_game)
	ui.build_requested.connect(_start_placing)
	ui.blueprint_named.connect(_on_blueprint_named)
	ui.blueprint_cancelled.connect(_stop_placing)
	ui.global_set.connect(_on_global_set)
	ui.global_removed.connect(_on_global_removed)

	if not ClassDB.class_exists("KindleSimulation"):
		ui.show_fatal("Розширення kindlescript не завантажено.\nЗберіть його з кореня репозиторію: ./build.ps1\nі перезапустіть Godot.")
		set_process(false)
		return
	sim = ClassDB.instantiate("KindleSimulation")
	var problems: String = sim.load_balance_json(FileAccess.get_file_as_string(BALANCE_PATH))
	if problems != "":
		push_warning(problems)
	ui.set_build_costs(sim.get_build_costs())
	# CodeEdit keeps LF line ends only; normalize, or the editor would always look "not applied".
	default_code = FileAccess.get_file_as_string(DEFAULT_CODE_PATH).replace(String.chr(13), "")
	if default_code == "":
		default_code = FALLBACK_CODE
	start_game()


func start_game() -> void:
	_stop_placing()
	sim.start(START_OPTIONS)
	for name in globals:
		sim.set_global(name, globals[name])
	ui.clear_log()
	ui.clear_drafts()
	world_view.build(sim.get_buildings())
	var characters: Array = sim.get_characters()
	for c in characters:
		sim.set_code(c["id"], default_code)
	ui.set_characters(characters)
	select_character(characters[0]["id"] if not characters.is_empty() else -1)
	_set_speed(1.0)


func _process(delta: float) -> void:
	if speed > 0.0:
		sim.advance(minf(delta, MAX_FRAME_SECONDS) * sim.get_time_scale() * speed)
	var buildings: Array = sim.get_buildings()
	var characters: Array = sim.get_characters()
	var winter: bool = sim.get_season() == "WINTER"
	world_view.update_world(buildings, characters, sim.get_time_of_day(), sim.is_raining(), winter, selected_id,
		camera_rig.camera)
	ui.update_status(sim.get_time_text(), sim.is_raining(), sim.is_night(), winter, sim.get_season_days_left(),
		sim.get_temperature(), buildings)
	ui.update_character(_find_character(characters, selected_id))
	ui.set_globals(sim.get_globals())
	for entry in sim.take_log():
		ui.add_log_entry(entry)


func _input(event: InputEvent) -> void:
	# F1 works everywhere, including inside the code editor.
	if event is InputEventKey and event.pressed and not event.echo and event.keycode == KEY_F1:
		ui.show_help()
		get_viewport().set_input_as_handled()


func _unhandled_input(event: InputEvent) -> void:
	if sim == null:
		return
	if placing_kind != "" and not naming:
		_placing_input(event)
		return
	if event is InputEventKey and event.pressed and not event.echo:
		match event.keycode:
			KEY_SPACE:
				_set_speed(0.0 if speed > 0.0 else last_speed)
			KEY_1:
				_set_speed(1.0)
			KEY_2:
				_set_speed(3.0)
			KEY_3:
				_set_speed(10.0)
	elif event is InputEventMouseButton and event.pressed and event.button_index == MOUSE_BUTTON_LEFT:
		var id := world_view.pick_character(camera_rig.camera, event.position)
		if id >= 0:
			select_character(id)


func select_character(id: int) -> void:
	selected_id = id
	ui.select_character(id, sim.get_code(id) if id >= 0 else "")


func _set_speed(value: float) -> void:
	speed = value
	if value > 0.0:
		last_speed = value
	ui.set_speed(value)


func _on_code_applied(code: String, to_all: bool) -> void:
	if to_all:
		for c in sim.get_characters():
			sim.set_code(c["id"], code)
	elif selected_id >= 0:
		sim.set_code(selected_id, code)
	ui.mark_applied(code, to_all)


func _find_character(characters: Array, id: int) -> Dictionary:
	for c in characters:
		if c["id"] == id:
			return c
	return {}


# ---------------------------------------------------------------- blueprints

func _start_placing(kind: String) -> void:
	placing_kind = kind
	naming = false
	ui.set_hint("Клацніть на карті, де поставити креслення «%s». ПКМ або Esc — скасувати." % BUILD_PREFIX[kind])


func _stop_placing() -> void:
	placing_kind = ""
	naming = false
	ui.set_hint("")
	world_view.hide_ghost()


func _placing_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and event.keycode == KEY_ESCAPE:
		_stop_placing()
	elif event is InputEventMouseMotion:
		var point = world_view.ground_point(camera_rig.camera, event.position)
		if point != null:
			world_view.show_ghost(placing_kind, point, _place_is_free(point))
	elif event is InputEventMouseButton and event.pressed:
		if event.button_index == MOUSE_BUTTON_RIGHT:
			_stop_placing()
		elif event.button_index == MOUSE_BUTTON_LEFT:
			var point = world_view.ground_point(camera_rig.camera, event.position)
			if point == null:
				return
			if not _place_is_free(point):
				ui.add_ui_message("warning", "Тут не можна будувати: занадто близько до іншої будівлі чи лісу.")
				return
			placing_pos = point
			naming = true
			ui.ask_blueprint_name(placing_kind, _suggest_name(placing_kind))
		get_viewport().set_input_as_handled()


func _on_blueprint_named(name: String) -> void:
	var error: String = sim.add_blueprint(placing_kind, name, placing_pos)
	if error != "":
		ui.add_ui_message("warning", "Креслення не поставлено: " + error)
	_stop_placing()


## The core checks the distances again; this only colors the preview.
func _place_is_free(point: Vector2) -> bool:
	for b in sim.get_buildings():
		var limit := BLUEPRINT_MIN_DISTANCE + (FOREST_RADIUS_M if b["kind"] == "forest" else 0.0)
		if (b["position"] as Vector2).distance_to(point) < limit:
			return false
	return true


func _suggest_name(kind: String) -> String:
	var taken := {}
	for b in sim.get_buildings():
		taken[b["name"]] = true
	var n := 1
	while taken.has("%s_%d" % [BUILD_PREFIX[kind], n]):
		n += 1
	return "%s_%d" % [BUILD_PREFIX[kind], n]


# ---------------------------------------------------------------- global variables

func _on_global_set(name: String, text: String) -> void:
	var value = _parse_value(text)
	var error: String = sim.set_global(name, value)
	if error != "":
		ui.add_ui_message("warning", "Глобальна змінна не змінена: " + error)
		return
	globals[name] = value
	ui.add_ui_message("info", "Наказ гравця: %s = %s" % [name, text.strip_edges()])


func _on_global_removed(name: String) -> void:
	sim.remove_global(name)
	globals.erase(name)
	ui.add_ui_message("info", "Глобальну змінну %s видалено" % name)


## "True"/"False" -> bool, whole numbers -> int, other numbers -> float, anything else -> text (quotes optional).
func _parse_value(text: String) -> Variant:
	var t := text.strip_edges()
	if t == "True":
		return true
	if t == "False":
		return false
	if t.is_valid_int():
		return t.to_int()
	if t.is_valid_float():
		return t.to_float()
	if t.length() >= 2 and (t[0] == '"' or t[0] == "'") and t[t.length() - 1] == t[0]:
		return t.substr(1, t.length() - 2)
	return t
