# Kindlescript — prelude: the Python API available to character scripts.
#
# Executed in every character VM (module `kindle`) before the player's code; public names are
# copied into builtins. Lines of this file are not charged as the character's "thinking" time.
# Natives live in the `_kn` module (core/runtime.cpp). The API itself is specified in
# API_прототипу.md — keep names and semantics in sync with it.

import _kn
import builtins as _builtins
import inspect as _inspect

_DAY = 86400.0
_POLL_SECONDS = 60.0
_RESOURCES = ("wood", "bread", "grain", "flour", "hay", "milk", "eggs", "meat")


def _is_number(x):
    return (isinstance(x, int) or isinstance(x, float)) and not isinstance(x, bool)


def _tname(x):
    return type(x).__name__


# ------------------------------------------------------------------ time

class Duration:
    """Тривалість: hours(2), minutes(30)."""

    def __init__(self, seconds):
        self.seconds = float(seconds)

    @property
    def hours(self):
        return self.seconds / 3600.0

    @property
    def minutes(self):
        return self.seconds / 60.0

    def _other(self, other):
        if not isinstance(other, Duration):
            raise TypeError(f"тривалість можна порівнювати лише з тривалістю (hours(...), minutes(...)), а не з {_tname(other)}")
        return other.seconds

    def __add__(self, other):
        if isinstance(other, Duration):
            return Duration(self.seconds + other.seconds)
        if isinstance(other, TimeOfDay):
            return other + self
        return NotImplemented

    def __sub__(self, other):
        if isinstance(other, Duration):
            return Duration(self.seconds - other.seconds)
        return NotImplemented

    def __mul__(self, k):
        if _is_number(k):
            return Duration(self.seconds * k)
        return NotImplemented

    def __rmul__(self, k):
        return self.__mul__(k)

    def __truediv__(self, k):
        if _is_number(k):
            return Duration(self.seconds / k)
        return NotImplemented

    def __neg__(self):
        return Duration(-self.seconds)

    def __eq__(self, other):
        return isinstance(other, Duration) and self.seconds == other.seconds

    def __ne__(self, other):
        return not self.__eq__(other)

    def __lt__(self, other):
        return self.seconds < self._other(other)

    def __le__(self, other):
        return self.seconds <= self._other(other)

    def __gt__(self, other):
        return self.seconds > self._other(other)

    def __ge__(self, other):
        return self.seconds >= self._other(other)

    def __hash__(self):
        return hash(self.seconds)

    def __repr__(self):
        total = int(round(abs(self.seconds)))
        h = total // 3600
        m = (total % 3600) // 60
        s = total % 60
        parts = []
        if h:
            parts.append(f"{h} год")
        if m:
            parts.append(f"{m} хв")
        if s or not parts:
            parts.append(f"{s} с")
        return ("-" if self.seconds < 0 else "") + " ".join(parts)

    __str__ = __repr__


