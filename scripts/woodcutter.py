# Лісоруб: щодня рубає дрова рейсами до вечора, їсть, коли голодний,
# ховається вдома від дощу. Еталонний «хороший» скрипт для перевірки темпу
# (тест tempo_workday і навантажувальний тест).

storage = get("Склад_1")
forest = get("Ліс_1")


@on(Event.RAIN, priority=1)
def hide_from_rain(me):
    me.go_home()
    me.wait_until(lambda: not world.is_raining, timeout=hours(3))


def eat_if_hungry():
    if me.hunger > 40:
        try:
            me.pick_up(storage, "bread", 1)
            me.eat("bread")
        except NoItemError:
            notify("Хліб закінчився")


while True:
    me.wait_until(time(6, 0))
    eat_if_hungry()
    try:
        me.take(Axe, storage)
    except (NoItemError, ReservedError) as e:
        notify(f"Без сокири: {e.reason}")
        me.sleep(until=time(6, 0))
        continue

    # Рейси: рубати, поки руки не повні або не настане 18:00, віднести, поїсти.
    while world.time < time(17, 0):
        try:
            me.chop_wood(forest, until=time(18, 0))
        except TooHeavyError:
            pass
        except ToolBrokenError as e:
            notify(f"Зламалася сокира, нарубано {e.done}")
            me.drop(storage)
            me.dismantle()  # інструменти не ремонтують: руки вже вільні — брухт і дерево на склад
            me.drop(storage)
            break
        me.drop(storage)
        eat_if_hungry()

    if me.tool is not None:
        me.put_back(storage)
    eat_if_hungry()
    me.sleep(until=time(6, 0))
