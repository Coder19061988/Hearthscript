"""Kindlescript: перевірка, що реєстри, які живуть у кількох файлах, не розійшлися.

Запуск із кореня репозиторію:
    python .claude/skills/kindle-verify/scripts/check_sync.py [--root <інша копія репозиторію>]

ПОМИЛКА — розбіжність, через яку щось зламається або тихо не працюватиме (код виходу 1).
УВАГА  — ймовірний недогляд, який варто переглянути (код виходу не змінює).
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
errors: list[str] = []
warnings: list[str] = []


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def block(text: str, start: str, end: str) -> str:
    i = text.index(start)
    return text[i:text.index(end, i)]


def dict_keys(gd_text: str, const_name: str) -> set[str]:
    body = block(gd_text, f"const {const_name}", "}")
    return set(re.findall(r'"(\w+)"\s*:', body))


# ---------------------------------------------------------------- balance
def check_balance() -> None:
    header = block(read("core/balance.h"), "struct Balance", "bool load_json_file")
    defaults = {m[0]: float(m[1]) for m in re.findall(r"double\s+(\w+)\s*=\s*([-+0-9.eE]+)\s*;", header)}
    fields = re.findall(r"^\s*KINDLE_FIELD\((\w+)\),", read("core/balance.cpp"), re.M)
    data = json.loads(read("config/balance.json"))
    recipes = data.pop("recipes", {})  # workshop recipes: a section of their own, not a number of Balance
    workshops = data.pop("workshops", {})  # workshop types: name, materials, hours
    for wtype, w in workshops.items():
        missing = {"name", "materials", "hours"} - set(w)
        if missing:
            errors.append(f"майстерні: у \"{wtype}\" бракує {', '.join(sorted(missing))}")
    keys = {k: v for k, v in data.items() if not k.startswith("_")}
    recipes = {item: (v if isinstance(v, list) else [v]) for item, v in recipes.items()}  # variants
    for item, variants in recipes.items():
        for r in variants:
            missing = {"at", "workplace", "inputs", "minutes"} - set(r)
            if missing:
                errors.append(f"рецепти: у \"{item}\" бракує {', '.join(sorted(missing))}")
            if r.get("at") not in set(workshops) | {"house"}:
                errors.append(f"рецепти: \"{item}\" робиться в \"{r.get('at')}\", а такої майстерні немає в \"workshops\"")
    if recipes:
        prelude_text = read("core/prelude.py")
        res = set(re.findall(r'"(\w+)"', block(prelude_text, "_RESOURCES = (", ")")))
        tools = set(re.findall(r'_kind = "(\w+)"', block(prelude_text, "class Tool:", "_TOOLS = {")))
        for item, variants in recipes.items():
            if item not in res and item not in tools:
                errors.append(f"рецепти: \"{item}\" — ні ресурс (_RESOURCES), ні інструмент у прелюдії")
            for r in variants:
                for need in r.get("inputs", {}):
                    if need not in res:
                        errors.append(f"рецепти: \"{item}\" потребує \"{need}\", якого немає в _RESOURCES прелюдії")

    for name in defaults.keys() - set(fields):
        errors.append(f"баланс: поле {name} є в core/balance.h, але не в kFields (core/balance.cpp) — JSON його не змінить")
    for name in set(fields) - defaults.keys():
        errors.append(f"баланс: {name} є в kFields, але не в struct Balance")
    for name in defaults.keys() - keys.keys():
        errors.append(f"баланс: поле {name} відсутнє в config/balance.json")
    for name in keys.keys() - defaults.keys():
        errors.append(f"баланс: ключ {name} у config/balance.json невідомий ядру (буде пропущено з попередженням)")
    dupes = {f for f in fields if fields.count(f) > 1}
    for name in dupes:
        errors.append(f"баланс: {name} двічі в kFields")
    for name in defaults.keys() & keys.keys():
        if isinstance(keys[name], (int, float)) and abs(defaults[name] - keys[name]) > 1e-9:
            warnings.append(f"баланс: {name} — у balance.h {defaults[name]:g}, у balance.json {keys[name]:g}; "
                            "значення за замовчуванням мають збігатися з Баланс_прототипу.md")


# ---------------------------------------------------------------- scenario tests
def check_tests() -> None:
    tests_cpp = read("proto/tests.cpp")
    registry = block(tests_cpp, "const TestCase kTests[]", "};")
    registered = re.findall(r'\{\s*"(\w+)"\s*,\s*"[^"]*"\s*,\s*(t_\w+)\s*\}', registry)
    names = [n for n, _ in registered]
    funcs_used = {f for _, f in registered}
    defined = set(re.findall(r"^void (t_\w+)\(Ctx&", tests_cpp + read("proto/tests_mvp.inc"), re.M))
    cmake = re.search(r"set\(KINDLE_TESTS(.*?)\)", read("CMakeLists.txt"), re.S)
    ctest = cmake.group(1).split() if cmake else []

    for n in set(names) - set(ctest):
        errors.append(f"тести: {n} є в kTests, але не в KINDLE_TESTS (CMakeLists.txt) — ctest його не запустить")
    for n in set(ctest) - set(names):
        errors.append(f"тести: {n} є в KINDLE_TESTS, але не в kTests — ctest упаде з «Невідомий тест»")
    for f in defined - funcs_used:
        warnings.append(f"тести: функція {f} визначена, але не зареєстрована в kTests")
    for n in {n for n in names if names.count(n) > 1}:
        errors.append(f"тести: {n} двічі в kTests")


# ---------------------------------------------------------------- actions
def check_actions() -> None:
    core = set(re.findall(r'kind == "(\w+)"', block(read("core/world.cpp"), "BeginResult World::begin_action", "switch (a.kind)")))
    prelude_text = read("core/prelude.py")
    prelude = set(re.findall(r'_kn\.act\("(\w+)"', prelude_text))
    ui = dict_keys(read("godot/scripts/game_ui.gd"), "ACTION_NAMES")
    view = dict_keys(read("godot/scripts/world_view.gd"), "ACTION_SHORT")

    for n in prelude - core:
        errors.append(f"дії: прелюдія викликає _kn.act(\"{n}\"), але begin_action її не знає")
    for n in core - prelude:
        warnings.append(f"дії: begin_action знає \"{n}\", але прелюдія її не викликає")
    for n in core - ui:
        warnings.append(f"дії: \"{n}\" немає в ACTION_NAMES (godot/scripts/game_ui.gd) — панель покаже сиру назву")
    for n in core - view:
        warnings.append(f"дії: \"{n}\" немає в ACTION_SHORT (godot/scripts/world_view.gd) — підпис над головою буде сирим")

    me_block = block(prelude_text, "class Me(Person)", "\nme = Me(")
    properties = set(re.findall(r"^    @property\n    def (\w+)\(self", me_block, re.M))  # me.x, not me.x(...)
    me_methods = [m for m in re.findall(r"^    def ([a-z]\w*)\(self", me_block, re.M) if m not in properties]
    help_text = read("godot/scripts/help_text.gd")
    docs = read("API_прототипу.md") + read("API_MVP.md")
    for m in me_methods:
        if f"me.{m}(" not in help_text:
            warnings.append(f"довідка F1: немає me.{m}(...) у godot/scripts/help_text.gd")
        if f"me.{m}(" not in docs:
            warnings.append(f"документи: немає me.{m}(...) ні в API_прототипу.md, ні в API_MVP.md")


# ---------------------------------------------------------------- exceptions and public names
def check_api_names() -> None:
    prelude_text = read("core/prelude.py")
    exc = set(re.findall(r"^class (\w+Error)\(", prelude_text, re.M))
    all_names = set(re.findall(r'"(\w+)"', block(prelude_text, "__all__ = [", "]")))
    used = set(re.findall(r'fail\("(\w+)"', read("core/world.cpp")))
    used |= set(re.findall(r'ActionError\{"(\w+)"', read("core/world.cpp")))
    for t in used - exc - {"ActionError"}:
        errors.append(f"винятки: ядро кидає {t}, але в core/prelude.py немає такого класу")
    for t in exc - all_names:
        errors.append(f"винятки: {t} не експортовано в __all__ прелюдії — гравець не зможе його перехопити")
    defined = set(re.findall(r"^(?:class|def) ([A-Za-z]\w*)", prelude_text, re.M))
    defined |= set(re.findall(r"^([A-Za-z]\w*) = ", prelude_text, re.M))  # module-level objects: me, world, merchant
    for n in all_names - defined:
        errors.append(f"прелюдія: {n} є в __all__, але не визначено")

    highlighted = set(re.findall(r'"(\w+)"', block(read("godot/scripts/python_highlighter.gd"), "const API", "]")))
    missing = sorted(all_names - highlighted)
    if missing:
        warnings.append("підсвічування: імена з __all__, яких немає в API (godot/scripts/python_highlighter.gd): "
                        + ", ".join(missing))


def main() -> int:
    global ROOT
    if len(sys.argv) == 3 and sys.argv[1] == "--root":
        ROOT = Path(sys.argv[2]).resolve()
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    for check in (check_balance, check_tests, check_actions, check_api_names):
        try:
            check()
        except (ValueError, OSError, AttributeError) as e:
            errors.append(f"{check.__name__}: не вдалося розібрати файли ({e}) — оновіть check_sync.py")
    for e in errors:
        print("ПОМИЛКА:", e)
    for w in warnings:
        print("УВАГА:", w)
    print(f"\nПомилок: {len(errors)}, попереджень: {len(warnings)}")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
