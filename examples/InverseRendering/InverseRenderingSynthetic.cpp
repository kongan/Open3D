//
// Created by wei on 4/28/19.
//

#include <Open3D/Open3D.h>
#include <InverseRendering/Visualization/Visualizer/VisualizerPBR.h>
#include <InverseRendering/Geometry/TriangleMeshExtended.h>
#include <InverseRendering/IO/ClassIO/TriangleMeshExtendedIO.h>
#include <Open3D/Utility/Console.h>
#include <InverseRendering/Geometry/ImageExt.h>
#include <InverseRendering/Visualization/Utility/DrawGeometryPBR.h>

using namespace open3d;

int main(int argc, char **argv) {
    auto mesh = std::make_shared<geometry::TriangleMeshExtended>();
    io::ReadTriangleMeshExtendedFromPLY(
        "/media/wei/Data/data/pbr/model/sphere_plastic.ply",
        *mesh);
    for (auto &color : mesh->vertex_colors_) {
        color = Eigen::Vector3d(1, 1, 0.0);
    }
    for (auto &material : mesh->vertex_materials_) {
        material(0) = 1.0;
    }
    std::vector<geometry::Image> textures;
    textures.emplace_back(*io::CreateImageFromFile(
        "/media/wei/Data/data/pbr/image/gold_alex_apt.png"));

    auto ibl = std::make_shared<geometry::IBLLighting>();
    ibl->ReadEnvFromHDR("/media/wei/Data/data/pbr/env/Alexs_Apt_2k.hdr");

    visualization::VisualizerDR visualizer;
    if (!visualizer.CreateVisualizerWindow("DR", 640, 480, 0, 0)) {
        utility::PrintWarning("Failed creating OpenGL window.\n");
        return 0;
    }
    visualizer.BuildUtilities();
    visualizer.UpdateWindowTitle();

    visualizer.AddGeometryPBR(mesh, textures, ibl);
    for (int i = 0; i < 200; ++i) {
        std::cout << i << "\n";
        visualizer.UpdateRender();
        visualizer.PollEvents();
    }
}