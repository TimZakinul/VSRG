/**
 * VSRG - Vertical Scrolling Rhythm Game with Procedural Beatmap Generation
 * Enhanced version with better beat detection and visual effects
 * 
 * Compile (Linux): g++ -std=c++17 -O2 main.cpp -o vsrg -lsfml-graphics -lsfml-window -lsfml-system -lsfml-audio -pthread
 * Compile (Windows/MSYS2): g++ -std=c++17 -O2 main.cpp -o vsrg.exe -lsfml-graphics -lsfml-window -lsfml-system -lsfml-audio
 * Run: ./vsrg music.wav [speed]
 * 
 * Video support requires FFmpeg installed
 */

#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>
#include <vector>
#include <cmath>
#include <algorithm>
#include <random>
#include <string>
#include <iostream>
#include <deque>
#include <optional>
#include <cstdint>
#include <array>
#include <fstream>
#include <filesystem>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>

// Windows compatibility
#ifdef _WIN32
    #define popen _popen
    #define pclose _pclose
    #include <cstdio>
#endif

namespace fs = std::filesystem;

// ============================================================================
// VIDEO BACKGROUND SUPPORT (requires FFmpeg)
// ============================================================================

class VideoBackground {
public:
    bool enabled = false;
    bool prepared = false;
    std::string videoPath;
    sf::Texture frameTexture;
    std::optional<sf::Sprite> frameSprite;
    std::atomic<bool> running{false};
    std::atomic<bool> paused{false};
    std::thread decoderThread;
    std::mutex frameMutex;
    std::vector<uint8_t> frameBuffer;
    unsigned int frameWidth = 0;
    unsigned int frameHeight = 0;
    bool newFrameReady = false;
    bool textureCreated = false;
    float fps = 25.0f;
    
    // Для синхронизации с аудио
    std::atomic<float> targetTime{0.0f};
    
    ~VideoBackground() {
        stop();
    }
    
    bool prepare(const std::string& path, unsigned int targetWidth, unsigned int targetHeight) {
        videoPath = path;
        frameWidth = targetWidth;
        frameHeight = targetHeight;
        frameBuffer.resize(frameWidth * frameHeight * 4);
        
        // Проверяем наличие ffmpeg (Linux: which, Windows: where)
        #ifdef _WIN32
        if (system("where ffmpeg > nul 2>&1") != 0) {
        #else
        if (system("which ffmpeg > /dev/null 2>&1") != 0) {
        #endif
            std::cerr << "FFmpeg not found, video background disabled\n";
            return false;
        }
        
        // Получаем FPS видео
        std::string fpsCmd = "ffprobe -v error -select_streams v -of default=noprint_wrappers=1:nokey=1 -show_entries stream=r_frame_rate \"" + videoPath + "\" 2>/dev/null";
        FILE* fpsPipe = popen(fpsCmd.c_str(), "r");
        if (fpsPipe) {
            char fpsStr[64];
            if (fgets(fpsStr, sizeof(fpsStr), fpsPipe)) {
                int num = 25, den = 1;
                if (sscanf(fpsStr, "%d/%d", &num, &den) == 2 && den > 0) {
                    fps = static_cast<float>(num) / den;
                } else if (sscanf(fpsStr, "%d", &num) == 1) {
                    fps = static_cast<float>(num);
                }
            }
            pclose(fpsPipe);
        }
        std::cout << "Video FPS: " << fps << "\n";
        
        if (!frameTexture.resize({frameWidth, frameHeight})) {
            std::cerr << "Failed to create video texture\n";
            return false;
        }
        textureCreated = true;
        prepared = true;
        enabled = true;
        
        return true;
    }
    
    void play() {
        if (!prepared || running) return;
        paused = false;
        running = true;
        targetTime = 0.0f;
        decoderThread = std::thread(&VideoBackground::decodeLoop, this);
    }
    
    void pause() { paused = true; }
    void resume() { paused = false; }
    
    // Установить текущее время для синхронизации
    void setTime(float time) {
        targetTime = time;
    }
    
    void stop() {
        running = false;
        paused = false;
        if (decoderThread.joinable()) {
            decoderThread.join();
        }
    }
    
    void update() {
        if (!enabled || !textureCreated) return;
        
        std::lock_guard<std::mutex> lock(frameMutex);
        if (newFrameReady && !frameBuffer.empty()) {
            frameTexture.update(frameBuffer.data());
            frameSprite.emplace(frameTexture);
            newFrameReady = false;
        }
    }
    
    void render(sf::RenderWindow& window, float dimAmount = 0.6f) {
        if (!enabled || !frameSprite.has_value() || frameWidth == 0) return;
        
        float scaleX = static_cast<float>(window.getSize().x) / frameWidth;
        float scaleY = static_cast<float>(window.getSize().y) / frameHeight;
        float scale = std::max(scaleX, scaleY);
        
        frameSprite->setScale({scale, scale});
        
        float offsetX = (window.getSize().x - frameWidth * scale) / 2;
        float offsetY = (window.getSize().y - frameHeight * scale) / 2;
        frameSprite->setPosition({offsetX, offsetY});
        
        frameSprite->setColor(sf::Color(
            static_cast<uint8_t>(255 * (1.0f - dimAmount)),
            static_cast<uint8_t>(255 * (1.0f - dimAmount)),
            static_cast<uint8_t>(255 * (1.0f - dimAmount))
        ));
        
        window.draw(*frameSprite);
    }
    
private:
    void decodeLoop() {
        // FFmpeg с realtime воспроизведением - ключевой флаг -re
        std::string cmd = "ffmpeg -re -i \"" + videoPath + "\" -vf \"scale=" + 
                          std::to_string(frameWidth) + ":" + std::to_string(frameHeight) + 
                          "\" -pix_fmt rgba -f rawvideo -v quiet - 2>/dev/null";
        
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            running = false;
            return;
        }
        
        std::vector<uint8_t> tempBuffer(frameWidth * frameHeight * 4);
        
        while (running) {
            if (paused) {
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
                continue;
            }
            
            size_t bytesRead = fread(tempBuffer.data(), 1, tempBuffer.size(), pipe);
            
            if (bytesRead != tempBuffer.size()) {
                // Видео закончилось, перезапускаем
                pclose(pipe);
                pipe = popen(cmd.c_str(), "r");
                if (!pipe) break;
                continue;
            }
            
            {
                std::lock_guard<std::mutex> lock(frameMutex);
                frameBuffer = tempBuffer;
                newFrameReady = true;
            }
        }
        
        pclose(pipe);
    }
};

// ============================================================================
// AUDIO EXTRACTOR (for video files)
// ============================================================================

class AudioExtractor {
public:
    static bool isVideoFile(const std::string& path) {
        // Получаем расширение файла
        size_t dotPos = path.rfind('.');
        if (dotPos == std::string::npos) return false;
        
        std::string ext = path.substr(dotPos);
        for (auto& c : ext) c = std::tolower(c);
        
        return ext == ".mp4" || ext == ".mkv" || ext == ".avi" || 
               ext == ".webm" || ext == ".mov" || ext == ".flv" ||
               ext == ".m4v" || ext == ".wmv";
    }
    
    static std::string extractAudio(const std::string& videoPath) {
        // Создаём временный файл для аудио
        std::string tempAudio = "/tmp/vsrg_audio_" + std::to_string(std::hash<std::string>{}(videoPath)) + ".wav";
        
        // Проверяем, не извлечено ли уже
        if (fs::exists(tempAudio)) {
            std::cout << "Using cached audio: " << tempAudio << "\n";
            return tempAudio;
        }
        
        std::cout << "Extracting audio from video...\n";
        
        // Извлекаем аудио через ffmpeg
        std::string cmd = "ffmpeg -i \"" + videoPath + "\" -vn -acodec pcm_s16le -ar 44100 -ac 2 \"" + 
                          tempAudio + "\" -y -v quiet 2>/dev/null";
        
        int result = system(cmd.c_str());
        
        if (result != 0 || !fs::exists(tempAudio)) {
            std::cerr << "Failed to extract audio. Is FFmpeg installed?\n";
            return "";
        }
        
        std::cout << "Audio extracted successfully\n";
        return tempAudio;
    }
};

// ============================================================================
// YOUTUBE DOWNLOADER (requires yt-dlp)
// ============================================================================

class YouTubeDownloader {
public:
    static bool isYouTubeURL(const std::string& url) {
        return url.find("youtube.com") != std::string::npos ||
               url.find("youtu.be") != std::string::npos ||
               url.find("youtube") != std::string::npos;
    }
    
    static std::string downloadAudio(const std::string& url) {
        // Проверяем наличие yt-dlp
        #ifdef _WIN32
        if (system("where yt-dlp > nul 2>&1") != 0) {
        #else
        if (system("which yt-dlp > /dev/null 2>&1") != 0) {
        #endif
            std::cerr << "yt-dlp not found! Install it:\n";
            std::cerr << "  Arch: sudo pacman -S yt-dlp\n";
            std::cerr << "  Ubuntu: sudo apt install yt-dlp\n";
            std::cerr << "  pip: pip install yt-dlp\n";
            return "";
        }
        
        // Создаём временный файл
        std::string tempAudio = "/tmp/vsrg_yt_" + std::to_string(std::hash<std::string>{}(url)) + ".wav";
        
        // Проверяем кэш
        if (fs::exists(tempAudio)) {
            std::cout << "Using cached YouTube audio: " << tempAudio << "\n";
            return tempAudio;
        }
        
        std::cout << "Downloading audio from YouTube...\n";
        std::cout << "URL: " << url << "\n";
        
        // Скачиваем аудио через yt-dlp и конвертируем в wav
        std::string cmd = "yt-dlp -x --audio-format wav -o \"" + tempAudio + "\" \"" + url + "\" 2>&1";
        
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            std::cerr << "Failed to run yt-dlp\n";
            return "";
        }
        
        char buffer[256];
        while (fgets(buffer, sizeof(buffer), pipe)) {
            std::cout << buffer;
        }
        
        int result = pclose(pipe);
        
        // yt-dlp добавляет расширение, проверяем разные варианты
        std::string actualFile = tempAudio;
        if (!fs::exists(actualFile)) {
            actualFile = tempAudio + ".wav";
        }
        if (!fs::exists(actualFile)) {
            // Ищем файл с похожим именем
            std::string baseName = "/tmp/vsrg_yt_" + std::to_string(std::hash<std::string>{}(url));
            for (const auto& entry : fs::directory_iterator("/tmp")) {
                std::string filename = entry.path().string();
                if (filename.find(baseName) != std::string::npos) {
                    actualFile = filename;
                    break;
                }
            }
        }
        
        if (!fs::exists(actualFile)) {
            std::cerr << "Failed to download audio from YouTube\n";
            return "";
        }
        
        std::cout << "YouTube audio downloaded: " << actualFile << "\n";
        return actualFile;
    }
    
