extends AcceptDialog
## The end of the game (MVP goal: survive the first winter). Says "Першу зиму пережито" or "Усі персонажі
## загинули", who died, when and why, and what to fix in the code for each cause of death.
## "Почати знову" keeps the player's code, so the colony can be replayed with improved code.

signal closed(survived: bool)  # "Грати далі" / "Закрити" or the window's X
signal restart_pressed
signal load_pressed

## Causes of death come from the core (World::pass_time): "голод", "виснаження", "холод", joined with " і ".
const TIPS := {
	"голод": "[b]Голод.[/b] Персонаж має їсти ([code]me.eat(\"bread\")[/code]), а хліб — не закінчуватися: поле → зерно → борошно → хліб.",
	"виснаження": "[b]Виснаження.[/b] Потрібен сон ([code]me.sleep(...)[/code]), краще вдома, або відпочинок ([code]me.rest(...)[/code]).",
	"холод": "[b]Холод.[/b] Узимку — теплий одяг ([code]me.wear(WarmClothes)[/code]) і дрова в печі ([code]me.heat(...)[/code]).",
}
const WIDTH := 680

var survived := false
var _text: RichTextLabel
var _load_button: Button


func _init() -> void:
	exclusive = true
	_text = RichTextLabel.new()
	_text.bbcode_enabled = true
	_text.fit_content = true
	_text.scroll_active = false
	_text.custom_minimum_size = Vector2(WIDTH, 0)
	add_child(_text)
	add_button("Почати знову", false, "restart")
	_load_button = add_button("Завантажити гру", false, "load")
	custom_action.connect(_on_custom_action)
	confirmed.connect(func() -> void: closed.emit(survived))
	canceled.connect(func() -> void: closed.emit(survived))


## `characters` as KindleSimulation.get_characters() returns them; `time_text` is the current game time
## (a lost game is dated by the last death instead).
func show_outcome(survived_now: bool, characters: Array, time_text: String) -> void:
	survived = survived_now
	if not survived:
		var last := -1.0
		for c in characters:
			if not c["alive"] and c["death_time"] > last:
				last = c["death_time"]
				time_text = c["death_time_text"]
	title = "Підсумок: " + time_text
	ok_button_text = "Грати далі" if survived else "Закрити"
	_load_button.visible = not survived
	_text.text = compose(survived, characters, time_text)
	reset_size()
	popup_centered()


static func compose(survived_now: bool, characters: Array, time_text: String) -> String:
	var dead := []
	for c in characters:
		if not c["alive"]:
			dead.append(c)
	var t := ""
	if survived_now:
		t += "[font_size=26][b]Першу зиму пережито![/b][/font_size]\n\n"
		t += "Вижило %d з %d. Ваш код провів поселення крізь зиму — мету гри досягнуто.\n" % [
			characters.size() - dead.size(), characters.size()]
	else:
		t += "[font_size=26][b]Усі персонажі загинули[/b][/font_size]\n\n"
		t += "Поселення протрималося до: %s. Мета — пережити першу зиму.\n" % time_text
	if not dead.is_empty():
		t += "\n[b]Загинули:[/b]\n"
		for c in dead:
			t += "  • %s — %s, причина: %s\n" % [c["name"], c["death_time_text"], c["death_cause"]]
		var tips := []
		for cause in TIPS:
			for c in dead:
				if (c["death_cause"] as String).contains(cause) and not tips.has(TIPS[cause]):
					tips.append(TIPS[cause])
		if not tips.is_empty():
			t += "\n[b]Що виправити в коді[/b]\n"
			for tip in tips:
				t += "  " + tip + "\n"
		t += "\nПодробиці — у журналі подій: попередження («сильний голод», «мерзне», «здоров'я нижче 50») і помилки коду.\n"
	t += "\n"
	if survived_now:
		t += "Можна грати далі (зима повернеться) або почати знову — ваш код, спільний і кожного персонажа, збережеться."
	else:
		t += "Виправте код і почніть знову — ваш код, спільний і кожного персонажа, збережеться."
	return t


func _on_custom_action(action: StringName) -> void:
	hide()
	if action == &"restart":
		restart_pressed.emit()
	elif action == &"load":
		load_pressed.emit()
