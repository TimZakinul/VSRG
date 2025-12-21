# EVERYTHING HERE WAS DONE BY AI AND FREE FOR COPY

# VSRG - Rhythm Game with Procedural Beatmap Generation

[🇷🇺 Русский](#русский) | [🇬🇧 English](#english)

![vsrg](vsrg.png)


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
