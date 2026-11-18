# Спільний код еталонного поселення: виконується в кожного персонажа першим.
# Тут — функції, ролі, правила для всіх і розпорядок дня main().
# Код персонажа виконується після спільного й має пріоритет — досить вибрати роль:
#   Остап, Богдан:  work = woodcutter
#   Марта:          work = farmer
#   Ярина:          work = baker
#                   def evening():
#                       if world.season == Season.WINTER:
#                           keep_fire(NIGHT_FIREWOOD)
# З цим кодом поселення переживає першу зиму (тест mvp_winter_survival).

storage = get("Склад_1")
house = get("Будинок_1")
forest = get("Ліс_1")
field = get("Поле_1")

SEEDS = 10           # зерно, яке завжди лишається на посів
BREAD_STOCK = 30     # пекарка пече, поки на складі менше хліба
NIGHT_FIREWOOD = 8   # стільки дров має бути в печі на зимову ніч (одна одиниця горить 2 год)


# ---- правила для всіх

@on(Event.RAIN, priority=1)
def hide_from_rain(me):
    me.go_home()
    me.wait_until(lambda: not world.is_raining, timeout=hours(3))


# ---- допоміжні функції

def eat_until(limit):
    """Їсти хліб, поки голод вищий за limit."""
    while me.hunger > limit:
        try:
            me.pick_up(storage, "bread", 1)
        except NoItemError:
            notify("Хліб закінчився!")
            return
        me.eat("bread")


def dress_for_season():
    if world.season == Season.WINTER and me.clothes != WarmClothes:
        me.wear(WarmClothes)
    elif world.season == Season.WARM and me.clothes != Clothes:
        me.wear(Clothes)


def keep_fire(units):
    """Докласти дров у піч, щоб їх було щонайменше units."""
    need = min(int(units - house.firewood) + 1, storage.amount("wood"))
    if need > 0:
        me.pick_up(storage, "wood", need)
        me.heat(house)


# ---- ролі: кожна виконує одне завдання й повертає True, якщо щось зробила

def woodcutter():
    try:
        me.take(Axe, storage)
    except (NoItemError, ReservedError) as e:
        notify(f"Без сокири: {e.reason}")
        return False
    try:
        me.chop_wood(forest, until=time(18, 0))
    except TooHeavyError:
        pass
    me.drop(storage)
    return True


def baker():
    grain = storage.amount("grain") - SEEDS
    if storage.amount("bread") >= BREAD_STOCK or grain <= 0:
        return False
    keep_fire(3)
    me.pick_up(storage, "grain", min(grain, 20))
    me.grind(house)
    me.bake(house)
    me.drop(storage)
    return True


def farmer():
    # Пече лише пекарка: якби фермерка теж брала зерно, обидві могли б одночасно
    # побачити «зайве» зерно на складі й разом забрати те, що лишали на посів.
    if field.state == "empty" and world.season == Season.WARM and storage.amount("grain") >= SEEDS:
        me.pick_up(storage, "grain", SEEDS)
        me.sow(field)
        return True
    if field.is_ripe:
        me.take(Sickle, storage)
        try:
            me.harvest(field, until=time(19, 0))
        except TooHeavyError:
            pass
        me.drop(storage)
        return True
    return False


# ---- розпорядок дня; персонаж змінює його, перевизначаючи work() чи evening()

def work():
    """Роль персонажа; без ролі він відпочиває."""
    return False


def evening():
    """Що зробити ввечері після роботи."""
    pass


def main():
    while True:
        if not (time(6, 0) <= world.time < time(18, 0)):
            # Код (пере)запущено ввечері чи вночі — після зміни коду або завантаження гри: спершу виспатися.
            me.sleep(until=time(6, 0))
        dress_for_season()
        while world.time < time(18, 0):
            eat_until(40)
            if not work():
                me.go_home()  # узимку надворі холодно — відпочивати краще вдома
                me.rest(hours=1)
        if me.tool is not None:
            me.put_back(storage)
        evening()
        me.drop(storage)
        eat_until(10)
        me.sleep(until=time(6, 0))
