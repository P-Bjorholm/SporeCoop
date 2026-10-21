# 6. Тестирование

## 6.1 Окружение

* Игра установлена в бутылку Bottles **Games** (runner `caffe-10.0`, DXVK), реестр Spore там уже есть.
* Запуск: `tools/spore.sh run [-p профиль] [-i N]` → `bottles-cli run -b Games -e .../SporeApp.exe -- <ключи>`.
  Аргументы игры идут после `--`; переменные окружения Bottles фильтрует, поэтому настройки
  передаются ключами командной строки (`-sporekit-profile:test`, `-sporekit-instance:2`).
* Сборка: llvm-mingw (`~/.local/opt/llvm-mingw-*/bin/i686-w64-mingw32-clang++`), `make`.

## 6.2 Профили — тесты не трогают ваши сохранения

`-sporekit-profile:<имя>` перенаправляет пользовательские данные Spore в
`AppData/Roaming/SporeKit-<имя>/Spore`. `spore.sh run -p test` создаёт профиль из копии ваших
настроек, но в окне 1440×1080, без обучалок и подсказок. Экземпляр `-i 2` сам использует
профиль `inst2`. Фикстура с сохранением на этапе Существа:

```bash
tools/spore.sh fixture save creature test       # снять
tools/spore.sh fixture restore creature inst2   # положить в другой профиль
python3 tools/tests/load_game.py -i 2           # загрузить её через меню
```

## 6.3 Цикл разработки

```bash
make && tools/spore.sh deploy
tools/spore.sh run -p test
python3 tools/tests/load_game.py                 # до этапа Существа
make coop && tools/spore.sh deploy && tools/sporectl.py mod.reload coop
tools/sporectl.py shot                           # посмотреть экран
tools/sporectl.py exc                            # пойманные исключения (адрес + символ)
tools/sporectl.py hangdump                       # стек главного потока в лог
tools/spore.sh logs                              # хвост sporekit.log
```

## 6.4 Уровни тестов

| Уровень | Что | Как |
|---|---|---|
| Юнит (Linux) | сериализация, надёжная доставка под 30% потерь, сессия по localhost, интерполяция | `make test` |
| Loopback в игре | марионетка повторяет собственного аватара | `sporectl coop.echo on 6 0.3` |
| Протокол | хост в игре + фейковый гость на Python | `python3 -I tools/fakepeer.py 127.0.0.1:47810 --mode circle` |
| Интеграция | две копии игры на одной машине, host/join, передача мира | `python3 tools/tests/coop_two_instances.py` |
| Ручной | реальная игра по сети | два ПК / VPN, пакет `tools/package.sh` |

## 6.5 Известные особенности Wine

* MinHook не должен замораживать потоки (может зависнуть `VirtualProtect`).
* Во втором процессе одного wineserver блокирующие сокеты/`select()` и старт новых потоков
  иногда зависают — консоль работает на неблокирующих сокетах в одном потоке.
* Без фокуса Spore перестаёт рисовать; в тестовых запусках включён «фоновый режим» SporeKit.
