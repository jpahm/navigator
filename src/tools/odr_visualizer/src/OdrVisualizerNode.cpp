#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <rclcpp/rclcpp.hpp>

#include <iostream>
#include <fstream>
#include <iomanip>

#include <ctime>
#include <cstdlib>

using namespace std;

#include "odr_visualizer/OdrVisualizerNode.hpp"
/*
PSEUDOCODE

On start:
	1. Read xodr file
	2. Iterate over each lane in each road
		a. For each lane, gather vertices and generate triangles
		b. Put tris in array.
	3. Convert tri array to triangle list Marker message (or MarkerArray?)

Every n seconds:
	1. Publish tri array

*/

using geometry_msgs::msg::Point;
using geometry_msgs::msg::Point32;
using geometry_msgs::msg::Polygon;
using geometry_msgs::msg::TransformStamped;
using geometry_msgs::msg::Vector3;
using nav_msgs::msg::Odometry;
using navigator::opendrive::LaneIdentifier;
using std_msgs::msg::ColorRGBA;
using visualization_msgs::msg::Marker;
using visualization_msgs::msg::MarkerArray;
using voltron_msgs::msg::PolygonArray;
using namespace std::chrono_literals;

OdrVisualizerNode::OdrVisualizerNode() : Node("odr_visualizer_node")
{
	// Handle parameters
	this->declare_parameter<std::string>("xodr_path", "/home/main/navigator/data/maps/town10/Town10HD_Opt.xodr");

	// Create publishers and subscribers
	marker_pub = this->create_publisher<visualization_msgs::msg::MarkerArray>("/map/viz", 1);

	// Init transform buffer and listener
	tf_buffer_ = std::make_unique<tf2_ros::Buffer>(this->get_clock());
	transform_listener_ = std::make_shared<tf2_ros::TransformListener>(*tf_buffer_);

	// Init timers
	map_pub_timer = this->create_wall_timer(5s, std::bind(&OdrVisualizerNode::publishMarkerArray, this));
	check_surrounding_road_timer = this->create_wall_timer(1s, std::bind(&OdrVisualizerNode::publishNearbyLanePolygons, this));

	// Read map from file, using our path param
	std::string xodr_path = this->get_parameter("xodr_path").as_string();
	RCLCPP_INFO(this->get_logger(), "Reading from " + xodr_path);
	odr_map = navigator::opendrive::load_map(xodr_path);

	generateMapMarkers();
}

