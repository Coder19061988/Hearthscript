extends Control
## A line chart for the town hall window: game days across, values from 0 up to the largest one; a legend with
## the latest value of every line on top.

const FONT_SIZE := 14
const MARGIN_LEFT := 52.0
const MARGIN_TOP := 30.0
const MARGIN_RIGHT := 14.0
const MARGIN_BOTTOM := 26.0

var _days := PackedFloat64Array()
var _series := []  # [{name, color, values: PackedFloat64Array}]


func _init() -> void:
	custom_minimum_size = Vector2(640, 220)
	size_flags_vertical = Control.SIZE_EXPAND_FILL


## `series`: [{name, color, values}], every values array as long as `days`.
func set_data(days: PackedFloat64Array, series: Array) -> void:
	_days = days
	_series = series
	queue_redraw()


func series_count() -> int:
	return _series.size()


func _draw() -> void:
	var font := get_theme_default_font()
	var area := Rect2(Vector2(MARGIN_LEFT, MARGIN_TOP), size - Vector2(MARGIN_LEFT + MARGIN_RIGHT, MARGIN_TOP + MARGIN_BOTTOM))
	draw_rect(area, Color(0, 0, 0, 0.25))
	if _days.size() < 2 or _series.is_empty():
		draw_string(font, area.position + Vector2(10, 24), "Ще немає даних: знімок поселення робиться щогодини",
			HORIZONTAL_ALIGNMENT_LEFT, -1, FONT_SIZE, Color(0.8, 0.8, 0.8))
		return
	var top := 1.0
	for s in _series:
		for v in s["values"]:
			top = maxf(top, v)
	top = _round_up(top)
	var x0 := _days[0]
	var x1 := maxf(_days[_days.size() - 1], x0 + 1e-6)

	# Round grid steps: 10 -> 2, 20 -> 5, 50 -> 10.
	var leading := roundi(top / pow(10.0, floor(log(top) / log(10.0) + 1e-9)))
	var lines := 4 if leading == 2 else 5
	for i in lines + 1:
		var value := top * i / lines
		var y := area.end.y - area.size.y * i / lines
		draw_line(Vector2(area.position.x, y), Vector2(area.end.x, y), Color(1, 1, 1, 0.1))
		draw_string(font, Vector2(4, y + 5), _number(value), HORIZONTAL_ALIGNMENT_LEFT, MARGIN_LEFT - 8, FONT_SIZE,
			Color(0.75, 0.75, 0.75))
	var first := int(ceil(x0))
	var last := int(floor(x1))
	var step := maxi(1, int(ceil((last - first + 1) / 12.0)))  # no more than ~12 day labels
	for day in range(first, last + 1, step):
		var x := area.position.x + area.size.x * (day - x0) / (x1 - x0)
		draw_line(Vector2(x, area.position.y), Vector2(x, area.end.y), Color(1, 1, 1, 0.06))
		draw_string(font, Vector2(x - 14, area.end.y + 18), "д. %d" % day, HORIZONTAL_ALIGNMENT_LEFT, -1, FONT_SIZE,
			Color(0.75, 0.75, 0.75))

	var legend_x := area.position.x
	for s in _series:
		var values: PackedFloat64Array = s["values"]
		var points := PackedVector2Array()
		for i in mini(values.size(), _days.size()):
			points.append(Vector2(area.position.x + area.size.x * (_days[i] - x0) / (x1 - x0),
				area.end.y - area.size.y * values[i] / top))
		draw_polyline(points, s["color"], 2.0, true)
		var text := "%s %s" % [s["name"], _number(values[values.size() - 1])]
		draw_rect(Rect2(legend_x, 9, 12, 12), s["color"])
		draw_string(font, Vector2(legend_x + 16, 20), text, HORIZONTAL_ALIGNMENT_LEFT, -1, FONT_SIZE, Color(0.9, 0.9, 0.9))
		legend_x += 16 + font.get_string_size(text, HORIZONTAL_ALIGNMENT_LEFT, -1, FONT_SIZE).x + 18


## 1, 2, 5 × 10^n just above `value`, so the grid has round numbers.
static func _round_up(value: float) -> float:
	var power := pow(10.0, floor(log(value) / log(10.0)))
	for k in [1.0, 2.0, 5.0, 10.0]:
		if value <= k * power:
			return k * power
	return 10.0 * power


static func _number(value: float) -> String:
	return "%d" % int(round(value)) if absf(value - round(value)) < 0.05 or absf(value) >= 10.0 else "%.1f" % value
