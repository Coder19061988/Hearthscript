# Поселення: один скрипт для всіх персонажів, роль — за іменем.
#   Остап, Богдан — лісоруби: рубають дрова рейсами до вечора.
#   Марта — фермерка: сіє і жне, а поки поле росте — відпочиває.
#   Ярина — пекарка: меле зерно, пече хліб і тримає піч розпаленою.
# День складається з коротких завдань до 18:00; між ними персонаж їсть,
# а якщо роботи немає — годину відпочиває вдома й перевіряє знову.
# Узимку всі вдягають теплий одяг, а на ніч у печі лишаються дрова.
# Еталонний «хороший» скрипт: з ним поселення переживає першу зиму (тест mvp_winter_survival).

storage = get("Склад_1")
house = get("Будинок_1")
forest = get("Ліс_1")
field = get("Поле_1")

SEEDS = 10           # зерно, яке завжди лишається на посів
BREAD_STOCK = 30     # пекарка пече, поки на складі менше хліба
NIGHT_FIREWOOD = 8   # стільки дров має бути в печі на зимову ніч (одна одиниця горить 2 год)


@on(Event.RAIN, priority=1)
def hide_from_rain(me):
    me.go_home()
    me.wait_until(lambda: not world.is_raining, timeout=hours(3))


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


# Кожна роль — одне завдання. Повертає True, якщо щось зробила.

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
    # Пече лише пекарка: якби фермерка теж брала зерно, обидві могли б одночасно
    # побачити «зайве» зерно на складі й разом забрати те, що лишали на посів.
    return False


ROLES = {"Остап": woodcutter, "Богдан": woodcutter, "Марта": farmer, "Ярина": baker}
role = ROLES.get(me.name, woodcutter)

while True:
    me.wait_until(time(6, 0))
    dress_for_season()
    while world.time < time(18, 0):
        eat_until(40)
        if not role():
            me.go_home()  # узимку надворі холодно — відпочивати краще вдома
            me.rest(hours=1)
    if me.tool is not None:
        me.put_back(storage)
    if role == baker and world.season == Season.WINTER:
        keep_fire(NIGHT_FIREWOOD)
    me.drop(storage)
    eat_until(10)
    me.sleep(until=time(6, 0))