    // Скачать видео для фона
    static std::string downloadVideo(const std::string& url) {
        std::string tempVideo = "/tmp/vsrg_yt_video_" + std::to_string(std::hash<std::string>{}(url)) + ".mp4";
        
        if (fs::exists(tempVideo)) {
            std::cout << "Using cached YouTube video: " << tempVideo << "\n";
            return tempVideo;
        }
        
        std::cout << "Downloading video from YouTube (for background)...\n";
        
        // Скачиваем видео в низком качестве для фона
        std::string cmd = "yt-dlp -f 'bestvideo[height<=480]+bestaudio/best[height<=480]' --merge-output-format mp4 -o \"" + tempVideo + "\" \"" + url + "\" 2>&1";
        
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) return "";
        
        char buffer[256];
        while (fgets(buffer, sizeof(buffer), pipe)) {
            std::cout << buffer;
        }
        pclose(pipe);
        
        if (!fs::exists(tempVideo)) {
            // Пробуем без merge
            cmd = "yt-dlp -f 'best[height<=480]' -o \"" + tempVideo + "\" \"" + url + "\" 2>&1";
            system(cmd.c_str());
        }
        
        if (fs::exists(tempVideo)) {
            std::cout << "YouTube video downloaded: " << tempVideo << "\n";
            return tempVideo;
        }
        
        return "";
    }
};

// ============================================================================
// CONFIGURATION
// ============================================================================

namespace Config {
    inline unsigned int WINDOW_WIDTH = 800;
    inline unsigned int WINDOW_HEIGHT = 600;
    constexpr unsigned int FPS_LIMIT = 144;
    inline bool fullscreen = false;
    
    constexpr int NUM_LANES = 4;
    constexpr float LANE_WIDTH = 80.0f;  // фиксированная ширина дорожки
    constexpr float NOTE_HEIGHT = 20.0f;
    
    inline float HIT_LINE_Y = 520.0f;
    inline float SCROLL_SPEED = 400.0f;
    
    // Масштаб текста относительно базового разрешения 800x600
    inline float getTextScale() {
        return std::min(WINDOW_WIDTH / 800.0f, WINDOW_HEIGHT / 600.0f);
    }
    
    // Пересчёт только позиции линии попадания (дорожки фиксированные)
    inline void recalculateLayout() {
        HIT_LINE_Y = WINDOW_HEIGHT * 0.87f;  // 87% высоты
    }
    
    constexpr float PERFECT_WINDOW = 45.0f;
    constexpr float GOOD_WINDOW = 100.0f;
    constexpr float MISS_WINDOW = 150.0f;
    
    constexpr int PERFECT_SCORE = 300;
    constexpr int GOOD_SCORE = 100;
    constexpr int HOLD_TICK_SCORE = 10;
    constexpr int HOLD_COMPLETE_SCORE = 100;
    
    // Difficulty settings
    enum class Difficulty { VERY_EASY, EASY, MEDIUM, HARD, EXTREME };
    inline Difficulty difficulty = Difficulty::MEDIUM;
    inline bool autoPlay = false;
    inline bool clearMode = false;  // режим без эффектов
    
    // Difficulty parameters
    struct DifficultyParams {
        float beatThreshold;      // порог детекции битов (ниже = больше нот)
        float minNoteInterval;    // минимальный интервал между нотами
        float holdNoteChance;     // шанс hold note
        float maxHoldDuration;    // макс длина hold
        bool allowDoubles;        // двойные ноты
        float doubleChance;       // шанс двойных нот
    };
    
    inline DifficultyParams getDifficultyParams() {
        switch (difficulty) {
            case Difficulty::VERY_EASY:
                return {1.9f, 0.5f, 0.0f, 0.0f, false, 0.0f};  // очень мало нот, без холдов
            case Difficulty::EASY:
                return {1.6f, 0.25f, 0.1f, 0.8f, false, 0.0f};
            case Difficulty::MEDIUM:
                return {1.4f, 0.15f, 0.2f, 1.2f, false, 0.0f};
            case Difficulty::HARD:
                return {1.3f, 0.10f, 0.25f, 1.5f, true, 0.15f};
            case Difficulty::EXTREME:
                return {1.2f, 0.08f, 0.3f, 2.0f, true, 0.25f};
        }
        return {1.4f, 0.15f, 0.2f, 1.2f, false, 0.0f};
    }
    
    inline std::string getDifficultyName() {
        switch (difficulty) {
            case Difficulty::VERY_EASY: return "VERY EASY";
            case Difficulty::EASY: return "EASY";
            case Difficulty::MEDIUM: return "MEDIUM";
            case Difficulty::HARD: return "HARD";
            case Difficulty::EXTREME: return "EXTREME";
        }
        return "MEDIUM";
    }
    
    inline sf::Color getDifficultyColor() {
        switch (difficulty) {
            case Difficulty::VERY_EASY: return sf::Color(100, 200, 100);
            case Difficulty::EASY: return sf::Color(100, 255, 100);
            case Difficulty::MEDIUM: return sf::Color(255, 255, 100);
            case Difficulty::HARD: return sf::Color(255, 150, 50);
            case Difficulty::EXTREME: return sf::Color(255, 50, 50);
        }
        return sf::Color::White;
    }
    
    const sf::Color LANE_COLORS[4] = {
        sf::Color(255, 80, 80),
        sf::Color(80, 255, 80),
        sf::Color(80, 80, 255),
        sf::Color(255, 255, 80)
    };
    const sf::Color BG_COLOR(15, 15, 25);
    const sf::Color LANE_BG_COLOR(30, 30, 45);
    
    const sf::Keyboard::Key LANE_KEYS[4] = {
        sf::Keyboard::Key::D,
        sf::Keyboard::Key::F,
        sf::Keyboard::Key::J,
        sf::Keyboard::Key::K
    };
}

// ============================================================================
// NOTE STRUCTURE
// ============================================================================

struct Note {
    float timestamp;
    float endTimestamp;
    int lane;
    float intensity;  // сила бита (для визуальных эффектов)
    bool hit = false;
    bool missed = false;
    bool holding = false;
    bool holdCompleted = false;
    bool holdFailed = false;
    
    Note(float t, int l, float dur = 0.0f, float intens = 1.0f) 
        : timestamp(t), endTimestamp(t + dur), lane(l), intensity(intens) {}
    
    bool isHoldNote() const { return endTimestamp > timestamp + 0.01f; }
};

// ============================================================================
// PARTICLE SYSTEM - Визуальные эффекты
// ============================================================================

struct Particle {
    sf::Vector2f pos;
    sf::Vector2f vel;
    sf::Color color;
    float lifetime;
    float maxLifetime;
    float size;
    
    bool update(float dt) {
        lifetime -= dt;
        pos += vel * dt;
        vel.y += 200.0f * dt; // гравитация
        return lifetime > 0;
    }
    
    float getAlpha() const {
        return (lifetime / maxLifetime);
    }
};

class ParticleSystem {
public:
    std::vector<Particle> particles;
    std::mt19937 rng{std::random_device{}()};
    
    // Искры при попадании
    void spawnHitParticles(float x, float y, sf::Color color, int count = 15) {
        std::uniform_real_distribution<float> angleDist(0, 2 * 3.14159f);
        std::uniform_real_distribution<float> speedDist(100, 300);
        std::uniform_real_distribution<float> sizeDist(2, 6);
        
        for (int i = 0; i < count; ++i) {
            Particle p;
            p.pos = {x, y};
            float angle = angleDist(rng);
            float speed = speedDist(rng);
            p.vel = {std::cos(angle) * speed, std::sin(angle) * speed - 150};
            p.color = color;
            p.lifetime = p.maxLifetime = 0.5f;
            p.size = sizeDist(rng);
            particles.push_back(p);
        }
    }
    
    // Взрыв при комбо
    void spawnComboExplosion(float x, float y, int comboLevel) {
        int count = 20 + comboLevel * 5;
        std::uniform_real_distribution<float> angleDist(0, 2 * 3.14159f);
        std::uniform_real_distribution<float> speedDist(150, 400);
        
        for (int i = 0; i < count; ++i) {
            Particle p;
            p.pos = {x, y};
            float angle = angleDist(rng);
            float speed = speedDist(rng);
            p.vel = {std::cos(angle) * speed, std::sin(angle) * speed};
            
            // Радужные цвета для комбо
            float hue = (i * 360.0f / count);
            p.color = hsvToRgb(hue, 1.0f, 1.0f);
            p.lifetime = p.maxLifetime = 0.8f;
            p.size = 4 + comboLevel * 0.5f;
            particles.push_back(p);
        }
    }
    
    // Трейл для hold notes
    void spawnHoldTrail(float x, float y, sf::Color color) {
        Particle p;
        p.pos = {x + (rng() % 20 - 10), y};
        p.vel = {0, -50};
        p.color = color;
        p.lifetime = p.maxLifetime = 0.3f;
        p.size = 3;
        particles.push_back(p);
    }
    
    void update(float dt) {
        particles.erase(
            std::remove_if(particles.begin(), particles.end(),
                [dt](Particle& p) { return !p.update(dt); }),
            particles.end()
        );
    }
    
