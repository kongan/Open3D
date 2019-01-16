//
// Created by wei on 1/3/19.
//

#include <iostream>
#include <memory>
#include <Eigen/Dense>

#include <Core/Core.h>
#include <IO/IO.h>
#include <Visualization/Visualization.h>

#include <Core/Utility/Timer.h>
#include <Core/Registration/ColoredICP.h>
#include <Cuda/Registration/ColoredICPCuda.h>

#include "ReadDataAssociation.h"

std::vector<int> GetCorrespondences(
    open3d::PointCloud &source, open3d::PointCloud &target) {
    open3d::Timer timer;
    std::vector<int> correspondence_set;
    correspondence_set.resize(source.points_.size());

    timer.Start();
    open3d::KDTreeFlann target_kdtree;
    target_kdtree.SetGeometry(target);
    timer.Stop();
    open3d::PrintInfo("build kd tree: %f ms\n", timer.GetDuration());

    timer.Start();
#ifdef _OPENMP
    {
#endif
#ifdef _OPENMP
#pragma omp for nowait
#endif
        for (int i = 0; i < (int) source.points_.size(); i++) {
            std::vector<int> indices(1);
            std::vector<double> dists(1);
            const auto &point = source.points_[i];
            if (target_kdtree.SearchHybrid(point, 0.05, 1, indices, dists)
                > 0) {
                correspondence_set[i] = indices[0];
            }
        }
#ifdef _OPENMP
    }
#endif

    timer.Stop();
    open3d::PrintInfo("query: %f ms for %d queries\n",
                      timer.GetDuration(), source.points_.size());
    return correspondence_set;
}

std::shared_ptr<open3d::RGBDImage> ReadRGBDImage(
    const char *color_filename, const char *depth_filename,
    const open3d::PinholeCameraIntrinsic &intrinsic,
    bool visualize) {
    open3d::Image color, depth;
    ReadImage(color_filename, color);
    ReadImage(depth_filename, depth);
    open3d::PrintDebug("Reading RGBD image : \n");
    open3d::PrintDebug("     Color : %d x %d x %d (%d bits per channel)\n",
                       color.width_, color.height_,
                       color.num_of_channels_, color.bytes_per_channel_ * 8);
    open3d::PrintDebug("     Depth : %d x %d x %d (%d bits per channel)\n",
                       depth.width_, depth.height_,
                       depth.num_of_channels_, depth.bytes_per_channel_ * 8);
    double depth_scale = 1000.0, depth_trunc = 4.0;
    bool convert_rgb_to_intensity = true;
    std::shared_ptr<open3d::RGBDImage> rgbd_image =
        CreateRGBDImageFromColorAndDepth(color,
                                         depth,
                                         depth_scale,
                                         depth_trunc,
                                         convert_rgb_to_intensity);
    if (visualize) {
        auto pcd = CreatePointCloudFromRGBDImage(*rgbd_image, intrinsic);
        open3d::DrawGeometries({pcd});
    }
    return rgbd_image;
}

void VisualizeRegistration(const open3d::PointCloud &source,
                           const open3d::PointCloud &target,
                           const Eigen::Matrix4d &Transformation) {
    using namespace open3d;
    std::shared_ptr<PointCloud> source_transformed_ptr(new PointCloud);
    std::shared_ptr<PointCloud> target_ptr(new PointCloud);
    *source_transformed_ptr = source;
    *target_ptr = target;
    source_transformed_ptr->Transform(Transformation);
    DrawGeometries({source_transformed_ptr, target_ptr}, "Registration result");
}

class PointCloudForColoredICP : public open3d::PointCloud {
public:
    std::vector<Eigen::Vector3d> color_gradient_;
};

