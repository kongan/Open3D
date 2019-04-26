//
// Created by wei on 4/15/19.
//

#include <Material/Visualization/Shader/GeometryRendererPBR.h>
#include "VisualizerPBR.h"

namespace open3d {
namespace visualization {

bool VisualizerPBR::AddGeometryPBR(
    std::shared_ptr<const geometry::Geometry> geometry_ptr,
    const std::vector<geometry::Image> &textures,
    const std::shared_ptr<physics::Lighting> &lighting) {

    if (geometry_ptr->GetGeometryType() ==
        geometry::Geometry::GeometryType::TriangleMesh) {
        auto renderer_ptr = std::make_shared<glsl::TriangleMeshRendererPBR>();
        if (!(renderer_ptr->AddGeometry(geometry_ptr)
            && renderer_ptr->AddTextures(textures)
            && renderer_ptr->AddLights(lighting))) {
            utility::PrintDebug("Failed to add geometry\n");
            return false;
        }
        geometry_renderer_ptrs_.push_back(renderer_ptr);
    }

    geometry_ptrs_.push_back(geometry_ptr);

    view_control_ptr_->FitInGeometry(*geometry_ptr);
    ResetViewPoint();
    utility::PrintDebug("Add geometry and update bounding box to %s\n",
        view_control_ptr_->GetBoundingBox().GetPrintInfo().c_str());
    return UpdateGeometry();
}

void VisualizerPBR::Render() {
    glfwMakeContextCurrent(window_);

    view_control_ptr_->SetViewMatrices();

    glEnable(GL_MULTISAMPLE);
    glDisable(GL_BLEND);
    auto &background_color = render_option_ptr_->background_color_;
    glClearColor((GLclampf) background_color(0),
                 (GLclampf) background_color(1),
                 (GLclampf) background_color(2), 1.0f);
    glClearDepth(1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    geometry_renderer_fbo_outputs_.clear();
    for (const auto &renderer_ptr : geometry_renderer_ptrs_) {
        renderer_ptr->Render(*render_option_ptr_, *view_control_ptr_);

        auto renderer_pbr_ptr =
            (const std::shared_ptr<glsl::GeometryRendererPBR> &) renderer_ptr;
        geometry_renderer_fbo_outputs_.emplace_back(
            renderer_pbr_ptr->fbo_output_);
    }
    for (const auto &renderer_ptr : utility_renderer_ptrs_) {
        renderer_ptr->Render(*render_option_ptr_, *view_control_ptr_);
    }

    glfwSwapBuffers(window_);
}
}
}