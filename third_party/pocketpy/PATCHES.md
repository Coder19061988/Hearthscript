# pocketpy — локальні зміни

Джерело: https://github.com/pocketpy/pocketpy/releases/tag/v2.2.0 (`pocketpy.c`, `pocketpy.h`).
Усі зміни позначені в коді коментарем `KINDLESCRIPT PATCH`. При оновленні pocketpy їх треба перенести вручну.

1. **Ліміт VM (`PK_MAX_VM_COUNT`).** Upstream жорстко обмежує кількість VM числом 16, а нам потрібна окрема VM на кожного персонажа. Ліміт став макросом (за замовчуванням 16, наш CMake задає 128). Змінено: `pk_all_vm[]`, `py_finalize`, `VM__index`, `py_switchvm`, `py_resetallvm`.

2. **Трасування однорядкових циклів.** Upstream викликає trace-функцію лише тоді, коли змінюється рядок, тому `while True: pass` не породжував подій, і «роздуми» не рахувалися. Тепер стрибок назад у межах того самого рядка (`frame->ip <= prev_ip`) теж повідомляється як `TRACE_EVENT_LINE`. Змінено: структура `TraceInfo` (поля `prev_frame`, `prev_ip`) і блок трасування у `VM__run_top_frame`.

3. **Заборона `import`.** Вбудовані модулі (`time`, `sys`, `stdc` тощо) реєструються одразу під час створення VM, тому колбек `importfile` їх не блокує. Додано глобальний гард `py_kindle_setimportguard()`, який перевіряється в інструкції `OP_IMPORT_PATH`.
