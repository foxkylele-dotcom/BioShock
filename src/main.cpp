#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <cmath>
#include <vector>

// Шейдеры
const char* vertexShaderSource = R"(
#version 460 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 offset;
layout(location = 2) in vec3 color;
uniform vec2 scale;
uniform float aspect;

out vec3 vertexColor;

void main() {
    vec2 pos = aPos * scale * vec2(aspect, 1.0) + offset;
    gl_Position = vec4(pos, 0.0, 1.0);
    vertexColor = color;
}
)";

const char* fragmentShaderSource = R"(
#version 460 core
out vec4 FragColor;
in vec3 vertexColor;

void main() {
    FragColor = vec4(vertexColor, 1.0);
}
)";

// Структуры 

struct Booker { // Букер ДеВитт, наш герой
    float x, y;
    float w, h;
    int hp;
    int maxHp;
    float shootCooldown; // кулдаун стрельбы
};

struct Enemy { // Враг (Сифонир/полицейский)
    float x, y;
    float w, h;
    float velX, velY;
    bool isGrounded;
    int hp;
    int maxHp;
    float shootCooldown; // кулдаун перед следующим выстрелом
    bool alive;
};

struct Bullet { // Пуля
    float x, y;       // центр пули
    float w, h;       // размер
    float vx, vy;     // скорость
    bool fromPlayer;  // чья пуля
    bool alive;
};

struct Ground { // Земля... ей всё едино
    float x, y;
    float w, h;
};

// Глобальные переменные 
Booker player = { 0.0f, 5.0f, 1.0f, 2.0f, 10, 10, 0.0f }; // hp=10, кулдаун=0
float velX = 0.0f;
float velY = 0.0f;
bool isGrounded = false;
int facingDirection = 1; // 1 = вправо, -1 = влево

Ground platform = { 0.0f, 0.0f, 20.0f, 1.0f };

std::vector<Enemy> enemies;
std::vector<Bullet> bullets;

// Камера
float cameraX = 0.0f;
float cameraY = 5.0f;
const float VISIBLE_HEIGHT = 15.0f;

// Физические константы
const float GRAVITY = -25.0f;
const float JUMP_FORCE = 10.0f;
const float MOVE_SPEED = 5.0f;

// Константы стрельбы и врагов
const float BULLET_SPEED = 15.0f;
const float BULLET_DAMAGE = 2;
const float PLAYER_SHOOT_COOLDOWN = 0.3f;   // 0.3 сек между выстрелами игрока
const float ENEMY_SHOOT_COOLDOWN = 1.5f;    // 1.5 сек между выстрелами врага
const float ENEMY_DETECT_RANGE = 6.0f;      // радиус обнаружения игрока
const float ENEMY_MOVE_SPEED = 2.0f;        // скорость движения врага к игроку

// Флаги для однократного нажатия клавиш
bool oPressedLastFrame = false;

// Функция создания пули
void spawnBullet(float x, float y, float dirX, float dirY, bool fromPlayer) {
    Bullet b;
    b.x = x;
    b.y = y;
    b.w = 0.25f;
    b.h = 0.15f;
    b.vx = dirX * BULLET_SPEED;
    b.vy = dirY * BULLET_SPEED;
    b.fromPlayer = fromPlayer;
    b.alive = true;
    bullets.push_back(b);
}

// Функция создания врага
void spawnEnemy(float x, float y) {
    Enemy e;
    e.x = x;
    e.y = y;
    e.w = 1.0f;
    e.h = 2.0f;
    e.velX = 0.0f;
    e.velY = 0.0f;
    e.isGrounded = false;
    e.hp = 6;
    e.maxHp = 6;
    e.shootCooldown = 0.0f;
    e.alive = true;
    enemies.push_back(e);
}

// Проверка пересечения двух прямоугольников (AABB)
bool rectsOverlap(float ax, float ay, float aw, float ah,
    float bx, float by, float bw, float bh) {
    return (ax - aw / 2.0f < bx + bw / 2.0f) &&
        (ax + aw / 2.0f > bx - bw / 2.0f) &&
        (ay - ah / 2.0f < by + bh / 2.0f) &&
        (ay + ah / 2.0f > by - bh / 2.0f);
}

