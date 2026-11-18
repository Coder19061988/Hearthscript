# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Стан проєкту

**Kindlescript** — симулятор середньовічного поселення, де гравець керує персонажами лише Python-кодом; автоматичної поведінки немає. `Hearthscript` — лише назва теки.

Що вже є:
- C++-ядро: світ і виконання коду гравця.
- Консольний прототип зі сценарними тестами (MVP §7).
- Підключення до Godot 4.6: GDExtension `kindlescript` і проєкт [godot/](godot/) — 3D-сцена з примітивів та інтерфейс за MVP §6.
- Механіка MVP:
  - пори року, тепло й одяг, опалення, поле й ланцюжок хліба;
  - будівництво за кресленнями (`deliver` / `build`), заселення (`move_into`);
  - глобальні змінні гравця, робочі місця (`WorkplaceBusyError`);
  - збереження й завантаження гри (JSON);
  - спільний код + код персонажа (пріоритет) + `main()`, правила `@on(time(...))` / `@on(lambda: ...)` і `off(...)` (API_MVP.md, розділ 12);
  - кінець гри: «Першу зиму пережито» / «Усі персонажі загинули» — запис `LogKind::Outcome` і вікно підсумку (API_MVP.md, розділ 6).

  Критерій MVP («хороший код переживає першу зиму») перевіряє тест `mvp_winter_survival`.
- Ще немає: `tools(...)` і `ToolBrokenError` — вони потребують якості та зносу предметів, відкладених у MVP §8.

## Команди (PowerShell, корінь репозиторію)

```powershell
git submodule update --init                   # після клонування: third_party/godot-cpp (тег 10.0.0-stable)
./build.ps1                                   # збірка всього (CMake з VS 2022, RelWithDebInfo); DLL копіюється в godot/bin
./build.ps1 -Test                             # усі тести через ctest, включно з godot_smoke
./build.ps1 -Test -Filter rain_interrupt      # тести, назва яких містить рядок
./run.ps1 [-Editor] [-NoBuild]                 # зібрати й запустити гру (або відкрити редактор Godot)
./build/RelWithDebInfo/kindle_proto.exe test rain_interrupt   # один сценарний тест; у разі провалу друкує журнал подій
./build/RelWithDebInfo/kindle_proto.exe list
./build/RelWithDebInfo/kindle_proto.exe run scripts/woodcutter.py --days 3 [--chars N] [--no-rain] [--seed N]
./build/RelWithDebInfo/kindle_proto.exe bench --chars 50 --days 3

# Godot (D:\Godot4). Перший запуск після клонування — спершу ./build.ps1, бо без DLL гра покаже підказку.
D:\Godot4\Godot_v4.6.2-stable_win64.exe --path godot -e        # редактор
D:\Godot4\Godot_v4.6.2-stable_win64.exe --path godot           # одразу гра
D:\Godot4\Godot_v4.6.2-stable_win64_console.exe --headless --path godot --script res://tests/smoke_test.gd
D:\Godot4\Godot_v4.6.2-stable_win64_console.exe --path godot --script res://tests/screenshot.gd -- out=C:/tmp/shot.png hours=5 [help=1]
```

- **Скріншоти.** Інструмент `screenshot.gd` рендерить сцену у вікні й зберігає PNG; так можна перевірити візуальні зміни без редактора.
- **Новий сценарний тест:** додай функцію та рядок у `kTests` у [proto/tests.cpp](proto/tests.cpp), а назву — у `KINDLE_TESTS` у [CMakeLists.txt](CMakeLists.txt).
- **Прелюдія:** [core/prelude.py](core/prelude.py) вшивається в бінарник, тож після змін у ній потрібна перезбірка.
- **Баланс:** [config/balance.json](config/balance.json) читається під час запуску. Новий параметр додавай у трьох місцях: поле в `Balance` ([core/balance.h](core/balance.h)), запис у `kFields` ([core/balance.cpp](core/balance.cpp)) і ключ у JSON.
- **Дані для Godot:** `config/balance.json`, `scripts/woodcutter.py` і `scripts/start_shared.py` (стає `godot/data/shared.py`) копіюються в `godot/data/` під час збірки (ціль `kindle_godot_data`). Редагуй оригінали, а не копії.
- **Код нової гри:** мінімальний спільний код [scripts/start_shared.py](scripts/start_shared.py) (лише їжа й сон), коду персонажів немає.
- **Еталонне поселення:** [scripts/mvp/shared.py](scripts/mvp/shared.py) + рядок ролі в коді кожного персонажа (`set_settlement_code` у [proto/tests_mvp.inc](proto/tests_mvp.inc)). Якщо змінюєш баланс або механіку, переконайся, що `mvp_winter_survival` проходить, а `mvp_bad_code` — ні.

## Документи

