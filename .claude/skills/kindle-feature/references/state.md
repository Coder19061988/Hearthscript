# Новий стан світу

Нове поле стану проходить чотири шари: ядро, збереження, GDExtension і Godot. Пропущене збереження помітно не одразу: гра працює, а після «Завантажити» поле тихо скидається.

## 1. Ядро (core/world.h)

- **Поле `Object`.** Клади його в блок свого типу з коментарем про зміст і одиниці. Для інших типів воно не використовується.
- **Поле `Character`.** Клади поруч зі спорідненими полями.
- **Новий тип об'єкта (`ObjKind`).** Потрібно оновити:
  - `enum class ObjKind` і `kind_name`; `is_building`, якщо об'єкт будується за кресленням;
  - Python-клас, словник `_CLASSES` і `__all__` у прелюдії;
  - `kind_from` у core/save.cpp;
  - `BUILD_KINDS` і `BUILD_NAMES` у game_ui.gd, малювання у world_view.gd;
  - `get_build_costs` у GDExtension, якщо це будівля;
  - стартову сцену `build_start_scene` у core/simulation.cpp, якщо об'єкт є на старті.
- **Новий ресурс.** Потрібно оновити:
  - `_RESOURCES` у прелюдії;
  - `World::is_resource` і `World::resource_kg` у core/world.cpp;
  - вагу `<ресурс>_kg` у балансі;
  - `RESOURCE_NAMES` у game_ui.gd і world_view.gd та `CARGO_COLORS` у world_view.gd.
- **Новий вид запису журналу (`LogKind`).** Потрібно оновити:
  - `log_kind_name` у core/world.cpp (назву бачать збереження й Godot) і `log_kind_from` у core/save.cpp;
  - `format_entry` у proto/main.cpp;
  - `LOG_COLORS` у game_ui.gd.

## 2. Збереження (core/save.cpp)

- **`Object`.** Онови пару `object_to_json` і `object_from_json`. Та сама пара серіалізує копії об'єктів у пам'яті персонажів (`Knowledge::state`), тож поле збережеться і там.
- **`Character`.** Онови блок `characters` у `SaveAccess::save` і `SaveAccess::load`.
- **Поле `World`.** Клади поруч із `winters_survived`.
- **Без сумісності.** Гра нова: збереження з попередніх версій коду не підтримуємо. Нові ключі читай через `.at()`, формат змінюй прямо, без запасних гілок для старих файлів і без номера версії.
- **Що не зберігати.** Дії, резерви інструментів і зайнятість робочих місць не зберігаються, бо після завантаження код персонажів стартує спочатку.
- **Тест.** `save_load` перевіряє, що «зберегти → завантажити → зберегти» дає той самий файл. Додай у нього перевірку нового поля зі значенням не за замовчуванням.

## 3. Обмежене знання

Персонаж бачить стан об'єкта лише поруч і пам'ятає копію (`Knowledge`), див. API_MVP.md §13.
- Якщо нове поле видно з коду гравця, нативна функція читає його через `World::knowledge(viewer, id)`, а не прямо з об'єкта.
- Для небаченого об'єкта повертай нейтральне значення: 0, `"unknown"` або None.
- Публічні факти не обмежені знанням: назва, позиція, `is_built`, `capacity`, `residents`, `home`.

## 4. GDExtension і Godot

- **Дані для інтерфейсу.** Додай ключ у словник `get_buildings` (у гілку свого типу) або `get_characters` у gdextension/kindle_simulation.cpp.
- **Новий метод.** Потрібні оголошення в .h і `ClassDB::bind_method` у `_bind_methods`. Додай метод і в перелік методів KindleSimulation у CLAUDE.md.
- **Колбеки ядра.** `World::on_log` та інші викликаються зі стека співпрограми. У GDExtension вони лише складають дані в чергу.
- **Показ.** Панель у game_ui.gd, 3D у world_view.gd, див. ui.md.

## Перевірка

```powershell
./build.ps1 -Test -Filter save_load
./build.ps1 -Test        # разом із godot_smoke, який зберігає й завантажує гру через меню
```
