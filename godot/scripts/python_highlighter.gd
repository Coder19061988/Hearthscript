extends RefCounted
## Python syntax colors for the code editor: keywords, builtins, Kindlescript API names, strings, comments.

const KEYWORDS := [
	"False", "None", "True", "and", "as", "assert", "break", "class", "continue", "def", "del", "elif",
	"else", "except", "finally", "for", "from", "global", "if", "import", "in", "is", "lambda", "nonlocal",
	"not", "or", "pass", "raise", "return", "try", "while", "with", "yield", "match", "case",
]
const BUILTINS := [
	"print", "len", "range", "min", "max", "sum", "abs", "int", "float", "str", "list", "dict", "set",
	"tuple", "enumerate", "zip", "sorted", "isinstance", "round", "any", "all", "callable",
]
const API := [
	"me", "world", "get", "buildings", "animals", "tools", "time", "hours", "minutes", "notify", "on", "off", "Event",
	"Season", "Axe", "Sickle", "Scythe", "Pickaxe", "Deposit", "Tool", "Clothes", "WarmClothes", "Storage", "House", "Forest", "Field", "Board",
	"Barn", "Meadow", "Animal", "Cow", "Chicken", "TownHall", "TradingPost", "merchant", "Workshop", "Smithy", "Tailor", "Sawmill", "Carpentry", "Kiln", "Well", "Hospital", "Church", "bodies", "OilPress", "Lantern", "Cellar", "Smokehouse", "Dairy", "Nursery", "HeatedWorkshop", "Bathhouse", "Brewery", "Tavern", "School", "SKILLS",
	"Building", "GameObject", "Person", "Me", "Duration", "TimeOfDay", "Moment", "EventInfo",
	"ActionError", "NoItemError", "ReservedError", "TooHeavyError", "NoHomeError", "NotFoundError",
	"WorkplaceBusyError", "ToolBrokenError",
]


static func create() -> CodeHighlighter:
	var h := CodeHighlighter.new()
	h.number_color = Color("#b5cea8")
	h.symbol_color = Color("#d4d4d4")
	h.function_color = Color("#dcdcaa")
	h.member_variable_color = Color("#9cdcfe")
	for word in KEYWORDS:
		h.add_keyword_color(word, Color("#569cd6"))
	for word in BUILTINS:
		h.add_keyword_color(word, Color("#4ec9b0"))
	for word in API:
		h.add_keyword_color(word, Color("#d7a3e0"))
	h.add_color_region("#", "", Color("#6a9955"), true)
	h.add_color_region("\"", "\"", Color("#ce9178"))
	h.add_color_region("'", "'", Color("#ce9178"))
	return h
