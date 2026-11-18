extends RefCounted
## F1 help: short reference of the API (full text: API_прототипу.md and API_MVP.md in the repository).

const TEXT := """[font_size=20][b]Kindlescript — довідка[/b][/font_size]

Кожен персонаж виконує свій Python-код. Гра нічого не робить за вас: їжа, сон, тепло і робота — лише через код.
Кожна дія займає ігровий час; кожен рядок коду — 2 ігрові секунди «роздумів».
Мета: пережити першу зиму (тепла пора — 10 днів, зима — 5).

[b]Глобальні функції[/b]
[code]get("Склад_1")[/code] — об'єкт за назвою (інакше NotFoundError)
[code]buildings(Storage)[/code] — список будівель типу Storage / House / Forest / Field
[code]time(18, 0)[/code], [code]hours(2)[/code], [code]minutes(30)[/code] — час доби і тривалість
[code]notify("текст")[/code] — запис у журнал подій;  [code]print(...)[/code] — теж у журнал
[code]@on(Event.RAIN, priority=1)[/code] — обробник події (менше число — вищий пріоритет)

[b]Світ (лише читання)[/b]
[code]world.time, world.day, world.is_night, world.is_raining[/code]
[code]world.season[/code] (Season.WARM / Season.WINTER), [code]world.season_days_left[/code], [code]world.temperature[/code] (°C)

[b]Персонаж: me[/b]
[code]me.name, me.hunger, me.fatigue, me.warmth, me.health[/code] — 0…100 (тепло нижче 30 — шкодить здоров'ю)
[code]me.tool[/code] (Axe / Sickle / None), [code]me.clothes[/code] (Clothes / WarmClothes), [code]me.carrying[/code], [code]me.home[/code]

[b]Дії[/b] (тривалі можна обмежити: amount=, until=time(18, 0), hours=2)
[code]me.go_to(об'єкт)[/code], [code]me.go_home()[/code], [code]me.travel_time(об'єкт)[/code]
[code]me.take(Axe, склад)[/code], [code]me.take(Sickle, склад)[/code], [code]me.put_back(склад)[/code]
[code]me.chop_wood(ліс)[/code] — дрова в руки; повертає кількість
[code]me.drop(склад)[/code] — покласти все, що несе;  [code]me.pick_up(склад, "bread", 1)[/code]
[code]me.eat("bread")[/code]
[code]me.sleep(until=time(6, 0))[/code] — спати вдома;  [code]me.rest(hours=1)[/code];  [code]me.wait_until(time(6, 0))[/code]

[b]Тепло й зима[/b]
[code]me.wear(WarmClothes)[/code] / [code]me.wear(Clothes)[/code] — переодягтися вдома (одяг лежить у скрині)
[code]me.heat(будинок, amount=None)[/code] — покласти в піч дрова, які несеш (1 дрова горить 2 год)
[code]будинок.firewood, будинок.temperature, будинок.is_heated, будинок.has(WarmClothes)[/code]

[b]Хліб: поле → зерно → жорна → борошно → піч → хліб[/b]
[code]me.sow(поле)[/code] — сіяти (10 зерна в руках; лише в теплу пору); поле росте 4 дні
[code]me.harvest(поле)[/code] — жати серпом; зерно в руки
[code]me.grind(будинок)[/code] — змолоти зерно, яке несеш, на борошно (жорна в будинку)
[code]me.bake(будинок)[/code] — спекти хліб із борошна (піч має горіти)
[code]поле.state[/code] ("empty" / "sown" / "ripe"), [code]поле.is_ripe, поле.growth, поле.grain_left[/code]

[b]Будівництво й заселення[/b]
«Будувати» вгорі → тип → клік на карті → назва. Далі — кодом:
[code]me.deliver(будова)[/code] — віднести на будову матеріали з рук;  [code]me.build(будова)[/code] — будувати
[code]будова.is_built, будова.progress, будова.needs[/code] ({"wood": 12} — чого бракує)
[code]me.move_into(будинок)[/code];  [code]будинок.capacity, будинок.residents, будинок.free_places[/code]

[b]Глобальні змінні[/b]
Вкладка «Глобальні змінні»: накази для всіх, напр. WINTER_MODE = True. У коді — лише читання: [code]if WINTER_MODE: ...[/code]

[b]Склад і ліс[/b]
[code]склад.amount("wood")[/code] — також "bread", "grain", "flour";  [code]склад.has_tool(Axe)[/code];  [code]ліс.trees_left[/code]

[b]Винятки[/b] (усі — ActionError: [code]e.reason[/code], [code]e.done[/code])
NoItemError, ReservedError, TooHeavyError, NoHomeError, NotFoundError, WorkplaceBusyError (жорна чи піч зайняті).
Необроблений виняток зупиняє код персонажа — дивіться журнал подій.

[b]Керування[/b]
Пробіл — пауза; 1 / 2 / 3 — швидкість ×1 / ×3 / ×10; Ctrl+Enter — застосувати код.
Клік на персонажа — вибрати. Камера: ПКМ — обертати, СКМ або WASD — рухати, коліщатко — масштаб.
"""