void OdrVisualizerNode::generateMapMarkers()
{
	// Iterate through all roads->lanesections->lanes
	// For each lane: Construct Line Strip markers for left and right bound
	// Append markers to MarkerArray

	/**
	 * ELEMENT COLORS
	 **/

	ColorRGBA line_color;
	line_color.a = 1.0;
	line_color.r = 0.59;
	line_color.g = 0.67;
	line_color.b = 0.69;

	ColorRGBA driving_color;
	driving_color.a = 1.0;
	driving_color.r = 0.22;
	driving_color.g = 0.27;
	driving_color.b = 0.27;

	ColorRGBA shoulder_color;
	shoulder_color.a = 1.0;
	shoulder_color.r = 0.71;
	shoulder_color.g = 0.77;
	shoulder_color.b = 0.79;

	ColorRGBA sidewalk_color;
	sidewalk_color.a = 1.0;
	sidewalk_color.r = 0.89;
	sidewalk_color.g = 0.91;
	sidewalk_color.b = 0.91;

	ColorRGBA nearby_color;
	sidewalk_color.a = 1.0;
	sidewalk_color.r = 0.00;
	sidewalk_color.g = 0.65;
	sidewalk_color.b = 0.65;

	/**
	 * Mesh markers (triangle lists) and line list marker
	 **/

	Marker trilist_driving;
	trilist_driving.type = trilist_driving.TRIANGLE_LIST;
	trilist_driving.header.stamp = now();
	trilist_driving.header.frame_id = "map";
	trilist_driving.ns = "lanes_driving";
	trilist_driving.action = trilist_driving.MODIFY;
	trilist_driving.frame_locked = true;
	trilist_driving.color = driving_color;
	trilist_driving.scale.x = 1.0;
	trilist_driving.scale.y = 1.0;
	trilist_driving.scale.z = 1.0;

	Marker trilist_shoulder = trilist_driving;
	trilist_shoulder.ns = "lanes_shoulder";
	trilist_shoulder.color = shoulder_color;

	Marker trilist_sidewalk = trilist_driving;
	trilist_sidewalk.ns = "lanes_sidewalk";
	trilist_sidewalk.color = sidewalk_color;

	Marker trilist_nearby = trilist_driving;
	trilist_nearby.ns = "lanes_sidewalk";
	trilist_nearby.color = nearby_color;

	Marker line_list; // Stores borders for lane bounds, sidewalks, etc.
	line_list.type = line_list.LINE_LIST;
	line_list.header.stamp = now();
	line_list.header.frame_id = "map";
	line_list.ns = "lanes";
	line_list.id = 883883; // Why not? WSH.
	line_list.action = line_list.MODIFY;
	line_list.scale.x = 0.3;	   // Only scale.x is used
	line_list.frame_locked = true; // Move with the Rviz camera
	line_list.color = line_color;
	line_list.pose.position.z = 0.1; // Set lines ever-so-slightly above the surface to prevent overlap.

	// typedef boost::polygon::polygon_traits<polygon>::point_type point;

	/**
	 * Iterate through every lane in the map.
	 * For each lane, get its mesh, including vertices and indices.
	 * For each index (which corresponds to a triange's point in the mesh),
	 * 	- Add the point to the appropriate mesh
	 *  - Add the point and its predecessor to the line list (borders etc)
	 **/
	int lane_qty = 0;
	int road_qty = 0;
	auto closest_lane = odr_map->get_lane_from_xy(-117.0, 19.0);
	std::shared_ptr<odr::Road> closest_road = (closest_lane->lane_section.lock())->road.lock();
	double dist = closest_road->ref_line->get_distance(-117.0, 19.0);
	double s = closest_road->ref_line->match(-117.0, 19.0);
	double draw_detail = this->get_parameter("draw_detail").as_double();
	RCLCPP_INFO(get_logger(), "%s/%i: %f, %f, %f", closest_road->id.c_str(), closest_lane->id, dist, s, closest_road->length);
	for (auto road : odr_map->get_roads())
	{
		road_qty++;
		// std::shared_ptr<odr::Road> road = lane->road.lock();
		// RCLCPP_INFO(get_logger(), "%i", lane->id);
		// RCLCPP_INFO(get_logger(), "%s: %f, %f, %f", road->id.c_str(), dist, s, road->length);
		for (auto lsec : road->get_lanesections())
		{
			// auto road = *(lsec->road);
			// std::shared_ptr<odr::Road> road = lsec->road.lock();

			for (auto lane : lsec->get_lanes())
			{
				// Convert lane curves to triangles. The last get_mesh param describes resolution.
				auto mesh = lane->get_mesh(lsec->s0, lsec->get_end(), draw_detail);
				auto pts = mesh.vertices;	 // Points are triangle vertices
				auto indices = mesh.indices; // Describes order of verts to make tris

				lane_qty++;
				std::shared_ptr<odr::Road> road = lane->road.lock();
				// RCLCPP_INFO(get_logger(), "%i", lane->id);
				// RCLCPP_INFO(get_logger(), "%s", road->id.c_str());

				for (auto idx : indices)
				{
					Point p;
					p.x = pts[idx][0];
					p.y = pts[idx][1];

					if (lane->type == "driving")
					{
						trilist_driving.points.push_back(p);
					}
					if (lane->type == "driving")
					{
						trilist_driving.points.push_back(p);
					}
					else if (lane->type == "shoulder")
					{
						p.z -= 0.03; // Prevent overlap glitching. WSH.
						trilist_shoulder.points.push_back(p);
					}
					else if (lane->type == "sidewalk")
					{
						p.z += 0.1;
						trilist_sidewalk.points.push_back(p);
					}

					// Add a line segment to our line list marker.
					// See http://wiki.ros.org/rviz/DisplayTypes/Marker#Line_List_.28LINE_LIST.3D5.29
					// We can do this because points in the libOpenDRIVE mesh alternate from the
					// left to right side, so that even indices are on one side and odds are on the other.
					if (idx > 1)
					{
						Point a;
						a.x = pts[idx - 2][0];
						a.y = pts[idx - 2][1];

						Point b;
						b.x = pts[idx][0];
						b.y = pts[idx][1];

						line_list.points.push_back(a);
						line_list.points.push_back(b);
					}
				}
			}
		}
	}
	// Add each marker to our marker array.

	point_count = trilist_driving.points.size() +
				  trilist_shoulder.points.size() +
				  trilist_sidewalk.points.size() +
				  line_list.points.size();
	lane_markers.markers.push_back(trilist_driving); // Triangles that form surfaces for e.g. roads
	lane_markers.markers.push_back(trilist_shoulder);
	lane_markers.markers.push_back(trilist_sidewalk); // Triangles that form surfaces for e.g. roads
	lane_markers.markers.push_back(line_list);		  // Borders and other lines
	RCLCPP_INFO_ONCE(get_logger(), "%i lanes, %i roads", lane_qty, road_qty);
}