// Универсальная отрисовка прямоугольника (чтобы не дублировать код)
void drawRect(int scaleLoc, float cx, float cy, float w, float h,
    float r, float g, float b, float camX, float camY,
    float sx, float sy, float aspect) {
    float offset[] = { (cx - camX) * sx * aspect, (cy - camY) * sy };
    float scale[] = { w * sx, h * sy };
    float color[] = { r, g, b };
    glUniform2f(scaleLoc, scale[0], scale[1]);
    glVertexAttrib2fv(1, offset);
    glVertexAttrib3fv(2, color);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

// Отрисовка полоски HP над объектом
void drawHpBar(int scaleLoc, float cx, float cy, float objW,
    float hp, float maxHp, float camX, float camY,
    float sx, float sy, float aspect) {
    float barW = objW * 1.2f;
    float barH = 0.15f;
    float barY = cy + 1.3f; // чуть выше головы

    // Фон (серый)
    drawRect(scaleLoc, cx, barY, barW, barH, 0.2f, 0.2f, 0.2f,
        camX, camY, sx, sy, aspect);
    // Заполнение (красное)
    float fillW = barW * ((float)hp / (float)maxHp);
    float fillX = cx - barW / 2.0f + fillW / 2.0f;
    drawRect(scaleLoc, fillX, barY, fillW, barH, 0.9f, 0.1f, 0.1f,
        camX, camY, sx, sy, aspect);
}

int main() {
    GLFWwindow* window;

    /* Initialize the library */
    if (!glfwInit()) {
        std::cerr << "Can't load GLFW." << std::endl;
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    /* Create a windowed mode window and its OpenGL context */
    window = glfwCreateWindow(1024, 768, "BioShock Infinite 2D", NULL, NULL);
    if (!window) {
        std::cerr << "Can't make a window." << std::endl;
        glfwTerminate();
        return -1;
    }

    /* Make the window's context current */
    glfwMakeContextCurrent(window);

    if (!gladLoadGL()) {
        std::cerr << "Can't load GLAD.\n";
        return -1;
    }

    std::cout << "OpenGL " << GLVersion.major << "." << GLVersion.minor << "\n";

    glfwSwapInterval(1);

    // Сборка шейдеров
    unsigned int vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vertexShaderSource, NULL);
    glCompileShader(vs);
    unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fragmentShaderSource, NULL);
    glCompileShader(fs);
    unsigned int program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    // Геометрия
    float vertices[] = { -0.5f,-0.5f, 0.5f,-0.5f, 0.5f,0.5f, -0.5f,-0.5f, 0.5f,0.5f, -0.5f,0.5f };

    unsigned int VAO, VBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    int scaleLocation = glGetUniformLocation(program, "scale");
    int aspectLocation = glGetUniformLocation(program, "aspect");

    float lastFrameTime = 0.0f; // время с последнего кадра
    float skyR = 0.5f, skyG = 0.8f, skyB = 1.0f;

    // Спавним двух врагов по краям платформы
    spawnEnemy(-6.0f, 5.0f);
    spawnEnemy(6.0f, 5.0f);

    while (!glfwWindowShouldClose(window)) {
        float currentFrameTime = (float)glfwGetTime();
        float deltaTime = currentFrameTime - lastFrameTime;
        lastFrameTime = currentFrameTime;

        if (deltaTime > 0.1f) deltaTime = 0.1f;

        int w, h;
        glfwGetWindowSize(window, &w, &h);
        float aspect = (float)w / (float)h;

        float oldPlayerY = player.y;

        // ВВОД ИГРОКА
        velX = 0.0f;

        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
            velX = -MOVE_SPEED;
            facingDirection = -1;
        }
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
            velX = MOVE_SPEED;
            facingDirection = 1;
        }

        // Прыжок (W или Space), если персонаж на земле
        if ((glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) && isGrounded) {
            velY = JUMP_FORCE;
            isGrounded = false;
        }

        // СТРЕЛЬБА ИГРОКА (кнопка O)
        bool oPressed = (glfwGetKey(window, GLFW_KEY_O) == GLFW_PRESS);
        if (oPressed && !oPressedLastFrame && player.shootCooldown <= 0.0f && player.hp > 0) {
            // Стреляем в направлении, куда смотрит игрок
            float bulletStartX = player.x + facingDirection * (player.w / 2.0f + 0.2f);
            float bulletStartY = player.y + 0.2f; // чуть ниже центра (уровень пистолета)
            spawnBullet(bulletStartX, bulletStartY, (float)facingDirection, 0.0f, true);
            player.shootCooldown = PLAYER_SHOOT_COOLDOWN;
        }
        oPressedLastFrame = oPressed;

        if (player.shootCooldown > 0.0f) player.shootCooldown -= deltaTime;

        // ФИЗИКА ИГРОКА
        velY += GRAVITY * deltaTime;
        player.x += velX * deltaTime;
        player.y += velY * deltaTime;
        isGrounded = false;

        float platTop = platform.y + platform.h / 2.0f;
        float platLeft = platform.x - platform.w / 2.0f;
        float platRight = platform.x + platform.w / 2.0f;

        float footY = player.y - (player.h / 2.0f);
        float oldFootY = oldPlayerY - (player.h / 2.0f);

        bool isOverPlatform = (player.x - player.w / 2.0f + 0.05f < platRight) &&
            (player.x + player.w / 2.0f - 0.05f > platLeft);
        if (isOverPlatform) {
            if (oldFootY >= platTop - 0.01f && footY <= platTop && velY <= 0.0f) {
                player.y = platTop + (player.h / 2.0f);
                velY = 0.0f;
                isGrounded = true;
            }
        }

        // ИИ И ФИЗИКА ВРАГОВ
        for (auto& e : enemies) {
            if (!e.alive) continue;

            float oldEnemyY = e.y;

            // Расстояние до игрока
            float dx = player.x - e.x;
            float dy = player.y - e.y;
            float dist = std::sqrt(dx * dx + dy * dy);

            // Если игрок мёртв — враг стоит
            if (player.hp <= 0) {
                e.velX = 0.0f;
            }
            else if (dist <= ENEMY_DETECT_RANGE) {
                // Видит игрока — идёт к нему
                if (dx > 0.1f) e.velX = ENEMY_MOVE_SPEED;
                else if (dx < -0.1f) e.velX = -ENEMY_MOVE_SPEED;
                else e.velX = 0.0f;

                // Стреляет с кулдауном
                if (e.shootCooldown <= 0.0f) {
                    float dirX = (dx > 0.0f) ? 1.0f : -1.0f;
                    float bulletStartX = e.x + dirX * (e.w / 2.0f + 0.2f);
                    float bulletStartY = e.y + 0.2f;
                    spawnBullet(bulletStartX, bulletStartY, dirX, 0.0f, false);
                    e.shootCooldown = ENEMY_SHOOT_COOLDOWN;
                }
            }
            else {
                e.velX = 0.0f; // стоит на месте, если не видит
            }

            if (e.shootCooldown > 0.0f) e.shootCooldown -= deltaTime;

            // Физика врага (гравитация + приземление на платформу)
            e.velY += GRAVITY * deltaTime;
            e.x += e.velX * deltaTime;
            e.y += e.velY * deltaTime;
            e.isGrounded = false;

            float eFootY = e.y - (e.h / 2.0f);
            float eOldFootY = oldEnemyY - (e.h / 2.0f);

            bool eOverPlatform = (e.x - e.w / 2.0f + 0.05f < platRight) &&
                (e.x + e.w / 2.0f - 0.05f > platLeft);
            if (eOverPlatform) {
                if (eOldFootY >= platTop - 0.01f && eFootY <= platTop && e.velY <= 0.0f) {
                    e.y = platTop + (e.h / 2.0f);
                    e.velY = 0.0f;
                    e.isGrounded = true;
                }
            }
        }

        // ОБНОВЛЕНИЕ ПУЛЬ
        for (auto& b : bullets) {
            if (!b.alive) continue;

            b.x += b.vx * deltaTime;
            b.y += b.vy * deltaTime;

            // Удаляем пули, улетевшие далеко
            if (std::abs(b.x - player.x) > 30.0f || std::abs(b.y - player.y) > 30.0f) {
                b.alive = false;
                continue;
            }

            // Попадание пули игрока во врагов
            if (b.fromPlayer) {
                for (auto& e : enemies) {
                    if (!e.alive) continue;
                    if (rectsOverlap(b.x, b.y, b.w, b.h, e.x, e.y, e.w, e.h)) {
                        e.hp -= (int)BULLET_DAMAGE;
                        b.alive = false;
                        if (e.hp <= 0) {
                            e.alive = false;
                        }
                        break;
                    }
                }
            }
            // Попадание пули врага в игрока
            else {
                if (player.hp > 0 && rectsOverlap(b.x, b.y, b.w, b.h,
                    player.x, player.y, player.w, player.h)) {
                    player.hp -= (int)BULLET_DAMAGE;
                    b.alive = false;
                }
            }
        }

        // Чистим мёртвые пули
        bullets.erase(std::remove_if(bullets.begin(), bullets.end(),
            [](const Bullet& b) { return !b.alive; }), bullets.end());

        //КАМЕРА
        cameraX = player.x;
        cameraY = player.y;

        //ОТРИСОВКА
        float sy = 2.0f / VISIBLE_HEIGHT;
        float sx = sy;

        glClearColor(skyR, skyG, skyB, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(program);
        glUniform1f(aspectLocation, aspect);
        glBindVertexArray(VAO);
        glDisableVertexAttribArray(1);
        glDisableVertexAttribArray(2);

        // Платформа
        drawRect(scaleLocation, platform.x, platform.y, platform.w, platform.h,
            0.4f, 0.25f, 0.1f, cameraX, cameraY, sx, sy, aspect);

        // Игрок
        if (player.hp > 0) {
            drawRect(scaleLocation, player.x, player.y, player.w, player.h,
                0.8f, 0.1f, 0.1f, cameraX, cameraY, sx, sy, aspect);
            //HP над игроком
            drawHpBar(scaleLocation, player.x, player.y, player.w,
                player.hp, player.maxHp, cameraX, cameraY, sx, sy, aspect);
        }

        // Враги
        for (auto& e : enemies) {
            if (!e.alive) continue;
            drawRect(scaleLocation, e.x, e.y, e.w, e.h,
                0.1f, 0.2f, 0.8f, cameraX, cameraY, sx, sy, aspect);
            //HP над врагом
            drawHpBar(scaleLocation, e.x, e.y, e.w,
                e.hp, e.maxHp, cameraX, cameraY, sx, sy, aspect);
        }

        // Пули жёлтые — игрока, красные — врага
        for (auto& b : bullets) {
            if (!b.alive) continue;
            if (b.fromPlayer) {
                drawRect(scaleLocation, b.x, b.y, b.w, b.h,
                    1.0f, 0.9f, 0.2f, cameraX, cameraY, sx, sy, aspect);
            }
            else {
                drawRect(scaleLocation, b.x, b.y, b.w, b.h,
                    1.0f, 0.2f, 0.2f, cameraX, cameraY, sx, sy, aspect);
            }
        }

        // Если игрок
        if (player.hp <= 0) {
            // Большой красный прямоугольник-подложка СЫРОЕ!!!!
            drawRect(scaleLocation, cameraX, cameraY + 2.0f, 8.0f, 1.5f,
                0.1f, 0.1f, 0.1f, cameraX, cameraY, sx, sy, aspect);
            drawRect(scaleLocation, cameraX, cameraY + 2.0f, 6.0f, 0.4f,
                0.9f, 0.1f, 0.1f, cameraX, cameraY, sx, sy, aspect);
        }

        glEnableVertexAttribArray(1);
        glEnableVertexAttribArray(2);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteProgram(program);
    glfwTerminate();
    return 0;
}

