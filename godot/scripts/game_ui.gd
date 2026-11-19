extends CanvasLayer
## Interface (MVP §6): top bar with time, speed and the "Будувати" / "Гра" menus, event log, side panel with
## the selected character's state and tabs: the character's own code, the shared code, global variables.
## The left area stays transparent: the 3D view is behind it. In first person (set_first_person) only the top bar
## stays, with a crosshair in the middle of the screen.

signal speed_selected(speed: float)
signal code_applied(code: String)         # the selected character's own code
signal shared_code_applied(code: String)  # the shared code of the settlement
signal character_selected(id: int)
signal restart_requested
signal build_requested(kind: String)      # "house" / "storage" / "field" / "barn" / "trading_post" / "board"
signal blueprint_named(name: String)      # the name dialog was confirmed
signal blueprint_cancelled
signal global_set(name: String, text: String)
signal global_removed(name: String)
signal save_requested
signal load_requested
signal save_path_chosen(path: String)
signal load_path_chosen(path: String)
signal outcome_closed(survived: bool)     # the end-of-game window was closed: back to the game
signal first_person_requested            # the "Від першої особи" button (the V key is handled by main.gd)

const CodePane := preload("res://scripts/code_pane.gd")
const OutcomeDialog := preload("res://scripts/outcome_dialog.gd")
const TownHallDialog := preload("res://scripts/town_hall_dialog.gd")
const HelpText := preload("res://scripts/help_text.gd")

const SAVES_DIR := "user://saves"
const PANEL_WIDTH := 560
const LOG_HEIGHT := 200
const LOG_LIMIT := 400
const SPEEDS := [0.0, 1.0, 3.0, 10.0]

## What a character makes by a recipe (me.craft, me.grind, me.bake).
const CRAFT_NAMES := {"flour": "меле зерно", "bread": "пече хліб", "axe": "кує сокиру", "sickle": "кує серп",
	"scythe": "кує косу", "cloth": "тче тканину", "clothes": "шиє одяг", "warm_clothes": "шиє теплий одяг",
	"planks": "пиляє дошки", "bed": "робить ліжко", "charcoal": "палить вугілля", "iron": "плавить залізо",
	"pickaxe": "кує кайло", "water": "набирає воду"}
const ACTION_NAMES := {
	"go_to": "іде до", "go_home": "іде додому", "take": "бере інструмент", "put_back": "повертає інструмент",
	"chop_wood": "рубає дрова", "drop": "кладе все на склад", "pick_up": "бере зі складу", "eat": "їсть",
	"sleep": "спить", "rest": "відпочиває", "wait": "чекає", "heat": "топить піч", "wear": "переодягається",
	"sow": "сіє", "harvest": "жне", "craft": "виготовляє",
	"deliver": "несе матеріали на будову", "build": "будує", "move_into": "переселяється",
	"post": "пише на дошці оголошень", "read": "читає дошку оголошень",
	"mine": "добуває", "water": "поливає поле", "mow": "косить сіно", "feed": "годує тварину", "milk": "доїть корову", "collect_eggs": "збирає яйця",
	"slaughter": "забиває тварину", "order": "залишає замовлення купцю", "dismantle": "розбирає інструмент"}
const RESOURCE_NAMES := {"wood": "дрова", "bread": "хліб", "grain": "зерно", "flour": "борошно",
	"hay": "сіно", "milk": "молоко", "eggs": "яйця", "meat": "м'ясо", "clothes": "звичайний одяг",
	"warm_clothes": "теплий одяг", "scrap": "брухт", "flax": "льон", "cloth": "тканина", "rags": "ганчір'я",
	"planks": "дошки", "bed": "ліжко", "stone": "камінь", "ore": "руда", "charcoal": "вугілля", "iron": "залізо",
	"water": "вода"}
const TOOL_NAMES := {"axe": "сокира", "sickle": "серп", "scythe": "коса", "pickaxe": "кайло"}
const CLOTHES_NAMES := {"clothes": "звичайний", "warm_clothes": "теплий"}
const LOG_COLORS := {
	"info": "#d8d8d8", "notify": "#8fd0ff", "print": "#a8a8a8",
	"warning": "#f0b45a", "error": "#ff6b6b", "death": "#ff8080", "outcome": "#ffd75e",
}
const FIRST_PERSON_HINT := ("Від першої особи: миша — роззиратися · WASD — іти · Shift — бігти · " +
	"клік — вибрати персонажа · V або Esc — вийти")

