/**
* This file is part of ORB-SLAM2.
*
* Copyright (C) 2014-2016 Raúl Mur-Artal <raulmur at unizar dot es> (University of Zaragoza)
* For more information see <https://github.com/raulmur/ORB_SLAM2>
*
* ORB-SLAM2 is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* ORB-SLAM2 is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with ORB-SLAM2. If not, see <http://www.gnu.org/licenses/>.
*/


#include<iostream>
#include<algorithm>
#include<fstream>
#include<chrono>

#include<ros/ros.h>
#include <cv_bridge/cv_bridge.h>
#include <message_filters/subscriber.h>
#include <message_filters/time_synchronizer.h>
#include <message_filters/sync_policies/approximate_time.h>

#include<opencv2/core/core.hpp>

#include"../../../include/System.h"


// add pcl publish header
#include <pcl_ros/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl_conversions/pcl_conversions.h>

typedef pcl::PointCloud<pcl::PointXYZ> PointCloud;

// add Camera pose publish type header
#include "std_msgs/MultiArrayDimension.h"
#include "std_msgs/Float64MultiArray.h"

using namespace std;



class ImageGrabber
{
public:

    ImageGrabber(ORB_SLAM2::System* pSLAM):mpSLAM(pSLAM){}

    void GrabRGBD(const sensor_msgs::ImageConstPtr& msgRGB,const sensor_msgs::ImageConstPtr& msgD);

    // define Pointcloud topic
    ros::Publisher KeyFrame_pub;
    // define camera Pose topic
    ros::Publisher CamPose_pub;

    // define CurrentFrame to get 2D(x,y) data from tracker
    vector<cv::KeyPoint> CurrentFrame;
    // define CurrentDepth to get depth(z) from tracker
    vector<float> CurrentDepth;
    // define CurrentPose to get camera pose from keyfram
    cv::Mat CurrentPose;
    // define pose to get save camera pose
    std_msgs::Float64MultiArray pose;
    // set pose varity setting
    void SetPose();

    // add callback function to get current frame from Tracker, and publish
    void callback();
    // add PublishPointcloud function to publish point cloud
    void PublishPointcloud();
    // add PublishPose function to publish camera pose
    void PublishCamPose();

    ORB_SLAM2::System* mpSLAM;
};


int main(int argc, char **argv)
{
    ros::init(argc, argv, "RGBD");
    ros::start();

    if(argc != 3)
    {
        cerr << endl << "Usage: rosrun ORB_SLAM2 RGBD path_to_vocabulary path_to_settings" << endl;        
        ros::shutdown();
        return 1;
    }

    // Create SLAM system. It initializes all system threads and gets ready to process frames.
    ORB_SLAM2::System SLAM(argv[1],argv[2],ORB_SLAM2::System::RGBD,true);

    ImageGrabber igb(&SLAM);


    ros::NodeHandle nh;

    // define /Ron/KeyFrame topic
    igb.KeyFrame_pub = nh.advertise<PointCloud>("/Ron/KeyFrame", 1);
    // define /Ron/CamPose topic
    igb.CamPose_pub = nh.advertise<std_msgs::Float64MultiArray>("/Ron/CamPose", 1);
    // set publish message of member pose
    igb.SetPose();

    message_filters::Subscriber<sensor_msgs::Image> rgb_sub(nh, "/camera/rgb/image_raw", 1);
    message_filters::Subscriber<sensor_msgs::Image> depth_sub(nh, "camera/depth_registered/image_raw", 1);
    typedef message_filters::sync_policies::ApproximateTime<sensor_msgs::Image, sensor_msgs::Image> sync_pol;
    message_filters::Synchronizer<sync_pol> sync(sync_pol(10), rgb_sub,depth_sub);
    sync.registerCallback(boost::bind(&ImageGrabber::GrabRGBD, &igb, _1, _2));

    ros::spin();

    // Stop all threads
    SLAM.Shutdown();

    // Save camera trajectory
    SLAM.SaveTrajectoryTUM("CameraTrajectory.txt");
    SLAM.SaveKeyFrameTrajectoryTUM("KeyFrameTrajectory.txt");  

    // Save customized Map 
    SLAM.SaveMap("MapPointandKeyFrame.bin");

    ros::shutdown();

    return 0;
}


// get current data from tracker
void ImageGrabber::callback()
{
    CurrentFrame = mpSLAM->GetmpTracker();
    CurrentDepth = mpSLAM->GetmvDepth();
    CurrentPose = mpSLAM->Getpose();
}

// publish point clouds
void ImageGrabber::PublishPointcloud()
{
    pcl::PointCloud<pcl::PointXYZ> cloud;
    cloud.header.frame_id = "camera_aligned_depth_to_color_frame";
    cloud.points.resize (CurrentFrame.size());
    for (size_t i=0; i<CurrentFrame.size(); i++)
    {
        cloud.points[i].x = CurrentFrame[i].pt.x / 100.0;
        cloud.points[i].y = CurrentFrame[i].pt.y / 100.0 ;
        cloud.points[i].z = CurrentDepth[i];
    }

    KeyFrame_pub.publish(cloud);
}

// publish camera pose
void ImageGrabber::PublishCamPose()
{
    std::vector<double> vec(CurrentPose.cols*CurrentPose.rows, 0);
    float* matData = (float*)CurrentPose.data;

    for (int i=0; i<CurrentPose.rows; i++)
        for (int j=0; j<CurrentPose.cols; j++)
            vec[i*CurrentPose.cols + j] = matData[i*CurrentPose.cols + j];

    pose.data = vec;
    CamPose_pub.publish(pose);
}

// set publish message of member pose
void ImageGrabber::SetPose()
{
    // fill out message:
    pose.layout.dim.push_back(std_msgs::MultiArrayDimension());
    pose.layout.dim.push_back(std_msgs::MultiArrayDimension());
    pose.layout.dim[0].label = "height";
    pose.layout.dim[1].label = "width";
    pose.layout.dim[0].size = 4;
    pose.layout.dim[1].size = 4;
    pose.layout.dim[0].stride = 16;
    pose.layout.dim[1].stride = 4;
    pose.layout.data_offset = 0;
}



void ImageGrabber::GrabRGBD(const sensor_msgs::ImageConstPtr& msgRGB,const sensor_msgs::ImageConstPtr& msgD)
{
    // Copy the ros image message to cv::Mat.
    cv_bridge::CvImageConstPtr cv_ptrRGB;

    try
    {
        cv_ptrRGB = cv_bridge::toCvShare(msgRGB);
    }
    catch (cv_bridge::Exception& e)
    {
        ROS_ERROR("cv_bridge exception: %s", e.what());
        return;
    }

    cv_bridge::CvImageConstPtr cv_ptrD;
    try
    {
        cv_ptrD = cv_bridge::toCvShare(msgD);
    }
    catch (cv_bridge::Exception& e)
    {
        ROS_ERROR("cv_bridge exception: %s", e.what());
        return;
    }
    mpSLAM->TrackRGBD(cv_ptrRGB->image,cv_ptrD->image,cv_ptrRGB->header.stamp.toSec());

    // get current data from tracker and keyframe
    callback();
    // publish point cloud and camera pose
    PublishPointcloud();
    PublishCamPose();
}