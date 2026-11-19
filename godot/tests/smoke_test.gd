extends SceneTree
## Headless smoke test of the Godot layer: the extension loads, the main scene runs,
## game time advances, characters work and the log reaches the UI.
## Run: Godot_v4.6.2-stable_win64_console.exe --headless --path godot --script res://tests/smoke_test.gd

const WARMUP_FRAMES := 60
const GAME_HOURS := 10.0
const EDITOR_MARK := "код змінено з редактора"
const OWN_CODE := """notify("%s")

def work():
    me.take(Axe, get("Склад_1"))
    try:
        me.chop_wood(get("Ліс_1"), amount=5)
    except TooHeavyError:
        pass
    me.drop(get("Склад_1"))
"""
const SHARED_RULE := """

@on(time(10, 0))
def ten_oclock(me):
    notify("правило спільного коду: 10:00")
"""

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
		# The way a player changes code: edit the selected character's own code and press "Застосувати".
		# It overrides work() of the shared code, so this character chops wood within the shared routine.
		_main.ui._own_pane.editor.text = OWN_CODE % EDITOR_MARK
		_main.ui._own_pane.apply_code()
		# ...and the shared code: a new rule for everyone (applying restarts all characters).
		_main.ui._shared_pane.editor.text = _main.ui._shared_pane.editor.text + SHARED_RULE
		_main.ui._shared_pane.apply_code()
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
	if _main.ui._own_pane.is_dirty() or _main.ui._shared_pane.is_dirty():
		failures.append("після застосування редактор досі позначений як «не застосовано»")
	if not _main.ui._log.get_parsed_text().contains("правило спільного коду: 10:00"):
		failures.append("правило, додане до спільного коду в редакторі, не спрацювало")
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
	failures.append_array(_check_first_person())
	# Save and load through the same functions as the "Гра" menu.
	var path := "user://saves/smoke_test.json"
	DirAccess.make_dir_recursive_absolute("user://saves")
	var saved_now: float = sim.get_now()
	if _main._save_to(path) != "":
		failures.append("гра не зберіглася")
	sim.advance(3600.0)  # the loaded game must go back to the saved moment
	var load_error: String = _main._load_from(path)
	if load_error != "":
		failures.append("гра не завантажилася: " + load_error)
	elif absf(sim.get_now() - saved_now) > 0.5:
		failures.append("після завантаження інший час: %s" % sim.get_time_text())
	elif _main.speed != 0.0 or sim.get_globals().get("WINTER_MODE") != true:
		failures.append("після завантаження гра не на паузі або загубилася глобальна змінна")
	else:
		var restored := false
		for b in sim.get_buildings():
			restored = restored or (b["name"] == "Будинок_2" and not b["built"])
		if not restored:
			failures.append("після завантаження зникло креслення")
	DirAccess.remove_absolute(path)
	var smoke_time: String = sim.get_time_text()
	# The end of the game: a starving settlement dies; the last winter night ends with "пережито".
	failures.append_array(_check_outcome({"characters": 2, "hunger": 100, "health": 2, "random_rain": false}, false))
	failures.append_array(_check_outcome({"characters": 2, "start_day": 15, "start_hour": 23.5, "random_rain": false}, true))
	failures.append_array(_check_animals())
	failures.append_array(_check_trade())
	failures.append_array(_check_hospital())
	print("Godot smoke: %s, дров на складі %d, записів у журналі %d" % [smoke_time, wood, _main.ui._log_lines])
	for f in failures:
		printerr("FAIL: " + f)
	print("OK" if failures.is_empty() else "ПРОВАЛ")
	quit(0 if failures.is_empty() else 1)