var _time_label: Label
var _storage_label: Label
var _speed_buttons := {}
var _build_menu: MenuButton
var _hint: Label
var _first_person_button: Button
var _crosshair: Control
var _log_panel: Control
var _log: RichTextLabel
var _log_lines := 0
var _side: PanelContainer
var _character_select: OptionButton
var _character_ids: Array[int] = []
var _info: RichTextLabel
var _own_pane: CodePane
var _shared_pane: CodePane
var _globals_rows: VBoxContainer
var _global_name: LineEdit
var _global_value: LineEdit
var _help: AcceptDialog
var _name_dialog: ConfirmationDialog
var _name_edit: LineEdit
var _name_caption: Label
var _file_dialog: FileDialog
var _file_dialog_saving := false
var _outcome: OutcomeDialog
var _town_hall: TownHallDialog
var _build_costs := {}        # KindleSimulation.get_build_costs()
var _build_kinds: Array[String] = []  # kind of every item of the "Будувати" menu
var _town_hall_button: Button
var _merchant := {}      # the last KindleSimulation.get_merchant()
var _merchant_status := ""  # "купець через 2 дн." — shown before the storages in the top bar
var _trade_buildings := []
var _fatal: Label

var _selected := -1
var _drafts := {}        # character id -> edited but not applied code
var _globals_shown := {}      # last globals put into the "Змінні" tab


func _ready() -> void:
	var root := Control.new()
	root.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(root)

	var columns := HBoxContainer.new()
	columns.set_anchors_preset(Control.PRESET_FULL_RECT)
	columns.mouse_filter = Control.MOUSE_FILTER_IGNORE
	columns.add_theme_constant_override("separation", 0)
	root.add_child(columns)

	var left := VBoxContainer.new()
	left.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	left.mouse_filter = Control.MOUSE_FILTER_IGNORE
	left.add_theme_constant_override("separation", 0)
	columns.add_child(left)
	left.add_child(_build_top_bar())
	var view_area := MarginContainer.new()  # transparent: clicks go to the 3D world
	view_area.size_flags_vertical = Control.SIZE_EXPAND_FILL
	view_area.mouse_filter = Control.MOUSE_FILTER_IGNORE
	view_area.add_theme_constant_override("margin_top", 8)
	left.add_child(view_area)
	_hint = Label.new()
	_hint.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_hint.size_flags_vertical = Control.SIZE_SHRINK_BEGIN
	_hint.add_theme_font_size_override("font_size", 18)
	_hint.add_theme_constant_override("outline_size", 6)
	_hint.add_theme_color_override("font_outline_color", Color(0, 0, 0, 0.9))
	_hint.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_hint.visible = false
	view_area.add_child(_hint)
	view_area.add_theme_constant_override("margin_right", 8)
	view_area.add_theme_constant_override("margin_bottom", 8)
	_first_person_button = Button.new()
	_first_person_button.text = "Від першої особи (V)"
	_first_person_button.tooltip_text = "Пройтися поселенням: миша — роззиратися, WASD — іти, Shift — бігти"
	_first_person_button.focus_mode = Control.FOCUS_NONE
	_first_person_button.size_flags_horizontal = Control.SIZE_SHRINK_END
	_first_person_button.size_flags_vertical = Control.SIZE_SHRINK_END
	_first_person_button.pressed.connect(func() -> void: first_person_requested.emit())
	view_area.add_child(_first_person_button)
	_log_panel = _build_log()
	left.add_child(_log_panel)

	_side = _build_side_panel()
	columns.add_child(_side)

	# Anchored to the screen center with no size: the lines are drawn around that point.
	_crosshair = Control.new()
	_crosshair.set_anchors_preset(Control.PRESET_CENTER)
	_crosshair.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_crosshair.visible = false
	_crosshair.draw.connect(func() -> void:
		for axis in [Vector2.RIGHT, Vector2.DOWN]:
			_crosshair.draw_line(-axis * 9.0, axis * 9.0, Color(0, 0, 0, 0.6), 4.0)
		for axis in [Vector2.RIGHT, Vector2.DOWN]:
			_crosshair.draw_line(-axis * 8.0, axis * 8.0, Color.WHITE, 2.0))
	root.add_child(_crosshair)

	_help = AcceptDialog.new()
	_help.title = "Довідка (F1)"
	_help.size = Vector2i(900, 680)
	var help_text := RichTextLabel.new()
	help_text.bbcode_enabled = true
	help_text.text = HelpText.TEXT
	help_text.custom_minimum_size = Vector2(860, 600)
	_help.add_child(help_text)
	root.add_child(_help)

	_name_dialog = ConfirmationDialog.new()
	_name_dialog.title = "Нове креслення"
	_name_dialog.ok_button_text = "Поставити"
	_name_dialog.cancel_button_text = "Скасувати"
	var name_box := VBoxContainer.new()
	_name_caption = Label.new()
	name_box.add_child(_name_caption)
	_name_edit = LineEdit.new()
	_name_edit.custom_minimum_size.x = 360
	name_box.add_child(_name_edit)
	_name_dialog.add_child(name_box)
	_name_dialog.register_text_enter(_name_edit)
	_name_dialog.confirmed.connect(func() -> void: blueprint_named.emit(_name_edit.text.strip_edges()))
	_name_dialog.canceled.connect(func() -> void: blueprint_cancelled.emit())
	root.add_child(_name_dialog)

	_file_dialog = FileDialog.new()
	_file_dialog.access = FileDialog.ACCESS_USERDATA
	_file_dialog.use_native_dialog = false
	_file_dialog.filters = PackedStringArray(["*.json ; Збереження Kindlescript"])
	_file_dialog.size = Vector2i(760, 500)
	_file_dialog.cancel_button_text = "Скасувати"
	for option in ["favorites_enabled", "recent_list_enabled"]:  # editor-style side panels, if this Godot has them
		if option in _file_dialog:
			_file_dialog.set(option, false)
	_file_dialog.file_selected.connect(_on_file_selected)
	root.add_child(_file_dialog)

	_town_hall = TownHallDialog.new()
	root.add_child(_town_hall)

	_outcome = OutcomeDialog.new()
	_outcome.closed.connect(func(survived: bool) -> void: outcome_closed.emit(survived))
	_outcome.restart_pressed.connect(func() -> void: restart_requested.emit())
	_outcome.load_pressed.connect(func() -> void: load_requested.emit())
	root.add_child(_outcome)

	_fatal = Label.new()
	_fatal.set_anchors_preset(Control.PRESET_CENTER)
	_fatal.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_fatal.add_theme_font_size_override("font_size", 24)
	_fatal.add_theme_color_override("font_color", Color(1.0, 0.45, 0.45))
	_fatal.visible = false
	root.add_child(_fatal)


