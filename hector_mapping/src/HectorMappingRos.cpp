//=================================================================================================
// Copyright (c) 2011, Stefan Kohlbrecher, TU Darmstadt
// All rights reserved.

// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//     * Neither the name of the Simulation, Systems Optimization and Robotics
//       group, TU Darmstadt nor the names of its contributors may be used to
//       endorse or promote products derived from this software without
//       specific prior written permission.

// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//=================================================================================================

#include "HectorMappingRos.h"

#include "map/GridMap.h"

#include <geometry_msgs/PoseWithCovarianceStamped.h>
#include <nav_msgs/Odometry.h>

#include "sensor_msgs/PointCloud2.h"

#include "HectorDrawings.h"
#include "HectorDebugInfoProvider.h"
#include "HectorMapMutex.h"

//Mod by sameer
#include <math.h>
#include <rosbag/bag.h>
#include <rosbag/view.h>
#include <boost/foreach.hpp>
#include <sensor_msgs/LaserScan.h>
#include <nav_msgs/OccupancyGrid.h>
#include <fstream>
#include <Eigen/Geometry>
#include <pcl/io/pcd_io.h>
#include <pcl/point_types.h>
#include <pcl/registration/icp.h>
//Mod by sameer

#ifndef TF_SCALAR_H
typedef btScalar tfScalar;
#endif
using namespace std;
HectorMappingRos::HectorMappingRos()
: debugInfoProvider(0)
, hectorDrawings(0)
, lastGetMapUpdateIndex(-100)
, tfB_(0)
, map__publish_thread_(0)
, initial_pose_set_(false)
, load_map_(false)
, load_status_(false)
, first_scan_(true)
, initial_pose_slam_()
, mapr_()
, map_points_()
, initialscanguess_()
{
	ros::NodeHandle private_nh_("~");

	std::string mapTopic_ = "map";
	//Mod by Sameer
	private_nh_.param("load_map", load_map_, false);
	//Mod by Sameer
	private_nh_.param("pub_drawings", p_pub_drawings, false);
	private_nh_.param("pub_debug_output", p_pub_debug_output_, false);
	private_nh_.param("pub_map_odom_transform", p_pub_map_odom_transform_,true);
	private_nh_.param("pub_odometry", p_pub_odometry_,false);
	private_nh_.param("advertise_map_service", p_advertise_map_service_,true);
	private_nh_.param("scan_subscriber_queue_size", p_scan_subscriber_queue_size_, 5);

	private_nh_.param("map_resolution", p_map_resolution_, 0.025);
	private_nh_.param("map_size", p_map_size_, 1024);
	private_nh_.param("map_start_x", p_map_start_x_, 0.5);
	private_nh_.param("map_start_y", p_map_start_y_, 0.5);
	private_nh_.param("map_multi_res_levels", p_map_multi_res_levels_, 3);

	private_nh_.param("update_factor_free", p_update_factor_free_, 0.4);
	private_nh_.param("update_factor_occupied", p_update_factor_occupied_, 0.9);

	private_nh_.param("map_update_distance_thresh", p_map_update_distance_threshold_, 0.4);
	private_nh_.param("map_update_angle_thresh", p_map_update_angle_threshold_, 0.9);

	private_nh_.param("scan_topic", p_scan_topic_, std::string("scan"));
	private_nh_.param("sys_msg_topic", p_sys_msg_topic_, std::string("syscommand"));
	private_nh_.param("pose_update_topic", p_pose_update_topic_, std::string("poseupdate"));

	private_nh_.param("use_tf_scan_transformation", p_use_tf_scan_transformation_,true);
	private_nh_.param("use_tf_pose_start_estimate", p_use_tf_pose_start_estimate_,false);
	private_nh_.param("map_with_known_poses", p_map_with_known_poses_, false);

	private_nh_.param("base_frame", p_base_frame_, std::string("base_link"));
	private_nh_.param("map_frame", p_map_frame_, std::string("map"));
	private_nh_.param("odom_frame", p_odom_frame_, std::string("odom"));

	private_nh_.param("pub_map_scanmatch_transform", p_pub_map_scanmatch_transform_,true);
	private_nh_.param("tf_map_scanmatch_transform_frame_name", p_tf_map_scanmatch_transform_frame_name_, std::string("scanmatcher_frame"));

	private_nh_.param("output_timing", p_timing_output_,false);

	private_nh_.param("map_pub_period", p_map_pub_period_, 2.0);
	//ROS_INFO("YOOOOOOOOOOOOO");
	double tmp = 0.0;
	private_nh_.param("laser_min_dist", tmp, 0.4);
	p_sqr_laser_min_dist_ = static_cast<float>(tmp*tmp);

	private_nh_.param("laser_max_dist", tmp, 30.0);
	p_sqr_laser_max_dist_ = static_cast<float>(tmp*tmp);

	private_nh_.param("laser_z_min_value", tmp, -1.0);
	p_laser_z_min_value_ = static_cast<float>(tmp);

	private_nh_.param("laser_z_max_value", tmp, 1.0);
	p_laser_z_max_value_ = static_cast<float>(tmp);

	if (p_pub_drawings)
	{
		ROS_INFO("HectorSM publishing debug drawings");
		hectorDrawings = new HectorDrawings();
	}

	if(p_pub_debug_output_)
	{
		ROS_INFO("HectorSM publishing debug info");
		debugInfoProvider = new HectorDebugInfoProvider();
	}

	if(p_pub_odometry_)
	{
		odometryPublisher_ = node_.advertise<nav_msgs::Odometry>("scanmatch_odom", 50);
	}

	slamProcessor = new hectorslam::HectorSlamProcessor(static_cast<float>(p_map_resolution_), p_map_size_, p_map_size_, Eigen::Vector2f(p_map_start_x_, p_map_start_y_), p_map_multi_res_levels_, hectorDrawings, debugInfoProvider);
	slamProcessor->setUpdateFactorFree(p_update_factor_free_);
	slamProcessor->setUpdateFactorOccupied(p_update_factor_occupied_);
	slamProcessor->setMapUpdateMinDistDiff(p_map_update_distance_threshold_);
	slamProcessor->setMapUpdateMinAngleDiff(p_map_update_angle_threshold_);

	int mapLevels = slamProcessor->getMapLevels();
	mapLevels = 1;

	for (int i = 0; i < mapLevels; ++i)
	{
		mapPubContainer.push_back(MapPublisherContainer());
		slamProcessor->addMapMutex(i, new HectorMapMutex());

		std::string mapTopicStr(mapTopic_);

		if (i != 0)
		{
			mapTopicStr.append("_" + boost::lexical_cast<std::string>(i));
		}

		std::string mapMetaTopicStr(mapTopicStr);
		mapMetaTopicStr.append("_metadata");

		MapPublisherContainer& tmp = mapPubContainer[i];
		tmp.mapPublisher_ = node_.advertise<nav_msgs::OccupancyGrid>(mapTopicStr, 1, true);
		tmp.mapMetadataPublisher_ = node_.advertise<nav_msgs::MapMetaData>(mapMetaTopicStr, 1, true);

		if ( (i == 0) && p_advertise_map_service_)
		{
			tmp.dynamicMapServiceServer_ = node_.advertiseService("dynamic_map", &HectorMappingRos::mapCallback, this);
		}

		setServiceGetMapData(tmp.map_, slamProcessor->getGridMap(i));

		if ( i== 0){
			mapPubContainer[i].mapMetadataPublisher_.publish(mapPubContainer[i].map_.map.info);
		}
	}

	ROS_INFO("HectorSM p_base_frame_: %s", p_base_frame_.c_str());
	ROS_INFO("HectorSM p_map_frame_: %s", p_map_frame_.c_str());
	ROS_INFO("HectorSM p_odom_frame_: %s", p_odom_frame_.c_str());
	ROS_INFO("HectorSM p_scan_topic_: %s", p_scan_topic_.c_str());
	ROS_INFO("HectorSM p_use_tf_scan_transformation_: %s", p_use_tf_scan_transformation_ ? ("true") : ("false"));
	ROS_INFO("HectorSM p_pub_map_odom_transform_: %s", p_pub_map_odom_transform_ ? ("true") : ("false"));
	ROS_INFO("HectorSM p_scan_subscriber_queue_size_: %d", p_scan_subscriber_queue_size_);
	ROS_INFO("HectorSM p_map_pub_period_: %f", p_map_pub_period_);
	ROS_INFO("HectorSM p_update_factor_free_: %f", p_update_factor_free_);
	ROS_INFO("HectorSM p_update_factor_occupied_: %f", p_update_factor_occupied_);
	ROS_INFO("HectorSM p_map_update_distance_threshold_: %f ", p_map_update_distance_threshold_);
	ROS_INFO("HectorSM p_map_update_angle_threshold_: %f", p_map_update_angle_threshold_);
	ROS_INFO("HectorSM p_laser_z_min_value_: %f", p_laser_z_min_value_);
	ROS_INFO("HectorSM p_laser_z_max_value_: %f", p_laser_z_max_value_);
	scanSubscriber_ = node_.subscribe(p_scan_topic_, p_scan_subscriber_queue_size_, &HectorMappingRos::scanCallback, this);
	sysMsgSubscriber_ = node_.subscribe(p_sys_msg_topic_, 2, &HectorMappingRos::sysMsgCallback, this);
	initialposesubscriber_ = private_nh_.subscribe("/initialpose",1,&HectorMappingRos::initPoseCallback,this);

	poseUpdatePublisher_ = node_.advertise<geometry_msgs::PoseWithCovarianceStamped>(p_pose_update_topic_, 1, false);
	posePublisher_ = node_.advertise<geometry_msgs::PoseStamped>("slam_out_pose", 1, false);
	corrected_points_publisher_ = node_.advertise<sensor_msgs::PointCloud>("pose_correction_check",1,true); //Mod by sameer
	guess_pose_publisher_ = private_nh_.advertise<geometry_msgs::PoseStamped>("pose_check",1,true); //Mod by sameer

	scan_point_cloud_publisher_ = private_nh_.advertise<sensor_msgs::PointCloud>("slam_cloud",1,false);

	tfB_ = new tf::TransformBroadcaster();
	ROS_ASSERT(tfB_);

	/*
	bool p_use_static_map_ = false;

	if (p_use_static_map_){
	mapSubscriber_ = node_.subscribe(mapTopic_, 1, &HectorMappingRos::staticMapCallback, this);
}
*/

initial_pose_sub_ = new message_filters::Subscriber<geometry_msgs::PoseWithCovarianceStamped>(node_, "initialpose", 2);
initial_pose_filter_ = new tf::MessageFilter<geometry_msgs::PoseWithCovarianceStamped>(*initial_pose_sub_, tf_, p_map_frame_, 2);
initial_pose_filter_->registerCallback(boost::bind(&HectorMappingRos::initialPoseCallback, this, _1));


map__publish_thread_ = new boost::thread(boost::bind(&HectorMappingRos::publishMapLoop, this, p_map_pub_period_));

map_to_odom_.setIdentity();

lastMapPublishTime = ros::Time(0,0);
//Mod by Sameer
if (load_map_)
{
	loadMap();
	first_scan_= true;
	initial_pose_slam_ = ros::topic::waitForMessage<geometry_msgs::PoseWithCovarianceStamped>("/initialpose");
	initialPoseCallback(initial_pose_slam_);
}

load_status_ = true;
//Mod by Sameer
}

