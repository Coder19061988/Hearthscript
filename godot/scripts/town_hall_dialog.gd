extends AcceptDialog
## The town hall window («Управа»): a summary (coins, the merchant and his prices, trading posts with their goods
## and orders, what lies in the storages) and charts of the settlement over the game, a sample every game hour.
## The player sees the real state here (the characters know only what they saw).

const StatChart := preload("res://scripts/stat_chart.gd")
const ITEM_NAMES := {
	"wood": "дрова", "grain": "зерно", "flour": "борошно", "bread": "хліб", "hay": "сіно", "milk": "молоко",
	"eggs": "яйця", "meat": "м'ясо", "clothes": "звичайний одяг", "warm_clothes": "теплий одяг",
	"axe": "сокира", "sickle": "серп", "scythe": "коса", "cow": "корова", "chicken": "курка", "scrap": "брухт",
	"flax": "льон", "cloth": "тканина", "rags": "ганчір'я", "planks": "дошки", "bed": "ліжко",
	"pickaxe": "кайло", "stone": "камінь", "ore": "руда", "charcoal": "вугілля", "iron": "залізо", "water": "вода",
	"herbs": "трави", "bandage": "бинти", "oil": "олія", "torch": "смолоскипи",
	"smoked_meat": "копчене м'ясо", "cheese": "сир", "sapling": "саджанці", "beer": "пиво"}
const STOCK_COLORS := {
	"wood": Color(0.65, 0.45, 0.3), "bread": Color(0.95, 0.5, 0.15), "grain": Color(0.98, 0.95, 0.5),
	"flour": Color(0.92, 0.92, 0.9), "hay": Color(0.4, 0.75, 0.4), "milk": Color(0.6, 0.8, 1.0),
	"eggs": Color(1.0, 0.6, 0.75), "meat": Color(0.9, 0.35, 0.3), "clothes": Color(0.6, 0.6, 0.7),
	"warm_clothes": Color(0.75, 0.55, 0.9), "scrap": Color(0.55, 0.55, 0.6), "flax": Color(0.45, 0.55, 0.85),
	"cloth": Color(0.85, 0.8, 0.65), "rags": Color(0.5, 0.45, 0.4), "planks": Color(0.85, 0.65, 0.4),
	"bed": Color(0.6, 0.35, 0.25), "stone": Color(0.7, 0.7, 0.68), "ore": Color(0.6, 0.35, 0.25),
	"charcoal": Color(0.25, 0.25, 0.25), "iron": Color(0.5, 0.55, 0.65), "water": Color(0.3, 0.55, 0.95),
	"herbs": Color(0.45, 0.8, 0.5), "bandage": Color(0.95, 0.95, 0.9),
	"oil": Color(0.9, 0.8, 0.3), "torch": Color(0.95, 0.5, 0.2),
	"smoked_meat": Color(0.6, 0.3, 0.2), "cheese": Color(1.0, 0.85, 0.4),
	"beer": Color(0.85, 0.6, 0.2)}

var _text: RichTextLabel
var _tabs: TabContainer
var _stock_chart: StatChart
var _people_chart: StatChart
var _coins_chart: StatChart
var _animals_chart: StatChart
var _stats_size := -1  # samples behind the charts; -1 = the charts must be refilled


func _init() -> void:
	title = "Управа"
	ok_button_text = "Закрити"
	size = Vector2i(760, 600)
	_tabs = TabContainer.new()
	_tabs.custom_minimum_size = Vector2(720, 520)
	add_child(_tabs)
	_text = RichTextLabel.new()
	_text.bbcode_enabled = true
	_tabs.add_child(_text)
	_stock_chart = StatChart.new()
	_tabs.add_child(_stock_chart)
	_people_chart = StatChart.new()
	_tabs.add_child(_people_chart)
	var money := VBoxContainer.new()
	_coins_chart = StatChart.new()
	_animals_chart = StatChart.new()
	money.add_child(_coins_chart)
	money.add_child(_animals_chart)
	_tabs.add_child(money)
	for i in 4:
		_tabs.set_tab_title(i, ["Зведення", "Запаси", "Люди", "Монети й тварини"][i])