    void render(sf::RenderWindow& window) {
        for (const auto& p : particles) {
            sf::CircleShape shape(p.size * p.getAlpha());
            shape.setPosition({p.pos.x - shape.getRadius(), p.pos.y - shape.getRadius()});
            sf::Color c = p.color;
            c.a = static_cast<uint8_t>(255 * p.getAlpha());
            shape.setFillColor(c);
            window.draw(shape);
        }
    }
    
private:
    sf::Color hsvToRgb(float h, float s, float v) {
        float c = v * s;
        float x = c * (1 - std::abs(std::fmod(h / 60.0f, 2) - 1));
        float m = v - c;
        float r, g, b;
        
        if (h < 60) { r = c; g = x; b = 0; }
        else if (h < 120) { r = x; g = c; b = 0; }
        else if (h < 180) { r = 0; g = c; b = x; }
        else if (h < 240) { r = 0; g = x; b = c; }
        else if (h < 300) { r = x; g = 0; b = c; }
        else { r = c; g = 0; b = x; }
        
        return sf::Color(
            static_cast<uint8_t>((r + m) * 255),
            static_cast<uint8_t>((g + m) * 255),
            static_cast<uint8_t>((b + m) * 255)
        );
    }
};

// ============================================================================
// BEAT FLASH EFFECT - Пульсация фона под бит
// ============================================================================

struct BeatFlash {
    float intensity = 0;
    float bassIntensity = 0;
    
    void trigger(float power) {
        intensity = std::min(1.0f, intensity + power);
    }
    
    void triggerBass(float power) {
        bassIntensity = std::min(1.0f, bassIntensity + power);
    }
    
    void update(float dt) {
        intensity *= std::exp(-8.0f * dt);
        bassIntensity *= std::exp(-5.0f * dt);
    }
};

// ============================================================================
// BACKGROUND BARS - Динамические полоски на фоне (эквалайзер)
// ============================================================================

class BackgroundBars {
public:
    static constexpr int NUM_BARS = 32;
    float barHeights[NUM_BARS] = {0};
    float targetHeights[NUM_BARS] = {0};
    float barColors[NUM_BARS] = {0};  // hue
    std::mt19937 rng{std::random_device{}()};
    
    BackgroundBars() {
        for (int i = 0; i < NUM_BARS; ++i) {
            barColors[i] = (i * 360.0f / NUM_BARS);
        }
    }
    
    void trigger(float intensity, float bassStrength = 1.0f) {
        std::uniform_int_distribution<int> barDist(0, NUM_BARS - 1);
        std::uniform_real_distribution<float> heightDist(0.3f, 1.0f);
        
        // Активируем несколько полосок
        int count = static_cast<int>(3 + intensity * 5);
        for (int i = 0; i < count; ++i) {
            int bar = barDist(rng);
            targetHeights[bar] = std::min(1.0f, targetHeights[bar] + heightDist(rng) * intensity);
        }
        
        // Басс активирует низкие частоты (левые полоски)
        if (bassStrength > 1.2f) {
            for (int i = 0; i < NUM_BARS / 4; ++i) {
                targetHeights[i] = std::min(1.0f, targetHeights[i] + bassStrength * 0.3f);
            }
        }
    }
    
    void update(float dt) {
        for (int i = 0; i < NUM_BARS; ++i) {
            // Плавное движение к цели
            float diff = targetHeights[i] - barHeights[i];
            barHeights[i] += diff * 15.0f * dt;
            
            // Затухание цели
            targetHeights[i] *= std::exp(-4.0f * dt);
            
            // Минимальное значение для "живости"
            if (barHeights[i] < 0.05f) barHeights[i] = 0.02f + (rng() % 100) * 0.0003f;
            
            // Медленное вращение цвета
            barColors[i] = std::fmod(barColors[i] + dt * 10.0f, 360.0f);
        }
    }
    
    void render(sf::RenderWindow& window) {
        float barWidth = Config::WINDOW_WIDTH / static_cast<float>(NUM_BARS);
        float maxHeight = Config::WINDOW_HEIGHT * 0.4f;
        
        for (int i = 0; i < NUM_BARS; ++i) {
            float height = barHeights[i] * maxHeight;
            
            // Полоска снизу
            sf::RectangleShape bar({barWidth - 2, height});
            bar.setPosition({i * barWidth + 1, Config::WINDOW_HEIGHT - height});
            
            sf::Color color = hsvToRgb(barColors[i], 0.7f, 0.3f + barHeights[i] * 0.4f);
            color.a = static_cast<uint8_t>(60 + barHeights[i] * 100);
            bar.setFillColor(color);
            window.draw(bar);
            
            // Зеркальная полоска сверху (меньше)
            sf::RectangleShape barTop({barWidth - 2, height * 0.3f});
            barTop.setPosition({i * barWidth + 1, 0});
            color.a = static_cast<uint8_t>(30 + barHeights[i] * 50);
            barTop.setFillColor(color);
            window.draw(barTop);
        }
    }
    
private:
    sf::Color hsvToRgb(float h, float s, float v) {
        float c = v * s;
        float x = c * (1 - std::abs(std::fmod(h / 60.0f, 2) - 1));
        float m = v - c;
        float r, g, b;
        
        if (h < 60) { r = c; g = x; b = 0; }
        else if (h < 120) { r = x; g = c; b = 0; }
        else if (h < 180) { r = 0; g = c; b = x; }
        else if (h < 240) { r = 0; g = x; b = c; }
        else if (h < 300) { r = x; g = 0; b = c; }
        else { r = c; g = 0; b = x; }
        
        return sf::Color(
            static_cast<uint8_t>((r + m) * 255),
            static_cast<uint8_t>((g + m) * 255),
            static_cast<uint8_t>((b + m) * 255)
        );
    }
};

// ============================================================================
// ============================================================================
// HIT EFFECT
// ============================================================================

struct HitEffect {
    int lane;
    float lifetime;
    float maxLifetime;
    std::string judgment;
    sf::Color color;
    float scale;
    
    HitEffect(int l, const std::string& j, sf::Color c, float s = 1.0f) 
        : lane(l), lifetime(0.5f), maxLifetime(0.5f), judgment(j), color(c), scale(s) {}
    
    bool update(float dt) {
        lifetime -= dt;
        return lifetime > 0;
    }
    
    float getAlpha() const { return lifetime / maxLifetime; }
    float getScale() const { return scale * (1.0f + (1.0f - getAlpha()) * 0.3f); }
};


// ============================================================================
// ADVANCED AUDIO ANALYZER - Улучшенный анализ с частотными полосами
// ============================================================================

class AudioAnalyzer {
public:
    struct BeatInfo {
        float timestamp;
        float intensity;
        float bassStrength;
        float midStrength;
        float highStrength;
        bool isBass;
        bool isSnare;
        bool isHiHat;
    };
    
    std::vector<Note> analyze(const sf::SoundBuffer& buffer) {
        const std::int16_t* samples = buffer.getSamples();
        std::size_t sampleCount = buffer.getSampleCount();
        unsigned int sampleRate = buffer.getSampleRate();
        unsigned int channelCount = buffer.getChannelCount();
        
        auto params = Config::getDifficultyParams();
        
        std::cout << "Analyzing: " << sampleCount << " samples, " 
                  << sampleRate << " Hz [" << Config::getDifficultyName() << "]\n";
        
        std::vector<BeatInfo> beats = detectBeats(samples, sampleCount, sampleRate, channelCount, params);
        std::cout << "Detected " << beats.size() << " beats\n";
        
        return generateNotes(beats, params);
    }
    
private:
    std::vector<BeatInfo> detectBeats(const std::int16_t* samples, std::size_t sampleCount,
                                       unsigned int sampleRate, unsigned int channelCount,
                                       const Config::DifficultyParams& params) {
        std::vector<BeatInfo> beats;
        
        const std::size_t blockSize = 1024;
        const std::size_t hopSize = 512;
        const std::size_t historySize = 43;
        
        std::deque<float> bassHistory, midHistory, highHistory, totalHistory;
        float lastBeatTime = -0.1f;
        
        for (std::size_t i = 0; i + blockSize * channelCount <= sampleCount; i += hopSize * channelCount) {
            float timestamp = static_cast<float>(i / channelCount) / sampleRate;
            
            float bassEnergy = 0, midEnergy = 0, highEnergy = 0;
            
            for (std::size_t j = 0; j < blockSize * channelCount; j += channelCount * 4) {
                float sum = 0;
                for (std::size_t k = 0; k < 4 * channelCount && j + k < blockSize * channelCount; ++k) {
                    sum += samples[i + j + k] / 32768.0f;
                }
                bassEnergy += (sum / (4 * channelCount)) * (sum / (4 * channelCount));
            }
            
            for (std::size_t j = channelCount; j < blockSize * channelCount; j += channelCount) {
                float diff = (samples[i + j] - samples[i + j - channelCount]) / 32768.0f;
                midEnergy += diff * diff;
            }
            
            for (std::size_t j = 2 * channelCount; j < blockSize * channelCount; j += channelCount) {
                float diff1 = (samples[i + j] - samples[i + j - channelCount]) / 32768.0f;
                float diff2 = (samples[i + j - channelCount] - samples[i + j - 2 * channelCount]) / 32768.0f;
                highEnergy += (diff1 - diff2) * (diff1 - diff2);
            }
            
            bassEnergy /= (blockSize / 4);
            midEnergy /= blockSize;
            highEnergy /= blockSize;
            
            float totalEnergy = bassEnergy + midEnergy * 0.5f + highEnergy * 0.3f;
            
            bassHistory.push_back(bassEnergy);
            midHistory.push_back(midEnergy);
            highHistory.push_back(highEnergy);
            totalHistory.push_back(totalEnergy);
            
            if (bassHistory.size() > historySize) {
                bassHistory.pop_front();
                midHistory.pop_front();
                highHistory.pop_front();
                totalHistory.pop_front();
            }
            
            if (totalHistory.size() < historySize / 2) continue;
            
            auto average = [](const std::deque<float>& h) {
                float sum = 0;
                for (float v : h) sum += v;
                return sum / h.size();
            };
            
            float avgBass = average(bassHistory);
            float avgMid = average(midHistory);
            float avgHigh = average(highHistory);
            float avgTotal = average(totalHistory);
            
            // Используем порог из настроек сложности
            float threshold = params.beatThreshold;
            bool isBass = bassEnergy > avgBass * (threshold + 0.1f) && bassEnergy > 0.001f;
            bool isSnare = midEnergy > avgMid * threshold && midEnergy > 0.0005f;
            bool isHiHat = highEnergy > avgHigh * (threshold - 0.1f) && highEnergy > 0.0001f;
            bool isAnyBeat = totalEnergy > avgTotal * threshold && avgTotal > 0.0005f;
            
            float minInterval = params.minNoteInterval * 0.5f;  // для детекции битов
            
            if ((isBass || isSnare || isHiHat || isAnyBeat) && 
                (timestamp - lastBeatTime) >= minInterval) {
                
                BeatInfo beat;
                beat.timestamp = timestamp;
                beat.intensity = totalEnergy / std::max(avgTotal, 0.001f);
                beat.bassStrength = bassEnergy / std::max(avgBass, 0.001f);
                beat.midStrength = midEnergy / std::max(avgMid, 0.001f);
                beat.highStrength = highEnergy / std::max(avgHigh, 0.001f);
                beat.isBass = isBass;
                beat.isSnare = isSnare;
                beat.isHiHat = isHiHat;
                
                beats.push_back(beat);
                lastBeatTime = timestamp;
            }
        }
        
        return beats;
    }
    
