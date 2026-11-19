extends Node3D
## Kindlescript: entry point. Owns the simulation (GDExtension class KindleSimulation),
## drives game time and connects the 3D view with the interface.

const WorldView := preload("res://scripts/world_view.gd")
const CameraRig := preload("res://scripts/camera_rig.gd")
const GameUI := preload("res://scripts/game_ui.gd")

## Start of the MVP game: 4 adults (MVP.md §3). proto/tests.cpp (mvp_opts) checks the same numbers.
const START_OPTIONS := {"characters": 4, "axes": 3, "sickles": 2, "scythes": 1, "pickaxes": 1, "bread": 40, "grain": 30, "flax": 20, "trees": 200,
	"random_rain": true, "seed": 3}
const BALANCE_PATH := "res://data/balance.json"
## Shared code of a new game (scripts/start_shared.py): settlers only eat and sleep; the player writes the rest.
const START_SHARED_CODE_PATH := "res://data/shared.py"
const FALLBACK_SHARED_CODE := "def main():\n    while True:\n        me.sleep(until=time(6, 0))\n"
## A frame never advances more than this much real time (a hitch must not skip hours of game time).
const MAX_FRAME_SECONDS := 0.25
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
var start_shared_code := ""
var globals := {}  # the player's global variables; survive "Почати знову"
var placing_kind := ""  # blueprint kind while the player chooses a place
var placing_pos := Vector2.ZERO
var naming := false  # the name dialog is open
# The end-of-game window is shown once per outcome; a loaded game past it does not show it again.
var survived_shown := false
var lost_shown := false


func _ready() -> void:
	world_view = WorldView.new()
	add_child(world_view)
	camera_rig = CameraRig.new()
	camera_rig.obstacles = world_view.keep_walkable
	add_child(camera_rig)
	ui = GameUI.new()
	add_child(ui)
	ui.first_person_requested.connect(func() -> void: _set_first_person(true))
	ui.speed_selected.connect(_set_speed)
	ui.code_applied.connect(_on_code_applied)
	ui.shared_code_applied.connect(_on_shared_code_applied)
	ui.character_selected.connect(select_character)
	ui.restart_requested.connect(start_game)
	ui.build_requested.connect(_start_placing)
	ui.blueprint_named.connect(_on_blueprint_named)
	ui.blueprint_cancelled.connect(_stop_placing)
	ui.global_set.connect(_on_global_set)
	ui.global_removed.connect(_on_global_removed)
	ui.save_requested.connect(func() -> void: ui.ask_save_path("поселення_день_%d.json" % sim.get_day()))
	ui.load_requested.connect(ui.ask_load_path)
	ui.save_path_chosen.connect(_save_to)
	ui.load_path_chosen.connect(_load_from)
	ui.outcome_closed.connect(_on_outcome_closed)

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
	start_shared_code = FileAccess.get_file_as_string(START_SHARED_CODE_PATH).replace(String.chr(13), "")
	if start_shared_code == "":
		start_shared_code = FALLBACK_SHARED_CODE
	start_game()


## A new world. "Почати знову" keeps the player's code (shared and every character's, by name),
## so the colony can be replayed with improved code; the very first game starts with the minimal code.
func start_game() -> void:
	var shared := start_shared_code
	var own := {}
	if sim.is_started():
		shared = sim.get_shared_code()
		for c in sim.get_characters():
			own[c["name"]] = sim.get_code(c["id"])
	sim.start(START_OPTIONS)
	for name in globals:
		sim.set_global(name, globals[name])
	sim.set_shared_code(shared, false)
	for c in sim.get_characters():
		sim.set_code(c["id"], own.get(c["name"], ""))
	_show_world()
	_set_speed(1.0)


## Rebuilds the view and the interface for the world that is in `sim` now (new game or a loaded one).
func _show_world() -> void:
	_set_first_person(false)
	_stop_placing()
	ui.hide_outcome()
	var outcome: Dictionary = sim.get_outcome()
	survived_shown = outcome["winters_survived"] >= 1
	lost_shown = outcome["alive"] == 0
	ui.clear_log()
	ui.clear_drafts()
	world_view.build(sim.get_buildings())
	ui.set_shared_code(sim.get_shared_code())
	var characters: Array = sim.get_characters()
	ui.set_characters(characters)
	select_character(characters[0]["id"] if not characters.is_empty() else -1)


# ---------------------------------------------------------------- saving

func _save_to(path: String) -> String:
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		var message := "Не вдалося зберегти гру: %s" % error_string(FileAccess.get_open_error())
		ui.add_ui_message("error", message)
		return message
	file.store_string(sim.save_game())
	file.close()
	ui.add_ui_message("info", "Гру збережено: " + ProjectSettings.globalize_path(path))
	return ""