std::shared_ptr<PointCloudForColoredICP>
InitializePointCloudForColoredICP(const open3d::PointCloud &target,
                                  const open3d::KDTreeSearchParamHybrid
                                  &search_param) {
    using namespace open3d;
    PrintDebug("InitializePointCloudForColoredICP\n");

    KDTreeFlann tree;
    tree.SetGeometry(target);

    auto output = std::make_shared<PointCloudForColoredICP>();
    output->colors_ = target.colors_;
    output->normals_ = target.normals_;
    output->points_ = target.points_;

    size_t n_points = output->points_.size();
    output->color_gradient_.resize(n_points, Eigen::Vector3d::Zero());

    for (auto k = 0; k < n_points; k++) {
        const Eigen::Vector3d &vt = output->points_[k];
        const Eigen::Vector3d &nt = output->normals_[k];
        double it = (output->colors_[k](0) + output->colors_[k](1)
            + output->colors_[k](2)) / 3.0;

        std::vector<int> point_idx;
        std::vector<double> point_squared_distance;

        if (tree.SearchHybrid(vt,
                              search_param.radius_,
                              search_param.max_nn_,
                              point_idx,
                              point_squared_distance) >= 3) {
            // approximate image gradient of vt's tangential plane
            size_t nn = point_idx.size();
            Eigen::MatrixXd A(nn, 3);
            Eigen::MatrixXd b(nn, 1);
            A.setZero();
            b.setZero();
            for (auto i = 1; i < nn; i++) {
                int P_adj_idx = point_idx[i];
                Eigen::Vector3d vt_adj = output->points_[P_adj_idx];
                Eigen::Vector3d vt_proj =
                    vt_adj - (vt_adj - vt).dot(nt) * nt;
                double it_adj = (output->colors_[P_adj_idx](0)
                    + output->colors_[P_adj_idx](1)
                    + output->colors_[P_adj_idx](2)) / 3.0;
                A(i - 1, 0) = (vt_proj(0) - vt(0));
                A(i - 1, 1) = (vt_proj(1) - vt(1));
                A(i - 1, 2) = (vt_proj(2) - vt(2));
                b(i - 1, 0) = (it_adj - it);
            }
            // adds orthogonal constraint
            A(nn - 1, 0) = (nn - 1) * nt(0);
            A(nn - 1, 1) = (nn - 1) * nt(1);
            A(nn - 1, 2) = (nn - 1) * nt(2);
            b(nn - 1, 0) = 0;
            // solving linear equation
            bool is_success;
            Eigen::MatrixXd x;
            std::tie(is_success, x) = SolveLinearSystem(
                A.transpose() * A, A.transpose() * b);
            if (is_success) {
                output->color_gradient_[k] = x;
            }
        }
    }
    return output;
}

int main(int argc, char **argv) {
    std::string base_path = "/home/wei/Work/data/stanford/lounge/";
    auto rgbd_filenames = ReadDataAssociation(
        base_path + "data_association.txt");

    open3d::Image source_color, source_depth, target_color, target_depth;
    open3d::PinholeCameraIntrinsic intrinsic = open3d::PinholeCameraIntrinsic(
        open3d::PinholeCameraIntrinsicParameters::PrimeSenseDefault);
    auto rgbd_source = ReadRGBDImage(
        (base_path + "/" + rgbd_filenames[0].second).c_str(),
        (base_path + "/" + rgbd_filenames[0].first).c_str(),
        intrinsic,
        false);
    auto rgbd_target = ReadRGBDImage(
        (base_path + "/" + rgbd_filenames[7].second).c_str(),
        (base_path + "/" + rgbd_filenames[7].first).c_str(),
        intrinsic,
        false);
    auto source_origin = CreatePointCloudFromRGBDImage(*rgbd_source, intrinsic);
    auto target_origin = CreatePointCloudFromRGBDImage(*rgbd_target, intrinsic);
    open3d::EstimateNormals(*source_origin);
    open3d::EstimateNormals(*target_origin);

    auto source = open3d::VoxelDownSample(*source_origin, 0.05);
    auto target = open3d::VoxelDownSample(*target_origin, 0.05);

    open3d::KDTreeFlann kdtree;
    kdtree.SetGeometry(*target);
    auto colored_pcl = InitializePointCloudForColoredICP(
        *target, open3d::KDTreeSearchParamHybrid(0.07 * 2.0, 30));
    for (int i = 0; i < colored_pcl->color_gradient_.size(); ++i) {
        std::cout << i << ": " << colored_pcl->color_gradient_[i].transpose()
                  << std::endl;
    }
    std::cout << "<<<<<<<<<<" << std::endl;

    open3d::Timer timer;
    timer.Start();
    open3d::cuda::TransformationEstimationCudaForColoredICP colored_icp;
    colored_icp.InitializeColorGradients(*target, kdtree,
                                         open3d::KDTreeSearchParamHybrid(
                                             0.07 * 2.0, 30));
    timer.Stop();
    open3d::PrintInfo("Computing color gradients takes %f ms",
                      timer.GetDuration() / 100.0f);




//    auto registration_result = open3d::RegistrationColoredICP(
//        *source, *target, 0.07,
//        Eigen::Matrix4d::Identity(),
//        open3d::ICPConvergenceCriteria(),
//        0.968);
//    VisualizeRegistration(*source, *target, registration_result.transformation_);
//    return 0;
}