    std::vector<Note> generateNotes(const std::vector<BeatInfo>& beats, 
                                     const Config::DifficultyParams& params) {
        std::vector<Note> notes;
        std::mt19937 rng(42);
        std::uniform_real_distribution<float> chanceDist(0.0f, 1.0f);
        
        std::array<float, 4> lastNoteTime = {-1, -1, -1, -1};
        int lastLane = -1;
        
        for (size_t i = 0; i < beats.size(); ++i) {
            const auto& beat = beats[i];
            
            // Выбираем дорожку
            int lane;
            if (beat.isBass) {
                lane = (rng() % 2);
            } else if (beat.isSnare) {
                lane = 1 + (rng() % 2);
            } else if (beat.isHiHat) {
                lane = 2 + (rng() % 2);
            } else {
                do {
                    lane = rng() % 4;
                } while (lane == lastLane && rng() % 3 != 0);
            }
            
            // Проверяем что дорожка свободна
            float minGap = params.minNoteInterval;
            if (beat.timestamp - lastNoteTime[lane] < minGap) {
                for (int l = 0; l < 4; ++l) {
                    if (beat.timestamp - lastNoteTime[l] >= minGap) {
                        lane = l;
                        break;
                    }
                }
            }
            
            // Определяем длительность для hold notes
            float duration = 0;
            
            if (beat.isBass && beat.bassStrength > 2.0f && chanceDist(rng) < params.holdNoteChance * 2) {
                float holdEnd = beat.timestamp + 0.3f;
                
                for (size_t j = i + 1; j < beats.size() && j < i + 10; ++j) {
                    if (beats[j].isBass && beats[j].bassStrength > 1.5f) {
                        holdEnd = beats[j].timestamp - 0.05f;
                        break;
                    }
                }
                
                duration = std::clamp(holdEnd - beat.timestamp, 0.25f, params.maxHoldDuration);
                if (duration < 0.25f) duration = 0;
            }
            else if (beat.isHiHat && i + 2 < beats.size() && chanceDist(rng) < params.holdNoteChance) {
                int hihatCount = 0;
                for (size_t j = i; j < beats.size() && j < i + 5; ++j) {
                    if (beats[j].isHiHat) hihatCount++;
                }
                if (hihatCount >= 3) {
                    duration = 0.3f + chanceDist(rng) * 0.5f;
                    duration = std::min(duration, params.maxHoldDuration);
                }
            }
            
            notes.emplace_back(beat.timestamp, lane, duration, beat.intensity);
            lastNoteTime[lane] = beat.timestamp + duration;
            lastLane = lane;
            
            // Двойные ноты для сложных режимов
            if (params.allowDoubles && chanceDist(rng) < params.doubleChance && beat.intensity > 1.5f) {
                int secondLane;
                do {
                    secondLane = rng() % 4;
                } while (secondLane == lane);
                
                if (beat.timestamp - lastNoteTime[secondLane] >= minGap) {
                    notes.emplace_back(beat.timestamp, secondLane, 0, beat.intensity * 0.8f);
                    lastNoteTime[secondLane] = beat.timestamp;
                }
            }
        }
        
        // Статистика
        int holdCount = 0;
        for (const auto& n : notes) if (n.isHoldNote()) holdCount++;
        std::cout << "Generated " << notes.size() << " notes (" << holdCount << " holds)\n";
        
        return notes;
    }
};


// ============================================================================
// GAME CLASS
// ============================================================================

class Game {
public:
    Game() : score(0), combo(0), maxCombo(0), 
             perfectCount(0), goodCount(0), missCount(0), holdCount(0),
             gameStarted(false), gameEnded(false), audioLoaded(false),
             fontLoaded(false), paused(false), volume(100.0f), 
             pauseMenuSelection(0), pauseOffset(0.0f) {
        
        // Создаём окно с учётом fullscreen
        if (Config::fullscreen) {
            auto desktop = sf::VideoMode::getDesktopMode();
            Config::WINDOW_WIDTH = desktop.size.x;
            Config::WINDOW_HEIGHT = desktop.size.y;
            Config::recalculateLayout();
            window.create(desktop, "VSRG", sf::State::Fullscreen);
        } else {
            window.create(sf::VideoMode({Config::WINDOW_WIDTH, Config::WINDOW_HEIGHT}), "VSRG");
        }
        
        window.setFramerateLimit(Config::FPS_LIMIT);
        
        // Загрузка шрифта (Linux и Windows пути)
        if (font.openFromFile("/usr/share/fonts/TTF/DejaVuSans.ttf")) {
            fontLoaded = true;
        } else if (font.openFromFile("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf")) {
            fontLoaded = true;
        } else if (font.openFromFile("C:/Windows/Fonts/arial.ttf")) {
            fontLoaded = true;  // Windows
        } else if (font.openFromFile("C:/Windows/Fonts/segoeui.ttf")) {
            fontLoaded = true;  // Windows альтернатива
        } else if (font.openFromFile("arial.ttf")) {
            fontLoaded = true;  // Локальный шрифт
        }
        
        recalculateLanePositions();
    }
    
    void recalculateLanePositions() {
        float totalWidth = Config::NUM_LANES * Config::LANE_WIDTH;
        float startX = (Config::WINDOW_WIDTH - totalWidth) / 2.0f;
        for (int i = 0; i < Config::NUM_LANES; ++i) {
            lanePositions[i] = startX + i * Config::LANE_WIDTH;
            keyPressed[i] = keyReleased[i] = keyHeld[i] = autoHeld[i] = false;
        }
    }
    
    bool loadAudio(const std::string& filename) {
        originalFilePath = filename;
        std::string audioFile = filename;
        
        // Проверяем, является ли файл видео
        bool isVideo = AudioExtractor::isVideoFile(filename);
        std::cout << "File: " << filename << "\n";
        std::cout << "Is video: " << (isVideo ? "yes" : "no") << "\n";
        
        if (isVideo) {
            std::cout << "Video file detected, extracting audio...\n";
            
            // Извлекаем аудио
            audioFile = AudioExtractor::extractAudio(filename);
            if (audioFile.empty()) {
                std::cerr << "Failed to extract audio from video\n";
                return false;
            }
            
            std::cout << "Audio extracted to: " << audioFile << "\n";
            
            // Подготавливаем видео фон (запустится при старте игры)
            videoBackground.prepare(filename, 640, 360);
        }
        
        if (!soundBuffer.loadFromFile(audioFile)) {
            std::cerr << "Error loading: " << audioFile << "\n";
            return false;
        }
        
        sound.emplace(soundBuffer);
        
        AudioAnalyzer analyzer;
        notes = analyzer.analyze(soundBuffer);
        
        std::sort(notes.begin(), notes.end(),
                  [](const Note& a, const Note& b) { return a.timestamp < b.timestamp; });
        
        audioLoaded = true;
        return true;
    }
    
    void run() {
        sf::Clock clock;
        
        while (window.isOpen()) {
            float dt = clock.restart().asSeconds();
            processEvents();
            
            if (gameStarted && !gameEnded && !paused) {
                update(dt);
            }
            
            beatFlash.update(dt);
            bgBars.update(dt);
            videoBackground.update();
            particles.update(dt);
            render();
        }
        
        // Останавливаем видео при выходе
        videoBackground.stop();
    }
    
private:
    sf::RenderWindow window;
    sf::Font font;
    bool fontLoaded;
    float lanePositions[Config::NUM_LANES];
    
    sf::SoundBuffer soundBuffer;
    std::optional<sf::Sound> sound;
    bool audioLoaded;
    
    std::vector<Note> notes;
    std::vector<HitEffect> hitEffects;
    ParticleSystem particles;
    BeatFlash beatFlash;
    BackgroundBars bgBars;
    VideoBackground videoBackground;
    std::string originalFilePath;
    
    int score, combo, maxCombo;
    int perfectCount, goodCount, missCount, holdCount;
    bool gameStarted, gameEnded;
    
    bool keyPressed[Config::NUM_LANES];
    bool keyReleased[Config::NUM_LANES];
    bool keyHeld[Config::NUM_LANES];
    sf::Clock gameClock;
    
    bool paused;
    float volume;
    int pauseMenuSelection;
    float pausedTime, pauseOffset;
    
    bool autoHeld[Config::NUM_LANES];  // для автобота
    
