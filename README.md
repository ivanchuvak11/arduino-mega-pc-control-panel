# PC Control Panel — Arduino Mega + Wokwi

Інтерактивна панель для моніторингу ПК, перегляду погоди, запуску програм,
мультимедійного керування та запису журналів на microSD.

## Що вже реалізовано

- 7 екранів інтерфейсу на TFT ILI9341;
- навігація енкодером, кнопки Back/Home та M1–M4;
- демонстраційні показники CPU/GPU/RAM без підключеного ПК;
- приймання реальних даних через Serial зі швидкістю 115200;
- команди для запуску програм і мультимедіа;
- журнали `SYSTEM.CSV` і `EVENTS.CSV` на microSD;
- часові позначки від DS1307;
- RGB-індикація та звукові підтвердження;
- обробка втрати зв'язку з ПК.

## Запуск у Wokwi

1. Створіть новий проєкт **Arduino Mega** у Wokwi.
2. Замініть вміст `sketch.ino` і `diagram.json` файлами з цієї папки.
3. Додайте бібліотеки з `libraries.txt` через Library Manager.
4. Запустіть симуляцію. Панель одразу працюватиме в Demo mode.

## Робота у VS Code

Репозиторій підтримує **PlatformIO** та розширення **Wokwi for VS Code**.

1. Відкрийте папку репозиторію у VS Code.
2. Встановіть рекомендовані розширення, коли VS Code покаже повідомлення:
   - PlatformIO IDE;
   - Wokwi Simulator.
3. На панелі PlatformIO натисніть **Build**. Залежності завантажаться автоматично.
4. Відкрийте `diagram.json` і виконайте команду **Wokwi: Start Simulator**.
5. Для реальної плати підключіть Arduino Mega та натисніть **Upload** у PlatformIO.

Основний код зберігається в `sketch.ino`. Файл `src/main.cpp` підключає його до
PlatformIO, тому підтримувати дві копії прошивки не потрібно.

### Структура репозиторію

```text
arduino-mega-pc-control-panel/
├── .vscode/            рекомендовані розширення та налаштування
├── sdcard/             вміст віртуальної SD-карти
├── src/main.cpp        точка входу PlatformIO
├── sketch.ino          основна прошивка
├── diagram.json        схема Wokwi
├── platformio.ini      плата, бібліотеки та Serial Monitor
├── wokwi.toml          запуск симулятора у VS Code
└── libraries.txt       бібліотеки для Wokwi Web
```

## Тестові повідомлення Serial

Надсилайте один рядок за раз із завершенням LF:

```text
SYS,cpu=72.5,ct=68.0,gpu=81.0,gt=74.0,ram=11.2,ramtotal=16,disk=73,down=24.5,up=3.1,uptime=24680
WEATHER,temp=14.5,feels=12.1,humidity=71,pressure=1012,wind=3.8,condition=Cloudy,city=Kyiv
PING
```

Відповіді панелі мають формат:

```text
READY,protocol=1,baud=115200
PONG
CMD,LAUNCH=0
CMD,MEDIA=PLAY_PAUSE
CMD,VOLUME=UP
```

## Позначення команд запуску

| ID | Програма |
|---:|---|
| 0 | Browser |
| 1 | VS Code |
| 2 | Arduino IDE |
| 3 | Discord |
| 4 | Task Manager |

Шляхи до програм зберігатимуться на ПК, а не в прошивці Arduino.

## Контакти Arduino Mega

| Пристрій | Контакти |
|---|---|
| TFT ILI9341 | CS 48, DC 47, MOSI 51, SCK 52 |
| microSD | CS 49, MISO 50, MOSI 51, SCK 52 |
| DS1307 | SDA 20, SCL 21 |
| Енкодер | CLK 2, DT 3, SW 4 |
| Back / Home | 5 / 6 |
| M1–M4 | 7, 8, 9, 10 |
| RGB LED | 11, 12, 13 через 330 Ом |
| Buzzer | 14 |

На фізичному пристрої дисплей і SD-модуль мають бути сумісними з логічними
рівнями 5 В або підключатися через перетворювач рівнів.
