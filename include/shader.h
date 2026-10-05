#pragma once

#include <string>
#include <glad/glad.h>

class ShaderProgram {
public:
    ShaderProgram();
    ~ShaderProgram();

    bool load_dosbox_shader(const std::string& filepath, bool nearest_filter);
    bool load_default_shader();
    void use() const;

    void set_uniform2f(const char* name, float x, float y) const;
    void set_uniform1i(const char* name, int val) const;

    GLuint get_id() const { return program_id; }

private:
    GLuint program_id;
    GLuint compile_stage(GLenum stage_type, const std::string& source, const std::string& define_tag, bool nearest);
    void free_resources();
};