    void processEvents() {
        for (int i = 0; i < Config::NUM_LANES; ++i) {
            keyPressed[i] = keyReleased[i] = false;
        }
        
        while (auto event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>()) {
                window.close();
            }
            else if (const auto* key = event->getIf<sf::Event::KeyPressed>()) {
                handleKeyPress(key->code);
            }
            else if (const auto* key = event->getIf<sf::Event::KeyReleased>()) {
                for (int i = 0; i < Config::NUM_LANES; ++i) {
                    if (key->code == Config::LANE_KEYS[i]) {
                        keyHeld[i] = false;
                        keyReleased[i] = true;
                    }
                }
            }
        }
    }
    
    void handleKeyPress(sf::Keyboard::Key code) {
        if (code == sf::Keyboard::Key::Escape) {
            if (gameEnded) {
                videoBackground.stop();  // Останавливаем видео
                window.close();  // ESC на экране результатов - выход
            } else if (gameStarted && !gameEnded) {
                togglePause();
            } else if (!gameStarted) {
                videoBackground.stop();  // Останавливаем видео
                window.close();
            }
            return;
        }
        
        if (paused) {
            if (code == sf::Keyboard::Key::Up || code == sf::Keyboard::Key::W)
                pauseMenuSelection = (pauseMenuSelection + 2) % 3;
            else if (code == sf::Keyboard::Key::Down || code == sf::Keyboard::Key::S)
                pauseMenuSelection = (pauseMenuSelection + 1) % 3;
            else if (code == sf::Keyboard::Key::Enter || code == sf::Keyboard::Key::Space)
                handlePauseSelection();
            else if (code == sf::Keyboard::Key::Left) adjustVolume(-10);
            else if (code == sf::Keyboard::Key::Right) adjustVolume(10);
            return;
        }
        
        if (code == sf::Keyboard::Key::Space && !gameStarted && audioLoaded)
            startGame();
        else if (code == sf::Keyboard::Key::R && gameEnded)
            restartGame();
        
        for (int i = 0; i < Config::NUM_LANES; ++i) {
            if (code == Config::LANE_KEYS[i] && !keyHeld[i]) {
                keyPressed[i] = keyHeld[i] = true;
            }
        }
    }
    
    void togglePause() {
        paused = !paused;
        if (paused) {
            pausedTime = gameClock.getElapsedTime().asSeconds();
            if (sound) sound->pause();
            videoBackground.pause();  // Пауза видео
            pauseMenuSelection = 0;
        } else {
            pauseOffset += gameClock.getElapsedTime().asSeconds() - pausedTime;
            if (sound) sound->play();
            videoBackground.resume();  // Продолжить видео
        }
    }
    
    void handlePauseSelection() {
        if (pauseMenuSelection == 0) togglePause();
        else if (pauseMenuSelection == 1) { paused = false; restartGame(); }
        else {
            videoBackground.stop();  // Останавливаем видео при выходе
            window.close();
        }
    }
    
    void adjustVolume(float d) {
        volume = std::clamp(volume + d, 0.0f, 100.0f);
        if (sound) sound->setVolume(volume);
    }
    
    void startGame() {
        gameStarted = true;
        gameEnded = false;
        if (sound) sound->play();
        videoBackground.play();  // Запускаем видео
        gameClock.restart();
    }
    
    void restartGame() {
        score = combo = maxCombo = 0;
        perfectCount = goodCount = missCount = holdCount = 0;
        gameStarted = gameEnded = paused = false;
        pauseOffset = 0;
        hitEffects.clear();
        particles.particles.clear();
        
        for (auto& n : notes) {
            n.hit = n.missed = n.holding = n.holdCompleted = n.holdFailed = false;
        }
        if (sound) sound->stop();
        videoBackground.stop();  // Останавливаем видео
    }
    
    void update(float dt) {
        float currentTime = gameClock.getElapsedTime().asSeconds() - pauseOffset;
        
        // Автобот
        if (Config::autoPlay) {
            updateAutoPlay(currentTime);
        } else {
            for (int lane = 0; lane < Config::NUM_LANES; ++lane) {
                if (keyPressed[lane]) processLaneInput(lane, currentTime);
                if (keyReleased[lane]) processLaneRelease(lane, currentTime);
            }
        }
        
        // Update hold notes
        for (auto& note : notes) {
            if (note.isHoldNote() && note.holding && !note.holdCompleted && !note.holdFailed) {
                bool isHeld = Config::autoPlay ? true : keyHeld[note.lane];
                
                if (!isHeld) {
                    note.holdFailed = true;
                    note.holding = false;
                    combo = 0;
                    missCount++;
                    hitEffects.emplace_back(note.lane, "RELEASED!", sf::Color::Red);
                } else {
                    score += static_cast<int>(Config::HOLD_TICK_SCORE * dt * 10);
                    float x = lanePositions[note.lane] + Config::LANE_WIDTH / 2;
                    particles.spawnHoldTrail(x, Config::HIT_LINE_Y, Config::LANE_COLORS[note.lane]);
                    
                    if (currentTime >= note.endTimestamp) {
                        note.holdCompleted = true;
                        note.holding = false;
                        score += Config::HOLD_COMPLETE_SCORE;
                        combo++;
                        holdCount++;
                        maxCombo = std::max(maxCombo, combo);
                        hitEffects.emplace_back(note.lane, "HOLD OK!", sf::Color::Magenta, 1.2f);
                        particles.spawnHitParticles(x, Config::HIT_LINE_Y, sf::Color::Magenta, 25);
                        
                        if (Config::autoPlay) autoHeld[note.lane] = false;
                    }
                }
            }
        }
        
        // Check missed notes (не для автобота)
        if (!Config::autoPlay) {
            for (auto& note : notes) {
                if (!note.hit && !note.missed) {
                    float diff = (currentTime - note.timestamp) * 1000.0f;
                    if (diff > Config::MISS_WINDOW) {
                        note.missed = true;
                        combo = 0;
                        missCount++;
                        hitEffects.emplace_back(note.lane, "MISS", sf::Color::Red);
                    }
                }
            }
        }
        
        // Update effects
        hitEffects.erase(
            std::remove_if(hitEffects.begin(), hitEffects.end(),
                [dt](HitEffect& e) { return !e.update(dt); }),
            hitEffects.end()
        );
        
        // Check game end
        if (sound && sound->getStatus() == sf::Sound::Status::Stopped && gameStarted) {
            bool done = true;
            for (const auto& n : notes) {
                if (!n.hit && !n.missed) { done = false; break; }
                if (n.isHoldNote() && n.holding) { done = false; break; }
            }
            if (done) gameEnded = true;
        }
    }
    
    void updateAutoPlay(float currentTime) {
        for (auto& note : notes) {
            if (!note.hit && !note.missed) {
                float diff = (currentTime - note.timestamp) * 1000.0f;
                
                // Автобот нажимает идеально
                if (diff >= -5 && diff <= 5) {
                    note.hit = true;
                    score += Config::PERFECT_SCORE;
                    combo++;
                    perfectCount++;
                    maxCombo = std::max(maxCombo, combo);
                    
                    float x = lanePositions[note.lane] + Config::LANE_WIDTH / 2;
                    
                    // Эффекты только если не clear режим
                    if (!Config::clearMode) {
                        beatFlash.trigger(0.5f);
                        bgBars.trigger(0.8f, 1.5f);  // Активируем полоски для Auto
                        particles.spawnHitParticles(x, Config::HIT_LINE_Y, sf::Color::Cyan, 20);
                        
                        if (combo > 0 && combo % 50 == 0) {
                            particles.spawnComboExplosion(Config::WINDOW_WIDTH / 2, Config::WINDOW_HEIGHT / 2, combo / 50);
                        }
                    }
                    
                    if (note.isHoldNote()) {
                        note.holding = true;
                        autoHeld[note.lane] = true;
                        if (!Config::clearMode) {
                            hitEffects.emplace_back(note.lane, "AUTO", sf::Color::Cyan, 1.1f);
                        }
                    } else {
                        if (!Config::clearMode) {
                            hitEffects.emplace_back(note.lane, "AUTO", sf::Color::Cyan, 1.2f);
                        }
                    }
                }
            }
        }
    }
    
    void processLaneInput(int lane, float currentTime) {
        Note* closest = nullptr;
        float closestDiff = Config::MISS_WINDOW + 1;
        
        for (auto& n : notes) {
            if (n.lane == lane && !n.hit && !n.missed) {
                float diff = std::abs(currentTime - n.timestamp) * 1000.0f;
                if (diff < closestDiff && diff <= Config::MISS_WINDOW) {
                    closestDiff = diff;
                    closest = &n;
                }
            }
        }
        
        if (!closest) return;
        
        closest->hit = true;
        float x = lanePositions[lane] + Config::LANE_WIDTH / 2;
        sf::Color color = Config::LANE_COLORS[lane];
        
        if (closestDiff <= Config::PERFECT_WINDOW) {
            score += Config::PERFECT_SCORE;
            combo++;
            perfectCount++;
            
            if (!Config::clearMode) {
                beatFlash.trigger(0.5f);
                bgBars.trigger(0.8f, 1.5f);
                particles.spawnHitParticles(x, Config::HIT_LINE_Y, sf::Color::Cyan, 20);
                
                if (closest->isHoldNote()) {
                    closest->holding = true;
                    hitEffects.emplace_back(lane, "HOLD!", sf::Color::Cyan, 1.1f);
                } else {
                    hitEffects.emplace_back(lane, "PERFECT", sf::Color::Cyan, 1.2f);
                }
                
                // Combo explosion every 50
                if (combo > 0 && combo % 50 == 0) {
                    particles.spawnComboExplosion(Config::WINDOW_WIDTH / 2, Config::WINDOW_HEIGHT / 2, combo / 50);
                }
            } else {
                if (closest->isHoldNote()) closest->holding = true;
            }
        }
        else if (closestDiff <= Config::GOOD_WINDOW) {
            score += Config::GOOD_SCORE;
            combo++;
            goodCount++;
            
            if (!Config::clearMode) {
                beatFlash.trigger(0.3f);
                bgBars.trigger(0.5f, 1.0f);
                particles.spawnHitParticles(x, Config::HIT_LINE_Y, sf::Color::Green, 12);
                
                if (closest->isHoldNote()) {
                    closest->holding = true;
                    hitEffects.emplace_back(lane, "HOLD!", sf::Color::Green);
                } else {
                    hitEffects.emplace_back(lane, "GOOD", sf::Color::Green);
                }
            } else {
                if (closest->isHoldNote()) closest->holding = true;
            }
        }
        else {
            combo = 0;
            missCount++;
            if (!Config::clearMode) {
                hitEffects.emplace_back(lane, "MISS", sf::Color::Red);
            }
            if (closest->isHoldNote()) closest->holdFailed = true;
        }
        
        maxCombo = std::max(maxCombo, combo);
    }
    
    void processLaneRelease(int lane, float currentTime) {
        for (auto& n : notes) {
            if (n.lane == lane && n.isHoldNote() && n.holding && !n.holdCompleted && !n.holdFailed) {
                float diff = std::abs(currentTime - n.endTimestamp) * 1000.0f;
                
                if (diff <= Config::GOOD_WINDOW) {
                    n.holdCompleted = true;
                    n.holding = false;
                    score += Config::HOLD_COMPLETE_SCORE;
                    combo++;
                    holdCount++;
                    maxCombo = std::max(maxCombo, combo);
                    
                    if (!Config::clearMode) {
                        float x = lanePositions[lane] + Config::LANE_WIDTH / 2;
                        if (diff <= Config::PERFECT_WINDOW) {
                            hitEffects.emplace_back(lane, "PERFECT!", sf::Color::Magenta, 1.3f);
                            particles.spawnHitParticles(x, Config::HIT_LINE_Y, sf::Color::Magenta, 30);
                        } else {
                            hitEffects.emplace_back(lane, "HOLD OK!", sf::Color::Green, 1.1f);
                            particles.spawnHitParticles(x, Config::HIT_LINE_Y, sf::Color::Green, 20);
                        }
                    }
                } else if (currentTime < n.endTimestamp) {
                    n.holdFailed = true;
                    n.holding = false;
                    combo = 0;
                    missCount++;
                    if (!Config::clearMode) {
                        hitEffects.emplace_back(lane, "TOO EARLY!", sf::Color::Red);
                    }
                }
                break;
            }
        }
    }

    
    // ========== RENDERING ==========
    
    void render() {
        // Background with beat flash (только если не clear режим)
        sf::Color bg = Config::BG_COLOR;
        if (!Config::clearMode) {
            bg.r = std::min(255, static_cast<int>(bg.r + beatFlash.intensity * 40));
            bg.g = std::min(255, static_cast<int>(bg.g + beatFlash.intensity * 30));
            bg.b = std::min(255, static_cast<int>(bg.b + beatFlash.bassIntensity * 60));
        }
        window.clear(bg);
        
        // Video background (if enabled and not clear mode)
        if (videoBackground.enabled && !Config::clearMode) {
            videoBackground.render(window, 0.7f);  // 70% затемнение
        }
        
        // Dynamic background bars (только если не clear режим)
        if (!Config::clearMode) {
            bgBars.render(window);
        }
        
        renderLanes();
        renderNotes();
        renderHitLine();
        
        // Частицы и эффекты только если не clear режим
        if (!Config::clearMode) {
            particles.render(window);
            renderHitEffects();
        }
        
        renderUI();
        
        if (paused) renderPauseMenu();
        else if (!gameStarted && audioLoaded) renderStartScreen();
        else if (gameEnded) renderEndScreen();
        else if (!audioLoaded) renderLoadingScreen();
        
        window.display();
    }
    
    void renderLanes() {
        for (int i = 0; i < Config::NUM_LANES; ++i) {
            // Lane background
            sf::RectangleShape lane({Config::LANE_WIDTH - 4, static_cast<float>(Config::WINDOW_HEIGHT)});
            lane.setPosition({lanePositions[i] + 2, 0});
            
            sf::Color laneColor = Config::LANE_BG_COLOR;
            if (keyHeld[i]) {
                // Glow effect when held
                laneColor = sf::Color(
                    Config::LANE_COLORS[i].r / 4,
                    Config::LANE_COLORS[i].g / 4,
                    Config::LANE_COLORS[i].b / 4
                );
            }
            lane.setFillColor(laneColor);
            window.draw(lane);
            
            // Lane separator lines
            sf::RectangleShape sep({2, static_cast<float>(Config::WINDOW_HEIGHT)});
            sep.setPosition({lanePositions[i], 0});
            sep.setFillColor(sf::Color(60, 60, 80));
            window.draw(sep);
        }
        
        // Right edge
        sf::RectangleShape sep({2, static_cast<float>(Config::WINDOW_HEIGHT)});
        sep.setPosition({lanePositions[3] + Config::LANE_WIDTH, 0});
        sep.setFillColor(sf::Color(60, 60, 80));
        window.draw(sep);
    }
    
    void renderNotes() {
        if (!gameStarted) return;
        
        float currentTime = gameClock.getElapsedTime().asSeconds() - pauseOffset;
        if (paused) currentTime = pausedTime - pauseOffset;
        
        for (const auto& note : notes) {
            if (note.missed) continue;
            if (!note.isHoldNote() && note.hit) continue;
            if (note.isHoldNote() && (note.holdCompleted || note.holdFailed)) continue;
            
            float timeUntilHit = note.timestamp - currentTime;
            float noteY = Config::HIT_LINE_Y - (timeUntilHit * Config::SCROLL_SPEED);
            
            sf::Color color = Config::LANE_COLORS[note.lane];
            
            if (note.isHoldNote()) {
                float timeUntilEnd = note.endTimestamp - currentTime;
                float endY = Config::HIT_LINE_Y - (timeUntilEnd * Config::SCROLL_SPEED);
                float startY = note.holding ? Config::HIT_LINE_Y : noteY;
                float holdHeight = startY - endY;
                
                if (endY < Config::WINDOW_HEIGHT && startY > -Config::NOTE_HEIGHT) {
                    // Glow when holding
                    if (note.holding) {
                        color = sf::Color(
                            std::min(255, color.r + 60),
                            std::min(255, color.g + 60),
                            std::min(255, color.b + 60)
                        );
                    }
                    
                    // Hold body with gradient effect
                    if (holdHeight > 0) {
                        sf::RectangleShape body({Config::LANE_WIDTH - 20, holdHeight});
                        body.setPosition({lanePositions[note.lane] + 10, endY});
                        body.setFillColor(sf::Color(color.r, color.g, color.b, 120));
                        body.setOutlineThickness(3);
                        body.setOutlineColor(color);
                        window.draw(body);
                        
                        // Inner glow
                        sf::RectangleShape inner({Config::LANE_WIDTH - 36, holdHeight - 8});
                        inner.setPosition({lanePositions[note.lane] + 18, endY + 4});
                        inner.setFillColor(sf::Color(color.r, color.g, color.b, 60));
                        window.draw(inner);
                    }
                    
                    // Head
                    if (!note.hit) {
                        drawNote(lanePositions[note.lane], noteY, color, note.intensity);
                    }
                    
                    // Tail with different style
                    sf::RectangleShape tail({Config::LANE_WIDTH - 12, Config::NOTE_HEIGHT * 0.7f});
                    tail.setPosition({lanePositions[note.lane] + 6, endY});
                    tail.setFillColor(color);
                    tail.setOutlineThickness(2);
                    tail.setOutlineColor(sf::Color::Yellow);
                    window.draw(tail);
                }
            } else {
                if (noteY > -Config::NOTE_HEIGHT && noteY < Config::WINDOW_HEIGHT) {
                    drawNote(lanePositions[note.lane], noteY, color, note.intensity);
                }
            }
        }
    }
    
    void drawNote(float x, float y, sf::Color color, float intensity) {
        // Main note body
        sf::RectangleShape shape({Config::LANE_WIDTH - 10, Config::NOTE_HEIGHT});
        shape.setPosition({x + 5, y});
        shape.setFillColor(color);
        shape.setOutlineThickness(2);
        shape.setOutlineColor(sf::Color::White);
        window.draw(shape);
        
        // Intensity indicator (brighter center for stronger beats)
        if (intensity > 1.2f) {
            float glowSize = std::min(intensity - 1.0f, 0.5f) * 10;
            sf::RectangleShape glow({Config::LANE_WIDTH - 20 - glowSize, Config::NOTE_HEIGHT - 6});
            glow.setPosition({x + 10 + glowSize/2, y + 3});
            glow.setFillColor(sf::Color(255, 255, 255, static_cast<uint8_t>(100 * (intensity - 1.0f))));
            window.draw(glow);
        }
    }
    
    void renderHitLine() {
        // Glow effect
        for (int i = 3; i >= 0; --i) {
            sf::RectangleShape glow({Config::NUM_LANES * Config::LANE_WIDTH + i * 4, 4.0f + i * 2});
            glow.setPosition({lanePositions[0] - i * 2, Config::HIT_LINE_Y - i});
            glow.setFillColor(sf::Color(255, 255, 255, static_cast<uint8_t>(40 - i * 10)));
            window.draw(glow);
        }
        
        // Main line
        sf::RectangleShape line({Config::NUM_LANES * Config::LANE_WIDTH, 4});
        line.setPosition({lanePositions[0], Config::HIT_LINE_Y});
        line.setFillColor(sf::Color(255, 255, 255, 200));
        window.draw(line);
        
        if (!fontLoaded) return;
        
        const char* labels[4] = {"D", "F", "J", "K"};
        for (int i = 0; i < Config::NUM_LANES; ++i) {
            sf::Text text(font, labels[i], 24);
            text.setFillColor(keyHeld[i] ? Config::LANE_COLORS[i] : sf::Color(180, 180, 180));
            sf::FloatRect bounds = text.getLocalBounds();
            text.setPosition({lanePositions[i] + (Config::LANE_WIDTH - bounds.size.x) / 2,
                             Config::HIT_LINE_Y + 15});
            window.draw(text);
        }
    }
    
    void renderHitEffects() {
        if (!fontLoaded) return;
        
        for (const auto& e : hitEffects) {
            sf::Text text(font, e.judgment, static_cast<unsigned int>(22 * e.getScale()));
            sf::Color c = e.color;
            c.a = static_cast<uint8_t>(255 * e.getAlpha());
            text.setFillColor(c);
            
            sf::FloatRect bounds = text.getLocalBounds();
            float x = lanePositions[e.lane] + (Config::LANE_WIDTH - bounds.size.x) / 2;
            float y = Config::HIT_LINE_Y - 60 - (e.maxLifetime - e.lifetime) * 40;
            text.setPosition({x, y});
            window.draw(text);
        }
    }
    
    void renderUI() {
        if (!fontLoaded) return;
        
        float scale = Config::getTextScale();
        
        // Auto indicator
        if (Config::autoPlay) {
            sf::Text autoText(font, "AUTO", static_cast<unsigned int>(20 * scale));
            autoText.setFillColor(sf::Color::Cyan);
            autoText.setPosition({Config::WINDOW_WIDTH - 70.0f * scale, 40 * scale});
            window.draw(autoText);
        }
        
        // Score
        sf::Text scoreText(font, "Score: " + std::to_string(score), static_cast<unsigned int>(26 * scale));
        scoreText.setFillColor(sf::Color::White);
        scoreText.setPosition({20 * scale, 15 * scale});
        window.draw(scoreText);
        
        // Combo with scaling effect
        if (combo > 0) {
            float comboScale = 1.0f + std::min(combo / 100.0f, 0.3f);
            sf::Text comboText(font, std::to_string(combo), static_cast<unsigned int>(48 * scale * comboScale));
            comboText.setFillColor(combo >= 50 ? sf::Color::Yellow : sf::Color::White);
            sf::FloatRect bounds = comboText.getLocalBounds();
            comboText.setPosition({(Config::WINDOW_WIDTH - bounds.size.x) / 2, 80 * scale});
            window.draw(comboText);
            
            sf::Text comboLabel(font, "COMBO", static_cast<unsigned int>(18 * scale));
            comboLabel.setFillColor(sf::Color(200, 200, 200));
            bounds = comboLabel.getLocalBounds();
            comboLabel.setPosition({(Config::WINDOW_WIDTH - bounds.size.x) / 2, 135 * scale});
            window.draw(comboLabel);
        }
        
        // Stats
        sf::Text stats(font, "P:" + std::to_string(perfectCount) + 
                       " G:" + std::to_string(goodCount) + 
                       " H:" + std::to_string(holdCount) +
                       " M:" + std::to_string(missCount), static_cast<unsigned int>(16 * scale));
        stats.setFillColor(sf::Color(180, 180, 180));
        stats.setPosition({20 * scale, 45 * scale});
        window.draw(stats);
        
        // Volume
        sf::Text vol(font, "Vol:" + std::to_string(static_cast<int>(volume)) + "%", static_cast<unsigned int>(14 * scale));
        vol.setFillColor(sf::Color(120, 120, 120));
        sf::FloatRect vb = vol.getLocalBounds();
        vol.setPosition({Config::WINDOW_WIDTH - vb.size.x - 15 * scale, 15 * scale});
        window.draw(vol);
    }
    
    void renderStartScreen() {
        if (!fontLoaded) return;
        
        float scale = Config::getTextScale();
        float centerY = Config::WINDOW_HEIGHT / 2.0f;
        
        sf::Text title(font, "VSRG", static_cast<unsigned int>(56 * scale));
        title.setFillColor(sf::Color::White);
        sf::FloatRect b = title.getLocalBounds();
        title.setPosition({(Config::WINDOW_WIDTH - b.size.x) / 2, centerY - 180 * scale});
        window.draw(title);
        
        sf::Text sub(font, "Rhythm Game", static_cast<unsigned int>(24 * scale));
        sub.setFillColor(sf::Color(150, 150, 150));
        b = sub.getLocalBounds();
        sub.setPosition({(Config::WINDOW_WIDTH - b.size.x) / 2, centerY - 115 * scale});
        window.draw(sub);
        
        sf::Text noteInfo(font, std::to_string(notes.size()) + " notes generated", static_cast<unsigned int>(20 * scale));
        noteInfo.setFillColor(sf::Color(180, 180, 180));
        b = noteInfo.getLocalBounds();
        noteInfo.setPosition({(Config::WINDOW_WIDTH - b.size.x) / 2, centerY - 50 * scale});
        window.draw(noteInfo);
        
        sf::Text speed(font, "Speed: " + std::to_string(static_cast<int>(Config::SCROLL_SPEED)), static_cast<unsigned int>(18 * scale));
        speed.setFillColor(sf::Color::Yellow);
        b = speed.getLocalBounds();
        speed.setPosition({(Config::WINDOW_WIDTH - b.size.x) / 2, centerY - 20 * scale});
        window.draw(speed);
        
        // Difficulty
        sf::Text diff(font, "Difficulty: " + Config::getDifficultyName(), static_cast<unsigned int>(18 * scale));
        diff.setFillColor(Config::getDifficultyColor());
        b = diff.getLocalBounds();
        diff.setPosition({(Config::WINDOW_WIDTH - b.size.x) / 2, centerY + 5 * scale});
        window.draw(diff);
        
        // Auto mode indicator
        if (Config::autoPlay) {
            sf::Text autoText(font, "[ AUTO MODE ]", static_cast<unsigned int>(22 * scale));
            autoText.setFillColor(sf::Color::Cyan);
            b = autoText.getLocalBounds();
            autoText.setPosition({(Config::WINDOW_WIDTH - b.size.x) / 2, centerY + 30 * scale});
            window.draw(autoText);
        }
        
        sf::Text prompt(font, "Press SPACE to start", static_cast<unsigned int>(26 * scale));
        prompt.setFillColor(sf::Color::Cyan);
        b = prompt.getLocalBounds();
        prompt.setPosition({(Config::WINDOW_WIDTH - b.size.x) / 2, centerY + (Config::autoPlay ? 80.0f : 50.0f) * scale});
        window.draw(prompt);
        
        sf::Text controls(font, "D  F  J  K", static_cast<unsigned int>(22 * scale));
        controls.setFillColor(sf::Color(100, 100, 100));
        b = controls.getLocalBounds();
        controls.setPosition({(Config::WINDOW_WIDTH - b.size.x) / 2, centerY + (Config::autoPlay ? 150.0f : 120.0f) * scale});
        window.draw(controls);
    }
    
    void renderPauseMenu() {
        sf::RectangleShape overlay({static_cast<float>(Config::WINDOW_WIDTH), 
                                    static_cast<float>(Config::WINDOW_HEIGHT)});
        overlay.setFillColor(sf::Color(0, 0, 0, 180));
        window.draw(overlay);
        
        if (!fontLoaded) return;
        
        float scale = Config::getTextScale();
        float centerY = Config::WINDOW_HEIGHT / 2.0f;
        
        sf::Text title(font, "PAUSED", static_cast<unsigned int>(48 * scale));
        title.setFillColor(sf::Color::White);
        sf::FloatRect b = title.getLocalBounds();
        title.setPosition({(Config::WINDOW_WIDTH - b.size.x) / 2, centerY - 150 * scale});
        window.draw(title);
        
        const char* items[3] = {"Resume", "Restart", "Quit"};
        for (int i = 0; i < 3; ++i) {
            sf::Text item(font, items[i], static_cast<unsigned int>(28 * scale));
            item.setFillColor(i == pauseMenuSelection ? sf::Color::Cyan : sf::Color(150, 150, 150));
            b = item.getLocalBounds();
            item.setPosition({(Config::WINDOW_WIDTH - b.size.x) / 2, centerY - 40 * scale + i * 50 * scale});
            window.draw(item);
        }
        
        // Volume bar
        sf::Text volLabel(font, "Volume: " + std::to_string(static_cast<int>(volume)) + "%", static_cast<unsigned int>(20 * scale));
        volLabel.setFillColor(sf::Color::Yellow);
        b = volLabel.getLocalBounds();
        volLabel.setPosition({(Config::WINDOW_WIDTH - b.size.x) / 2, centerY + 150 * scale});
        window.draw(volLabel);
        
        float barW = 200 * scale, barH = 15 * scale;
        float barX = (Config::WINDOW_WIDTH - barW) / 2;
        sf::RectangleShape barBg({barW, barH});
        barBg.setPosition({barX, centerY + 185 * scale});
        barBg.setFillColor(sf::Color(60, 60, 60));
        window.draw(barBg);
        
        sf::RectangleShape barFill({barW * volume / 100, barH});
        barFill.setPosition({barX, centerY + 185 * scale});
        barFill.setFillColor(sf::Color::Cyan);
        window.draw(barFill);
    }
    
    void renderEndScreen() {
        sf::RectangleShape overlay({static_cast<float>(Config::WINDOW_WIDTH), 
                                    static_cast<float>(Config::WINDOW_HEIGHT)});
        overlay.setFillColor(sf::Color(0, 0, 0, 200));
        window.draw(overlay);
        
        if (!fontLoaded) return;
        
        float scale = Config::getTextScale();
        float centerY = Config::WINDOW_HEIGHT / 2.0f;
        
        // Вычисляем точность и ранг
        int total = perfectCount + goodCount + missCount + holdCount;
        float acc = total > 0 ? (perfectCount * 100.0f + goodCount * 50.0f + holdCount * 80.0f) / total : 0;
        
        // Определяем ранг
        std::string rank;
        sf::Color rankColor;
        if (acc >= 95 && missCount == 0) {
            rank = "SS"; rankColor = sf::Color(255, 215, 0);
        } else if (acc >= 90) {
            rank = "S"; rankColor = sf::Color(255, 200, 50);
        } else if (acc >= 80) {
            rank = "A"; rankColor = sf::Color(100, 255, 100);
        } else if (acc >= 70) {
            rank = "B"; rankColor = sf::Color(100, 200, 255);
        } else if (acc >= 60) {
            rank = "C"; rankColor = sf::Color(255, 255, 100);
        } else if (acc >= 50) {
            rank = "D"; rankColor = sf::Color(255, 150, 50);
        } else {
            rank = "F"; rankColor = sf::Color(255, 50, 50);
        }
        
        // Большой ранг
        sf::Text rankText(font, rank, static_cast<unsigned int>(120 * scale));
        rankText.setFillColor(rankColor);
        sf::FloatRect b = rankText.getLocalBounds();
        rankText.setPosition({(Config::WINDOW_WIDTH - b.size.x) / 2, centerY - 260 * scale});
        window.draw(rankText);
        
        // Заголовок
        sf::Text title(font, missCount == 0 ? "FULL COMBO!" : "RESULTS", static_cast<unsigned int>(32 * scale));
        title.setFillColor(missCount == 0 ? sf::Color::Yellow : sf::Color::White);
        b = title.getLocalBounds();
        title.setPosition({(Config::WINDOW_WIDTH - b.size.x) / 2, centerY - 130 * scale});
        window.draw(title);
        
        // Счёт
        sf::Text scoreText(font, "Score: " + std::to_string(score), static_cast<unsigned int>(36 * scale));
        scoreText.setFillColor(sf::Color::Yellow);
        b = scoreText.getLocalBounds();
        scoreText.setPosition({(Config::WINDOW_WIDTH - b.size.x) / 2, centerY - 80 * scale});
        window.draw(scoreText);
        
        // Макс комбо
        sf::Text comboText(font, "Max Combo: " + std::to_string(maxCombo), static_cast<unsigned int>(24 * scale));
        comboText.setFillColor(sf::Color::Cyan);
        b = comboText.getLocalBounds();
        comboText.setPosition({(Config::WINDOW_WIDTH - b.size.x) / 2, centerY - 30 * scale});
        window.draw(comboText);
        
        // Точность
        sf::Text accText(font, "Accuracy: " + std::to_string(static_cast<int>(acc)) + "%", static_cast<unsigned int>(26 * scale));
        accText.setFillColor(acc >= 90 ? sf::Color::Green : (acc >= 70 ? sf::Color::Yellow : sf::Color::Red));
        b = accText.getLocalBounds();
        accText.setPosition({(Config::WINDOW_WIDTH - b.size.x) / 2, centerY + 10 * scale});
        window.draw(accText);
        
        // Сложность
        sf::Text diffText(font, Config::getDifficultyName(), static_cast<unsigned int>(20 * scale));
        diffText.setFillColor(Config::getDifficultyColor());
        b = diffText.getLocalBounds();
        diffText.setPosition({(Config::WINDOW_WIDTH - b.size.x) / 2, centerY + 50 * scale});
        window.draw(diffText);
        
        // Статистика
        float statsY = centerY + 90 * scale;
        float statsX = Config::WINDOW_WIDTH / 2 - 100 * scale;
        
        sf::Text pText(font, "Perfect: " + std::to_string(perfectCount), static_cast<unsigned int>(18 * scale));
        pText.setFillColor(sf::Color::Cyan);
        pText.setPosition({statsX, statsY});
        window.draw(pText);
        
        sf::Text gText(font, "Good: " + std::to_string(goodCount), static_cast<unsigned int>(18 * scale));
        gText.setFillColor(sf::Color::Green);
        gText.setPosition({statsX, statsY + 25 * scale});
        window.draw(gText);
        
        sf::Text hText(font, "Hold: " + std::to_string(holdCount), static_cast<unsigned int>(18 * scale));
        hText.setFillColor(sf::Color::Magenta);
        hText.setPosition({statsX + 120 * scale, statsY});
        window.draw(hText);
        
        sf::Text mText(font, "Miss: " + std::to_string(missCount), static_cast<unsigned int>(18 * scale));
        mText.setFillColor(sf::Color::Red);
        mText.setPosition({statsX + 120 * scale, statsY + 25 * scale});
        window.draw(mText);
        
        // Auto mode indicator
        if (Config::autoPlay) {
            sf::Text autoText(font, "[ AUTO ]", static_cast<unsigned int>(18 * scale));
            autoText.setFillColor(sf::Color(150, 150, 150));
            b = autoText.getLocalBounds();
            autoText.setPosition({(Config::WINDOW_WIDTH - b.size.x) / 2, centerY + 160 * scale});
            window.draw(autoText);
        }
        
        // Подсказка
        sf::Text prompt(font, "Press R to restart | ESC to quit", static_cast<unsigned int>(18 * scale));
        prompt.setFillColor(sf::Color(100, 100, 100));
        b = prompt.getLocalBounds();
        prompt.setPosition({(Config::WINDOW_WIDTH - b.size.x) / 2, centerY + (Config::autoPlay ? 190.0f : 170.0f) * scale});
        window.draw(prompt);
    }
    
    void renderLoadingScreen() {
        if (!fontLoaded) return;
        float scale = Config::getTextScale();
        sf::Text text(font, "No audio loaded\n\nUsage: ./vsrg music.wav", static_cast<unsigned int>(24 * scale));
        text.setFillColor(sf::Color::Red);
        sf::FloatRect b = text.getLocalBounds();
        text.setPosition({(Config::WINDOW_WIDTH - b.size.x) / 2, Config::WINDOW_HEIGHT / 2 - 50 * scale});
        window.draw(text);
    }
};

