/*
 * Package:   curb_localizer
 * Filename:  CurbLocalizerNode.cpp
 * Author:    Egan Johnson
 * Email:     egan.johnson@utdallas.edu
 * Copyright: 2022, Voltron UTD
 * License:   MIT License
 */

#include "curb_localizer/CurbLocalizerNode.hpp"

#include <pcl_conversions/pcl_conversions.h>
#include <pcl/conversions.h>
#include <pcl/common/transforms.h>

using namespace navigator::curb_localizer;

CurbLocalizerNode::CurbLocalizerNode() : Node("curb_localizer"){
    this->declare_parameter<std::string>("map_file_path", "SET_FILE_PATH.xodr");
    this->map_file_path = this->get_parameter("map_file_path").as_string();
    this->map = opendrive::load_map(this->map_file_path)->map;

    this->left_curb_points_sub = this->create_subscription<sensor_msgs::msg::PointCloud2>("left_curb_points",
        rclcpp::QoS(rclcpp::KeepLast(1)),
        std::bind(&CurbLocalizerNode::left_curb_points_callback, this, std::placeholders::_1));
    this->right_curb_points_sub = this->create_subscription<sensor_msgs::msg::PointCloud2>("right_curb_points",
        rclcpp::QoS(rclcpp::KeepLast(1)),
        std::bind(&CurbLocalizerNode::right_curb_points_callback, this, std::placeholders::_1));
    this->odom_in_sub = this->create_subscription<nav_msgs::msg::Odometry>("odom_in",
        rclcpp::QoS(rclcpp::KeepLast(1)),
        std::bind(&CurbLocalizerNode::odom_in_callback, this, std::placeholders::_1));
    this->odom_out = this->create_publisher<nav_msgs::msg::Odometry>("odom_out",
        rclcpp::QoS(rclcpp::KeepLast(1)));
}

void convert_to_pcl(const sensor_msgs::msg::PointCloud2::SharedPtr msg, pcl::PointCloud<pcl::PointXYZ> &out_cloud) {
    pcl::PCLPointCloud2 pcl_cloud;
    pcl_conversions::toPCL(*msg, pcl_cloud);
    pcl::fromPCLPointCloud2(pcl_cloud, out_cloud);
}

void CurbLocalizerNode::left_curb_points_callback(const sensor_msgs::msg::PointCloud2::SharedPtr msg){
    convert_to_pcl(msg, this->left_curb_points);
}

void CurbLocalizerNode::right_curb_points_callback(const sensor_msgs::msg::PointCloud2::SharedPtr msg){
    convert_to_pcl(msg, this->right_curb_points);
}

void CurbLocalizerNode::odom_in_callback(const nav_msgs::msg::Odometry::SharedPtr msg){
    this->odom_in = msg;
}

/**
 * @brief Translates the given point cloud to the given pose.
 *  (car reference --> map reference)
 * @param in_cloud 
 * @param odom 
 * @param out_cloud 
 */
void transform_points_to_odom(const pcl::PointCloud<pcl::PointXYZ> &in_cloud,
    const nav_msgs::msg::Odometry &odom,
    pcl::PointCloud<pcl::PointXYZ> &out_cloud) {

    Eigen::Affine3d odom_pose;
    odom_pose.rotate(Eigen::Quaterniond(odom.pose.pose.orientation.w,
        odom.pose.pose.orientation.x,
        odom.pose.pose.orientation.y,
        odom.pose.pose.orientation.z));

    odom_pose.translate(Eigen::Vector3d(odom.pose.pose.position.x,
        odom.pose.pose.position.y,
        odom.pose.pose.position.z));

    pcl::transformPointCloud(in_cloud, out_cloud, odom_pose);
}

/**
 * Algorithm:
 *  1. Start with GPS estimate of current position 
 *  2. Find left, right curb linestrings
 *      a. Find current lane
 *      b. Find current road
 *      c. ????
 *          i. Behavior near intersections/end of road warrents
 *              a few extra ??
 *      d. get descriptions of the curb
 *      e. a lot of parsing?
 *      f. linestring
 *      (may be able to assume lane boundary is curb since we are
 *      in the rightmost lane. Need ability to get next road for full curb)
 *  3. For each point in the cloud, find the minumum translation
 *      vector that will move the point to the curb linestring
 *  4. The odometry translation is the average point translation
 *  5. The confidence of the translation is some measure of how
 *    consitent the translation vector is- vectors pointing in 
 *      different directions are more likely to be wrong. 
 *      Try confidence 
 *          C = ||sum(displacement vectors)|| / sum(||displacement vectors||),
 *      or maybe ||sum_vec||^2 / sum(||displacement vectors||^2)
 * 
 * 
 */