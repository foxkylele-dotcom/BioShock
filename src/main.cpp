#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <cmath>

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

// Структуры и глобальные переменные для игры 

struct Booker { //Где п?
    float x, y;
    float w, h;
};

// Параметры игрока
Booker player = { 0.0f, 5.0f, 1.0f, 2.0f };
float velX = 0.0f; // скорость движения туды-сюды
float velY = 0.0f;
bool isGrounded = false;

// Платформа
struct Ground { //Земля... ей всё едино
    float x, y;
    float w, h;
};
Ground platform = { 0.0f, 0.0f, 20.0f, 1.0f };

// Камера
float cameraX = 0.0f;
float cameraY = 5.0f;
const float VISIBLE_HEIGHT = 15.0f; // скока мы видим

// Физические константы
const float GRAVITY = -25.0f;
const float JUMP_FORCE = 10.0f;
const float MOVE_SPEED = 5.0f;

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

    std::cout << "OpenGL" << GLVersion.major << "." << GLVersion.minor << "\n";

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

    while (!glfwWindowShouldClose(window)) {
        float currentFrameTime = (float)glfwGetTime();
        float deltaTime = currentFrameTime - lastFrameTime;
        lastFrameTime = currentFrameTime;

        if (deltaTime > 0.1f) deltaTime = 0.1f;

        int w, h;
        glfwGetWindowSize(window, &w, &h);
        float aspect = (float)w / (float)h;

        float oldPlayerY = player.y;

        velX = 0.0f;

        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
            velX = -MOVE_SPEED;
        }
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
            velX = MOVE_SPEED;
        }

        // Прыжок (W или Space), если персонаж на земле
        if ((glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) && isGrounded) {
            velY = JUMP_FORCE;
            isGrounded = false;
        }

        velY += GRAVITY * deltaTime;
        player.x += velX * deltaTime;
        player.y += velY * deltaTime;
        isGrounded = false;

        float platTop = platform.y + platform.h / 2.0f;
        float platBottom = platform.y - platform.h / 2.0f;
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
        cameraX = player.x;
        cameraY = player.y;

        // Масштабирование
        float sy = 2.0f / VISIBLE_HEIGHT;
        float sx = sy;

        /* Render here */
        glClearColor(skyR, skyG, skyB, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(program);

        glUniform1f(aspectLocation, aspect);

        glBindVertexArray(VAO);

        // Перевод координат
        float platOffset[] = { (platform.x - cameraX) * sx * aspect, (platform.y - cameraY) * sy };
        float platScale[] = { platform.w * sx, platform.h * sy };
        float platColor[] = { 0.4f, 0.25f, 0.1f };

        glUniform2f(scaleLocation, platScale[0], platScale[1]);
        glDisableVertexAttribArray(1);
        glDisableVertexAttribArray(2);
        glVertexAttrib2fv(1, platOffset);
        glVertexAttrib3fv(2, platColor);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // Отрисовка игрока 
        float plOffset[] = { (player.x - cameraX) * sx * aspect, (player.y - cameraY) * sy };
        float plScale[] = { player.w * sx, player.h * sy };
        float plColor[] = { 0.8f, 0.1f, 0.1f };

        glUniform2f(scaleLocation, plScale[0], plScale[1]);
        glVertexAttrib2fv(1, plOffset);
        glVertexAttrib3fv(2, plColor);
        glDrawArrays(GL_TRIANGLES, 0, 6);

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
