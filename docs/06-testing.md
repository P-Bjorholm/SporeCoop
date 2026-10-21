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

Фикстуры других режимов (`out/fixtures/*.tgz`) делаются так:

```bash
python3 tools/tests/new_stage_game.py <cell|creature|tribe|civ|space>  # новая игра через выбор этапа
python3 tools/tests/space_setup.py      # космос: новая игра, пропуск обучения, выход с планеты, сохранение
tools/sporectl.py coop.save             # сохранить (как кнопка меню)
tools/spore.sh fixture save <имя> test
tools/sporectl.py coop.load Game0 Планета   # загрузить сохранение профиля прямо из меню
```

Тестовые запуски не мешают за компьютером:
* **без звука** — `prepare_profile` ставит в тестовых профилях `AudioMuteAll true` и
  `AudioMasterVolume 0` (настройки звука игрока и PipeWire не трогаются: PipeWire запомнил
  бы приглушение по имени приложения и для обычной игры);
* **на втором мониторе** — SporeKit в тестовых запусках переносит окно игры на неосновной
  монитор (экземпляр 1 к левому краю, остальные к правому), не забирая фокус.
  `[sporekit] test_monitor = auto|primary|off` в `mods/sporekit.ini`; команда `window [x y|test]`.

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
| Режимы | любой режим по фикстуре: гость из меню получает и грузит мир, обе марионетки, движение | `coop_mode.py --fixture <cell\|tribe\|civ\|space> --planet GameN/Планета`, `coop_mode.py --adventure` |
| Ручной | реальная игра по сети | два ПК / VPN, пакет `tools/package.sh` |

## 6.5 Известные особенности Wine

* MinHook не должен замораживать потоки (может зависнуть `VirtualProtect`).
* Во втором процессе одного wineserver блокирующие сокеты/`select()` и старт новых потоков
  иногда зависают — консоль работает на неблокирующих сокетах в одном потоке.
* Без фокуса Spore перестаёт рисовать; в тестовых запусках включён «фоновый режим» SporeKit.
* **Выход одного экземпляра убивает все экземпляры бутылки** (так ведёт себя запуск через
  bottles-cli). Поэтому тесты перезапускают обе копии вместе: сначала гостя, потом хоста.
* Зависший процесс Wine может остаться наполовину мёртвым (зомби-лидер с живыми потоками)
  и держать UDP-порт 47810. `spore.sh kill` добивает такие процессы `SIGKILL`.
