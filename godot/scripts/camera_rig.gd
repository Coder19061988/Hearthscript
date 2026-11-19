extends Node3D
## Camera with two modes.
## Overview (orbit): right mouse — rotate, middle mouse or WASD/arrows — move, wheel — zoom.
## First person: the player walks among the settlers as an unseen observer (not a character: changes nothing
## in the world). Mouse — look, WASD/arrows — walk, Shift — run; buildings and trees block the way.

const MIN_DISTANCE := 12.0
const MAX_DISTANCE := 250.0
const OVERVIEW_FOV := 50.0
const FIRST_PERSON_FOV := 75.0
## Scene units, like world_view.gd: a character is 1.8 tall, a house wall 3.2.
const EYE_HEIGHT := 1.6
const BODY_RADIUS := 0.4
const WALK_SPEED := 6.0
const RUN_FACTOR := 3.0
## A fast step is split into parts no longer than this, so it cannot jump through a thin wall.
const MAX_STEP := 0.3
const LOOK_SENSITIVITY := 0.15
## Entering first person, the observer stands this far before the point the overview looked at, facing it;
## leaving, the overview looks at the point this far ahead of the observer.
const STEP_BACK := 10.0

var camera: Camera3D
var first_person := false
## (position: Vector3, radius: float) -> Vector3: the observer's position kept on the ground and out of
## buildings and trees (WorldView.keep_walkable). Without it the observer walks through everything.
var obstacles: Callable
var _yaw := 90.0
var _pitch := -50.0
var _overview_pitch := -50.0
var _distance := 105.0
var _rotating := false
var _panning := false


func _ready() -> void:
	position = Vector3(8.0, 0.0, 22.0)
	camera = Camera3D.new()
	camera.fov = OVERVIEW_FOV
	camera.far = 800.0
	add_child(camera)
	camera.make_current()
	_apply()


func set_first_person(on: bool) -> void:
	if on == first_person:
		return
	first_person = on
	_rotating = false
	_panning = false
	var forward := _forward()
	if on:
		_overview_pitch = _pitch
		_pitch = -5.0
		position = _walkable(position - forward * STEP_BACK)
		camera.fov = FIRST_PERSON_FOV
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
	else:
		_pitch = _overview_pitch
		position = Vector3(position.x, 0.0, position.z) + forward * STEP_BACK
		camera.fov = OVERVIEW_FOV
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	_apply()


## First person: walking for `delta` seconds; `move.x` — to the right, `move.y` — forward (each -1…1).
func walk(move: Vector2, delta: float, run: bool) -> void:
	var step := move.normalized() * WALK_SPEED * (RUN_FACTOR if run else 1.0) * delta
	var parts := maxi(1, ceili(step.length() / MAX_STEP))
	for i in parts:
		_pan(step.x / parts, step.y / parts)
		position = _walkable(position)


func _apply() -> void:
	rotation_degrees = Vector3(_pitch, _yaw, 0.0)
	if first_person:
		camera.position = Vector3.ZERO
		camera.h_offset = 0.0
		camera.v_offset = 0.0
		return
	camera.position = Vector3(0.0, 0.0, _distance)
	# The code panel covers ~35% on the right and the log the bottom: center the picture in what is left.
	camera.h_offset = _distance * 0.29
	camera.v_offset = -_distance * 0.08


## Horizontal direction the camera looks at.
func _forward() -> Vector3:
	var yaw := deg_to_rad(_yaw)
	return Vector3(-sin(yaw), 0.0, -cos(yaw))


func _pan(right: float, forward: float) -> void:
	var yaw := deg_to_rad(_yaw)
	var right_dir := Vector3(cos(yaw), 0.0, -sin(yaw))
	position += right_dir * right + _forward() * forward


func _walkable(p: Vector3) -> Vector3:
	p.y = EYE_HEIGHT
	return obstacles.call(p, BODY_RADIUS) if obstacles.is_valid() else p


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
	if move == Vector2.ZERO:
		return
	if first_person:
		walk(move, delta, Input.is_key_pressed(KEY_SHIFT))
	else:
		var speed := _distance * 0.8 * delta
		_pan(move.x * speed, move.y * speed)


func _unhandled_input(event: InputEvent) -> void:
	if first_person:
		if event is InputEventMouseMotion:
			_yaw -= event.relative.x * LOOK_SENSITIVITY
			_pitch = clampf(_pitch - event.relative.y * LOOK_SENSITIVITY, -85.0, 85.0)
			_apply()
		return
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