# ---------------------------------------------------------------- building blocks

func _build_top_bar() -> Control:
	var panel := PanelContainer.new()
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 12)
	panel.add_child(row)

	_time_label = Label.new()
	_time_label.custom_minimum_size.x = 340
	_time_label.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS  # long text must not push the side panel
	_time_label.add_theme_font_size_override("font_size", 18)
	row.add_child(_time_label)

	var group := ButtonGroup.new()
	for value in SPEEDS:
		var b := Button.new()
		b.text = "Пауза" if value == 0.0 else "×%d" % int(value)
		b.toggle_mode = true
		b.button_group = group
		b.focus_mode = Control.FOCUS_NONE
		b.pressed.connect(func() -> void: speed_selected.emit(value))
		row.add_child(b)
		_speed_buttons[value] = b

	_build_menu = MenuButton.new()
	_build_menu.text = "Будувати ▾"
	_build_menu.flat = false
	_build_menu.focus_mode = Control.FOCUS_NONE
	_build_menu.get_popup().index_pressed.connect(func(index: int) -> void: build_requested.emit(_build_kinds[index]))
	row.add_child(_build_menu)

	_town_hall_button = Button.new()
	_town_hall_button.text = "Управа"
	_town_hall_button.tooltip_text = "Монети, ціни купця, замовлення й залишки на складах"
	_town_hall_button.focus_mode = Control.FOCUS_NONE
	_town_hall_button.pressed.connect(func() -> void: _town_hall.open(_merchant, _trade_buildings))
	row.add_child(_town_hall_button)

	_storage_label = Label.new()
	_storage_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_storage_label.custom_minimum_size.x = 0
	_storage_label.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
	_storage_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	row.add_child(_storage_label)

	var game := MenuButton.new()
	game.text = "Гра ▾"
	game.flat = false
	game.focus_mode = Control.FOCUS_NONE
	var game_menu := game.get_popup()
	game_menu.add_item("Зберегти…")
	game_menu.add_item("Завантажити…")
	game_menu.add_separator()
	game_menu.add_item("Почати знову")
	game_menu.index_pressed.connect(_on_game_menu)
	row.add_child(game)
	var help := Button.new()
	help.text = "Довідка (F1)"
	help.focus_mode = Control.FOCUS_NONE
	help.pressed.connect(show_help)
	row.add_child(help)
	return panel


