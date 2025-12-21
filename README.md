# EVERYTHING HERE WAS DONE BY AI AND FREE FOR COPY

# VSRG - Rhythm Game with Procedural Beatmap Generation

[🇷🇺 Русский](#русский) | [🇬🇧 English](#english)

---

## English

Vertical scrolling rhythm game like osu!mania / Piano Tiles with automatic note generation based on audio analysis.

### Features

- **Automatic beatmap generation** - audio analysis and note placement on beats
- **4 lanes** - D, F, J, K keys (fixed size, centered)
- **Hold notes** - long notes you need to hold
- **5 difficulty levels** - from VERY EASY to EXTREME
- **Auto-bot** - automatic playthrough
- **Visual effects** - particles, background pulse, dynamic equalizer
- **Rank system** - SS, S, A, B, C, D, F
- **Custom window size** - any resolution from 640x480 to 4K
- **Fullscreen mode** - native fullscreen support
- **Video support** - play with video files, video plays in background (requires FFmpeg)

---

### Linux Build

Requires SFML 3.x:

```bash
# Arch Linux
sudo pacman -S sfml ffmpeg

# Ubuntu/Debian
sudo apt install libsfml-dev ffmpeg

# Compile
g++ -std=c++17 -O2 main.cpp -o vsrg -lsfml-graphics -lsfml-window -lsfml-system -lsfml-audio -pthread
```

---

### Windows Build

#### Option 1: MSYS2 + MinGW (Recommended) "For Linux System"

1. Install [MSYS2](https://www.msys2.org/)

2. Open MSYS2 MinGW64 terminal and install dependencies:
```bash
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-sfml mingw-w64-x86_64-ffmpeg
```

3. Compile:
```bash
g++ -std=c++17 -O2 main.cpp -o vsrg.exe -lsfml-graphics -lsfml-window -lsfml-system -lsfml-audio -pthread
```

4. Run:
```bash
./vsrg.exe music.wav
```

#### Option 2: Visual Studio

1. Install [Visual Studio](https://visualstudio.microsoft.com/) with C++ workload

2. Download [SFML 3.x](https://www.sfml-dev.org/download.php) for Visual C++

3. Download [FFmpeg](https://ffmpeg.org/download.html) and add to PATH

4. Create new project, add main.cpp, configure SFML paths

5. Build and run

#### Windows Notes

- For video support, install FFmpeg and add to PATH
- Font path will auto-detect Windows fonts (C:\Windows\Fonts\arial.ttf)
- Use forward slashes or escape backslashes in paths

---

### Usage

```bash
./vsrg <audio_or_video_file> [options]
```

#### Supported File Formats

Audio: WAV, OGG, FLAC (MP3 may work)
Video: MP4, MKV, AVI, WEBM, MOV, FLV (requires FFmpeg)

#### Options

| Option | Description |
|--------|-------------|
| `slow` / `1` | Slow speed (200) |
| `normal` / `2` | Normal speed (400) |
| `fast` / `3` | Fast speed (600) |
| `extreme` / `4` | Extreme speed (800) |
| `<number>` | Custom speed |
| `very-easy` / `ve` | Very easy difficulty |
| `easy` / `e` | Easy difficulty |
| `medium` / `m` | Medium difficulty (default) |
| `hard` / `h` | Hard difficulty |
| `extreme` / `x` | Extreme difficulty |
| `auto` | Enable auto-bot |
| `WIDTHxHEIGHT` | Window size (e.g. 1280x720) |
| `fullscreen` / `fs` | Fullscreen mode |

#### Examples

```bash
# Play with audio file
./vsrg music.wav

# Play with video file (video shows in background)
./vsrg video.mp4

# Fullscreen with video
./vsrg video.mp4 fullscreen hard

# Auto-bot with video
./vsrg music_video.mkv auto extreme fs
```

### Controls

| Key | Action |
|-----|--------|
| D, F, J, K | Hit notes (4 lanes) |
| ESC | Pause / Exit |
| +/- | Volume |
| R | Restart (on results screen) |
| SPACE | Start game |

---

## Русский

Вертикальная ритм-игра в стиле osu!mania / Piano Tiles с автоматической генерацией нот на основе анализа аудио.

### Возможности

- **Автоматическая генерация карт** - анализ аудио и создание нот под бит
- **4 дорожки** - управление клавишами D, F, J, K
- **Hold-ноты** - длинные ноты, которые нужно зажимать
- **5 уровней сложности** - от VERY EASY до EXTREME
- **Авто-бот** - автоматическое прохождение
- **Визуальные эффекты** - частицы, пульсация фона, эквалайзер
- **Система рангов** - SS, S, A, B, C, D, F
- **Любой размер окна** - от 640x480 до 4K
- **Полноэкранный режим**
- **Поддержка видео** - видео на фоне (требуется FFmpeg)

---

### Сборка на Linux

```bash
# Arch Linux
sudo pacman -S sfml ffmpeg

# Ubuntu/Debian
sudo apt install libsfml-dev ffmpeg

# Компиляция
g++ -std=c++17 -O2 main.cpp -o vsrg -lsfml-graphics -lsfml-window -lsfml-system -lsfml-audio -pthread
```

---

### Сборка на Windows

#### Вариант 1: MSYS2 + MinGW (Рекомендуется) "Для Linux систем" 

1. Установи [MSYS2](https://www.msys2.org/)

2. Открой терминал MSYS2 MinGW64 и установи зависимости:
```bash
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-sfml mingw-w64-x86_64-ffmpeg
```

3. Скомпилируй:
```bash
g++ -std=c++17 -O2 main.cpp -o vsrg.exe -lsfml-graphics -lsfml-window -lsfml-system -lsfml-audio -pthread
```

4. Запусти:
```bash
./vsrg.exe music.wav
```

#### Вариант 2: Visual Studio

1. Установи [Visual Studio](https://visualstudio.microsoft.com/) с C++ компонентами

2. Скачай [SFML 3.x](https://www.sfml-dev.org/download.php) для Visual C++

3. Скачай [FFmpeg](https://ffmpeg.org/download.html) и добавь в PATH

4. Создай проект, добавь main.cpp, настрой пути к SFML

5. Собери и запусти

#### Заметки для Windows

- Для видео нужен FFmpeg в PATH
- Шрифты автоматически ищутся в C:\Windows\Fonts\
- Используй прямые слеши или экранируй обратные в путях

---

### Запуск

```bash
./vsrg <аудио_или_видео> [опции]
```

#### Форматы

Аудио: WAV, OGG, FLAC (MP3 может работать)
Видео: MP4, MKV, AVI, WEBM, MOV, FLV (нужен FFmpeg)

#### Опции

| Опция | Описание |
|-------|----------|
| `slow` / `1` | Медленная скорость |
| `normal` / `2` | Обычная скорость |
| `fast` / `3` | Быстрая скорость |
| `extreme` / `4` | Экстремальная скорость |
| `very-easy` / `ve` | Очень лёгкая сложность |
| `easy` / `e` | Лёгкая |
| `medium` / `m` | Средняя (по умолчанию) |
| `hard` / `h` | Сложная |
| `extreme` / `x` | Экстремальная |
| `auto` | Авто-бот |
| `ШИРИНАxВЫСОТА` | Размер окна |
| `fullscreen` / `fs` | Полный экран |

### Управление

| Клавиша | Действие |
|---------|----------|
| D, F, J, K | Нажатие нот |
| ESC | Пауза / Выход |
| +/- | Громкость |
| R | Рестарт |
| SPACE | Старт |