class TimeOfDay:
    """Час доби: time(18, 0). Порівняння діють у межах однієї доби (00:00–24:00)."""

    def __init__(self, seconds):
        self.seconds = float(seconds)

    @property
    def hour(self):
        return int(self.seconds // 3600) % 24

    @property
    def minute(self):
        return int(self.seconds % 3600 // 60)

    def _other(self, other):
        if not isinstance(other, TimeOfDay):
            raise TypeError(f"час доби можна порівнювати лише з часом доби, наприклад time(18, 0), а не з {_tname(other)}")
        return other.seconds

    def __add__(self, other):
        if isinstance(other, Duration):
            return TimeOfDay((self.seconds + other.seconds) % _DAY)
        return NotImplemented

    def __radd__(self, other):
        return self.__add__(other)

    def __sub__(self, other):
        if isinstance(other, Duration):
            return TimeOfDay((self.seconds - other.seconds) % _DAY)
        if isinstance(other, TimeOfDay):
            return Duration(self.seconds - other.seconds)
        return NotImplemented

    def __eq__(self, other):
        return isinstance(other, TimeOfDay) and self.seconds == other.seconds

    def __ne__(self, other):
        return not self.__eq__(other)

    def __lt__(self, other):
        return self.seconds < self._other(other)

    def __le__(self, other):
        return self.seconds <= self._other(other)

    def __gt__(self, other):
        return self.seconds > self._other(other)

    def __ge__(self, other):
        return self.seconds >= self._other(other)

    def __hash__(self):
        return hash(self.seconds)

    def __repr__(self):
        if self.seconds >= _DAY:
            return "24:00"
        total = int(self.seconds)
        return f"{total // 3600:02d}:{total % 3600 // 60:02d}"

    __str__ = __repr__


class Moment:
    """Момент гри: день і час доби (world.now, obj.known_at). Різниця моментів — тривалість."""

    def __init__(self, seconds):
        self.seconds = float(seconds)

    @property
    def day(self):
        return int(self.seconds // _DAY) + 1

    @property
    def time(self):
        return TimeOfDay(self.seconds % _DAY)

    def _other(self, other):
        if not isinstance(other, Moment):
            raise TypeError(f"момент гри можна порівнювати лише з моментом (world.now, obj.known_at), а не з {_tname(other)}")
        return other.seconds

    def __add__(self, other):
        if isinstance(other, Duration):
            return Moment(self.seconds + other.seconds)
        return NotImplemented

    def __radd__(self, other):
        return self.__add__(other)

    def __sub__(self, other):
        if isinstance(other, Moment):
            return Duration(self.seconds - other.seconds)
        if isinstance(other, Duration):
            return Moment(self.seconds - other.seconds)
        return NotImplemented

    def __eq__(self, other):
        return isinstance(other, Moment) and self.seconds == other.seconds

    def __ne__(self, other):
        return not self.__eq__(other)

    def __lt__(self, other):
        return self.seconds < self._other(other)

    def __le__(self, other):
        return self.seconds <= self._other(other)

    def __gt__(self, other):
        return self.seconds > self._other(other)

    def __ge__(self, other):
        return self.seconds >= self._other(other)

    def __hash__(self):
        return hash(self.seconds)

    def __repr__(self):
        return f"день {self.day}, {self.time}"

    __str__ = __repr__


def time(h, m=0):
    """Час доби, наприклад time(18, 0)."""
    if not isinstance(h, int) or not isinstance(m, int):
        raise TypeError("time(h, m) приймає цілі числа: time(18, 0)")
    if h == 24 and m == 0:
        return TimeOfDay(_DAY)
    if not (0 <= h < 24 and 0 <= m < 60):
        raise ValueError(f"некоректний час доби: {h}:{m}")
    return TimeOfDay(h * 3600 + m * 60)


def hours(n):
    """Тривалість у годинах: hours(2)."""
    if not _is_number(n):
        raise TypeError("hours(n) приймає число")
    return Duration(n * 3600.0)


def minutes(n):
    """Тривалість у хвилинах: minutes(30)."""
    if not _is_number(n):
        raise TypeError("minutes(n) приймає число")
    return Duration(n * 60.0)


# ------------------------------------------------------------------ world, events, errors

class _World:
    @property
    def time(self):
        return TimeOfDay(_kn.world_time())

    @property
    def day(self):
        return _kn.world_day()

    @property
    def now(self):
        """Поточний момент (день і час): world.now - storage.known_at — скільки часу минуло."""
        return Moment(_kn.world_now())

    @property
    def is_night(self):
        return _kn.world_is_night()

    @property
    def is_raining(self):
        return _kn.world_is_raining()

    @property
    def season(self):
        return _kn.world_season()

    @property
    def season_days_left(self):
        return _kn.world_season_days_left()

    @property
    def temperature(self):
        return _kn.world_temperature()

    def __repr__(self):
        return f"world(день {self.day}, {self.time})"


world = _World()


class Event:
    RAIN = "RAIN"
    RAIN_STOPPED = "RAIN_STOPPED"
    TIME = "TIME"            # event.kind for @on(time(22, 0))
    CONDITION = "CONDITION"  # event.kind for @on(lambda: ...)


_EVENTS = (Event.RAIN, Event.RAIN_STOPPED)


class Season:
    WARM = "WARM"
    WINTER = "WINTER"


class EventInfo:
    def __init__(self, kind):
        self.kind = kind

    def __repr__(self):
        return f"EventInfo({self.kind})"


class _ErrorInfo:
    # pocketpy exceptions have no __dict__ and keep a single `args` value, so reason and done
    # travel together in this object; str() of it is the reason, which keeps str(e) readable.
    def __init__(self, reason, done):
        self.reason = reason
        self.done = done

    def __str__(self):
        return str(self.reason)

    def __repr__(self):
        return repr(self.reason)


class ActionError(Exception):
    """Базовий клас для всіх невдалих дій: e.reason — причина, e.done — скільки встигли зробити."""

    def __init__(self, reason="", done=0):
        super().__init__(_ErrorInfo(reason, done))

    def _info(self):
        args = self.args
        return args[0] if len(args) > 0 and isinstance(args[0], _ErrorInfo) else None

    @property
    def reason(self):
        info = self._info()
        return info.reason if info is not None else ""

    @property
    def done(self):
        info = self._info()
        return info.done if info is not None else 0


class NoItemError(ActionError):
    pass


class ReservedError(ActionError):
    pass


class TooHeavyError(ActionError):
    pass


class NoHomeError(ActionError):
    pass


class NotFoundError(ActionError):
    pass


class WorkplaceBusyError(ActionError):
    pass


class ToolBrokenError(ActionError):
    pass


# ------------------------------------------------------------------ tools and objects

class Tool:
    """Інструмент. Axe і Sickle — типи (me.take(Axe, склад)); me.tool і tools(...) повертають самі
    інструменти: quality (якість 1–5), condition (стан, %), is_broken, where (склад чи персонаж).
    Дані — такі, якими персонаж їх знає на момент виклику. Тип: isinstance(me.tool, Axe)."""
    _kind = ""

    def __init__(self, tid=-1, quality=1, condition=100.0, where=None):
        self._id = tid
        self._quality = quality
        self._condition = condition
        self._where = where

    @property
    def quality(self):
        return self._quality

    @property
    def condition(self):
        return self._condition

    @property
    def is_broken(self):
        return self._condition <= 0

    @property
    def where(self):
        return self._where

    def __eq__(self, other):
        return isinstance(other, Tool) and other._id == self._id

    def __ne__(self, other):
        return not self.__eq__(other)

    def __hash__(self):
        return hash(self._id)

    def __repr__(self):
        state = "зламаний" if self.is_broken else f"стан {self._condition:.0f}%"
        return f"{_tname(self)}(якість {self._quality}, {state})"


class Axe(Tool):
    _kind = "axe"


class Sickle(Tool):
    _kind = "sickle"


class Scythe(Tool):
    _kind = "scythe"


_TOOLS = {"axe": Axe, "sickle": Sickle, "scythe": Scythe}


def _tool_from(info):
    tid, kind, quality, condition, where = info
    return _TOOLS[kind](tid, quality, condition, _wrap(where))


class Clothes:
    """Звичайний (робочий) одяг."""
    _kind = "clothes"


class WarmClothes(Clothes):
    """Теплий одяг для зими."""
    _kind = "warm_clothes"


_CLOTHES = {"clothes": Clothes, "warm_clothes": WarmClothes}


def _clothes_kind(item):
    if isinstance(item, type) and issubclass(item, Clothes):
        return item._kind
    raise TypeError(f"очікується одяг: Clothes або WarmClothes, а не {_tname(item)}")


def _tool_kind(tool):
    if isinstance(tool, type) and issubclass(tool, Tool) and tool is not Tool:
        return tool._kind
    raise TypeError(f"очікується тип інструмента, наприклад Axe, а не {_tname(tool)}")


def _check_resource(resource):
    if resource not in _RESOURCES:
        raise ValueError(f"невідомий ресурс {resource!r}; доступні: " + ", ".join(_RESOURCES))


class GameObject:
    _kind = ""

    def __init__(self, oid, name):
        self._id = oid
        self._name = name

    @property
    def name(self):
        return self._name

    @property
    def known_at(self):
        """Коли персонаж востаннє бачив цей об'єкт (сам чи з дошки оголошень); None — ніколи."""
        t = _kn.known_at(self._id)
        return None if t is None else Moment(t)

    @property
    def known_ago(self):
        """Як давно персонаж знає стан об'єкта (тривалість); None — ніколи не бачив."""
        t = _kn.known_at(self._id)
        return None if t is None else Duration(_kn.world_now() - t)

    def __eq__(self, other):
        return isinstance(other, GameObject) and other._id == self._id

    def __ne__(self, other):
        return not self.__eq__(other)

    def __hash__(self):
        return hash(self._id)

    def __repr__(self):
        return f"{_tname(self)}(\"{self._name}\")"


class Building(GameObject):
    """Склад, будинок, поле або дошка. Поставлене гравцем креслення — будова, доки не збудована.
    Стан будівлі персонаж знає лише таким, яким бачив його востаннє (known_at)."""

    @property
    def is_built(self):
        return _kn.building_info(self._id, "built")

    @property
    def progress(self):
        """Скільки збудовано, 0..1."""
        return _kn.building_info(self._id, "progress")

    @property
    def needs(self):
        """Яких матеріалів ще бракує на будові: {"wood": 12}."""
        return _kn.building_info(self._id, "needs")


class Storage(Building):
    _kind = "storage"

    def amount(self, resource):
        _check_resource(resource)
        return _kn.storage_amount(self._id, resource)

    def has_tool(self, tool):
        return _kn.storage_has_tool(self._id, _tool_kind(tool))


class House(Building):
    _kind = "house"

    @property
    def capacity(self):
        return _kn.building_info(self._id, "capacity")

    @property
    def residents(self):
        return [_wrap(info) for info in _kn.building_info(self._id, "residents")]

    @property
    def free_places(self):
        return self.capacity - len(_kn.building_info(self._id, "residents"))

    @property
    def firewood(self):
        return _kn.house_info(self._id, "firewood")

    @property
    def temperature(self):
        return _kn.house_info(self._id, "temperature")

    @property
    def is_heated(self):
        return _kn.house_info(self._id, "heated")

    def has(self, item):
        """Чи є в скрині будинку одяг: house.has(WarmClothes)."""
        return _kn.house_chest(self._id, _clothes_kind(item)) > 0


class Forest(GameObject):
    _kind = "forest"

    @property
    def trees_left(self):
        return _kn.forest_trees(self._id)


class Field(Building):
    _kind = "field"

    @property
    def state(self):
        """"empty", "sown", "ripe" або "unknown" — якщо персонаж ще не бачив поля."""
        return _kn.field_info(self._id, "state")

    @property
    def is_ripe(self):
        return _kn.field_info(self._id, "state") == "ripe"

    @property
    def growth(self):
        return _kn.field_info(self._id, "growth")

    @property
    def grain_left(self):
        return _kn.field_info(self._id, "grain_left")


class Barn(Building):
    """Хлів: тут живуть тварини; capacity — скільки місць."""
    _kind = "barn"

    @property
    def capacity(self):
        return _kn.building_info(self._id, "capacity")

    @property
    def animals(self):
        return [_wrap(info) for info in _kn.building_info(self._id, "residents")]

    @property
    def free_places(self):
        return self.capacity - len(_kn.building_info(self._id, "residents"))


class Meadow(GameObject):
    """Луг: сінокіс. me.mow(луг) — косити сіно косою."""
    _kind = "meadow"

    @property
    def hay_left(self):
        return _kn.meadow_hay(self._id)


class Animal(GameObject):
    """Тварина: Cow (корова) чи Chicken (курка). Живе в хліві (home);
    стан — такий, яким персонаж бачив її востаннє."""
    _kind = "animal"

    @property
    def hunger(self):
        return _kn.animal_info(self._id, "hunger")

    @property
    def health(self):
        return _kn.animal_info(self._id, "health")

    @property
    def is_alive(self):
        return _kn.animal_info(self._id, "alive")

    @property
    def home(self):
        return _wrap(_kn.animal_info(self._id, "home"))


class Cow(Animal):
    _kind = "cow"

    @property
    def milk(self):
        """Скільки молока можна надоїти."""
        return _kn.animal_info(self._id, "produce")


class Chicken(Animal):
    _kind = "chicken"

    @property
    def eggs(self):
        """Скільки яєць чекає на збір."""
        return _kn.animal_info(self._id, "produce")


class Board(Building):
    """Дошка оголошень: me.post(дошка, ...) записує, що персонаж знає; me.read(дошка) — читає записи."""
    _kind = "board"


class Person(GameObject):
    _kind = "person"

    @property
    def hunger(self):
        return _kn.char_stat(self._id, "hunger")

    @property
    def fatigue(self):
        return _kn.char_stat(self._id, "fatigue")

    @property
    def health(self):
        return _kn.char_stat(self._id, "health")

    @property
    def warmth(self):
        return _kn.char_stat(self._id, "warmth")

    @property
    def clothes(self):
        kind = _kn.char_clothes(self._id)
        return None if kind is None else _CLOTHES.get(kind)

    @property
    def tool(self):
        """Інструмент у руках (Axe / Sickle з quality, condition, is_broken) або None."""
        info = _kn.char_tool(self._id)  # None is unhashable in pocketpy: no dict lookup with it
        return None if info is None else _tool_from(info)

    @property
    def carrying(self):
        return _kn.char_carrying(self._id)

    @property
    def home(self):
        return _wrap(_kn.char_home(self._id))


_CLASSES = {"storage": Storage, "house": House, "forest": Forest, "field": Field, "person": Person, "board": Board,
            "barn": Barn, "meadow": Meadow, "cow": Cow, "chicken": Chicken}


def _wrap(info):
    if info is None:
        return None
    kind, oid, name = info
    return _CLASSES[kind](oid, name)


def _id_of(obj, cls, action):
    if not isinstance(obj, cls):
        raise TypeError(f"{action}(): очікується {cls.__name__}, а отримано {_tname(obj)}")
    return obj._id


def _limits(amount, until, hours_):
    p = {}
    if amount is not None:
        if not isinstance(amount, int) or isinstance(amount, bool) or amount <= 0:
            raise ValueError("amount= має бути додатним цілим числом")
        p["amount"] = amount
    if until is not None:
        if not isinstance(until, TimeOfDay):
            raise TypeError("until= очікує час доби, наприклад time(18, 0)")
        p["until"] = until.seconds
    if hours_ is not None:
        if isinstance(hours_, Duration):
            s = hours_.seconds
        elif _is_number(hours_):
            s = hours_ * 3600.0
        else:
            raise TypeError("hours= очікує число годин або тривалість, наприклад hours=2")
        if s <= 0:
            raise ValueError("hours= має бути більше нуля")
        p["hours"] = float(s)
    return p


class Me(Person):
    """Поточний персонаж. Кожна дія займає ігровий час."""

    def go_to(self, obj):
        return _kn.act("go_to", _id_of(obj, GameObject, "go_to"), None)

    def go_home(self):
        return _kn.act("go_home", -1, None)

    def take(self, tool, storage=None):
        """Взяти інструмент зі складу: me.take(Axe, склад) — найкращий цілий вільний,
        me.take(сокира) — саме цей (з tools(...)); персонаж іде туди, де його бачив."""
        if isinstance(tool, Tool):
            where = storage if storage is not None else tool._where
            if not isinstance(where, Storage):
                raise ValueError("невідомо, на якому складі лежить цей інструмент: me.take(інструмент, склад)")
            return _kn.act("take", where._id, {"tool": tool._kind, "tool_id": tool._id})
        if storage is None:
            raise TypeError("me.take(Axe, склад): вкажіть склад, з якого взяти інструмент")
        return _kn.act("take", _id_of(storage, Storage, "take"), {"tool": _tool_kind(tool)})

    def put_back(self, storage):
        return _kn.act("put_back", _id_of(storage, Storage, "put_back"), None)

    def chop_wood(self, forest, amount=None, until=None, hours=None):
        return _kn.act("chop_wood", _id_of(forest, Forest, "chop_wood"), _limits(amount, until, hours))

    def drop(self, storage):
        return _kn.act("drop", _id_of(storage, Storage, "drop"), None)

    def pick_up(self, storage, resource, amount):
        sid = _id_of(storage, Storage, "pick_up")
        _check_resource(resource)
        if not isinstance(amount, int) or isinstance(amount, bool) or amount <= 0:
            raise ValueError("pick_up(): кількість має бути додатним цілим числом")
        return _kn.act("pick_up", sid, {"resource": resource, "count": amount})

    def eat(self, resource):
        _check_resource(resource)
        return _kn.act("eat", -1, {"resource": resource})

    def sleep(self, until=None, hours=None):
        return _kn.act("sleep", -1, _limits(None, until, hours))

    def rest(self, until=None, hours=None):
        return _kn.act("rest", -1, _limits(None, until, hours))

    def wait_until(self, target, timeout=None):
        limit = None
        if timeout is not None:
            if not isinstance(timeout, Duration):
                raise TypeError("timeout= очікує тривалість, наприклад hours(3)")
            limit = timeout.seconds
        if isinstance(target, TimeOfDay):
            p = {"until": target.seconds}
            if limit is not None:
                p["timeout"] = limit
            return _kn.act("wait", -1, p)
        if callable(target):
            deadline = None if limit is None else _kn.world_now() + limit
            while True:
                if target():
                    return True
                now = _kn.world_now()
                if deadline is not None and now >= deadline:
                    return False
                step = _POLL_SECONDS if deadline is None else min(_POLL_SECONDS, deadline - now)
                _kn.act("wait", -1, {"seconds": step})
        raise TypeError("wait_until() очікує час доби (time(6, 0)) або умову (lambda: ...)")

    def heat(self, house, amount=None):
        """Покласти в піч будинку дрова, які несе персонаж (усі або amount)."""
        hid = _id_of(house, House, "heat")
        p = {}
        if amount is not None:
            if not isinstance(amount, int) or isinstance(amount, bool) or amount <= 0:
                raise ValueError("heat(): amount= має бути додатним цілим числом")
            p["amount"] = amount
        return _kn.act("heat", hid, p)

    def wear(self, clothes):
        """Переодягтися вдома: me.wear(WarmClothes) / me.wear(Clothes)."""
        return _kn.act("wear", -1, {"item": _clothes_kind(clothes)})

    def sow(self, field, until=None, hours=None):
        return _kn.act("sow", _id_of(field, Field, "sow"), _limits(None, until, hours))

    def harvest(self, field, amount=None, until=None, hours=None):
        return _kn.act("harvest", _id_of(field, Field, "harvest"), _limits(amount, until, hours))

    def grind(self, house, amount=None, until=None, hours=None):
        return _kn.act("grind", _id_of(house, House, "grind"), _limits(amount, until, hours))

    def bake(self, house, amount=None, until=None, hours=None):
        return _kn.act("bake", _id_of(house, House, "bake"), _limits(amount, until, hours))

    def deliver(self, site):
        """Віднести на будову матеріали, яких їй бракує, з тих, що несе персонаж. Повертає кількість."""
        return _kn.act("deliver", _id_of(site, Building, "deliver"), None)

    def build(self, site, until=None, hours=None):
        """Будувати за кресленням (усі матеріали мають бути на місці)."""
        return _kn.act("build", _id_of(site, Building, "build"), _limits(None, until, hours))

    def move_into(self, house):
        """Переселитися в збудований будинок (з запасним комплектом одягу)."""
        return _kn.act("move_into", _id_of(house, House, "move_into"), None)

    def mow(self, meadow, amount=None, until=None, hours=None):
        """Косити луг косою: сіно — в руки. Лише в теплу пору."""
        return _kn.act("mow", _id_of(meadow, Meadow, "mow"), _limits(amount, until, hours))

    def feed(self, animal, amount=None):
        """Нагодувати тварину кормом із рук: корову — сіном, курку — зерном. Повертає кількість."""
        aid = _id_of(animal, Animal, "feed")
        p = {}
        if amount is not None:
            if not isinstance(amount, int) or isinstance(amount, bool) or amount <= 0:
                raise ValueError("feed(): amount= має бути додатним цілим числом")
            p["amount"] = amount
        return _kn.act("feed", aid, p)

    def milk(self, cow):
        """Подоїти корову: молоко — в руки. Повертає кількість (0 — молока ще немає)."""
        return _kn.act("milk", _id_of(cow, Animal, "milk"), None)

    def collect_eggs(self, chicken):
        """Зібрати яйця курки в руки. Повертає кількість (0 — яєць немає)."""
        return _kn.act("collect_eggs", _id_of(chicken, Animal, "collect_eggs"), None)

    def slaughter(self, animal):
        """Забити тварину: м'ясо — в руки, тварина зникає. Повертає кількість м'яса."""
        return _kn.act("slaughter", _id_of(animal, Animal, "slaughter"), None)

    def post(self, board, *objects):
        """Записати на дошку оголошень, що персонаж знає про об'єкти (без об'єктів — про все, що знає).
        Старіші записи замінюються новішими. Повертає, скільки записів оновлено."""
        bid = _id_of(board, Board, "post")
        if len(objects) == 1 and (isinstance(objects[0], list) or isinstance(objects[0], tuple)):
            objects = objects[0]
        ids = [_id_of(o, GameObject, "post") for o in objects]
        return _kn.act("post", bid, {"objects": ids})

    def read(self, board):
        """Прочитати дошку оголошень: знання персонажа оновлюються новішими записами. Повертає, скільки нового."""
        return _kn.act("read", _id_of(board, Board, "read"), None)

    def travel_time(self, obj):
        return Duration(_kn.travel_time(_id_of(obj, GameObject, "travel_time")))


me = Me(_kn.self_id(), _kn.self_name())


# ------------------------------------------------------------------ global functions

def get(name):
    """Об'єкт за унікальною назвою: get("Склад_1")."""
    if not isinstance(name, str):
        raise TypeError("get() приймає назву об'єкта (рядок)")
    obj = _wrap(_kn.find(name))
    if obj is None:
        raise NotFoundError(f"об'єкт «{name}» не знайдено")
    return obj


def buildings(kind):
    """Список будівель заданого типу: buildings(Storage)."""
    if kind not in (Storage, House, Forest, Field, Board, Barn, Meadow):
        raise TypeError("buildings() приймає тип: Storage, House, Forest, Field, Board, Barn або Meadow")
    return [_wrap(info) for info in _kn.list_objects(kind._kind)]


def animals(kind=None):
    """Тварини поселення: animals(), animals(Cow), animals(Chicken). Забитих у списку немає."""
    if kind is None:
        kind = Animal
    if kind not in (Animal, Cow, Chicken):
        raise TypeError("animals() приймає тип: Cow або Chicken")
    return [_wrap(info) for info in _kn.list_objects(kind._kind)]


def tools(kind=None, free=False):
    """Інструменти, про які персонаж знає: на складах (такими, якими бачив їх востаннє) і свій.
    tools(Axe) — лише сокири; free=True — лише цілі й не зарезервовані, що лежать на складі.
    Найкраща: max(tools(Axe, free=True), key=lambda a: a.condition)."""
    k = "" if kind is None else _tool_kind(kind)
    return [_tool_from(info) for info in _kn.list_tools(k, bool(free))]


def notify(text):
    """Записує повідомлення в журнал подій."""
    _kn.notify(str(text))


_handlers = []    # registered rule handlers; None after off()
_arities = []
_events = []      # what each rule listens to (event name, TimeOfDay or condition)
_conditions = []  # condition of each CONDITION rule (None for the others)
_names = []       # function name of each rule ("" for lambdas)
_owns = []        # True for rules written in the character's own code


def _fn_name(fn):
    try:
        name = fn.__name__
    except Exception:
        return ""
    return "" if not isinstance(name, str) or name == "<lambda>" else name


def _arity(fn):
    try:
        sig = _inspect._signature_data(fn)
    except Exception:
        return 1
    positional = 0
    for item in sig:
        if item[1] == 1:
            positional += 1
        elif item[1] == 2:
            return 2
    return 2 if positional >= 2 else 1


def on(event, handler=None, priority=1):
    """Правило, яке перериває будь-який код персонажа:
    @on(Event.RAIN) — подія; @on(time(22, 0)) — щодня о 22:00;
    @on(lambda: умова) — коли умова стає істинною (перевіряється щохвилини, без витрат часу).
    Можна й без декоратора: on(подія, функція, priority=1). Менше число priority — вищий пріоритет.
    Правило з коду персонажа має пріоритет над правилом спільного коду з тим самим priority,
    а правило з тим самим іменем функції замінює спільне."""
    if isinstance(event, TimeOfDay):
        kind = "TIME"
        tod = event.seconds % _DAY
    elif callable(event):
        kind = "CONDITION"
        tod = 0.0
    elif event in _EVENTS:
        kind = event
        tod = 0.0
    else:
        raise ValueError(f"невідома подія {event!r}: очікується Event.RAIN, time(22, 0) або lambda: умова")
    if not isinstance(priority, int) or isinstance(priority, bool):
        raise TypeError("priority= має бути цілим числом (менше число — вищий пріоритет)")

    def register(fn):
        if not callable(fn):
            raise TypeError("обробник події має бути функцією")
        index = len(_handlers)
        name = _fn_name(fn)
        _handlers.append(fn)
        _arities.append(_arity(fn))
        _events.append(event)
        _conditions.append(event if kind == "CONDITION" else None)
        _names.append(name)
        own = _kn.register_handler(kind, index, priority, float(tod))
        _owns.append(own)
        # The character's own code wins: its rule replaces the shared rule(s) with the same function name.
        if own and name:
            for i in range(index):
                if _handlers[i] is not None and not _owns[i] and _names[i] == name:
                    _kn.unregister_handler(i)
                    _handlers[i] = None
        return fn

    if handler is None:
        return register
    register(handler)
    return handler


def off(target):
    """Відмінити правило для цього персонажа: off(функція_правила), off(Event.RAIN), off(time(22, 0)).
    Повертає, скільки правил відмінено."""
    removed = 0
    for i in range(len(_handlers)):
        if _handlers[i] is None:
            continue
        same = _handlers[i] is target or _events[i] is target
        if not same and not callable(target):
            same = _events[i] == target
        if same:
            _kn.unregister_handler(i)
            _handlers[i] = None
            removed += 1
    return removed


def _dispatch(index, kind):
    fn = _handlers[index]
    if fn is None:
        return
    if _arities[index] >= 2:
        fn(me, EventInfo(kind))
    else:
        fn(me)


def _check(index):
    return bool(_conditions[index]())


_kn.set_dispatch(_dispatch, _check)

# ------------------------------------------------------------------ export + sandbox

__all__ = [
    "Duration", "TimeOfDay", "Moment", "time", "hours", "minutes",
    "world", "Event", "EventInfo", "Season",
    "ActionError", "NoItemError", "ReservedError", "TooHeavyError", "NoHomeError", "NotFoundError",
    "WorkplaceBusyError", "ToolBrokenError", "Building",
    "Tool", "Axe", "Sickle", "Scythe", "Clothes", "WarmClothes",
    "GameObject", "Storage", "House", "Forest", "Field", "Board", "Barn", "Meadow", "Person", "Me", "me",
    "Animal", "Cow", "Chicken",
    "get", "buildings", "animals", "tools", "notify", "on", "off",
]

for _name in __all__:
    setattr(_builtins, _name, globals()[_name])

# No process exit, no stdin, no dynamic imports (import statements are blocked natively).
for _name in ("exit", "input", "__import__"):
    if hasattr(_builtins, _name):
        delattr(_builtins, _name)
