# 5. Реверс-инжиниринг

## 5.1 Цель и версия

`SporebinEP1/SporeApp.exe` (GOG, 2016): MSVC 2008, x86, без RTTI (2 type descriptor-а на весь
exe), `DYNAMIC_BASE`, есть `.reloc`. Импортирует из `steam_api.dll` всего `SteamApps`,
`SteamAPI_Init`, `SteamAPI_Shutdown`. Размер 24 888 320 байт — **не** совпадает ни с одной
сборкой, известной Spore ModAPI (Disk: 24 904 192 / 24 909 584; March2017: 24 885 248 /
24 895 536 / 25 066 744).

## 5.2 Методика

1. **Перенос адресов ModAPI (March2017 → GOG 2016)** — `tools/addrport/port_modapi.py`.
   Проверка «похож ли адрес на начало функции» показала: код в обеих сборках совпадает
   кусками, сдвиги кусочно-постоянные (0, ±0x10…0x90). Скрипт находит начала функций
   в нашем exe (цели `call rel32` + указатели на код в `.rdata/.data`), затем Витерби-проходом
   выравнивает отсортированный список функций ModAPI по кандидатам-сдвигам.
   Итог: 1973 символа (1744 high, 196 medium, 33 low) → `data/symbols/gog2016_922af7c7.sym`.
2. **Проверка через REA** — полный exe Ghidra анализирует дольше лимита REA (330 с), поэтому
   `tools/addrport/pe_window.py` делает «оконную» копию exe: те же заголовки, импорт,
   `.rdata/.data`, но `.text` урезан до нужного окна (например 0xC00000–0xC30000). Адреса
   совпадают с реальными, анализ занимает секунды. Проверенные адреса → `overrides_922af7c7.sym`.
3. **Runtime-проверка** через devkit: `mem.read`, `call`, логгер исключений (`exc`),
   сторожевой поток (`hangdump`).

## 5.3 Подтверждённые функции

| Символ | Адрес | Как подтверждено |
|---|---|---|
| `cCreatureAnimal::Create` | 0x00C09B00 | REA asm: cdecl `(const Vector3* pos, cSpeciesProfile*, int age, cHerd*, bool isAvatar, bool)`; age → +0xB34, herd → +0x1674, при isAvatar вызывает `SetAvatar` |
| `cCreatureAnimal::Update` | 0x00C0ADF0 | REA: виртуальный 0x60; если `!(+0xB58 & 0x200)` → `NPCTickAI`, иначе `AvatarTickAI` |
| `cCreatureAnimal::NPCTickAI` | 0x00C09530 | REA callers: единственный вызов из Update |
| `cCreatureAnimal::GetHerd` | 0x00C046B0 | runtime-исключение: возвращает mHerd, вызывающие не проверяют null |
| `cGameNounManager::Get` | 0x00B3D3D0 | REA (вызов из Create) + runtime |
| `cGameNounManager::CreateInstance` | 0x00B20B80 | REA: Create вызывает с noun 0x18EB45E |
| `cGameNounManager::DestroyInstance` | 0x00B224F0 | REA: ветка ошибки Create |
| `cGameNounManager::SetAvatar` | 0x00B1FC70 | REA |
| `cCreatureAnimal::TakeDamage` | 0x00C075A0 | runtime: `cCombatant` vtable[6] у cCreatureAnimal (`this` = существо+0x5A8), `ret 0x14`: `(float dmg, u32 attackerPolitical, int type, const Vector3*, cCombatant* attacker)` |
| `cGameNounManager::CreateHerd` | 0x00B238B0 | REA: null только если species == null; `herdSize=0` → пустое стадо; поля стада: позиция +0x34, species +0xA4 |
| `Editors::cSpeciesManager::Get/GetSpeciesProfile` | 0x00401090 / 0x004DF580 | runtime: ключ вида аватара → его профиль |
| `cCreatureBase::PlayAnimation(To)` | 0x00C12430 / 0x00C14AE0 | REA + `ret 0xC` / `ret 0x10` |
| `ArgScript::PrintF` | 0x00841050 | REA: vsnprintf в буфер 0x400 + запись в поток |
| `App::cAppSystem::PreInit/Init`, `cSporeApp::Init/Shutdown` | 0x7E8BD0 / 0x7E67E0 / 0xF47E90 / 0xF475A0 | runtime: хуки срабатывают в правильном порядке |
| `App::ICheatManager::Get`, `IGameModeManager::Get` | 0x67DDE0 / 0x67DCD0 | runtime (читы, id режимов) |

## 5.4 Структуры (смещения от `cCreatureAnimal*`)

