# Приклад зі «API_прототипу.md», розділ 8 — без змін.

@on(Event.RAIN, priority=1)
def hide_from_rain(me):
    me.go_home()
    me.wait_until(lambda: not world.is_raining, timeout=hours(3))

storage = get("Склад_1")
forest = get("Ліс_1")

while True:
    me.wait_until(time(6, 0))

    if me.hunger > 50:
        try:
            me.pick_up(storage, "bread", 1)
            me.eat("bread")
        except NoItemError:
            notify("Хліб закінчився")

    try:
        me.take(Axe, storage)
        me.chop_wood(forest, until=time(18, 0))
    except NoItemError:
        notify("Немає вільної сокири")
    except TooHeavyError as e:
        notify(f"Руки повні: нарубано {e.done}")

    me.drop(storage)
    me.put_back(storage)
    me.sleep(until=time(6, 0))