Дизайн-документи українською:
- [План_гри.md](План_гри.md) — повне бачення гри;
- [MVP.md](MVP.md) — обсяг MVP; §6 описує інтерфейс, §7 — технічний прототип;
- [API_прототипу.md](API_прототипу.md) — назви API прототипу, головне джерело; розділ 10 фіксує уточнення семантики;
- [API_MVP.md](API_MVP.md) — розширення API для MVP: пори року, тепло, будинок, поле, будівництво, заселення, глобальні змінні;
- [Баланс_прототипу.md](Баланс_прототипу.md) — числа й орієнтири темпу; розділи 8–9 — числа MVP.

Якщо змінюєш назву чи семантику API, онови разом `API_прототипу.md` / `API_MVP.md`, `core/prelude.py`, приклади в документах і довідку F1 ([godot/scripts/help_text.gd](godot/scripts/help_text.gd)).

## Архітектура

Симуляція та виконання коду гравця живуть у C++-ядрі (`kindle_core`). Godot — лише шар візуалізації та інтерфейсу. Це рішення відходить від MVP §7, де логіку гри планували на GDScript.

### Ядро (`core/`)

- **[core/world.h](core/world.h)** — світ: об'єкти, потреби, погода, журнал подій.
  - Дії — відновлювані машини станів: `begin_action` / `step_action(id, budget)` / `end_action`. Дія з ціллю сама веде персонажа до цілі, тож після переривання продовжується з того самого місця.
  - Кожен персонаж має власний `local_time`.
  - Збір (рубка, жнива) і перетворення (жорна, піч) — спільні кроки `step_gather` і `step_convert`.
  - Будинки (піч, температура) й поля (ріст) оновлюються раз на тік в `update_environment`.
  - Тепло персонажа рахується в `pass_time` за температурою довкола: будинок біля дверей або надворі.
  - Креслення — це той самий `Object` з `built == false`. Матеріали лежать у `materials_delivered`, прогрес — у `build_done`; недобудоване відхиляється в `begin_action` (`fail_unbuilt`). Нові креслення з'являються посеред гри через `add_blueprint`.
  - Глобальні змінні гравця зберігаються в `World::globals()`. `ScriptRuntime::set_global` записує їх у builtins кожної VM, а `init_vm` — у нові VM.
- **[core/runtime.h](core/runtime.h)** — `ScriptRuntime`: на кожного персонажа одна VM pocketpy і одна співпрограма minicoro.
  - Програма персонажа (`fiber_main`): спільний код (`<спільний код>`) → код персонажа (`<код>`) → `main()`, якщо визначена; усе в одному `__main__`. `set_shared_code` перезапускає всіх, `set_code` — одного. `current_file` каже, у якому з двох файлів поточний рядок.
  - Правила: `Agent::Handler` з `event` = `RAIN` / `RAIN_STOPPED` / `TIME` (`next_fire`) / `CONDITION` (`last`, `last_check`). `check_rules` у `deliver_events` ставить їх у чергу. Умови виконуються через `_kn._check` з `in_condition`: без витрат часу, дії заборонені, ліміт 1000 рядків.
  - Пріоритет коду персонажа: `rank = priority * 2 + (спільне ? 1 : 0)`; чи правило «своє», визначає `current_file` рядка `@on` у момент `register_handler`. Своє правило з тим самим іменем функції замінює спільне (у прелюдії, `_names` / `_owns`). Правило-умова, що чекало в черзі, перед запуском перевіряє умову ще раз.
  - Нативна `_kn.act(...)` виконує дію в співпрограмі й робить `mco_yield`, коли бюджет тіку вичерпано.
  - Trace-хук списує 2 с «роздумів» за кожен рядок.
  - Обробники `@on` викликаються з `deliver_events` у точці дії.
  - Якщо код змінено, персонаж помер або обробник упав, співпрограма покидається (`abandon`), а VM скидається під час наступного `set_code`.
- **[core/prelude.py](core/prelude.py)** — Python-API з `API_прототипу.md`; публічні імена копіюються в builtins.
- **[core/simulation.h](core/simulation.h)** — `Simulation::tick(dt)` і стартова сцена.
- **Кінець гри:** `World::winter_ended` (перехід зима → тепла пора) рахує `winters_survived()` і на першій зимі пише `LogKind::Outcome`; `die()` пише `Outcome`, коли помер останній. `LogKind` зберігається числом, тож нові види додавай лише в кінець.
- **[core/save.h](core/save.h)** — `save_game` / `load_game` (JSON через nlohmann/json). `SaveAccess` — друг `World`. Дії не зберігаються: після завантаження код стартує спочатку, а резерви інструментів і зайнятість робочих місць обнуляються. Якщо додаєш поле в `Object` чи `Character`, додай його в `save.cpp`. Тест `save_load` перевіряє, що «зберегти → завантажити → зберегти» дає той самий файл.

### Інші частини