HectorMappingRos::~HectorMappingRos()
{
	delete slamProcessor;

	if (hectorDrawings)
	delete hectorDrawings;

	if (debugInfoProvider)
	delete debugInfoProvider;

	if (tfB_)
	delete tfB_;

	if(map__publish_thread_)
	delete map__publish_thread_;
}
void HectorMappingRos::poseCorrection(sensor_msgs::LaserScan scan)
{

    double minangle = 0, maxangle = 0, angleinc = 0, minrange = 0, maxrange = 0, lrange = 0, laserx = 0, lasery = 0, current_angle = 0;
    minangle = scan.angle_min;
    maxangle = scan.angle_max;
    minrange = scan.range_min;
    maxrange = scan.range_max;
    angleinc = scan.angle_increment;
    double size_estimate;
    size_estimate = (maxangle - minangle) / angleinc + 1;
    for (int i = 0; i < int(size_estimate); i++)
    {
        lrange = scan.ranges[i];
        if (lrange >= minrange && lrange <= maxrange)
        {
            current_angle = minangle + i * angleinc;
            laserx = lrange * cos(current_angle);
            lasery = lrange * sin(current_angle);
        }
    }

    geometry_msgs::PoseWithCovarianceStamped guesspose;
    guesspose = *initial_pose_slam_;
    // Basically we want to transform the points from the laser frame to the
    // world frame, for this purpose we need two transforms, one from the laser
    // frame to the base frame and one from the base frame to the world frame the
    // transform from the laser frame to the base frame is already given and is
    // fixed. The transform from the base frame to the world frame is on which we
    // have to determine, after correction using ICP. This is given in the form
    // of a "guesspose" and is then refined and this refinement is reflected in
    // the form of an updated guesspose of the robot which in turn is the updated
    // initial pose of the robot. Make the transformation matrix from the base
    // frame of the robot to the world
    Eigen::Quaternionf quat_base_world(
            guesspose.pose.pose.orientation.w,
            guesspose.pose.pose.orientation.x,
            guesspose.pose.pose.orientation.y,
            guesspose.pose.pose.orientation.z);
    Eigen::Matrix3f rot_base_world = quat_base_world.toRotationMatrix();
    Eigen::Vector4f t_base_world;
    t_base_world(0) = guesspose.pose.pose.position.x;
    t_base_world(1) = guesspose.pose.pose.position.y;
    t_base_world(2) = guesspose.pose.pose.position.z;
    t_base_world(3) = 1;
    ROS_INFO("[sameer][base-world transform] Quaternion is %f , %f, %f, %f", guesspose.pose.pose.orientation.w, guesspose.pose.pose.orientation.x, guesspose.pose.pose.orientation.y, guesspose.pose.pose.orientation.z);
    Eigen::Matrix4f trafo_base_world;
    trafo_base_world.setIdentity();
    trafo_base_world.block<3, 3>(0, 0) = rot_base_world;
    trafo_base_world.rightCols<1>() = t_base_world;

    //Make the transformation matrix from laser frame to the base frame of the robot
    ros::Duration dur(0.5);
    tf::StampedTransform laserTransform;
    if (tf_.waitForTransform(p_base_frame_, scan.header.frame_id, ros::Time(0), dur))
    {
        tf_.lookupTransform(p_base_frame_, scan.header.frame_id, ros::Time(0), laserTransform);
    }
    else
    {
        ROS_INFO("[pose correction][sameer]Could not get transform from base frame to laser frame, something is wrong.");
    }
    Eigen::Vector4f laserpoint;
    tf::Vector3 laser_in;
    initialscanguess_ = Eigen::MatrixXd::Zero(int(size_estimate), 2);
    int count = 0;
    for (int i = 0; i < int(size_estimate); i++)
    {
        lrange = scan.ranges[i];
        if (lrange >= minrange && lrange <= maxrange)
        {
            current_angle = minangle + i * angleinc;
            laserx = lrange * cos(current_angle);
            lasery = lrange * sin(current_angle);
            laser_in.setX(laserx);
            laser_in.setY(lasery);
            laser_in.setZ(0);
            laser_in = laserTransform(laser_in);
            laserpoint(0) = laser_in.x();
            laserpoint(1) = laser_in.y();
            laserpoint(2) = 0;
            laserpoint(3) = 1;
            laserpoint = trafo_base_world * laserpoint;
            initialscanguess_(count, 0) = laserpoint(0) / laserpoint(3);
            initialscanguess_(count, 1) = laserpoint(1) / laserpoint(3);
            count++;
        }
    }
    pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_in(new pcl::PointCloud<pcl::PointXYZ>);
    pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_out(new pcl::PointCloud<pcl::PointXYZ>);
    cloud_in->width = map_points_.rows();
    cloud_in->height = 1;
    cloud_in->is_dense = true;
    cloud_in->points.resize(cloud_in->width * cloud_in->height);
    count = 0;
    for (size_t i = 0; i< cloud_in->points.size(); ++i)
    {
        cloud_in->points[i].x = map_points_(count, 0);
        cloud_in->points[i].y = map_points_(count, 1);
        cloud_in->points[i].z = 0;
        count++;
    }

    cloud_out->width = initialscanguess_.rows();
    cloud_out->height = 1;
    cloud_out->is_dense = true;
    cloud_out->points.resize(cloud_out->width * cloud_out->height);
    count = 0;
    for (size_t i = 0; i< cloud_out->points.size(); ++i)
    {
        cloud_out->points[i].x = initialscanguess_(count, 0);
        cloud_out->points[i].y = initialscanguess_(count, 1);
        cloud_out->points[i].z = 0;
        count++;
    }
    double seed_dist = 400;
    pcl::IterativeClosestPoint<pcl::PointXYZ, pcl::PointXYZ> icp;
    icp.setInputSource(cloud_out);
    icp.setInputTarget(cloud_in);
    icp.setMaximumIterations(100);
		icp.setRANSACOutlierRejectionThreshold(0.01);
    icp.setTransformationEpsilon(1e-8);
    icp.setEuclideanFitnessEpsilon(0.005);
    Eigen::Matrix4f correction;
    pcl::PointCloud<pcl::PointXYZ> temp;
    pcl::PointCloud<pcl::PointXYZ>::Ptr Final(new pcl::PointCloud<pcl::PointXYZ>);
    correction.setIdentity();
    for (int i = 0; i < 200; i++)
    {
        icp.setMaxCorrespondenceDistance(seed_dist / (pow(2, i)));
        //icp.setRANSACOutlierRejectionThreshold(0.01);
        icp.align(temp);
        *Final = temp;
        temp.clear();
        icp.setInputSource(Final);
        cout << "ICP result:" << icp.hasConverged() << " with quality:" << icp.getFitnessScore() << endl;
        cout << "Correspondence distance is:" << icp.getMaxCorrespondenceDistance() << endl;
        cout << icp.getFinalTransformation() << endl;
        correction = (icp.getFinalTransformation()) * correction;
    }
    trafo_base_world = correction * trafo_base_world;
    rot_base_world = trafo_base_world.block<3, 3>(0, 0);
    t_base_world = trafo_base_world.rightCols<1>();
    guesspose.pose.pose.position.x = t_base_world(0);
    guesspose.pose.pose.position.y = t_base_world(1);
    guesspose.pose.pose.position.z = t_base_world(2);
    quat_base_world = rot_base_world;
    guesspose.pose.pose.orientation.w = quat_base_world.w();
    guesspose.pose.pose.orientation.x = quat_base_world.x();
    guesspose.pose.pose.orientation.y = quat_base_world.y();
    guesspose.pose.pose.orientation.z = quat_base_world.z();
		sensor_msgs::PointCloud check_cloud; //Scan to be published to check if everything is okay.
		check_cloud.header.frame_id = p_map_frame_;
		check_cloud.header.stamp = ros::Time::now();
		check_cloud.points.resize(initialscanguess_.rows());

		//check_cloud.channels.intensities.values.resize(initialscanguess_.rows());
		for (int i = 0; i < initialscanguess_.rows(); i++)
    {
        laserpoint(0) = initialscanguess_(i, 0);
        laserpoint(1) = initialscanguess_(i, 1);
        laserpoint(2) = 0;
        laserpoint(3) = 1;
        laserpoint = correction*laserpoint;

				check_cloud.points[i].x = laserpoint(0);
				check_cloud.points[i].y = laserpoint(1);
				check_cloud.points[i].z = 0.0;

				//check_cloud.channels.intensity.values[i] = 0;
  	}
		geometry_msgs::PoseStamped check_pose;
		check_pose.header = guesspose.header;
		check_pose.pose = guesspose.pose.pose;
		corrected_points_publisher_.publish(check_cloud);
		guess_pose_publisher_.publish(check_pose);
    tf::Pose correctpose;
    tf::poseMsgToTF(guesspose.pose.pose, correctpose);


    initial_pose_(0) = guesspose.pose.pose.position.x;
    initial_pose_(1) = guesspose.pose.pose.position.y;
    initial_pose_(2) = tf::getYaw(correctpose.getRotation());
}
void HectorMappingRos::scanCallback(sensor_msgs::LaserScan scan)
{
	if (first_scan_ && load_map_)
	{
		first_scan_ = false;
		poseCorrection(scan);
	}

	scan.header.stamp = ros::Time::now();
	if (hectorDrawings)
	{
		hectorDrawings->setTime(scan.header.stamp);
	}

	ros::WallTime startTime = ros::WallTime::now();

	if (!p_use_tf_scan_transformation_)
	{
		if (rosLaserScanToDataContainer(scan, laserScanContainer,slamProcessor->getScaleToMap()))
		{
			if (initial_pose_set_ && load_status_)
			{
				initial_pose_set_ = false;
				ROS_INFO("Using initial pose with world coords x: %f y: %f yaw: %f", initial_pose_[0], initial_pose_[1], initial_pose_[2]);
				slamProcessor->update(laserScanContainer,initial_pose_,true);
			}
			else
			{
				slamProcessor->update(laserScanContainer,slamProcessor->getLastScanMatchPose());
			}
		}
	}
	else
	{
		ros::Duration dur (0.5);
		if (tf_.waitForTransform(p_base_frame_,scan.header.frame_id, ros::Time(0),dur))
		{ //ROS_INFO("yoooo");
		tf::StampedTransform laserTransform;
		tf_.lookupTransform(p_base_frame_,scan.header.frame_id, ros::Time(0), laserTransform);

		//projector_.transformLaserScanToPointCloud(p_base_frame_ ,scan, pointCloud,tf_);
		projector_.projectLaser(scan, laser_point_cloud_,30.0);

		if (scan_point_cloud_publisher_.getNumSubscribers() > 0){
			scan_point_cloud_publisher_.publish(laser_point_cloud_);
		}

		Eigen::Vector3f startEstimate(Eigen::Vector3f::Zero());

		if(rosPointCloudToDataContainer(laser_point_cloud_, laserTransform, laserScanContainer, slamProcessor->getScaleToMap()))
		{
			//	ROS_INFO("%s",load_status_ ? "true":"false");
			if (initial_pose_set_ && load_status_)
			{
				initial_pose_set_ = false;
				startEstimate = initial_pose_;
			}
			else if (p_use_tf_pose_start_estimate_)
			{
				try
				{
					tf::StampedTransform stamped_pose;
					tf_.waitForTransform(p_map_frame_,p_base_frame_, ros::Time(0), ros::Duration(0.5));
					tf_.lookupTransform(p_map_frame_, p_base_frame_,  ros::Time(0), stamped_pose);

					tfScalar yaw, pitch, roll;
					stamped_pose.getBasis().getEulerYPR(yaw, pitch, roll);

					startEstimate = Eigen::Vector3f(stamped_pose.getOrigin().getX(),stamped_pose.getOrigin().getY(), yaw);
				}
				catch(tf::TransformException e)
				{
					ROS_ERROR("Transform from %s to %s failed\n", p_map_frame_.c_str(), p_base_frame_.c_str());
					startEstimate = slamProcessor->getLastScanMatchPose();
				}

			}else{
				startEstimate = slamProcessor->getLastScanMatchPose();
			}


			if (p_map_with_known_poses_){
				slamProcessor->update(laserScanContainer, startEstimate, true);
			}else{
				slamProcessor->update(laserScanContainer, startEstimate);
			}
		}

		if (initial_pose_set_ && load_status_)
		{
			initial_pose_set_ = false;
			startEstimate = initial_pose_;
			ROS_INFO("boo");
		}

	}else{
		ROS_INFO("lookupTransform %s to %s timed out. Could not transform laser scan into base_frame.", p_base_frame_.c_str(), scan.header.frame_id.c_str());
		return;
	}
}

if (p_timing_output_)
{
	ros::WallDuration duration = ros::WallTime::now() - startTime;
	ROS_INFO("HectorSLAM Iter took: %f milliseconds", duration.toSec()*1000.0f );
}

//If we're just building a map with known poses, we're finished now. Code below this point publishes the localization results.
if (p_map_with_known_poses_)
{
	return;
}

poseInfoContainer_.update(slamProcessor->getLastScanMatchPose(), slamProcessor->getLastScanMatchCovariance(), scan.header.stamp, p_map_frame_);

poseUpdatePublisher_.publish(poseInfoContainer_.getPoseWithCovarianceStamped());
posePublisher_.publish(poseInfoContainer_.getPoseStamped());

if(p_pub_odometry_)
{
	nav_msgs::Odometry tmp;
	tmp.pose = poseInfoContainer_.getPoseWithCovarianceStamped().pose;

	tmp.header = poseInfoContainer_.getPoseWithCovarianceStamped().header;
	odometryPublisher_.publish(tmp);
}

if (p_pub_map_odom_transform_)
{
	tf::StampedTransform odom_to_base;

	try
	{
		tf_.waitForTransform(p_odom_frame_, p_base_frame_, ros::Time(0), ros::Duration(0.5));

		tf_.lookupTransform(p_odom_frame_, p_base_frame_, ros::Time(0), odom_to_base);
	}
	catch(tf::TransformException e)
	{
		ROS_ERROR("Transform failed during publishing of map_odom transform: %s",e.what());
		odom_to_base.setIdentity();
	}
	map_to_odom_ = tf::Transform(poseInfoContainer_.getTfTransform() * odom_to_base.inverse());
	tfB_->sendTransform( tf::StampedTransform (map_to_odom_, scan.header.stamp, p_map_frame_, p_odom_frame_));
}

if (p_pub_map_scanmatch_transform_){
	tfB_->sendTransform( tf::StampedTransform(poseInfoContainer_.getTfTransform(), scan.header.stamp, p_map_frame_, p_tf_map_scanmatch_transform_frame_name_));
}
}

