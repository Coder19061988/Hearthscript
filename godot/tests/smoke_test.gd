extends SceneTree
## Headless smoke test of the Godot layer: the extension loads, the main scene runs,
## game time advances, characters work and the log reaches the UI.
## Run: Godot_v4.6.2-stable_win64_console.exe --headless --path godot --script res://tests/smoke_test.gd

const WARMUP_FRAMES := 60
const GAME_HOURS := 10.0
const EDITOR_MARK := "код змінено з редактора"

var _main: Node
var _frames := 0
var _start := 0.0


func _initialize() -> void:
	if not ClassDB.class_exists("KindleSimulation"):
		printerr("FAIL: клас KindleSimulation не зареєстровано (зберіть розширення: ./build.ps1)")
		quit(1)
		return
	_main = load("res://main.tscn").instantiate()
	root.add_child(_main)


func _process(_delta: float) -> bool:
	_frames += 1
	if _frames == 1:
		_start = _main.sim.get_now()
	if _frames == WARMUP_FRAMES:
		# The way a player changes code: edit the text of the selected character and press "Застосувати".
		_main.ui._editor.text = "notify(\"%s\")\n%s" % [EDITOR_MARK, _main.default_code]
		_main.ui._apply_code()
		# The way a player places a blueprint: menu -> click on the map -> name.
		_main._start_placing("house")
		var click := InputEventMouseButton.new()
		click.button_index = MOUSE_BUTTON_LEFT
		click.pressed = true
		click.position = _main.camera_rig.camera.unproject_position(Vector3(60, 0, 100) * 0.2)
		_main._unhandled_input(click)
		_main._on_blueprint_named("Будинок_2")
		_main._on_global_set("WINTER_MODE", "True")
		_main.sim.advance(GAME_HOURS * 3600.0)
	if _frames < WARMUP_FRAMES + 5:
		return false
	_finish()
	return true


func _finish() -> void:
	var sim = _main.sim
	var failures := []
	var elapsed: float = sim.get_now() - _start
	if elapsed < GAME_HOURS * 3600.0:
		failures.append("ігровий час не йде: минуло %.0f с" % elapsed)
	var wood := 0
	for b in sim.get_buildings():
		if b["kind"] == "storage":
			wood += b["resources"].get("wood", 0)
	if wood <= 0:
		failures.append("на складі немає дров — персонажі не працюють")
	for c in sim.get_characters():
		if not c["alive"] or c["state"] != "running":
			failures.append("%s: alive=%s state=%s" % [c["name"], c["alive"], c["state"]])
	if _main.ui._log_lines == 0:
		failures.append("журнал подій в інтерфейсі порожній")
	if not _main.ui._log.get_parsed_text().contains(EDITOR_MARK):
		failures.append("код, застосований з редактора, не запустився")
	if _main.ui._is_dirty():
		failures.append("після застосування редактор досі позначений як «не застосовано»")
	var blueprint := {}
	for b in sim.get_buildings():
		if b["name"] == "Будинок_2":
			blueprint = b
	if blueprint.is_empty() or blueprint["built"] or blueprint["needs"].get("wood", 0) != 40:
		failures.append("креслення, поставлене кліком по карті, не з'явилося: %s" % [blueprint])
	elif (blueprint["position"] as Vector2).distance_to(Vector2(60, 100)) > 2.0:
		failures.append("креслення стало не там, куди клацнули: %s" % [blueprint["position"]])
	if not _main.world_view._buildings.has(blueprint.get("id", -1)):
		failures.append("креслення не намальоване у 3D")
	if sim.get_globals().get("WINTER_MODE") != true or _main.ui._globals_shown.get("WINTER_MODE") != true:
		failures.append("глобальна змінна з панелі не дійшла до симуляції або до вкладки")
	print("Godot smoke: %s, дров на складі %d, записів у журналі %d" % [sim.get_time_text(), wood, _main.ui._log_lines])
	for f in failures:
		printerr("FAIL: " + f)
	print("OK" if failures.is_empty() else "ПРОВАЛ")
	quit(0 if failures.is_empty() else 1)