//#include <glad/glad.h>
//#include <GLFW/glfw3.h>
//#include <iostream>
//#include <cmath>
//
//// Шейдеры
//const char* vertexShaderSource = R"(
//#version 460 core
//layout(location = 0) in vec2 aPos;
//layout(location = 1) in vec2 offset;
//layout(location = 2) in vec3 color;
//uniform vec2 scale;
//uniform float aspect;
//
//out vec3 vertexColor;
//
//void main() {
//    vec2 pos = aPos * scale * vec2(aspect, 1.0) + offset;
//    gl_Position = vec4(pos, 0.0, 1.0);
//    vertexColor = color;
//}
//)";
//
//const char* fragmentShaderSource = R"(
//#version 460 core
//out vec4 FragColor;
//in vec3 vertexColor;
//
//void main() {
//    FragColor = vec4(vertexColor, 1.0);
//}
//)";
//
//// Структуры и глобальные переменные для игры 
//
//struct Booker { //Где п?
//    float x, y;
//    float w, h;
//};
//
//// Параметры игрока
//Booker player = { 0.0f, 5.0f, 1.0f, 2.0f };
//float velX = 0.0f; // скорость движения туды-сюды
//float velY = 0.0f;
//bool isGrounded = false;
//
//// Платформа
//struct Ground { //Земля... ей всё едино
//    float x, y;
//    float w, h;
//};
//Ground platform = { 0.0f, 0.0f, 20.0f, 1.0f };
//
//// Камера
//float cameraX = 0.0f;
//float cameraY = 5.0f;
//const float VISIBLE_HEIGHT = 15.0f; // скока мы видим
//
//// Физические константы
//const float GRAVITY = -25.0f;
//const float JUMP_FORCE = 10.0f;
//const float MOVE_SPEED = 5.0f;
//
//int main() {
//    GLFWwindow* window;
//
//    /* Initialize the library */
//    if (!glfwInit()) {
//        std::cerr << "Can't load GLFW." << std::endl;
//        return -1;
//    }
//
//    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
//    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
//    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
//
//    /* Create a windowed mode window and its OpenGL context */
//    window = glfwCreateWindow(1024, 768, "BioShock Infinite 2D", NULL, NULL);
//    if (!window) {
//        std::cerr << "Can't make a window." << std::endl;
//        glfwTerminate();
//        return -1;
//    }
//
//    /* Make the window's context current */
//    glfwMakeContextCurrent(window);
//
//    if (!gladLoadGL()) {
//        std::cerr << "Can't load GLAD.\n";
//        return -1;
//    }
//
//    std::cout << "OpenGL" << GLVersion.major << "." << GLVersion.minor << "\n";
//
//    glfwSwapInterval(1);
//
//    // Сборка шейдеров
//    unsigned int vs = glCreateShader(GL_VERTEX_SHADER);
//    glShaderSource(vs, 1, &vertexShaderSource, NULL);
//    glCompileShader(vs);
//    unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER);
//    glShaderSource(fs, 1, &fragmentShaderSource, NULL);
//    glCompileShader(fs);
//    unsigned int program = glCreateProgram();
//    glAttachShader(program, vs);
//    glAttachShader(program, fs);
//    glLinkProgram(program);
//
//    // Геометрия
//    float vertices[] = { -0.5f,-0.5f, 0.5f,-0.5f, 0.5f,0.5f, -0.5f,-0.5f, 0.5f,0.5f, -0.5f,0.5f };
//
//    unsigned int VAO, VBO;
//    glGenVertexArrays(1, &VAO);
//    glGenBuffers(1, &VBO);
//    glBindVertexArray(VAO);
//    glBindBuffer(GL_ARRAY_BUFFER, VBO);
//    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
//    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
//    glEnableVertexAttribArray(0);
//
//    int scaleLocation = glGetUniformLocation(program, "scale");
//    int aspectLocation = glGetUniformLocation(program, "aspect");
//
//    float lastFrameTime = 0.0f; // время с последнего кадра
//
//    float skyR = 0.5f, skyG = 0.8f, skyB = 1.0f;
//
//    while (!glfwWindowShouldClose(window)) {
//        float currentFrameTime = (float)glfwGetTime();
//        float deltaTime = currentFrameTime - lastFrameTime;
//        lastFrameTime = currentFrameTime;
//
//        if (deltaTime > 0.1f) deltaTime = 0.1f;
//
//        int w, h;
//        glfwGetWindowSize(window, &w, &h);
//        float aspect = (float)w / (float)h;
//
//        float oldPlayerY = player.y;
//
//        velX = 0.0f;
//
//        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
//            velX = -MOVE_SPEED;
//        }
//        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
//            velX = MOVE_SPEED;
//        }
//
//        // Прыжок (W или Space), если персонаж на земле
//        if ((glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) && isGrounded) {
//            velY = JUMP_FORCE;
//            isGrounded = false;
//        }
//
//        velY += GRAVITY * deltaTime;
//        player.x += velX * deltaTime;
//        player.y += velY * deltaTime;
//        isGrounded = false;
//
//        float platTop = platform.y + platform.h / 2.0f;
//        float platBottom = platform.y - platform.h / 2.0f;
//        float platLeft = platform.x - platform.w / 2.0f;
//        float platRight = platform.x + platform.w / 2.0f;
//
//        float footY = player.y - (player.h / 2.0f);
//        float oldFootY = oldPlayerY - (player.h / 2.0f);
//
//        bool isOverPlatform = (player.x - player.w / 2.0f + 0.05f < platRight) &&
//            (player.x + player.w / 2.0f - 0.05f > platLeft);
//        if (isOverPlatform) {
//            if (oldFootY >= platTop - 0.01f && footY <= platTop && velY <= 0.0f) {
//                player.y = platTop + (player.h / 2.0f);
//                velY = 0.0f;
//                isGrounded = true;
//            }
//        }
//        cameraX = player.x;
//        cameraY = player.y;
//
//        // Масштабирование
//        float sy = 2.0f / VISIBLE_HEIGHT;
//        float sx = sy;
//
//        /* Render here */
//        glClearColor(skyR, skyG, skyB, 1.0f);
//        glClear(GL_COLOR_BUFFER_BIT);
//        glUseProgram(program);
//
//        glUniform1f(aspectLocation, aspect);
//
//        glBindVertexArray(VAO);
//
//        // Перевод координат
//        float platOffset[] = { (platform.x - cameraX) * sx * aspect, (platform.y - cameraY) * sy };
//        float platScale[] = { platform.w * sx, platform.h * sy };
//        float platColor[] = { 0.4f, 0.25f, 0.1f };
//
//        glUniform2f(scaleLocation, platScale[0], platScale[1]);
//        glDisableVertexAttribArray(1);
//        glDisableVertexAttribArray(2);
//        glVertexAttrib2fv(1, platOffset);
//        glVertexAttrib3fv(2, platColor);
//        glDrawArrays(GL_TRIANGLES, 0, 6);
//
//        // Отрисовка игрока 
//        float plOffset[] = { (player.x - cameraX) * sx * aspect, (player.y - cameraY) * sy };
//        float plScale[] = { player.w * sx, player.h * sy };
//        float plColor[] = { 0.8f, 0.1f, 0.1f };
//
//        glUniform2f(scaleLocation, plScale[0], plScale[1]);
//        glVertexAttrib2fv(1, plOffset);
//        glVertexAttrib3fv(2, plColor);
//        glDrawArrays(GL_TRIANGLES, 0, 6);
//
//        glEnableVertexAttribArray(1);
//        glEnableVertexAttribArray(2);
//
//        glfwSwapBuffers(window);
//        glfwPollEvents();
//    }
//
//    glDeleteVertexArrays(1, &VAO);
//    glDeleteBuffers(1, &VBO);
//    glDeleteProgram(program);
//    glfwTerminate();
//    return 0;
//}