void HectorMappingRos::sysMsgCallback(const std_msgs::String& string)
{
	ROS_INFO("HectorSM sysMsgCallback, msg contents: %s", string.data.c_str());

	if (string.data == "reset")
	{
		ROS_INFO("HectorSM reset");
		slamProcessor->reset();
	}
}

bool HectorMappingRos::mapCallback(nav_msgs::GetMap::Request  &req,
	nav_msgs::GetMap::Response &res)
	{
		ROS_INFO("HectorSM Map service called");
		res = mapPubContainer[0].map_;
		return true;
	}

	void HectorMappingRos::publishMap(MapPublisherContainer& mapPublisher, const hectorslam::GridMap& gridMap, ros::Time timestamp, MapLockerInterface* mapMutex)
	{
		nav_msgs::GetMap::Response& map_ (mapPublisher.map_);

		//only update map if it changed
		if (lastGetMapUpdateIndex != gridMap.getUpdateIndex() || !load_status_)
		{
			//ROS_INFO("heyhey");
			int sizeX = gridMap.getSizeX();
			int sizeY = gridMap.getSizeY();

			int size = sizeX * sizeY;

			std::vector<int8_t>& data = map_.map.data;

			//std::vector contents are guaranteed to be contiguous, use memset to set all to unknown to save time in loop
			memset(&data[0], -1, sizeof(int8_t) * size);

			if (mapMutex)
			{
				mapMutex->lockMap();
			}

			for(int i=0; i < size; ++i)
			{
				if(gridMap.isFree(i))
				{
					data[i] = 0;
				}
				else if (gridMap.isOccupied(i))
				{
					data[i] = 100;
				}
			}

			lastGetMapUpdateIndex = gridMap.getUpdateIndex();

			if (mapMutex)
			{
				mapMutex->unlockMap();
			}
		}

		map_.map.header.stamp = timestamp;

		mapPublisher.mapPublisher_.publish(map_.map);
	}

	bool HectorMappingRos::rosLaserScanToDataContainer(const sensor_msgs::LaserScan& scan, hectorslam::DataContainer& dataContainer, float scaleToMap)
	{
		size_t size = scan.ranges.size();

		float angle = scan.angle_min;

		dataContainer.clear();

		dataContainer.setOrigo(Eigen::Vector2f::Zero());

		float maxRangeForContainer = scan.range_max - 0.1f;

		for (size_t i = 0; i < size; ++i)
		{
			float dist = scan.ranges[i];

			if ( (dist > scan.range_min) && (dist < maxRangeForContainer))
			{
				dist *= scaleToMap;
				dataContainer.add(Eigen::Vector2f(cos(angle) * dist, sin(angle) * dist));
			}

			angle += scan.angle_increment;
		}

		return true;
	}

	bool HectorMappingRos::rosPointCloudToDataContainer(const sensor_msgs::PointCloud& pointCloud, const tf::StampedTransform& laserTransform, hectorslam::DataContainer& dataContainer, float scaleToMap)
	{
		size_t size = pointCloud.points.size();
		//ROS_INFO("size: %d", size);

		dataContainer.clear();

		tf::Vector3 laserPos (laserTransform.getOrigin());
		dataContainer.setOrigo(Eigen::Vector2f(laserPos.x(), laserPos.y())*scaleToMap);

		for (size_t i = 0; i < size; ++i)
		{

			const geometry_msgs::Point32& currPoint(pointCloud.points[i]);

			float dist_sqr = currPoint.x*currPoint.x + currPoint.y* currPoint.y;

			if ( (dist_sqr > p_sqr_laser_min_dist_) && (dist_sqr < p_sqr_laser_max_dist_) ){

				if ( (currPoint.x < 0.0f) && (dist_sqr < 0.50f)){
					continue;
				}

				tf::Vector3 pointPosBaseFrame(laserTransform * tf::Vector3(currPoint.x, currPoint.y, currPoint.z));

				float pointPosLaserFrameZ = pointPosBaseFrame.z() - laserPos.z();

				if (pointPosLaserFrameZ > p_laser_z_min_value_ && pointPosLaserFrameZ < p_laser_z_max_value_)
				{
					dataContainer.add(Eigen::Vector2f(pointPosBaseFrame.x(),pointPosBaseFrame.y())*scaleToMap);
				}
			}
		}

		return true;
	}

	void HectorMappingRos::setServiceGetMapData(nav_msgs::GetMap::Response& map_, const hectorslam::GridMap& gridMap)
	{
		Eigen::Vector2f mapOrigin (gridMap.getWorldCoords(Eigen::Vector2f::Zero()));
		mapOrigin.array() -= gridMap.getCellLength()*0.5f;

		map_.map.info.origin.position.x = mapOrigin.x();
		map_.map.info.origin.position.y = mapOrigin.y();
		map_.map.info.origin.orientation.w = 1.0;

		map_.map.info.resolution = gridMap.getCellLength();

		map_.map.info.width = gridMap.getSizeX();
		map_.map.info.height = gridMap.getSizeY();

		map_.map.header.frame_id = p_map_frame_;
		map_.map.data.resize(map_.map.info.width * map_.map.info.height);
	}

	/*
	void HectorMappingRos::setStaticMapData(const nav_msgs::OccupancyGrid& map)
	{
	float cell_length = map.info.resolution;
	Eigen::Vector2f mapOrigin (map.info.origin.position.x + cell_length*0.5f,
	map.info.origin.position.y + cell_length*0.5f);

	int map_size_x = map.info.width;
	int map_size_y = map.info.height;

	slamProcessor = new hectorslam::HectorSlamProcessor(cell_length, map_size_x, map_size_y, Eigen::Vector2f(0.0f, 0.0f), 1, hectorDrawings, debugInfoProvider);
}
*/


