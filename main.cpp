/*
 * input thread:
 *  -> wait for all the threads to finish
 *  -> lock elements mutex
 *      -> adds elements to the buffer
 *  -> releases elements mutex
 *
 * update threads:
 *  -> wait on the mutex
 *      -> iterate on specified part of the buffer
 *      -> lock cell (x,y) mutex
 *      -> put in cell
 *      -> release cell (x,y) mutex
 *  -> barrier
 *  -> iterate on specified part of the cells with items
 *      -> lock cell (x,y) mutex
 *      -> lock cell (x+dx,y+dy) mutex
 *      -> solve collisions
 *      -> release cell (x+dx,y+dy) mutex
 *      -> release cell (x,y) mutex
 *  -> barrier 2
 *  -> signal all threads finished
 */


#include <iostream>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#define GLM_GTX_norm
#define GLM_GTC_type_ptr
#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>
#include <glm/gtc/type_ptr.hpp>
#define FMT_HEADER_ONLY
#include <fmt/core.h>
#include <vector>
#include <chrono>
#include <sstream>

const char *vertexShaderSource = "#version 450 core\n"
                                 "layout (location = 0) in vec3 inPos;\n"
                                 "layout (location = 1) in vec4 inColor;\n"
                                 "uniform mat4 viewMatrix;\n"
                                 "uniform mat4 projMatrix;\n"
                                 "out vec3 fragColor;\n"
                                 "void main()\n"
                                 "{\n"
                                 "   gl_Position = projMatrix * viewMatrix * vec4(inPos, 1.0);\n"
                                 "   fragColor = inColor.rgb;\n"
                                 "}\0";
const char *fragmentShaderSource = "#version 450 core\n"
                                   "in vec3 fragColor;\n"
                                   "out vec4 outColor;\n"
                                   "void main()\n"
                                   "{\n"
                                   "   vec2 coord = gl_PointCoord - vec2(0.5);\n"
                                   "    const float radius = 0.25;\n"
                                   "    if (length(gl_PointCoord - vec2(0.5)) > radius) {\n"
                                   "        discard;\n"
                                   "    }\n"
                                   "    outColor = vec4(fragColor, 1);"
                                   "}\n\0";


struct Particle {
    alignas(16) glm::vec3 position{};
    alignas(16) glm::vec3 velocity{};
    alignas(16) glm::vec3 force{};
    float density{}, pressure{};
    glm::vec4 color{};
};

class ParticleCollisionDemo {
public:
    static constexpr int WIDTH = 1000;
    static constexpr int HEIGHT = 800;
    const std::string appName = "Thread collisions";
    static constexpr uint32_t PARTICLE_COUNT = 512;
    static constexpr float radius = 8.0f;

    ParticleCollisionDemo() {
        // Initialize glfw
        if (!glfwInit())
            exit(EXIT_FAILURE);

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        // Create the window
        window = glfwCreateWindow(WIDTH, HEIGHT, appName.c_str(), nullptr, nullptr);
        if (!window) {
            glfwTerminate();
            exit(EXIT_FAILURE);
        }

        // Make this window the current context
        glfwMakeContextCurrent(window);

        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
        {
            std::cout << "Failed to initialize GLAD" << std::endl;
            exit(EXIT_FAILURE);
        }

    }

    ~ParticleCollisionDemo() {
        glDeleteVertexArrays(1, &VAO);
        glDeleteBuffers(1, &VBO);
        glDeleteProgram(shaderProgram);
        glfwDestroyWindow(window);
        glfwTerminate();
    }

