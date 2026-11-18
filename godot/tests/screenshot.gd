extends SceneTree
## Renders the main scene in a window and saves a screenshot (for checking visuals without the editor).
## Run: Godot_v4.6.2-stable_win64_console.exe --path godot --script res://tests/screenshot.gd -- out=C:/tmp/shot.png hours=3
## Arguments after "--": out=<png path>, hours=<game hours to advance before the shot>, help=1 (open the F1 window),
## construction=1 (a house blueprint that two characters build), save_dialog=1 (open the save dialog).

const BUILDERS := """storage = get("Склад_1")
forest = get("Ліс_1")
site = get("Будинок_2")
me.take(Axe, storage)
while site.needs:
    try:
        me.chop_wood(forest, amount=min(10, site.needs.get("wood", 0)))
    except TooHeavyError:
        pass
    try:
        me.deliver(site)
    except ActionError:
        me.drop(storage)
me.put_back(storage)
me.build(site)
me.move_into(site)
"""

var _main: Node
var _frames := 0
var _args := {"out": "user://screenshot.png", "hours": "3", "help": "0", "construction": "0", "save_dialog": "0"}


func _initialize() -> void:
	for arg in OS.get_cmdline_user_args():
		var parts := arg.split("=", true, 1)
		if parts.size() == 2:
			_args[parts[0]] = parts[1]
	_main = load("res://main.tscn").instantiate()
	root.add_child(_main)


func _process(_delta: float) -> bool:
	_frames += 1
	if _frames == 5:
		if _args["construction"] == "1":
			_main.sim.add_blueprint("house", "Будинок_2", Vector2(60, 100))
			var characters: Array = _main.sim.get_characters()
			for i in 2:
				_main.sim.set_code(characters[i]["id"], BUILDERS)
			_main.select_character(characters[0]["id"])
		_main.sim.advance(float(_args["hours"]) * 3600.0)
		_main._set_speed(0.0)
		if _args["help"] == "1":
			_main.ui.show_help()
		if _args["save_dialog"] == "1":
			_main.ui.ask_save_path("поселення_день_%d.json" % _main.sim.get_day())
	if _frames < 40:
		return false
	var image := root.get_texture().get_image()
	var err := image.save_png(_args["out"])
	print("screenshot: %s (%s)" % [_args["out"], error_string(err)])
	return true