func _build_log() -> Control:
	var panel := PanelContainer.new()
	panel.custom_minimum_size.y = LOG_HEIGHT
	var box := VBoxContainer.new()
	panel.add_child(box)
	var title := Label.new()
	title.text = "Журнал подій"
	title.add_theme_color_override("font_color", Color(0.7, 0.7, 0.7))
	box.add_child(title)
	_log = RichTextLabel.new()
	_log.bbcode_enabled = true
	_log.scroll_following = true
	_log.selection_enabled = true
	_log.size_flags_vertical = Control.SIZE_EXPAND_FILL
	box.add_child(_log)
	return panel


func _build_side_panel() -> PanelContainer:
	var panel := PanelContainer.new()
	panel.custom_minimum_size.x = PANEL_WIDTH
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 6)
	panel.add_child(box)

	var row := HBoxContainer.new()
	var caption := Label.new()
	caption.text = "Персонаж:"
	row.add_child(caption)
	_character_select = OptionButton.new()
	_character_select.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_character_select.item_selected.connect(func(index: int) -> void: character_selected.emit(_character_ids[index]))
	row.add_child(_character_select)
	box.add_child(row)

	_info = RichTextLabel.new()
	_info.bbcode_enabled = true
	_info.fit_content = true
	_info.scroll_active = false
	box.add_child(_info)

	_own_pane = CodePane.new(
		"Доповнює спільний код і має пріоритет: може перевизначити будь-що звідти, а off(правило) відміняє правило для цього персонажа.",
		"# Код персонажа виконується після спільного.\n# Наприклад, щоб персонаж робив свою роботу:\n#\n# def work():\n#     ...")
	_own_pane.applied.connect(func(code: String) -> void: code_applied.emit(code))
	_own_pane.expand_toggled.connect(_toggle_expand)
	_shared_pane = CodePane.new(
		"Один на все поселення, виконується першим у кожного: функції, ролі, правила @on(...) і розпорядок main(). Застосування перезапускає всіх.",
		"# Спільний код")
	_shared_pane.applied.connect(func(code: String) -> void: shared_code_applied.emit(code))
	_shared_pane.expand_toggled.connect(_toggle_expand)

	var tabs := TabContainer.new()
	tabs.size_flags_vertical = Control.SIZE_EXPAND_FILL
	tabs.add_child(_own_pane)
	tabs.add_child(_shared_pane)
	tabs.add_child(_build_globals_tab())
	tabs.set_tab_title(0, "Код персонажа")
	tabs.set_tab_title(1, "Спільний код")
	tabs.set_tab_title(2, "Глобальні змінні")
	box.add_child(tabs)

	var hint := Label.new()
	hint.text = "Пробіл — пауза · 1/2/3 — швидкість · клік — вибрати персонажа · F1 — довідка"
	hint.add_theme_color_override("font_color", Color(0.6, 0.6, 0.6))
	hint.add_theme_font_size_override("font_size", 12)
	box.add_child(hint)
	return panel


func _build_globals_tab() -> Control:
	var box := VBoxContainer.new()
	var about := Label.new()
	about.text = ("Глобальні змінні — накази, які всі персонажі бачать одразу. У коді їх можна лише читати " +
		"(наприклад, if WINTER_MODE: ...); змінює їх тільки гравець тут.\n" +
		"Назва — ВЕЛИКІ латинські літери, цифри та _. Значення — True / False, число або текст. Enter — зберегти.")
	about.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	about.add_theme_color_override("font_color", Color(0.75, 0.75, 0.75))
	box.add_child(about)

	var scroll := ScrollContainer.new()
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_globals_rows = VBoxContainer.new()
	_globals_rows.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.add_child(_globals_rows)
	box.add_child(scroll)

	var add_row := HBoxContainer.new()
	_global_name = LineEdit.new()
	_global_name.placeholder_text = "WINTER_MODE"
	_global_name.custom_minimum_size.x = 180
	add_row.add_child(_global_name)
	_global_value = LineEdit.new()
	_global_value.placeholder_text = "True"
	_global_value.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_global_value.text_submitted.connect(func(_t: String) -> void: _add_global())
	add_row.add_child(_global_value)
	var add := Button.new()
	add.text = "Додати"
	add.pressed.connect(_add_global)
	add_row.add_child(add)
	box.add_child(add_row)
	return box