| Поле | Смещение | Примечание |
|---|---|---|
| cSpatialObject / cLocomotiveObject | +0xC0 | vtable: GetPosition 0x2C, SetPosition 0x38, SetOrientation 0x3C, Teleport 0x44, SetDesiredSpeed 0xC4, MoveTo 0xE0, StopMovement 0xEC |
| mPosition / mOrientation | +0xC4 / +0xD0 | планета — сфера радиуса ~505 |
| mVelocity | +0x288 | |
| cCombatant | +0x5A8 | HP +0x5E0, MaxHP +0x5D4 (MaxHP читается как 1.0 — уточнить) |
| mpSpeciesProfile / mSpeciesKey | +0xB20 / +0xB28 | |
| флаги | +0xB58 | бит 0x200 = аватар |
| mHerd | +0x1674 | **обязателен**, см. 5.5 |
| `cGameNounManager` | | mpAvatar +0x54, mpAvatarHerd +0x58, mPosseMembers +0x5C |

ID режимов: Creature 0x1654C01, Editor 0xDBDBA1, главное меню (GalaxyGameEntry) 0x2CCD1D2.
Чит-консоль: `ICheatManager` vtable AddCheat 0x18, RemoveCheat 0x1C, ProcessLine 0x20, GetArgScript 0x38.
`ArgScript::Line`: `vector<char*> mArguments` +0x14, mFirstArgIndex +0x3C, mArgumentCount +0x40.
`ArgScript::ICommand` (MSVC vtable): ParseLine, GetDescription, SetData, AddRef, Release, ~dtor;
поля: FormatParser* +4, refcount +8, data +0xC.

## 5.5 Найденные проблемы и решения

* **Стадо обязательно.** Код 0x00D41A20 перебирает соседей существа и вызывает у каждого
  `GetHerd()` без проверки на null → у существа без стада каждое обновление падает с AV
  по адресу 0x15C (Wine глотает исключение, кадр прерывается, игра «замирает»).
  Решение: марионетки создаются в стаде аватара (`mpAvatarHerd`) — по смыслу это и есть стая.
* **MinHook под Wine.** При установке хука MinHook приостанавливает все потоки процесса;
  приостановленный поток может держать блокировку виртуальной памяти ntdll, и следующий
  `VirtualProtect` зависает навсегда (воспроизводилось при запуске второй копии игры).
  Решение: заморозка потоков в MinHook отключена (`MH_SetFreezeThreads`), хуки ставятся из
  главного потока.
* **Сокеты во втором процессе Wine.** `select()`/блокирующие операции на TCP могли
  зависать на десятки секунд во второй копии игры в том же wineserver. Консоль переписана
  на неблокирующие сокеты с опросом.
* **Пауза в фоне.** Spore перестаёт рисовать без фокуса; полноэкранный режим под Wine
  сворачивается. Решение: «фоновый режим» SporeKit (глотает WM_ACTIVATE*/KILLFOCUS,
  хуки GetForegroundWindow/GetActiveWindow/GetFocus) и тестовый профиль в окне.

## 5.6 Как устроена синхронизация NPC (M2)

* **Реестр существ** — хук `cCreatureAnimal::Update`: каждое существо, обновлённое за кадр,
  попадает в список; он же служит фильтром (у гостя свои NPC не обновляются и удаляются).
* Хост выдаёт NPC `net_id`, рассылает `NPC_SPAWN` (ключ вида, флаг «семья»), `NPC_STATE` 10 Гц,
  `NPC_DESPAWN`; гость создаёт прокси через `cCreatureAnimal::Create` в стаде своего вида
  (`CreateHerd`, размер 0) либо в стаде аватара для «семьи», без ИИ (хук `NPCTickAI`).
* Урон — хук `TakeDamage`: удар гостя по прокси → `DAMAGE_REQ` → хост применяет к настоящему
  NPC с атакующим = марионеткой гостя; удар NPC по марионетке на хосте → `DAMAGE` → гость
  применяет к своему аватару. Огня по своим нет.

## 5.7 Открытые вопросы

* Камера/матрицы вида для `world_to_screen` (ники над существами) — `cGameModeManager::GetViewer`.
* Перекраска марионетки (другой цвет/размер для отличия от родни).
* Анимации навыков (атака, социалка) для M2: `cCreatureBase::PlayAnimation`, способности
  `GetAbility(index)` (vtable 0xB4).
* Полный анализ exe в REA (нужен таймаут `REA_GHIDRA_STARTUP_TIMEOUT_MS` ≥ 2 ч и
  переподключение MCP-сервера).
