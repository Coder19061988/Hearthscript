extends RefCounted
## F1 help: short reference of the API (full text: API_прототипу.md and API_MVP.md in the repository).

const TEXT := """[font_size=20][b]Kindlescript — довідка[/b][/font_size]

Персонажі живуть лише за вашим Python-кодом: їжа, сон, тепло і робота — усе через код.
Кожна дія займає ігровий час; кожен рядок коду — 2 ігрові секунди «роздумів».
Мета: пережити першу зиму (тепла пора — 10 днів, зима — 5). Коли зима скінчиться або загинуть усі, гра покаже підсумок.

[b]Як складається код персонажа[/b]
1. [b]Спільний код[/b] (вкладка «Спільний код») — один на всіх, виконується першим: функції, ролі, правила, main().
2. [b]Код персонажа[/b] — виконується після спільного й має пріоритет: напр. [code]def work(): ...[/code] замінює спільну work().
3. Потім гра викликає [code]main()[/code] зі спільного коду.

[b]Правила[/b] (переривають будь-який код, потім перервана дія триває)
[code]@on(Event.RAIN)[/code] — подія;  [code]@on(time(22, 0))[/code] — щодня о 22:00;
[code]@on(lambda: world.time >= time(22, 0) or world.time < time(6, 0))[/code] — коли умова стає істинною
Код персонажа сильніший за спільні правила:
  • його власне правило (напр. [code]@on(time(21, 0))[/code] — зміна до 23:00) спільне не перериває: нічний сон чекає кінця зміни;
  • правило з тим самим іменем (свій [code]def night_sleep[/code]) замінює спільне;
  • [code]off(night_sleep)[/code] — відмінити спільне правило для цього персонажа.

[b]Обмежене знання[/b]
Персонаж бачить склад, поле, піч чи іншого персонажа лише стоячи поруч, а пам'ятає таким, яким бачив востаннє.
На старті він знає лише свій будинок. Про небачене: кількості — 0, [code]поле.state[/code] — "unknown".
[code]склад.known_at[/code] — коли бачив (None — ніколи);  [code]склад.known_ago > hours(6)[/code] — давно не бачив
Тож спершу подивіться: [code]me.go_to(склад)[/code], потім [code]склад.amount("wood")[/code].
Дошка оголошень ([code]get("Дошка_1")[/code]): [code]me.post(дошка, склад, поле)[/code] — записати, що знаєш (без об'єктів — усе);
[code]me.read(дошка)[/code] — дізнатися новіше з записів. Обидві дії — біля дошки й займають кілька хвилин.

[b]Глобальні функції[/b]
[code]get("Склад_1")[/code] — об'єкт за назвою (інакше NotFoundError)
[code]buildings(Storage)[/code] — список будівель типу Storage / House / Forest / Field / Board
[code]time(18, 0)[/code], [code]hours(2)[/code], [code]minutes(30)[/code] — час доби і тривалість
[code]notify("текст")[/code] — запис у журнал подій;  [code]print(...)[/code] — теж у журнал
[code]@on(Event.RAIN, priority=1)[/code] — обробник події (менше число — вищий пріоритет)

[b]Світ (лише читання)[/b]
[code]world.time, world.day, world.now[/code] (день і час),  [code]world.is_night, world.is_raining[/code]
[code]world.season[/code] (Season.WARM / Season.WINTER), [code]world.season_days_left[/code], [code]world.temperature[/code] (°C)

[b]Персонаж: me[/b]
[code]me.name, me.hunger, me.fatigue, me.warmth, me.health[/code] — 0…100 (тепло нижче 30 — шкодить здоров'ю)
[code]me.tool[/code] (Axe / Sickle / None), [code]me.clothes[/code] (Clothes / WarmClothes / None), [code]me.clothes_condition[/code] (%), [code]me.carrying[/code], [code]me.home[/code]

[b]Дії[/b] (тривалі можна обмежити: amount=, until=time(18, 0), hours=2)
[code]me.go_to(об'єкт)[/code], [code]me.go_home()[/code], [code]me.travel_time(об'єкт)[/code]
[code]me.take(Axe, склад)[/code] — найкращу цілу вільну;  [code]me.take(сокира)[/code] — саме цю;  [code]me.put_back(склад)[/code]
[code]me.chop_wood(ліс)[/code] — дрова в руки; повертає кількість
[code]me.drop(склад)[/code] — покласти все, що несе;  [code]me.pick_up(склад, "bread", 1)[/code]
[code]me.eat("bread")[/code]
[code]me.sleep(until=time(6, 0))[/code] — спати вдома;  [code]me.rest(hours=1)[/code];  [code]me.wait_until(time(6, 0))[/code]

[b]Інструменти зношуються[/b] (~40 год рубки, жнив чи косіння; їх не ремонтують — бережіть)
[code]me.tool.condition[/code] — стан, %;  [code]me.tool.is_broken[/code];  [code]me.tool.quality[/code] (поки 1);  [code]isinstance(me.tool, Axe)[/code]
[code]tools(Axe, free=True)[/code] — цілі вільні сокири, про які персонаж знає (бачив на складі)
Поломка посеред роботи — [code]ToolBrokenError[/code] ([code]e.done[/code] — скільки встигли). Ремонту немає:
[code]me.dismantle()[/code] — розібрати інструмент у руках на брухт і дерево (брухт купує купець)

[b]Тепло й зима[/b]
[code]me.wear(WarmClothes)[/code] / [code]me.wear(Clothes)[/code] — переодягтися вдома: зі скрині береться комплект у найкращому стані
Одяг зношується, поки його носять (звичайний ~20 днів, теплий ~14); зношений стає ганчір'ям ("rags") у руках
[code]me.heat(будинок, amount=None)[/code] — покласти в піч дрова, які несеш (1 дрова горить 2 год)
[code]будинок.firewood, будинок.temperature, будинок.is_heated, будинок.has(WarmClothes)[/code]

[b]Хліб: поле → зерно → жорна → борошно → піч → хліб[/b]
[code]me.sow(поле)[/code] — сіяти (10 зерна в руках; лише в теплу пору); поле росте 4 дні
[code]me.sow(поле, crop="flax")[/code] — посіяти льон (10 льону в руках; урожай — 40 льону);  [code]поле.crop[/code] ("grain" / "flax" / None)
[code]me.harvest(поле)[/code] — жати серпом; урожай (зерно чи льон) — у руки
[code]me.grind(будинок)[/code] — змолоти зерно, яке несеш, на борошно (жорна в будинку)
[code]me.bake(будинок)[/code] — спекти хліб із борошна (піч має горіти)
[code]поле.state[/code] ("empty" / "sown" / "ripe" / "unknown"), [code]поле.is_ripe, поле.growth, поле.grain_left[/code]

[b]Тварини[/b] (купуються в торгівлі й живуть у хліві: «Будувати» → «Хлів»)
[code]animals(Cow)[/code], [code]animals(Chicken)[/code];  [code]тварина.hunger, .health, .is_alive, .home[/code];  [code]корова.milk[/code], [code]курка.eggs[/code]
У теплу пору пасуться самі, узимку — лише на кормі: [code]me.feed(тварина)[/code] (корові — сіно, курці — зерно з рук)
[code]me.milk(корова)[/code], [code]me.collect_eggs(курка)[/code];  [code]me.slaughter(тварина)[/code] — м'ясо в руки
Сіно: [code]me.take(Scythe, склад)[/code], [code]me.mow(get("Луг_1"))[/code] — лише в теплу пору; корові на зиму ~30 сіна
Їжа: [code]me.eat("bread")[/code], а також "meat", "milk", "eggs"

[b]Торгівля[/b] (купець приїжджає на торговий пост раз на 5 днів, 08:00–20:00, і торгує, коли від'їжджає)
Торговий пост ставлять кресленням; монети лежать в управі: [code]get("Управа_1").coins[/code].
Кнопка «Управа» вгорі: зведення (ціни купця, замовлення, склади) і діаграми запасів, людей, монет і тварин по днях.
[code]me.drop(post)[/code] — покласти товар на продаж;  [code]me.order(post, "grain", 10)[/code], [code]me.order(post, Cow)[/code] — замовити
Куплене купець лишає на посту: [code]me.pick_up(post, "grain", 10)[/code], [code]me.take(Axe, post)[/code]; тварин — одразу в хлів
[code]merchant.is_here, merchant.days_until, merchant.price(Cow), merchant.offer("wood")[/code];  [code]@on(Event.MERCHANT_ARRIVED)[/code]
Одяг теж можна нести: [code]me.pick_up(post, "warm_clothes", 1)[/code], [code]me.drop(будинок)[/code] — у скриню

[b]Майстерні[/b] (виготовлення за рецептами; матеріали — з рук)
[code]me.craft(Axe, кузня)[/code] — брухт 2 + дрова 2 → сокира (2 год); коса й кайло — так само; серп — брухт 1 + дрова 1 (1,5 год)
Кузню ставлять кресленням (40 дров, 12 год); одне робоче місце — горн. Новий інструмент — у руки (вони мають бути вільні).
[code]me.grind(будинок)[/code] і [code]me.bake(будинок)[/code] — це [code]me.craft("flour", будинок)[/code] і [code]me.craft("bread", будинок)[/code]
Кравецька (30 дров, 8 год): [code]me.craft("cloth", кравецька)[/code] — льон 2 → тканина (1 год, ткацький верстат);
[code]me.craft(Clothes, кравецька)[/code] — тканина 3 → одяг (2 год), [code]WarmClothes[/code] — тканина 5 (3 год); одяг — у скриню: [code]me.drop(будинок)[/code]
Лісопилка (30 дров, 10 год): [code]me.craft("planks", лісопилка)[/code] — дрова 1 → дошки (20 хв)
Столярня (10 дров + 20 дощок, 8 год): [code]me.craft("bed", столярня)[/code] — дошки 4 + сіно 3 → ліжко (3 год)
Ліжко ставлять у будинок: [code]me.drop(будинок)[/code];  [code]будинок.beds[/code]. У ліжку сон знімає втому швидше (16/год проти 12)

[b]Камінь і метал[/b]
[code]me.take(Pickaxe, склад)[/code], [code]me.mine(get("Кар'єр_1"))[/code] — камінь (15 хв/од.), [code]me.mine(get("Рудня_1"))[/code] — руда (30 хв/од.)
[code]родовище.resource[/code] ("stone" / "ore"), [code]родовище.left[/code] — скільки лишилося (бачене)
Вуглярня (20 каменю + 10 дров, 8 год): [code]me.craft("charcoal", вуглярня)[/code] — дрова 3 → вугілля (2 год)
Кузня: [code]me.craft("iron", кузня)[/code] — руда 2 + вугілля 1 → залізо (1 год); інструменти — із брухту або із заліза (залізо 2 + дрова 2)

[b]Вода[/b] (колодязь: 15 каменю + 5 дров, 6 год)
[code]me.draw_water(колодязь)[/code] — набрати води (відро 10 кг, 5 хв; без обмежень — до повних рук)
[code]me.water(поле)[/code] — полити: 5 відер, 30 хв; полите поле добу росте на 50% швидше (дощ теж поливає);  [code]поле.is_watered[/code]

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
NoItemError, ReservedError, TooHeavyError, NoHomeError, NotFoundError, WorkplaceBusyError (жорна чи піч зайняті),
ToolBrokenError (інструмент зламався).
Необроблений виняток зупиняє код персонажа — дивіться журнал подій.

[b]Збереження[/b]
Меню «Гра» → «Зберегти…» / «Завантажити…» (файли — у теці збережень гри). Після завантаження гра на паузі,
а персонажі починають свій код спочатку; прогрес роботи (поле, будова, недорубане) зберігається.

[b]Керування[/b]
Пробіл — пауза; 1 / 2 / 3 — швидкість ×1 / ×3 / ×10; Ctrl+Enter — застосувати код.
Клік на персонажа — вибрати. Камера: ПКМ — обертати, СКМ або WASD — рухати, коліщатко — масштаб.
[b]V[/b] — від першої особи: пройтися поселенням між персонажами (на гру це не впливає).
Миша — роззиратися, WASD — іти, Shift — бігти, клік — вибрати персонажа під прицілом; V або Esc — назад до огляду.
"""