// ============================================================================
// MAIN
// ============================================================================

float parseSpeed(const std::string& s) {
    if (s == "slow" || s == "1") return 200.0f;
    if (s == "normal" || s == "2") return 400.0f;
    if (s == "fast" || s == "3") return 600.0f;
    if (s == "extreme" || s == "4") return 800.0f;
    try { float v = std::stof(s); if (v > 0) return v; } catch (...) {}
    return 400.0f;
}

Config::Difficulty parseDifficulty(const std::string& s) {
    std::string lower = s;
    for (auto& c : lower) c = std::tolower(c);
    
    if (lower == "easy" || lower == "e") return Config::Difficulty::EASY;
    if (lower == "medium" || lower == "m" || lower == "normal" || lower == "n") return Config::Difficulty::MEDIUM;
    if (lower == "hard" || lower == "h") return Config::Difficulty::HARD;
    if (lower == "extreme" || lower == "x" || lower == "insane") return Config::Difficulty::EXTREME;
    
    return Config::Difficulty::MEDIUM;
}

int main(int argc, char* argv[]) {
    std::cout << "=== VSRG - Rhythm Game ===\n\n";
    
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <audio_file_or_youtube_url> [options]\n\n";
        std::cout << "Options:\n";
        std::cout << "  Speed: slow(1), normal(2), fast(3), extreme(4), or number\n";
        std::cout << "  Difficulty: very-easy, easy, medium, hard, extreme\n";
        std::cout << "  Window: WIDTHxHEIGHT (e.g. 1280x720, 1920x1080)\n";
        std::cout << "  fullscreen / fs - fullscreen mode\n";
        std::cout << "  auto - enable auto-play bot\n";
        std::cout << "  clear - no visual effects (clean mode)\n\n";
        std::cout << "Examples:\n";
        std::cout << "  " << argv[0] << " music.wav fast hard\n";
        std::cout << "  " << argv[0] << " https://youtube.com/watch?v=xxx 800 extreme\n";
        std::cout << "  " << argv[0] << " music.wav auto clear\n";
        std::cout << "  " << argv[0] << " music.wav fs auto\n\n";
        std::cout << "Controls: D F J K | ESC=pause | +/-=volume\n";
        return 1;
    }
    
    std::string inputPath = argv[1];
    
    // Парсим аргументы
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        std::string lower = arg;
        for (auto& c : lower) c = std::tolower(c);
        
        // Проверяем формат WIDTHxHEIGHT
        size_t xPos = arg.find('x');
        if (xPos == std::string::npos) xPos = arg.find('X');
        
        if (xPos != std::string::npos && xPos > 0 && xPos < arg.length() - 1) {
            try {
                int w = std::stoi(arg.substr(0, xPos));
                int h = std::stoi(arg.substr(xPos + 1));
                if (w >= 640 && h >= 480 && w <= 3840 && h <= 2160) {
                    Config::WINDOW_WIDTH = w;
                    Config::WINDOW_HEIGHT = h;
                    Config::recalculateLayout();
                    continue;
                }
            } catch (...) {}
        }
        
        if (lower == "auto") {
            Config::autoPlay = true;
        } else if (lower == "clear" || lower == "clean" || lower == "noeffects") {
            Config::clearMode = true;
        } else if (lower == "fullscreen" || lower == "fs" || lower == "full") {
            Config::fullscreen = true;
        } else if (lower == "very-easy" || lower == "veryeasy" || lower == "ve" || lower == "beginner") {
            Config::difficulty = Config::Difficulty::VERY_EASY;
        } else if (lower == "easy" || lower == "e") {
            Config::difficulty = Config::Difficulty::EASY;
        } else if (lower == "medium" || lower == "m" || lower == "normal" || lower == "n") {
            Config::difficulty = Config::Difficulty::MEDIUM;
        } else if (lower == "hard" || lower == "h") {
            Config::difficulty = Config::Difficulty::HARD;
        } else if (lower == "extreme" || lower == "x" || lower == "insane") {
            Config::difficulty = Config::Difficulty::EXTREME;
        } else {
            // Пробуем как скорость
            try {
                float speed = std::stof(arg);
                if (speed > 0) {
                    Config::SCROLL_SPEED = speed;
                }
            } catch (...) {}
        }
    }
    
    // Проверяем, является ли это YouTube ссылкой
    if (YouTubeDownloader::isYouTubeURL(inputPath)) {
        std::cout << "YouTube URL detected!\n";
        
        // Скачиваем аудио
        std::string audioPath = YouTubeDownloader::downloadAudio(inputPath);
        if (audioPath.empty()) {
            std::cerr << "Failed to download from YouTube\n";
            return 1;
        }
        inputPath = audioPath;
        
        // Опционально скачиваем видео для фона (если не clear режим)
        if (!Config::clearMode) {
            std::string videoPath = YouTubeDownloader::downloadVideo(argv[1]);
            if (!videoPath.empty()) {
                // Видео будет использоваться как фон
                // Сохраняем путь для VideoBackground
            }
        }
    }
    
    Game game;
    if (!game.loadAudio(inputPath)) return 1;
    
    std::cout << "Window: " << Config::WINDOW_WIDTH << "x" << Config::WINDOW_HEIGHT;
    if (Config::fullscreen) std::cout << " (fullscreen)";
    std::cout << "\n";
    std::cout << "Speed: " << Config::SCROLL_SPEED << "\n";
    std::cout << "Difficulty: " << Config::getDifficultyName() << "\n";
    if (Config::autoPlay) std::cout << "Auto-play: ENABLED\n";
    if (Config::clearMode) std::cout << "Clear mode: ENABLED (no effects)\n";
    std::cout << "\nPress SPACE to start!\n";
    game.run();
    
    return 0;
}