## First person the way a player uses it: V — the view at eye height, the side panel hidden; the mouse looks
## around; walking into the storage stops at its wall; a click selects the character under the crosshair;
## Esc — back to the overview; the button in the corner of the view enters first person too.
func _check_first_person() -> Array:
	var problems := []
	var rig = _main.camera_rig
	var ui = _main.ui
	_main._unhandled_input(_key(KEY_V))
	if not rig.first_person or ui._side.visible or ui._log_panel.visible or not ui._crosshair.visible:
		problems.append("V не перемкнуло на вигляд від першої особи")
		return problems
	if absf(rig.camera.global_position.y - rig.EYE_HEIGHT) > 0.01:
		problems.append("від першої особи камера не на висоті очей: %.2f" % rig.camera.global_position.y)
	var label: Label3D = _main.world_view._buildings.values()[0]["label"]
	if not label.fixed_size:
		problems.append("від першої особи написи будівель не сталого розміру на екрані")
	var look := InputEventMouseMotion.new()
	look.relative = Vector2(100.0, 0.0)
	var yaw: float = rig._yaw
	rig._unhandled_input(look)  # right after V: the cursor jumping to the window center
	if not is_equal_approx(rig._yaw, yaw):
		problems.append("від першої особи погляд смикнувся, коли курсор перескочив у центр вікна")
	rig._captured_frame -= rig.CAPTURE_SETTLE_FRAMES  # as if a few frames have passed
	rig._unhandled_input(look)
	if is_equal_approx(rig._yaw, yaw):
		problems.append("від першої особи миша не повертає погляд")
	# Склад_1 (0, 0 m) stands at x -3…3, z -6.5…-1.5 in the scene: walk north at it from the south.
	rig.position = Vector3(0.0, rig.EYE_HEIGHT, 5.0)
	rig._yaw = 0.0
	rig._apply()
	rig.walk(Vector2(0.0, 1.0), 3.0, false)
	var stop: float = -1.5 + rig.BODY_RADIUS
	if absf(rig.position.z - stop) > 0.05:
		problems.append("від першої особи хода не спинилася біля стіни складу: z = %.2f, очікувано %.2f" % [
			rig.position.z, stop])
	# The crosshair is the screen center: look at a character from 6 units and click.
	var target: Dictionary = _main.sim.get_characters()[1]
	var body: Vector3 = _main.world_view._people[target["id"]]["root"].position
	rig.position = Vector3(body.x, rig.EYE_HEIGHT, body.z + 6.0)
	rig._pitch = -rad_to_deg(atan2(rig.EYE_HEIGHT - 1.0, 6.0))
	rig._apply()
	_main.select_character(_main.sim.get_characters()[0]["id"])
	var click := InputEventMouseButton.new()
	click.button_index = MOUSE_BUTTON_LEFT
	click.pressed = true
	_main._unhandled_input(click)
	if _main.selected_id != target["id"]:
		problems.append("від першої особи клік не вибрав персонажа під прицілом")
	_main._unhandled_input(_key(KEY_ESCAPE))
	if rig.first_person or not ui._side.visible or not ui._log_panel.visible or ui._crosshair.visible:
		problems.append("Esc не повернув огляд згори з бічною панеллю та журналом")
	elif absf(rig.position.y) > 0.01 or not is_equal_approx(rig.camera.fov, rig.OVERVIEW_FOV) or label.fixed_size:
		problems.append("після виходу з першої особи камера чи написи не стали оглядовими")
	ui._first_person_button.pressed.emit()
	if not rig.first_person:
		problems.append("кнопка «Від першої особи» не перемкнула вигляд")
	_main._unhandled_input(_key(KEY_V))
	if rig.first_person:
		problems.append("друге натискання V не повернуло огляд згори")
	return problems


func _key(code: Key) -> InputEventKey:
	var event := InputEventKey.new()
	event.keycode = code
	event.pressed = true
	return event


## Animals live in a barn and are drawn there; a bought animal needs a barn with a free place.
func _check_animals() -> Array:
	var problems := []
	var sim = _main.sim
	sim.start({"characters": 1, "barn": true, "cows": 1, "chickens": 2, "random_rain": false})
	_main._show_world()
	_main._process(0.0)
	if sim.get_animals().size() != 3 or _main.world_view._animals.size() != 3:
		problems.append("тварини в хліві не показані: %d / %d" % [sim.get_animals().size(), _main.world_view._animals.size()])
	if sim.add_animal("cow", "Хлів_1") != "" or sim.add_animal("cow", "Склад_1") == "":
		problems.append("куплена тварина має ставати лише в хлів")
	var meadow := false
	for b in sim.get_buildings():
		meadow = meadow or (b["kind"] == "meadow" and b["hay"] > 0)
	if not meadow:
		problems.append("на карті немає лугу з травою")
	var kinds: Array[String] = _main.ui._build_kinds
	if _main.ui._build_menu.get_popup().get_item_count() != sim.get_build_costs().size() or not kinds.has("smithy") \
			or not kinds.has("tailor"):
		problems.append("меню «Будувати» не збігається з тим, що можна будувати (зокрема кузні й кравецької)")
	var person: Dictionary = sim.get_characters()[0]
	if person["clothes"] != "clothes" or person["clothes_condition"] < 99.0:
		problems.append("у панелі персонажа немає стану одягу: %s %s" % [person["clothes"], person["clothes_condition"]])
	for b in sim.get_buildings():
		if b["kind"] == "field" and not b.has("crop"):
			problems.append("поле не каже, що на ньому росте (crop)")
	return problems


