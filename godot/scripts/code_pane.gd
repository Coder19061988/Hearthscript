extends VBoxContainer
## One code editor tab: CodeEdit with Python colors, "not applied" mark, the marker of the line that is
## running now, "Застосувати" (Ctrl+Enter) and "Розгорнути". Used for the character's own code and
## for the shared code.

signal applied(code: String)
signal expand_toggled

const PythonHighlighter := preload("res://scripts/python_highlighter.gd")
const EXECUTING_LINE_COLOR := Color(0.95, 0.8, 0.2, 0.18)

var editor: CodeEdit
var _caption: Label
var _dirty_label: Label
var _expand_button: Button
var _applied_code := ""  # code that is actually running
var _executing_line := -1
var _loading := false
var _scroll_to_line := false


func _init(caption: String, placeholder: String) -> void:
	var header := HBoxContainer.new()
	_caption = Label.new()
	_caption.text = caption
	_caption.add_theme_color_override("font_color", Color(0.75, 0.75, 0.75))
	_caption.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_caption.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	header.add_child(_caption)
	_dirty_label = Label.new()
	_dirty_label.add_theme_color_override("font_color", Color(0.95, 0.7, 0.3))
	header.add_child(_dirty_label)
	add_child(header)

	editor = CodeEdit.new()
	editor.size_flags_vertical = Control.SIZE_EXPAND_FILL
	editor.placeholder_text = placeholder
	editor.gutters_draw_line_numbers = true
	editor.gutters_draw_executing_lines = true
	editor.indent_automatic = true
	editor.indent_use_spaces = true
	editor.indent_size = 4
	editor.auto_brace_completion_enabled = true
	editor.highlight_current_line = true
	editor.draw_tabs = true
	editor.syntax_highlighter = PythonHighlighter.create()
	var font := SystemFont.new()
	font.font_names = PackedStringArray(["Consolas", "Cascadia Mono", "DejaVu Sans Mono", "monospace"])
	editor.add_theme_font_override("font", font)
	editor.add_theme_font_size_override("font_size", 15)
	editor.text_changed.connect(_on_text_changed)
	editor.gui_input.connect(_on_gui_input)
	add_child(editor)

	var buttons := HBoxContainer.new()
	var spacer := Control.new()
	spacer.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	buttons.add_child(spacer)
	_expand_button = Button.new()
	_expand_button.text = "Розгорнути"
	_expand_button.pressed.connect(func() -> void: expand_toggled.emit())
	buttons.add_child(_expand_button)
	var apply := Button.new()
	apply.text = "Застосувати (Ctrl+Enter)"
	apply.pressed.connect(apply_code)
	buttons.add_child(apply)
	add_child(buttons)


## Shows `code` as the code that is running now; `draft` (if not null) goes into the editor instead.
func load_code(code: String, draft = null) -> void:
	_applied_code = code.replace(String.chr(13), "")  # CodeEdit stores LF line ends only
	_loading = true
	editor.text = _applied_code if draft == null else draft
	editor.clear_undo_history()
	_loading = false
	_executing_line = -1
	_scroll_to_line = true
	_update_dirty()


func mark_applied(code: String) -> void:
	_applied_code = code
	_update_dirty()


func is_dirty() -> bool:
	return editor.text != _applied_code


func apply_code() -> void:
	applied.emit(editor.text)


func set_expanded(expanded: bool) -> void:
	_expand_button.text = "Згорнути" if expanded else "Розгорнути"


## Marks the running line (0-based, -1 = none). The marker only makes sense while the editor shows
## exactly the running code, so it is hidden while there are unapplied edits.
func show_executing_line(line: int) -> void:
	if is_dirty():
		line = -1
	if line == _executing_line:
		return
	if _executing_line >= 0 and _executing_line < editor.get_line_count():
		editor.set_line_as_executing(_executing_line, false)
		editor.set_line_background_color(_executing_line, Color(0, 0, 0, 0))
	_executing_line = line
	if line >= 0 and line < editor.get_line_count():
		editor.set_line_as_executing(line, true)
		editor.set_line_background_color(line, EXECUTING_LINE_COLOR)
		if _scroll_to_line:
			editor.set_line_as_center_visible(line)
			_scroll_to_line = false


func _update_dirty() -> void:
	_dirty_label.text = "● не застосовано" if is_dirty() else ""


func _on_text_changed() -> void:
	if not _loading:
		_update_dirty()


func _on_gui_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and event.keycode == KEY_ENTER and event.ctrl_pressed:
		apply_code()
		editor.accept_event()