void HectorMappingRos::publishMapLoop(double map_pub_period)
{
	ros::Rate r(1.0 / map_pub_period);
	while(ros::ok())
	{
		//ros::WallTime t1 = ros::WallTime::now();
		ros::Time mapTime (ros::Time::now());
		//publishMap(mapPubContainer[2],slamProcessor->getGridMap(2), mapTime);
		//publishMap(mapPubContainer[1],slamProcessor->getGridMap(1), mapTime);
		publishMap(mapPubContainer[0],slamProcessor->getGridMap(0), mapTime, slamProcessor->getMapMutex(0));

		//ros::WallDuration t2 = ros::WallTime::now() - t1;

		//std::cout << "time s: " << t2.toSec();
		//ROS_INFO("HectorSM ms: %4.2f", t2.toSec()*1000.0f);

		r.sleep();
	}
}

void HectorMappingRos::staticMapCallback(const nav_msgs::OccupancyGrid& map)
{

}

void HectorMappingRos::initialPoseCallback(const geometry_msgs::PoseWithCovarianceStampedConstPtr& msg)
{
	initial_pose_set_ = true;

	tf::Pose pose;
	tf::poseMsgToTF(msg->pose.pose, pose);
	initial_pose_ = Eigen::Vector3f(msg->pose.pose.position.x, msg->pose.pose.position.y, tf::getYaw(pose.getRotation()));
	ROS_INFO("Setting initial pose with world coords x: %f y: %f yaw: %f", initial_pose_[0], initial_pose_[1], initial_pose_[2]);
}