    void createShaders() {
        // build and compile our shader program
        // ------------------------------------
        // vertex shader
        unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertexShader, 1, &vertexShaderSource, nullptr);
        glCompileShader(vertexShader);
        // check for shader compile errors
        int success;
        char infoLog[512];
        glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(vertexShader, 512, nullptr, infoLog);
            std::cout << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << infoLog << std::endl;
        }
        // fragment shader
        unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragmentShader, 1, &fragmentShaderSource, nullptr);

        glCompileShader(fragmentShader);
        // check for shader compile errors
        glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(fragmentShader, 512, nullptr, infoLog);
            std::cout << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n" << infoLog << std::endl;
        }
        // link shaders
        shaderProgram = glCreateProgram();
        glAttachShader(shaderProgram, vertexShader);
        glAttachShader(shaderProgram, fragmentShader);
        glLinkProgram(shaderProgram);
        // check for linking errors
        glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(shaderProgram, 512, nullptr, infoLog);
            std::cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
        }
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
    }

    void createPoints(){

        auto accPos = glm::vec3(3*float(WIDTH)/8, 100, 0);

        for (uint32_t i = 0; i < PARTICLE_COUNT; i++) {
            auto& particle = particles[i];
            particle.position = accPos;
            particle.velocity = glm::vec3(0.0f, -1.0f, 0.0f);
            particle.color = glm::vec4(0.2f, 0.6f, 1.0f, 1.0f);

            accPos.x += radius*1.2f;

            if (accPos.x > (float) 5*float(WIDTH)/8) {
                accPos.y += radius*1.2f;
                accPos.x = 3*float(WIDTH)/8;
            }

        }

        glGenVertexArrays(1, &VAO);
        glBindVertexArray(VAO);

        glGenBuffers(1, &VBO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);


        glBufferData(GL_ARRAY_BUFFER, particles.size()*sizeof(Particle), particles.data(), GL_DYNAMIC_DRAW);

        // position
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Particle), (void*)offsetof(Particle, position));
        //color
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Particle), (void*)offsetof(Particle, color));
        size = (int) particles.size();
        glBindBuffer(GL_ARRAY_BUFFER, 0);

    }

    static glm::mat4 orthographicProjection(float left, float right, float top, float bottom, float near, float far) {
        auto proj = glm::mat4{1.0f};
        proj[0][0] = -2.f / (right - left);
        proj[1][1] = -2.f / (top - bottom);
        proj[2][2] = 1.f / (far - near);
        proj[3][0] = -(right + left) / (right - left);
        proj[3][1] = -(bottom + top) / (top - bottom);
        proj[3][2] = -(far + near) / (far - near);
        return proj;
    }
    static glm::mat4 viewTarget(glm::vec3 position, glm::vec3 target, glm::vec3 up) {
        const glm::vec3 w{glm::normalize(target - position)};
        const glm::vec3 u{glm::normalize(glm::cross(w, up))};
        const glm::vec3 v{glm::cross(w, u)};

        auto view = glm::mat4{1.f};
        view[0][0] = u.x;
        view[1][0] = u.y;
        view[2][0] = u.z;
        view[0][1] = v.x;
        view[1][1] = v.y;
        view[2][1] = v.z;
        view[0][2] = w.x;
        view[1][2] = w.y;
        view[2][2] = w.z;
        view[3][0] = -glm::dot(u, position);
        view[3][1] = -glm::dot(v, position);
        view[3][2] = -glm::dot(w, position);

        return view;
    }

    void run() {
        createShaders();
        createPoints();

        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime, accTime = 0;
        uint32_t frames = 0;

        auto viewMatrix = viewTarget({0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 0.0f }, {0.0f, 1.0f, 0.0f});

        auto projMatrix = orthographicProjection(0.0f, (float) WIDTH, (float) HEIGHT, 0.0f, 0.1f, 1000.f);


        while (!glfwWindowShouldClose(window)) {
            auto newTime = std::chrono::high_resolution_clock::now();
            deltaTime =
                    std::chrono::duration<float, std::chrono::seconds::period>(newTime - currentTime).count();
            currentTime = newTime;
            accTime += deltaTime;
            frames++;

            glfwPollEvents();

            glClearColor(0.05f, 0.1f, 0.1f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            glUseProgram(shaderProgram);

            GLint view = glGetUniformLocation(shaderProgram, "viewMatrix");
            glUniformMatrix4fv(view, 1, GL_FALSE, glm::value_ptr(viewMatrix));

            GLint proj = glGetUniformLocation(shaderProgram, "projMatrix");
            glUniformMatrix4fv(proj, 1, GL_FALSE, glm::value_ptr(projMatrix));

            updateParticles();

            glPointSize(2*radius);
            glDrawArrays(GL_POINTS, 0, size);

            // Display FPS
            if (accTime > 0.5f) {
                double fps = double(frames) / accTime;

                std::stringstream ss;
                ss << appName <<" [" << fps << " FPS]";

                glfwSetWindowTitle(window, ss.str().c_str());

                frames = 0;
                accTime = 0.0f;
            }
            glfwSwapBuffers(window);
        }

    }

    void updateParticles(){
        for (auto& particle : particles) {
            particle.position += particle.velocity;

            for (auto& other : particles){
                if (&other == &particle) continue;

                auto vec = particle.position - other.position;
                if (glm::dot(vec, vec) < radius*radius){
                    float distToMove = (radius - glm::length(vec))/2;
                    vec = glm::normalize(vec);
                    other.position -= vec*distToMove;
                    particle.position += vec*distToMove;


                    other.velocity = glm::vec3(0.0f);
                    particle.velocity = glm::vec3(0.0f);
                }
            }

            if (particle.position.x < 0.0f) {
                particle.position.x = 0.0f;
                particle.velocity.x = -particle.velocity.x;
            } else if (particle.position.x > WIDTH) {
                particle.position.x = WIDTH;
                particle.velocity.x = -particle.velocity.x;
            }
            if (particle.position.y < 0.0f) {
                particle.position.y = 0.0f;
                particle.velocity.y = -particle.velocity.y;
            } else if (particle.position.y > HEIGHT) {
                particle.position.y = HEIGHT;
                particle.velocity.y = -particle.velocity.y;
            }
        }

        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, particles.size()*sizeof(Particle), particles.data(), GL_DYNAMIC_DRAW);

    }


private:
    GLFWwindow *window = nullptr;
    GLuint shaderProgram{};
    GLuint VBO{}, VAO{};
    std::vector<Particle> particles{PARTICLE_COUNT};
    int size{};
    struct cudaGraphicsResource* cudaVbo{};
};

int main() {
    ParticleCollisionDemo example;

    example.run();

    return EXIT_SUCCESS;
}