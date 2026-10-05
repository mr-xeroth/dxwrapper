#include "../include/shader.h"
#include <fstream>
#include <sstream>
#include <vector>
#include <iostream>

// constructs shader program object with zero id
ShaderProgram::ShaderProgram() : program_id(0) {}

// cleans up opengl program resources
ShaderProgram::~ShaderProgram() {
    free_resources();
}

// deletes active opengl program object
void ShaderProgram::free_resources() {
    if (program_id) {
        glDeleteProgram(program_id);
        program_id = 0;
    }
}

// binds shader program to the opengl pipeline
void ShaderProgram::use() const {
    if (program_id) {
        glUseProgram(program_id);
    }
}

// sets 2d float uniform value in the active program
void ShaderProgram::set_uniform2f(const char* name, float x, float y) const {
    if (!program_id) return;
    GLint loc = glGetUniformLocation(program_id, name);
    if (loc >= 0) {
        glUniform2f(loc, x, y);
    }
}

// sets integer uniform value in the active program
void ShaderProgram::set_uniform1i(const char* name, int val) const {
    if (!program_id) return;
    GLint loc = glGetUniformLocation(program_id, name);
    if (loc >= 0) {
        glUniform1i(loc, val);
    }
}

// compiles single vertex or fragment shader stage with preprocessor defines
GLuint ShaderProgram::compile_stage(GLenum stage_type, const std::string& source, const std::string& define_tag, bool nearest) {
    std::string modified_src;
    size_t ver_pos = source.find("#version");
    
    std::string inject = "\n#define " + define_tag + " 1\n";
    if (nearest) {
        inject += "#define OPENGLNB 1\n";
    }

    if (ver_pos != std::string::npos) {
        size_t eol = source.find('\n', ver_pos);
        if (eol != std::string::npos) {
            std::string rest = source.substr(eol + 1);
            modified_src = "#version 130\n" + inject + rest;
        } else {
            modified_src = "#version 130\n" + inject;
        }
    } else {
        modified_src = "#version 130\n" + inject + source;
    }

    GLuint shader = glCreateShader(stage_type);
    const char* c_str = modified_src.c_str();
    glShaderSource(shader, 1, &c_str, nullptr);
    glCompileShader(shader);

    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLint len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
        if (len > 0) {
            std::vector<char> log(len);
            glGetShaderInfoLog(shader, len, nullptr, log.data());
            std::cerr << "Shader compile error (" << define_tag << "): " << log.data() << "\n";
        }
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

// loads and compiles custom dosbox style glsl shader file
bool ShaderProgram::load_dosbox_shader(const std::string& filepath, bool nearest_filter) {
    std::string win_path = filepath;
    for (char& c : win_path) {
        if (c == '/') c = '\\';
    }

    FILE* f = fopen(win_path.c_str(), "rb");
    if (!f) {
        return false;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    std::string src;
    if (size > 0) {
        src.resize(size);
        size_t read_bytes = fread(&src[0], 1, size, f);
        src.resize(read_bytes);
    }
    fclose(f);

    if (src.empty()) {
        return false;
    }

    GLuint vs = compile_stage(GL_VERTEX_SHADER, src, "VERTEX", nearest_filter);
    if (!vs) {
        return false;
    }

    GLuint fs = compile_stage(GL_FRAGMENT_SHADER, src, "FRAGMENT", nearest_filter);
    if (!fs) {
        glDeleteShader(vs);
        return false;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "a_position");
    glBindAttribLocation(prog, 0, "VertexCoord");
    glLinkProgram(prog);

    GLint linked = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    glDeleteShader(vs);
    glDeleteShader(fs);

    if (!linked) {
        glDeleteProgram(prog);
        return false;
    }

    free_resources();
    program_id = prog;
    return true;
}

// compiles built-in fallback textured quad shader
bool ShaderProgram::load_default_shader() {
    free_resources();

    const char* vs_code = 
        "#version 330 core\n"
        "layout(location = 0) in vec4 a_position;\n"
        "out vec2 v_texCoord;\n"
        "uniform vec2 rubyTextureSize;\n"
        "uniform vec2 rubyInputSize;\n"
        "uniform vec2 rubyOutputSize;\n"
        "void main() {\n"
        "    gl_Position = a_position;\n"
        "    v_texCoord = vec2(a_position.x + 1.0, 1.0 - a_position.y) / 2.0 * (rubyInputSize / rubyTextureSize);\n"
        "}\n";

    const char* fs_code =
        "#version 330 core\n"
        "in vec2 v_texCoord;\n"
        "out vec4 FragColor;\n"
        "uniform sampler2D rubyTexture;\n"
        "void main() {\n"
        "    FragColor = texture(rubyTexture, v_texCoord);\n"
        "}\n";

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vs_code, nullptr);
    glCompileShader(vs);

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fs_code, nullptr);
    glCompileShader(fs);

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "a_position");
    glLinkProgram(prog);

    glDeleteShader(vs);
    glDeleteShader(fs);

    program_id = prog;
    return true;
}