## Shows the window with the current state; the charts come with the next update_stats().
func open(merchant: Dictionary, buildings: Array) -> void:
	_stats_size = -1
	refresh(merchant, buildings)
	popup_centered()


## Whether the charts want fresh data: the window is open and there are new samples.
func wants_stats(samples: int) -> bool:
	return visible and samples != _stats_size


## `stats`: KindleSimulation.get_stats().
func update_stats(stats: Dictionary) -> void:
	var days: PackedFloat64Array = stats.get("days", PackedFloat64Array())
	_stats_size = days.size()
	var stock := []
	var stock_values: Dictionary = stats.get("stock", {})
	for res in stock_values:
		stock.append({"name": ITEM_NAMES.get(res, res), "color": STOCK_COLORS.get(res, Color.WHITE), "values": stock_values[res]})
	_stock_chart.set_data(days, stock)
	_people_chart.set_data(days, [
		{"name": "здоров'я", "color": Color(0.45, 0.85, 0.45), "values": stats.get("health", PackedFloat64Array())},
		{"name": "голод", "color": Color(0.95, 0.6, 0.25), "values": stats.get("hunger", PackedFloat64Array())},
		{"name": "тепло", "color": Color(0.95, 0.4, 0.35), "values": stats.get("warmth", PackedFloat64Array())},
		{"name": "втома", "color": Color(0.55, 0.65, 0.95), "values": stats.get("fatigue", PackedFloat64Array())},
		{"name": "настрій", "color": Color(0.85, 0.55, 0.9), "values": stats.get("mood", PackedFloat64Array())},
	])
	_coins_chart.set_data(days, [
		{"name": "монети", "color": Color(1.0, 0.84, 0.35), "values": stats.get("coins", PackedFloat64Array())},
	])
	_animals_chart.set_data(days, [
		{"name": "поселенці", "color": Color(0.85, 0.85, 0.85), "values": stats.get("people", PackedFloat64Array())},
		{"name": "тварини", "color": Color(0.7, 0.5, 0.3), "values": stats.get("animals", PackedFloat64Array())},
	])


## Updates the text while the window is open (called every frame by game_ui).
func refresh(merchant: Dictionary, buildings: Array) -> void:
	var text := compose(merchant, buildings)
	if _text.text != text:
		_text.text = text


static func compose(merchant: Dictionary, buildings: Array) -> String:
	var t := ""
	var posts := []
	var storages := []
	for b in buildings:
		match b["kind"]:
			"town_hall":
				t += "[font_size=22][b]%s[/b] — монет: %d[/font_size]\n\n" % [b["name"], b["coins"]]
			"trading_post":
				if b["built"]:
					posts.append(b)
			"storage":
				if b["built"]:
					storages.append(b)
	t += "[b]Купець[/b] — "
	if merchant.get("here", false):
		t += "зараз на «%s», торгує, коли від'їжджає о 20:00\n" % merchant["post"]
	else:
		t += "приїде через %d дн. (%s)\n" % [merchant.get("days_until", 0), merchant.get("arrival_text", "")]
	t += "Продає, монет за одиницю: %s\n" % _goods(merchant.get("prices", {}))
	t += "Купує, монет за одиницю: %s\n\n" % _goods(merchant.get("offers", {}))
	t += "[b]Торгові пости[/b]\n"
	if posts.is_empty():
		t += "[color=#f0b45a]Торгового поста немає — купець проїде повз. «Будувати» → «Торговий пост».[/color]\n"
	for p in posts:
		var orders := []
		for o in p["orders"]:
			orders.append("%s ×%d" % [ITEM_NAMES.get(o[0], o[0]), o[1]])
		t += "%s — на продаж: %s; замовлено: %s; отримано: %s\n" % [p["name"], _goods(p["for_sale"]),
			", ".join(orders) if not orders.is_empty() else "—", _goods(p["received"])]
	t += "\n[b]Склади[/b]\n"
	for s in storages:
		t += "%s — %s; інструменти: %s\n" % [s["name"], _goods(s["resources"]), _goods(s["tools"])]
	return t


static func _goods(items: Dictionary) -> String:
	var parts := []
	for item in items:
		if items[item] > 0:
			parts.append("%s %d" % [ITEM_NAMES.get(item, item), items[item]])
	return " · ".join(parts) if not parts.is_empty() else "—"