void HectorMappingRos::loadMap()
{

    mapr_ = ros::topic::waitForMessage<nav_msgs::OccupancyGrid>("staticmap");
    hectorslam::MapRepMultiMap* maprep_multimap = dynamic_cast<hectorslam::MapRepMultiMap*>(slamProcessor->mapRep);
    hectorslam::GridMap& mod_map = maprep_multimap->mapContainer[0].getGridMap();
    int sizeofmapX = mod_map.getSizeX();
    int sizeofmapY = mod_map.getSizeY();
    int sizeofmap = sizeofmapX * sizeofmapY;
    int map_points_count = 0;
    for (int i = 0; i < sizeofmap; ++i)
    {
        if (mapr_->data[i] == 0)
        {

            mod_map.updateSetFree(i);
        }
        else if (mapr_->data[i] == 100)
        {

            mod_map.updateSetOccupied(i);
            map_points_count++;
        }
    }
    ROS_INFO("Origin of the map is at x:%f, y:%f, z:%f, posex:%f, posey:%f, posez:%f, posew:%f",
            mapr_->info.origin.position.x, mapr_->info.origin.position.y,
            mapr_->info.origin.position.z, mapr_->info.origin.orientation.x,
            mapr_->info.origin.orientation.y, mapr_->info.origin.orientation.z,
            mapr_->info.origin.orientation.w);
    ofstream mapwriter;
    mapwriter.open("map_points_.csv");
    double xoffset, yoffset, xcoord, ycoord, zcoord, mapwidth, mapheight, mapres;
    zcoord = 0;
    xoffset = 0;
    yoffset = 0;
    xoffset = mapr_->info.origin.position.x;
    yoffset = mapr_->info.origin.position.y;
    mapwidth = mapr_->info.width;
    mapheight = mapr_->info.height;
    mapres = mapr_->info.resolution;
    ROS_INFO("Saving map into csv, hold on.. ");
    int count = 0;
    int occ_count = 0;
    map_points_ = Eigen::MatrixXd::Zero(map_points_count, 2);
    for (int i = 0; i < mapheight; i++)
    {
        for (int j = 0; j < mapwidth; j++)
        {
            if (mapr_->data[count] == 100)
            {
                xcoord = j * mapres + xoffset;
                ycoord = i * mapres + yoffset;
                map_points_(occ_count, 0) = xcoord;
                map_points_(occ_count, 1) = ycoord;
                mapwriter << map_points_(occ_count, 0) << "," << map_points_(occ_count, 1) << "," << 0 << endl;
                occ_count++;
            }
            count++;
        }
    }
    mapwriter.close();
    ROS_INFO(" ..Done");
    ros::Time mapTime(ros::Time::now());
    publishMap(mapPubContainer[0], slamProcessor->getGridMap(0), mapTime, slamProcessor->getMapMutex(0));
    //mod_map.updateSetFree(0);
    tf_.clear();
}
void HectorMappingRos::initPoseCallback(const geometry_msgs::PoseWithCovarianceStamped& initialpose)
{
	if (!load_map_)
		return;
	loadMap();
	initial_pose_slam_.reset(new geometry_msgs::PoseWithCovarianceStamped(initialpose));
	first_scan_ = true;

}