void OdrVisualizerNode::publishNearbyLanePolygons()
{
	PolygonArray nearby_lane_polygons;

	// clear our cached nearby_lane_ids
	nearby_lane_ids.clear();

	nearby_lane_polygons.header.stamp = get_clock()->now();
	nearby_lane_polygons.header.frame_id = 'map';

	RCLCPP_INFO(this->get_logger(), "Starting polygon search!");
	TransformStamped transformStamped;

	try
	{
		transformStamped = tf_buffer_->lookupTransform(
			"map", "base_link",
			tf2::TimePointZero);
	}
	catch (tf2::TransformException &ex)
	{
		RCLCPP_INFO(this->get_logger(), "Could not transform map->base_link: %s", ex.what());
		return;
	}
	RCLCPP_INFO(this->get_logger(), "Transform found.");

	// Find our current lane
	Vector3 pos = transformStamped.transform.translation;
	navigator::opendrive::get_lane_from_xy(odr_map, pos.x, pos.y);
	auto tic = get_clock()->now().nanoseconds();
	auto lanes = navigator::opendrive::get_nearby_lanes(odr_map, pos.x, pos.y, 30.0);
	auto toc = get_clock()->now().nanoseconds();
	RCLCPP_INFO(get_logger(), "Took %i seconds", (tic - toc));

	// Code to convert each lane into a polygon.
	// Don't get "polygon" confused with "mesh".
	// A polygon only contains border points in a ring.
	// A mesh is a collection of tris.
	for (auto lane : lanes)
	{
		// We only care about lanes of types "driving" and "shoulder"
		if (!(lane->type == "driving" || lane->type == "shoulder"))
			continue;

		double sample_res = 1.0;
		RCLCPP_INFO(get_logger(), "Nearby lane: %i", lane->id);
		Polygon pg;
		auto lsec = lane->lane_section.lock();
		auto outer_pts = lane->get_border_line(lsec->s0, lsec->get_end(), sample_res);
		auto inner_pts = lane->get_border_line(lsec->get_end(), lsec->s0, sample_res); // Reverse direction from outer_pts to form a loop
		for (odr::Vec3D border_pt : outer_pts)
		{
			Point32 ptmsg;
			ptmsg.x = border_pt[0];
			ptmsg.y = border_pt[1];
			pg.points.push_back(ptmsg);
		}
		for (odr::Vec3D border_pt : inner_pts)
		{
			Point32 ptmsg;
			ptmsg.x = border_pt[0];
			ptmsg.y = border_pt[1];
			pg.points.push_back(ptmsg);
		}
		nearby_lane_polygons.polygons.push_back(pg);
		ls_id = lsec->LaneIdentifier lane_id = {, ls_id, l_id};
	}
}

void OdrVisualizerNode::publishMarkerArray()
{
	RCLCPP_INFO_ONCE(get_logger(), "Publishing %i map markers with %i lanes. This will print once.", lane_markers.markers.size(), point_count);
	marker_pub->publish(lane_markers);
}