## A built hospital shows its temperature and stove; an ill character is marked over the head and in the panel.
func _check_hospital() -> Array:
	var problems := []
	var sim = _main.sim
	sim.start({"characters": 2, "hospital": true, "church": true, "lantern": true, "cellar": true, "start_hour": 22.0, "sick": 1,
		"random_rain": false})
	_main._show_world()
	var ill: Dictionary = sim.get_characters()[0]
	_main.select_character(ill["id"])
	_main._process(0.0)
	for b in sim.get_buildings():
		if b["name"] == "Лікарня_1":
			var label: Label3D = _main.world_view._buildings[b["id"]]["label"]
			if not b.has("temperature") or not label.text.contains("°C"):
				problems.append("лікарня не показує температуру й піч: %s" % label.text)
	var tag: Label = _main.world_view._people[ill["id"]]["label"]
	if ill["illness_left"] <= 0.0 or not tag.text.contains("хворіє"):
		problems.append("хворий персонаж не позначений над головою: %s" % tag.text)
	if not _main.ui._info.text.contains("Хвороба"):
		problems.append("панель персонажа не показує хворобу")
	if not _main.ui._build_kinds.has("hospital") or not _main.ui._build_kinds.has("church"):
		problems.append("лікарні чи церкви немає в меню «Будувати»")
	for b in sim.get_buildings():
		if b["name"] == "Церква_1" and (not b.has("graves") or _main.world_view._buildings[b["id"]]["graves"].size() == 0):
			problems.append("церква не показує кладовища")
	for b in sim.get_buildings():
		if b["name"] == "Ліхтар_1" and (not b["lit"] or not _main.world_view._buildings[b["id"]]["light"].visible):
			problems.append("запалений ліхтар не світить уночі")
	for b in sim.get_buildings():
		if b["name"] == "Погріб_1" and (not b["cellar"] or not _main.world_view._buildings.has(b["id"])):
			problems.append("погріб не показано")
	if not _main.ui._info.text.contains("Навички"):
		problems.append("панель персонажа не показує навичок")
	if not _main.ui._info.text.contains("Раціон"):
		problems.append("панель персонажа не показує раціон")
	if not _main.ui._info.text.contains("Настрій"):
		problems.append("панель персонажа не показує настрій")
	return problems


## The town hall window: coins, the merchant's prices, a hint that a trading post is needed.
func _check_trade() -> Array:
	var problems := []
	var sim = _main.sim
	sim.start(_main.START_OPTIONS)
	_main._show_world()
	_main._process(0.0)
	var ui = _main.ui
	ui._town_hall_button.pressed.emit()
	var text: String = ui._town_hall._text.get_parsed_text()
	if not ui._town_hall.visible or not text.contains("Управа_1 — монет: 50") or not text.contains("Торгового поста немає"):
		problems.append("вікно управи не показує монети чи підказку про торговий пост: " + text.left(200))
	_main._process(0.0)
	if not ui._storage_label.text.begins_with("купець через"):
		problems.append("верхня панель не показує, коли приїде купець: " + ui._storage_label.text)
	var merchant: Dictionary = sim.get_merchant()
	if merchant.get("days_until", -1) != 4 or (merchant["prices"] as Dictionary).get("cow", 0) <= 0:
		problems.append("купець не запланований на 5-й день або без цін: %s" % [merchant])
	# Charts: a sample every game hour reaches the open window.
	sim.advance(3 * 3600.0)
	_main._process(0.0)
	var stock_chart = ui._town_hall._stock_chart
	if sim.get_stats_size() != 4 or stock_chart._days.size() != 4 or stock_chart.series_count() == 0:
		problems.append("діаграми управи не отримали знімків: %d / %d, ліній %d" % [sim.get_stats_size(),
			stock_chart._days.size(), stock_chart.series_count()])
	if ui._town_hall._people_chart.series_count() != 5:
		problems.append("на діаграмі «Люди» немає здоров'я, голоду, тепла, втоми й настрою")
	ui._town_hall.hide()
	return problems


## A world started with `options` runs 3 game hours; the end-of-game window must show the right outcome once,
## pause the game, and "Почати знову" from it must start a new world with the player's code kept.
func _check_outcome(options: Dictionary, survived: bool) -> Array:
	var problems := []
	var sim = _main.sim
	sim.start(options)
	sim.set_shared_code(_main.start_shared_code, false)
	_main._show_world()
	_main._set_speed(1.0)
	sim.advance(3 * 3600.0)
	_main.check_outcome()
	var dialog = _main.ui._outcome
	var expected := "Першу зиму пережито" if survived else "Усі персонажі загинули"
	if not dialog.visible or dialog.survived != survived or not dialog._text.get_parsed_text().contains(expected):
		problems.append("вікно підсумку не показало «%s»" % expected)
	if not survived and not dialog._text.get_parsed_text().contains("Що виправити в коді"):
		problems.append("у вікні поразки немає порад, що виправити")
	if _main.speed != 0.0:
		problems.append("на підсумку гри («%s») гра не стала на паузу" % expected)
	dialog.hide()
	_main.check_outcome()
	if dialog.visible:
		problems.append("вікно підсумку («%s») показалося вдруге" % expected)
	if not survived:
		dialog._on_custom_action(&"restart")
		var alive := 0
		for c in sim.get_characters():
			alive += 1 if c["alive"] else 0
		if alive != 4 or sim.get_shared_code() != _main.start_shared_code or _main.speed == 0.0:
			problems.append("«Почати знову» з вікна поразки: живих %d, спільний код збережено: %s" % [
				alive, sim.get_shared_code() == _main.start_shared_code])
	return problems