# ---------------------------------------------------------------- called by main.gd

func set_characters(characters: Array) -> void:
	_character_select.clear()
	_character_ids.clear()
	for c in characters:
		_character_ids.append(c["id"])
		_character_select.add_item(c["name"])


func select_character(id: int, code: String) -> void:
	if _selected >= 0 and _own_pane.is_dirty():
		_drafts[_selected] = _own_pane.editor.text
	_selected = id
	var index := _character_ids.find(id)
	if index >= 0:
		_character_select.select(index)
	_own_pane.load_code(code, _drafts.get(id))


func mark_applied(code: String) -> void:
	_drafts.erase(_selected)
	_own_pane.mark_applied(code)


func set_shared_code(code: String) -> void:
	_shared_pane.load_code(code)


func mark_shared_applied(code: String) -> void:
	_shared_pane.mark_applied(code)


func clear_drafts() -> void:
	_drafts.clear()
	_selected = -1


func set_speed(value: float) -> void:
	for key in _speed_buttons:
		_speed_buttons[key].set_pressed_no_signal(key == value)


## The "Будувати" menu from KindleSimulation.get_build_costs(): name, materials and hours of every kind.
func set_build_costs(costs: Dictionary) -> void:
	_build_costs = costs
	_build_kinds.clear()
	var popup := _build_menu.get_popup()
	popup.clear()
	for kind in costs:
		var cost: Dictionary = costs[kind]
		var parts := []
		for res in cost["materials"]:
			parts.append("%s %d" % [RESOURCE_NAMES.get(res, res), cost["materials"][res]])
		var hours: float = cost["hours"]
		parts.append(("%d" % int(hours) if is_equal_approx(hours, roundf(hours)) else "%.1f" % hours) + " год роботи")
		popup.add_item("%s — %s" % [cost["name"], ", ".join(parts)])
		_build_kinds.append(kind)


func build_name(kind: String) -> String:
	return _build_costs.get(kind, {}).get("name", kind)


func build_prefix(kind: String) -> String:
	return _build_costs.get(kind, {}).get("prefix", kind)


func ask_save_path(default_name: String) -> void:
	DirAccess.make_dir_recursive_absolute(SAVES_DIR)
	_file_dialog_saving = true
	_file_dialog.title = "Зберегти гру"
	_file_dialog.file_mode = FileDialog.FILE_MODE_SAVE_FILE
	_file_dialog.ok_button_text = "Зберегти"
	_file_dialog.current_dir = SAVES_DIR
	_file_dialog.current_file = default_name
	_file_dialog.popup_centered()


func ask_load_path() -> void:
	DirAccess.make_dir_recursive_absolute(SAVES_DIR)
	_file_dialog_saving = false
	_file_dialog.title = "Завантажити гру"
	_file_dialog.file_mode = FileDialog.FILE_MODE_OPEN_FILE
	_file_dialog.ok_button_text = "Завантажити"
	_file_dialog.current_dir = SAVES_DIR
	_file_dialog.popup_centered()


## A short instruction over the 3D view (empty text hides it).
func set_hint(text: String) -> void:
	_hint.text = text
	_hint.visible = text != ""


## First person frees the whole screen for the view: no side panel, no log; a crosshair and a hint instead.
func set_first_person(on: bool) -> void:
	_side.visible = not on
	_log_panel.visible = not on
	_first_person_button.visible = not on
	_crosshair.visible = on
	set_hint(FIRST_PERSON_HINT if on else "")


func ask_blueprint_name(kind: String, default_name: String) -> void:
	_name_caption.text = "Назва для креслення «%s» (за нею код знаходить будівлю: get(\"…\"))" % build_name(kind)
	_name_edit.text = default_name
	_name_dialog.popup_centered(Vector2i(460, 130))
	_name_edit.grab_focus()
	_name_edit.select_all()


