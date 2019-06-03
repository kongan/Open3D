//
// Created by wei on 4/15/19.
//

#include "PreConvEnvDiffuseShader.h"

#include <Open3D/Geometry/TriangleMesh.h>
#include <Open3D/Visualization/Utility/ColorMap.h>

#include <AdvancedRendering/Visualization/Shader/Shader.h>
#include <AdvancedRendering/Geometry/ExtendedTriangleMesh.h>
#include <AdvancedRendering/Visualization/Shader/Primitives.h>
#include <AdvancedRendering/Visualization/Visualizer/RenderOptionWithLighting.h>

namespace open3d {
namespace visualization {

namespace glsl {

bool PreConvEnvDiffuseShader::Compile() {
    if (!CompileShaders(SimpleVertexShader,
                        nullptr,
                        PreConvDiffuseFragmentShader)) {
        PrintShaderWarning("Compiling shaders failed.");
        return false;
    }

    V_ = glGetUniformLocation(program_, "V");
    P_ = glGetUniformLocation(program_, "P");

    tex_env_ = glGetUniformLocation(program_, "tex_env");
    return true;
}

void PreConvEnvDiffuseShader::Release() {
    UnbindGeometry();
    ReleaseProgram();
}

bool PreConvEnvDiffuseShader::BindGeometry(const geometry::Geometry &geometry,
                                           const RenderOption &option,
                                           const ViewControl &view) {
    // If there is already geometry, we first unbind it.
    // We use GL_STATIC_DRAW. When geometry changes, we clear buffers and
    // rebind the geometry. Note that this approach is slow. If the geometry is
    // changing per frame, consider implementing a new ShaderWrapper using
    // GL_STREAM_DRAW, and replace UnbindGeometry() with Buffer Object
    // Streaming mechanisms.
    UnbindGeometry();

    // Create buffers and bind the geometry
    std::vector<Eigen::Vector3f> points;
    std::vector<Eigen::Vector3i> triangles;
    if (!PrepareBinding(geometry, option, view, points, triangles)) {
        PrintShaderWarning("Binding failed when preparing data.");
        return false;
    }
    vertex_position_buffer_ = BindBuffer(points, GL_ARRAY_BUFFER, option);
    triangle_buffer_ = BindBuffer(triangles, GL_ELEMENT_ARRAY_BUFFER, option);
    bound_ = true;
    return true;
}

bool PreConvEnvDiffuseShader::RenderGeometry(const geometry::Geometry &geometry,
                                             const RenderOption &option,
                                             const ViewControl &view) {
    if (!PrepareRendering(geometry, option, view)) {
        PrintShaderWarning("Rendering failed during preparation.");
        return false;
    }

    auto &lighting_option = (const RenderOptionWithLighting &) option;
    tex_env_buffer_ = lighting_option.tex_env_buffer_;

    /** 0. Setup framebuffers **/
    GLuint fbo, rbo;
    glGenFramebuffers(1, &fbo);
    glGenRenderbuffers(1, &rbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                          kCubemapSize, kCubemapSize);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, rbo);
    glViewport(0, 0, kCubemapSize, kCubemapSize);

    /** 1. Setup programs and constant uniforms **/
    glUseProgram(program_);
    glUniformMatrix4fv(P_, 1, GL_FALSE, projection_.data());

    /** 2. Setup constant textures **/
    glUniform1i(tex_env_, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, tex_env_buffer_);

    /** 3. Setup varying uniforms and rendering target **/
    for (int i = 0; i < 6; ++i) {
        glUniformMatrix4fv(V_, 1, GL_FALSE, views_[i].data());
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
                               tex_diffuse_buffer_, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glEnableVertexAttribArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, vertex_position_buffer_);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, triangle_buffer_);
        glDrawElements(draw_arrays_mode_,
                       draw_arrays_size_,
                       GL_UNSIGNED_INT,
                       nullptr);
        glDisableVertexAttribArray(0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glDeleteFramebuffers(1, &fbo);
    glDeleteRenderbuffers(1, &rbo);

    glViewport(0, 0, view.GetWindowWidth(), view.GetWindowHeight());

    return true;
}

void PreConvEnvDiffuseShader::UnbindGeometry() {
    if (bound_) {
        glDeleteBuffers(1, &vertex_position_buffer_);
        glDeleteBuffers(1, &triangle_buffer_);
        bound_ = false;
    }
}

bool PreConvEnvDiffuseShader::PrepareRendering(
    const geometry::Geometry &geometry,
    const RenderOption &option,
    const ViewControl &view) {

    /** Additional states **/
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

    return true;
}

bool PreConvEnvDiffuseShader::PrepareBinding(
    const geometry::Geometry &geometry,
    const RenderOption &option,
    const ViewControl &view,
    std::vector<Eigen::Vector3f> &points,
    std::vector<Eigen::Vector3i> &triangles) {

    /** Prepare camera **/
    projection_ = GLHelper::Perspective(90.0f, 1.0f, 0.1f, 10.0f);
    LoadViews(views_);

    /** Prepare data **/
    LoadCube(points, triangles);

    /** Prepare target texture **/
    tex_diffuse_buffer_ = CreateTextureCubemap(kCubemapSize, false, option);

    draw_arrays_mode_ = GL_TRIANGLES;
    draw_arrays_size_ = GLsizei(triangles.size() * 3);
    return true;
}

}  // namespace glsl

}  // namespace visualization
}  // namespace open3d