- **[proto/](proto/)** — консольна програма `kindle_proto` (`run`, `test`, `list`, `bench`).
- **[gdextension/](gdextension/)** — клас `KindleSimulation` (RefCounted) для GDScript. Методи: `start`, `advance(game_seconds)`, `get_buildings`, `get_characters`, `set_code`, `get_code`, `set_shared_code` / `get_shared_code`, `take_log`, `add_blueprint`, `get_build_costs`, `set_global` / `remove_global` / `get_globals`, `save_game` / `load_game`, `get_outcome` та гетери часу, погоди й пори року. Повертає Dictionary/Array; позиції — `Vector2` у метрах. Для тестів `start` приймає ще `start_day`, `start_hour`, `hunger`, `health`.
- **[godot/](godot/)** — проєкт Godot. Сцену й інтерфейс будує код, у `main.tscn` лише корінь зі скриптом:
  - `main.gd` — щокадру `advance(delta × time_scale × speed)` (не більше 0,25 с реального часу за кадр), потім оновлює вигляд;
  - `world_view.gd` — 3D: 1 м = 0,2 одиниці; кожна будівля має свій корінь, який перебудовується, коли креслення стає будівлею; підписи персонажів — 2D-мітки над головою;
  - `camera_rig.gd` — камера;
  - `game_ui.gd` — верхня панель із меню «Будувати» і «Гра» (зберегти / завантажити / почати знову), журнал, бічна панель із вкладками «Код персонажа», «Спільний код» (обидві — `code_pane.gd`: CodeEdit, маркер рядка, «Застосувати») і «Глобальні змінні»; режим розміщення креслення, збереження у файл і збереження коду під час «Почати знову» живуть у `main.gd`;
  - `outcome_dialog.gd` — вікно підсумку гри; `main.gd` (`check_outcome`) показує його раз на результат за `get_outcome()` і ставить гру на паузу;
  - `tests/` — `smoke_test.gd` (зокрема креслення кліком по карті, глобальна змінна з панелі, збереження й завантаження, обидва вікна підсумку) і `screenshot.gd` (`construction=1` — сцена будівництва, `save_dialog=1` — вікно збереження, `outcome=won|lost` — вікно підсумку).
  
  `godot/bin`, `godot/data` і `godot/.godot` генеруються й ігноруються git.
- **[third_party/](third_party/)** — бібліотеки:
  - pocketpy v2.2.0 з патчами `KINDLESCRIPT PATCH` ([PATCHES.md](third_party/pocketpy/PATCHES.md));
  - minicoro;
  - nlohmann/json v3.11.3 (один заголовок);
  - godot-cpp — git-субмодуль.

### Підводні камені

- **Поточна VM у pocketpy глобальна.** Перед відновленням співпрограми — `py_switchvm(agent->vm)`, як у `run_until`.
- **Python-об'єкти в C-змінних.** Нативні функції не тримають їх у C-локалях між діями чи `yield`; `py_retval()` не передавай як вхідний аргумент.
- **У винятків pocketpy немає `__dict__`.** `ActionError` зберігає `reason` і `done` в `args` через `_ErrorInfo`.
- **`None` у pocketpy нехешований.** `{...}.get(None)` падає з `TypeError`, тож у прелюдії перевіряй `None` до пошуку в словнику (див. `Person.tool`).
- **Закінчення рядків.** У git `core.autocrlf=true`. CodeEdit у Godot тримає лише LF, тому код для редактора нормалізуй (`replace(String.chr(13), "")`). Python-скрипти редагування на Windows пиши через `write_bytes`, бо `write_text` тихо перетворює LF на CRLF.
- **Код гравця виконується всередині співпрограм.** Колбеки ядра, як-от `World::on_log`, викликаються зі стека співпрограми. У GDExtension вони лише складають дані в чергу; Godot API там не викликати.
- **Пісочниця:**
  - `PK_ENABLE_OS=0`;
  - `import` заблоковано гардом;
  - `exit`, `input` і `__import__` видалено з builtins.
  
  Якщо додаєш модулі чи вбудовані функції, розшир тест `sandbox`.
- **Налаштування збірки godot-cpp:**
  - `GODOTCPP_API_VERSION=4.6` — підвищуй разом із версією Godot, бо DLL під новіше API не завантажиться в старіший Godot;
  - винятки C++ увімкнено (`GODOTCPP_DISABLE_EXCEPTIONS=OFF`);
  - усі цілі збираються зі статичним CRT (`CMAKE_MSVC_RUNTIME_LIBRARY`), інакше лінкування падає;
  - `/W3` у `kindle_core` — `PRIVATE`, щоб не сипати попередженнями з заголовків godot-cpp.
- **Відома проблема Godot 4.6.2 + godot-cpp 10.0.0.** Найперший імпорт проєкту (без `godot/.godot`) падає на виході з signal 11. Відтворюється з порожнім класом `RefCounted`, тобто не в нашому коді; наступні запуски працюють. `cmake/godot_smoke.cmake` повторює імпорт один раз.
- **Ліміт персонажів:** до 127 (`PK_MAX_VM_COUNT - 1`), пам'ять ~2,6 МБ на персонажа.
- **Тексти для гравця** пиши українською, без родових форм дієслів про персонажів: «Смерть персонажа «X»», а не «помер/померла».
