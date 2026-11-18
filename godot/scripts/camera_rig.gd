extends Node3D
## Orbit camera: right mouse — rotate, middle mouse or WASD/arrows — move, wheel — zoom.

const MIN_DISTANCE := 12.0
const MAX_DISTANCE := 250.0

var camera: Camera3D
var _yaw := 90.0
var _pitch := -50.0
var _distance := 105.0
var _rotating := false
var _panning := false


func _ready() -> void:
	position = Vector3(8.0, 0.0, 22.0)
	camera = Camera3D.new()
	camera.fov = 50.0
	camera.far = 800.0
	add_child(camera)
	camera.make_current()
	_apply()


func _apply() -> void:
	rotation_degrees = Vector3(_pitch, _yaw, 0.0)
	camera.position = Vector3(0.0, 0.0, _distance)
	# The code panel covers ~35% on the right and the log the bottom: center the picture in what is left.
	camera.h_offset = _distance * 0.29
	camera.v_offset = -_distance * 0.08


func _pan(right: float, forward: float) -> void:
	var yaw := deg_to_rad(_yaw)
	var forward_dir := Vector3(-sin(yaw), 0.0, -cos(yaw))
	var right_dir := Vector3(cos(yaw), 0.0, -sin(yaw))
	position += right_dir * right + forward_dir * forward


func _process(delta: float) -> void:
	var focus := get_viewport().gui_get_focus_owner()
	if focus is TextEdit or focus is LineEdit:
		return  # typing code must not move the camera
	var move := Vector2.ZERO
	if Input.is_key_pressed(KEY_A) or Input.is_key_pressed(KEY_LEFT):
		move.x -= 1.0
	if Input.is_key_pressed(KEY_D) or Input.is_key_pressed(KEY_RIGHT):
		move.x += 1.0
	if Input.is_key_pressed(KEY_W) or Input.is_key_pressed(KEY_UP):
		move.y += 1.0
	if Input.is_key_pressed(KEY_S) or Input.is_key_pressed(KEY_DOWN):
		move.y -= 1.0
	if move != Vector2.ZERO:
		var speed := _distance * 0.8 * delta
		_pan(move.x * speed, move.y * speed)


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseButton:
		match event.button_index:
			MOUSE_BUTTON_RIGHT:
				_rotating = event.pressed
			MOUSE_BUTTON_MIDDLE:
				_panning = event.pressed
			MOUSE_BUTTON_WHEEL_UP:
				if event.pressed:
					_distance = maxf(MIN_DISTANCE, _distance * 0.9)
			MOUSE_BUTTON_WHEEL_DOWN:
				if event.pressed:
					_distance = minf(MAX_DISTANCE, _distance * 1.1)
		_apply()
	elif event is InputEventMouseMotion:
		if _rotating:
			_yaw -= event.relative.x * 0.3
			_pitch = clampf(_pitch - event.relative.y * 0.3, -88.0, -10.0)
			_apply()
		elif _panning:
			var k := _distance * 0.002
			_pan(-event.relative.x * k, event.relative.y * k)