func set_globals(globals: Dictionary) -> void:
	if globals == _globals_shown:
		return
	_globals_shown = globals.duplicate()
	for child in _globals_rows.get_children():
		child.queue_free()
	var names := globals.keys()
	names.sort()
	for name in names:
		var row := HBoxContainer.new()
		var label := Label.new()
		label.text = name
		label.custom_minimum_size.x = 180
		row.add_child(label)
		var value := LineEdit.new()
		value.text = _format_value(globals[name])
		value.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		value.text_submitted.connect(func(text: String) -> void: global_set.emit(name, text))
		row.add_child(value)
		var remove := Button.new()
		remove.text = "Видалити"
		remove.pressed.connect(func() -> void: global_removed.emit(name))
		row.add_child(remove)
		_globals_rows.add_child(row)


func update_status(time_text: String, raining: bool, night: bool, winter: bool, season_days_left: int,
		temperature: float, buildings: Array) -> void:
	var season := "зима" if winter else "тепла пора"
	var extra := ["%s (ще %d дн.)" % [season, season_days_left], "%+.0f°C" % temperature]
	if night:
		extra.append("ніч")
	if raining:
		extra.append("дощ")
	_time_label.text = time_text + "  ·  " + ", ".join(extra)
	var parts := []
	for b in buildings:
		if b["kind"] == "storage" and b["built"]:
			var res: Dictionary = b["resources"]
			parts.append("%s: хліб %d, зерно %d, дрова %d, сіно %d" % [b["name"], res.get("bread", 0), res.get("grain", 0),
				res.get("wood", 0), res.get("hay", 0)])
	parts.push_front(_merchant_status)
	_storage_label.text = "   ".join(parts)


## `knowledge`: what the character knows about buildings (KindleSimulation.get_knowledge).
func update_character(c: Dictionary, knowledge: Array = []) -> void:
	if c.is_empty():
		_info.text = ""
		_own_pane.show_executing_line(-1)
		_shared_pane.show_executing_line(-1)
		return
	var lines := []
	var state: String = c["state"]
	var head := "[b]%s[/b] — код: %s" % [c["name"], c["state_text"]]
	if state == "running" and c["line"] > 0:
		head += ", рядок %d%s" % [c["line"], " спільного коду" if c["line_file"] == "shared" else ""]
	lines.append(head)
	if not c["alive"]:
		lines.append("[color=#ff8080]Смерть: %s[/color]" % c["death_cause"])
	else:
		var action: String = c["action"]
		if action != "":
			var text: String = ACTION_NAMES.get(action, action)
			if action == "craft":
				text = CRAFT_NAMES.get(c["action_item"], "виготовляє: " + c["action_item"])
			if c["action_target"] != "":
				text += " → " + c["action_target"]
			lines.append("Дія: " + text)
		elif state == "running":
			lines.append("Дія: думає (виконує код)")
		else:
			lines.append("Дія: стоїть без діла")
		lines.append("Голод %s · Втома %s · Тепло %s · Здоров'я %s" % [
			_need(c["hunger"], 80.0, false), _need(c["fatigue"], 90.0, false), _need(c["warmth"], 30.0, true),
			_need(c["health"], 30.0, true)])
		var carrying := []
		var carried: Dictionary = c["carrying"]
		for res in carried:
			carrying.append("%s ×%d" % [RESOURCE_NAMES.get(res, res), carried[res]])
		lines.append("Дім: %s · Одяг: %s · Інструмент: %s" % [
			c["home"] if c["home"] != "" else "—", _clothes_text(c), _tool_text(c)])
		lines.append("Несе: %s" % (", ".join(carrying) if not carrying.is_empty() else "—"))
		var known := []
		for k in knowledge:
			known.append("%s — %s" % [k["name"], _ago_text(k["ago"])])
		if not known.is_empty():
			lines.append("[color=#9a9a9a]Знає (бачив): %s[/color]" % " · ".join(known))
	_info.text = "\n".join(lines)
	var running_line: int = c["line"] - 1 if state == "running" else -1
	_own_pane.show_executing_line(running_line if c["line_file"] == "own" else -1)
	_shared_pane.show_executing_line(running_line if c["line_file"] == "shared" else -1)


func add_log_entry(e: Dictionary) -> void:
	var color: String = LOG_COLORS.get(e["kind"], "#d8d8d8")
	var who: String = e["who"]
	var text: String = e["text"]
	if e["kind"] == "notify" or e["kind"] == "print":
		text = who + ": " + text
	text = _escape(text)
	if e["kind"] == "outcome":
		text = "[b]" + text + "[/b]"
	var line := "[color=#808080]%s[/color]  [color=%s]%s[/color]\n" % [e["time_text"], color, text]
	_log.append_text(line)
	_log_lines += 1
	if _log_lines > LOG_LIMIT:
		_log.remove_paragraph(0)
		_log_lines -= 1