func _load_from(path: String) -> String:
	var text := FileAccess.get_file_as_string(path)
	var error: String = sim.load_game(text) if text != "" else "файл порожній або не читається"
	if error != "":
		ui.add_ui_message("error", "Не вдалося завантажити гру: " + error)
		return error
	_show_world()
	globals = sim.get_globals()
	for entry in sim.take_log():  # the restored history first, then the message below
		ui.add_log_entry(entry)
	_set_speed(0.0)
	ui.add_ui_message("info", "Гру завантажено (%s). Персонажі почнуть свій код спочатку. Пробіл — продовжити." %
		sim.get_time_text())
	return ""


func _process(delta: float) -> void:
	if speed > 0.0:
		sim.advance(minf(delta, MAX_FRAME_SECONDS) * sim.get_time_scale() * speed)
	var buildings: Array = sim.get_buildings()
	var characters: Array = sim.get_characters()
	var winter: bool = sim.get_season() == "WINTER"
	world_view.update_world(buildings, characters, sim.get_animals(), sim.get_time_of_day(), sim.is_raining(), winter,
		selected_id, camera_rig.camera)
	ui.update_status(sim.get_time_text(), sim.is_raining(), sim.is_night(), winter, sim.get_season_days_left(),
		sim.get_temperature(), buildings)
	ui.update_trade(sim.get_merchant(), buildings)
	var samples: int = sim.get_stats_size()
	if ui.wants_stats(samples):
		ui.update_stats(sim.get_stats())
	ui.update_character(_find_character(characters, selected_id),
		sim.get_knowledge(selected_id) if selected_id >= 0 else [])
	ui.set_globals(sim.get_globals())
	for entry in sim.take_log():
		ui.add_log_entry(entry)
	check_outcome()


## MVP goal: survive the first winter. Pauses the game and shows the end-of-game window once per outcome.
func check_outcome() -> void:
	var outcome: Dictionary = sim.get_outcome()
	var lost: bool = outcome["total"] > 0 and outcome["alive"] == 0
	var survived: bool = outcome["winters_survived"] >= 1
	if (lost and not lost_shown) or (survived and not survived_shown and not lost):
		lost_shown = lost_shown or lost
		survived_shown = survived_shown or survived
		_set_speed(0.0)
		_set_first_person(false)  # the window needs the mouse
		ui.show_outcome(not lost, sim.get_characters(), sim.get_time_text())


## "Грати далі" after the first winter resumes the game; after a loss it stays paused (nobody is left).
func _on_outcome_closed(survived: bool) -> void:
	if survived:
		_set_speed(last_speed)


func _input(event: InputEvent) -> void:
	# F1 works everywhere, including inside the code editor.
	if event is InputEventKey and event.pressed and not event.echo and event.keycode == KEY_F1:
		_set_first_person(false)
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
			KEY_V:
				_set_first_person(not camera_rig.first_person)
			KEY_ESCAPE:
				_set_first_person(false)
	elif event is InputEventMouseButton and event.pressed and event.button_index == MOUSE_BUTTON_LEFT:
		# First person: the mouse is captured, the character is picked under the crosshair (screen center).
		var at: Vector2 = get_viewport().get_visible_rect().size * 0.5 if camera_rig.first_person else event.position
		var id := world_view.pick_character(camera_rig.camera, at)
		if id >= 0:
			select_character(id)


## First person: the player walks among the settlers (camera_rig.gd); the side panel and the log hide to free
## the view. Only the view changes: the observer is not a character and the game goes on as before.
func _set_first_person(on: bool) -> void:
	if on == camera_rig.first_person:
		return
	if on:
		_stop_placing()
		get_viewport().gui_release_focus()  # WASD must walk, not type into the code editor
	camera_rig.set_first_person(on)
	world_view.set_first_person(on)
	ui.set_first_person(on)


func select_character(id: int) -> void:
	selected_id = id
	ui.select_character(id, sim.get_code(id) if id >= 0 else "")


func _set_speed(value: float) -> void:
	speed = value
	if value > 0.0:
		last_speed = value
	ui.set_speed(value)


func _on_code_applied(code: String) -> void:
	if selected_id >= 0:
		sim.set_code(selected_id, code)
	ui.mark_applied(code)


func _on_shared_code_applied(code: String) -> void:
	sim.set_shared_code(code)
	ui.mark_shared_applied(code)


func _find_character(characters: Array, id: int) -> Dictionary:
	for c in characters:
		if c["id"] == id:
			return c
	return {}


# ---------------------------------------------------------------- blueprints

func _start_placing(kind: String) -> void:
	placing_kind = kind
	naming = false
	ui.set_hint("Клацніть на карті, де поставити креслення «%s». ПКМ або Esc — скасувати." % ui.build_name(kind))


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
	while taken.has("%s_%d" % [ui.build_prefix(kind), n]):
		n += 1
	return "%s_%d" % [ui.build_prefix(kind), n]


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