## A message from the interface itself (not from the simulation), e.g. why a blueprint can't be placed.
func add_ui_message(kind: String, text: String) -> void:
	add_log_entry({"time_text": "", "kind": kind, "who": "", "text": text})


func clear_log() -> void:
	_log.clear()
	_log_lines = 0


## Trade state for the "Управа" button and window: who the merchant is, prices, posts, storages.
func update_trade(merchant: Dictionary, buildings: Array) -> void:
	_merchant = merchant
	_trade_buildings = buildings
	_merchant_status = "купець на посту" if merchant.get("here", false) else "купець через %d дн." % merchant.get("days_until", 0)
	if _town_hall.visible:
		_town_hall.refresh(merchant, buildings)


## Charts of the town hall window: refill them only while it is open and new samples came.
func wants_stats(samples: int) -> bool:
	return _town_hall.wants_stats(samples)


func update_stats(stats: Dictionary) -> void:
	_town_hall.update_stats(stats)


func show_help() -> void:
	_help.popup_centered()


## The end of the game: the first winter survived or everyone dead (see outcome_dialog.gd).
func show_outcome(survived: bool, characters: Array, time_text: String) -> void:
	_outcome.show_outcome(survived, characters, time_text)


func hide_outcome() -> void:
	_outcome.hide()


func show_fatal(text: String) -> void:
	_fatal.text = text
	_fatal.visible = true
	_side.visible = false
	_first_person_button.visible = false


# ---------------------------------------------------------------- internals

func _need(value: float, threshold: float, low_is_bad: bool) -> String:
	var bad := value <= threshold if low_is_bad else value >= threshold
	return "[color=#ff7070]%d[/color]" % int(round(value)) if bad else "%d" % int(round(value))


## The tool in hands with its condition: "сокира 64%", "сокира (зламана)", "—".
func _clothes_text(c: Dictionary) -> String:
	var clothes: String = c["clothes"]
	if clothes == "":
		return "[color=#ff7070]немає[/color]"
	var condition: float = c["clothes_condition"]
	var text := "%s %d%%" % [CLOTHES_NAMES.get(clothes, clothes), int(ceil(condition))]
	return "[color=#f0b45a]%s[/color]" % text if condition < 25.0 else text


func _tool_text(c: Dictionary) -> String:
	var tool: String = c["tool"]
	if tool == "":
		return "—"
	var condition: float = c["tool_condition"]
	if condition <= 0.0:
		return "[color=#ff7070]%s (зламаний)[/color]" % TOOL_NAMES.get(tool, tool)
	var text := "%s %d%%" % [TOOL_NAMES.get(tool, tool), int(ceil(condition))]
	return "[color=#f0b45a]%s[/color]" % text if condition < 25.0 else text


## How long ago a character saw something: game seconds, -1 = never.
func _ago_text(seconds: float) -> String:
	if seconds < 0.0:
		return "не бачив"
	if seconds < 60.0:
		return "щойно"
	if seconds < 3600.0:
		return "%d хв тому" % int(seconds / 60.0)
	if seconds < 86400.0:
		return "%d год тому" % int(seconds / 3600.0)
	return "%d дн тому" % int(seconds / 86400.0)


func _escape(text: String) -> String:
	return text.replace("[", "[lb]")


func _format_value(value: Variant) -> String:
	if value is bool:
		return "True" if value else "False"
	return str(value)


func _on_game_menu(index: int) -> void:
	match index:
		0:
			save_requested.emit()
		1:
			load_requested.emit()
		3:
			restart_requested.emit()


func _on_file_selected(path: String) -> void:
	if _file_dialog_saving:
		save_path_chosen.emit(path)
	else:
		load_path_chosen.emit(path)


func _add_global() -> void:
	var name := _global_name.text.strip_edges()
	if name == "":
		return
	global_set.emit(name, _global_value.text)
	_global_name.text = ""
	_global_value.text = ""


func _toggle_expand() -> void:
	var expanded := _side.custom_minimum_size.x > PANEL_WIDTH
	var full := int(_side.get_viewport().get_visible_rect().size.x * 0.92)
	_side.custom_minimum_size.x = PANEL_WIDTH if expanded else full
	_own_pane.set_expanded(not expanded)
	_shared_pane.set_expanded(not expanded)
