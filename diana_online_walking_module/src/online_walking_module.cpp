/*
 * diana_online_walking_module.cpp
 *
 *  Created on: 2018. 2. 27.
 *      Author: Crowban
 */

#include "diana_online_walking_module/online_walking_module.h"

using namespace diana;

class WalkingStatusMSG
{
public:
  static const std::string FAILED_TO_ADD_STEP_DATA_MSG;
  static const std::string BALANCE_PARAM_SETTING_STARTED_MSG;
  static const std::string BALANCE_PARAM_SETTING_FINISHED_MSG;
  static const std::string JOINT_FEEDBACK_GAIN_UPDATE_STARTED_MSG;
  static const std::string JOINT_FEEDBACK_GAIN_UPDATE_FINISHED_MSG;
  static const std::string WALKING_MODULE_IS_ENABLED_MSG;
  static const std::string WALKING_MODULE_IS_DISABLED_MSG;
  static const std::string BALANCE_HAS_BEEN_TURNED_OFF;
  static const std::string WALKING_START_MSG;
  static const std::string WALKING_FINISH_MSG;
};

const std::string WalkingStatusMSG::FAILED_TO_ADD_STEP_DATA_MSG = "Failed_to_add_Step_Data";
const std::string WalkingStatusMSG::BALANCE_PARAM_SETTING_STARTED_MSG = "Balance_Param_Setting_Started";
const std::string WalkingStatusMSG::BALANCE_PARAM_SETTING_FINISHED_MSG = "Balance_Param_Setting_Finished";
const std::string WalkingStatusMSG::JOINT_FEEDBACK_GAIN_UPDATE_STARTED_MSG = "Joint_FeedBack_Gain_Update_Started";
const std::string WalkingStatusMSG::JOINT_FEEDBACK_GAIN_UPDATE_FINISHED_MSG = "Joint_FeedBack_Gain_Update_Finished";
const std::string WalkingStatusMSG::WALKING_MODULE_IS_ENABLED_MSG = "Walking_Module_is_enabled";
const std::string WalkingStatusMSG::WALKING_MODULE_IS_DISABLED_MSG = "Walking_Module_is_disabled";
const std::string WalkingStatusMSG::BALANCE_HAS_BEEN_TURNED_OFF = "Balance_has_been_turned_off";
const std::string WalkingStatusMSG::WALKING_START_MSG = "Walking_Started";
const std::string WalkingStatusMSG::WALKING_FINISH_MSG = "Walking_Finished";

OnlineWalkingModule::OnlineWalkingModule()
: control_cycle_msec_(8)
{
  gazebo_          = false;
  enable_          = false;
  module_name_     = "online_walking_module";
  control_mode_    = robotis_framework::PositionControl;
  result_["r_hip_pitch"  ] = new robotis_framework::DynamixelState();
  result_["r_hip_roll"   ] = new robotis_framework::DynamixelState();
  result_["r_hip_yaw"    ] = new robotis_framework::DynamixelState();
  result_["r_knee_pitch" ] = new robotis_framework::DynamixelState();
  result_["r_ankle_pitch"] = new robotis_framework::DynamixelState();
  result_["r_ankle_roll" ] = new robotis_framework::DynamixelState();

  result_["l_hip_pitch"  ] = new robotis_framework::DynamixelState();
  result_["l_hip_roll"   ] = new robotis_framework::DynamixelState();
  result_["l_hip_yaw"    ] = new robotis_framework::DynamixelState();
  result_["l_knee_pitch" ] = new robotis_framework::DynamixelState();
  result_["l_ankle_pitch"] = new robotis_framework::DynamixelState();
  result_["l_ankle_roll" ] = new robotis_framework::DynamixelState();

  joint_name_to_index_["r_hip_pitch"  ] = 0;
  joint_name_to_index_["r_hip_roll"   ] = 1;
  joint_name_to_index_["r_hip_yaw"    ] = 2;
  joint_name_to_index_["r_knee_pitch" ] = 3;
  joint_name_to_index_["r_ankle_pitch"] = 4;
  joint_name_to_index_["r_ankle_roll" ] = 5;

  joint_name_to_index_["l_hip_pitch"  ] = 6;
  joint_name_to_index_["l_hip_roll"   ] = 7;
  joint_name_to_index_["l_hip_yaw"    ] = 8;
  joint_name_to_index_["l_knee_pitch" ] = 9;
  joint_name_to_index_["l_ankle_pitch"] = 10;
  joint_name_to_index_["l_ankle_roll" ] = 11;

  previous_running_    = present_running    = false;


  desired_matrix_g_to_pelvis_ = Eigen::MatrixXd::Identity(4,4);
  desired_matrix_g_to_rfoot_  = Eigen::MatrixXd::Identity(4,4);
  desired_matrix_g_to_lfoot_  = Eigen::MatrixXd::Identity(4,4);


  balance_update_with_loop_ = false;
  balance_update_duration_ = 2.0;
  balance_update_sys_time_ = 2.0;
  balance_update_tra_.changeTrajectory(0, 0, 0, 0, balance_update_duration_, 1, 0, 0);

  joint_feedback_update_with_loop_ = false;
  joint_feedback_update_duration_ = 2.0;
  joint_feedback_update_sys_time_ = 2.0;
  joint_feedback_update_tra_.changeTrajectory(0, 0, 0, 0, balance_update_duration_, 1, 0, 0);
}

OnlineWalkingModule::~OnlineWalkingModule()
{
  queue_thread_.join();
}

void OnlineWalkingModule::initialize(const int control_cycle_msec, robotis_framework::Robot *robot)
{
  queue_thread_ = boost::thread(boost::bind(&OnlineWalkingModule::queueThread, this));
  control_cycle_msec_ = control_cycle_msec;
  DIANAOnlineWalking* online_walking = DIANAOnlineWalking::getInstance();

  online_walking->initialize(control_cycle_msec*0.001);
  online_walking->start();
  online_walking->process();

  process_mutex_.lock();
  desired_matrix_g_to_pelvis_ = online_walking->mat_g_to_pelvis_;
  desired_matrix_g_to_rfoot_  = online_walking->mat_g_to_rfoot_;
  desired_matrix_g_to_lfoot_  = online_walking->mat_g_to_lfoot_;
  process_mutex_.unlock();

  result_["r_hip_pitch"  ]->goal_position_ = online_walking->out_angle_rad_[0];
  result_["r_hip_roll"   ]->goal_position_ = online_walking->out_angle_rad_[1];
  result_["r_hip_yaw"    ]->goal_position_ = online_walking->out_angle_rad_[2];
  result_["r_knee_pitch" ]->goal_position_  = online_walking->out_angle_rad_[3];
  result_["r_ankle_pitch"]->goal_position_  = online_walking->out_angle_rad_[4];
  result_["r_ankle_roll" ]->goal_position_  = online_walking->out_angle_rad_[5];

  result_["l_hip_pitch"  ]->goal_position_ = online_walking->out_angle_rad_[6];
  result_["l_hip_roll"   ]->goal_position_ = online_walking->out_angle_rad_[7];
  result_["l_hip_yaw"    ]->goal_position_ = online_walking->out_angle_rad_[8];
  result_["l_knee_pitch" ]->goal_position_ = online_walking->out_angle_rad_[9];
  result_["l_ankle_pitch"]->goal_position_ = online_walking->out_angle_rad_[10];
  result_["l_ankle_roll" ]->goal_position_ = online_walking->out_angle_rad_[11];



  previous_running_ = isRunning();

//  online_walking->hip_roll_feedforward_angle_rad_ = 0.0;
//
//  online_walking->balance_ctrl_.foot_roll_gyro_ctrl_.p_gain_ = 0;
//  online_walking->balance_ctrl_.foot_roll_gyro_ctrl_.d_gain_ = 0;
//  online_walking->balance_ctrl_.foot_pitch_gyro_ctrl_.p_gain_ = 0;
//  online_walking->balance_ctrl_.foot_pitch_gyro_ctrl_.d_gain_ = 0;
//
//  online_walking->balance_ctrl_.foot_roll_angle_ctrl_.p_gain_ = 0;
//  online_walking->balance_ctrl_.foot_roll_angle_ctrl_.d_gain_ = 0;
//  online_walking->balance_ctrl_.foot_pitch_angle_ctrl_.p_gain_ = 0;
//  online_walking->balance_ctrl_.foot_pitch_angle_ctrl_.d_gain_ = 0;
//
//  online_walking->balance_ctrl_.right_foot_force_x_ctrl_.p_gain_ = 0;
//  online_walking->balance_ctrl_.right_foot_force_x_ctrl_.d_gain_ = 0;
//  online_walking->balance_ctrl_.right_foot_force_y_ctrl_.p_gain_ = 0;
//  online_walking->balance_ctrl_.right_foot_force_y_ctrl_.d_gain_ = 0;
//  online_walking->balance_ctrl_.right_foot_force_z_ctrl_.p_gain_ = 0;
//  online_walking->balance_ctrl_.right_foot_force_z_ctrl_.d_gain_ = 0;
//  online_walking->balance_ctrl_.right_foot_torque_roll_ctrl_.p_gain_ = 0;
//  online_walking->balance_ctrl_.right_foot_torque_roll_ctrl_.d_gain_ = 0;
//  online_walking->balance_ctrl_.right_foot_torque_pitch_ctrl_.p_gain_ = 0;
//  online_walking->balance_ctrl_.right_foot_torque_pitch_ctrl_.d_gain_ = 0;
}


void OnlineWalkingModule::queueThread()
{
  ros::NodeHandle     ros_node;
  ros::CallbackQueue  callback_queue;

  ros_node.setCallbackQueue(&callback_queue);

  /* publish topics */
  robot_pose_pub_ = ros_node.advertise<diana_online_walking_module_msgs::RobotPose>("/heroehs/online_walking/robot_pose", 1);
  status_msg_pub_ = ros_node.advertise<robotis_controller_msgs::StatusMsg>("heroehs/status", 1);
  pelvis_base_msg_pub_ = ros_node.advertise<geometry_msgs::PoseStamped>("/heroehs/pelvis_pose_base_walking", 1);
  done_msg_pub_ = ros_node.advertise<std_msgs::String>("/heroehs/movement_done", 1);
  walking_joint_states_pub_ = ros_node.advertise<diana_online_walking_module_msgs::WalkingJointStatesStamped>("/robotis/walking/walking_joint_states", 1);


  /* ROS Service Callback Functions */
  ros::ServiceServer get_ref_step_data_server  = ros_node.advertiseService("/heroehs/online_walking/get_reference_step_data",   &OnlineWalkingModule::getReferenceStepDataServiceCallback,   this);
  ros::ServiceServer add_step_data_array_sever = ros_node.advertiseService("/heroehs/online_walking/add_step_data",             &OnlineWalkingModule::addStepDataServiceCallback,            this);
  ros::ServiceServer walking_start_server      = ros_node.advertiseService("/heroehs/online_walking/walking_start",             &OnlineWalkingModule::startWalkingServiceCallback,           this);
  ros::ServiceServer is_running_server         = ros_node.advertiseService("/heroehs/online_walking/is_running",                &OnlineWalkingModule::IsRunningServiceCallback,              this);
  //ros::ServiceServer set_balance_param_server  = ros_node.advertiseService("/heroehs/online_walking/set_balance_param",         &OnlineWalkingModule::setBalanceParamServiceCallback,        this);
  //ros::ServiceServer set_joint_feedback_gain   = ros_node.advertiseService("/heroehs/online_walking/joint_feedback_gain",       &OnlineWalkingModule::setJointFeedBackGainServiceCallback,   this);
  ros::ServiceServer remove_existing_step_data = ros_node.advertiseService("/heroehs/online_walking/remove_existing_step_data", &OnlineWalkingModule::removeExistingStepDataServiceCallback, this);

  /* sensor topic subscribe */
  //ros::Subscriber imu_data_sub      = ros_node.subscribe("/robotis/sensor/imu/imu",              3, &OnlineWalkingModule::imuDataOutputCallback,        this);

  ros::WallDuration duration(control_cycle_msec_ / 1000.0);
  if(ros::param::get("gazebo", gazebo_) == false)
    gazebo_ = false;

  while(ros_node.ok())
    callback_queue.callAvailable(duration);

//  while (ros_node.ok())
//  {
//    callback_queue.callAvailable();
//    usleep(1000);
//  }
}

void OnlineWalkingModule::publishStatusMsg(unsigned int type, std::string msg)
{
  robotis_controller_msgs::StatusMsg status_msg;
  status_msg.header.stamp = ros::Time::now();
  status_msg.type = type;
  status_msg.module_name = "Walking";
  status_msg.status_msg = msg;

  status_msg_pub_.publish(status_msg);
}

void OnlineWalkingModule::publishDoneMsg(std::string msg)
{
  std_msgs::String done_msg;
  done_msg.data = msg;

  done_msg_pub_.publish(done_msg);
}

int OnlineWalkingModule::convertStepDataMsgToStepData(diana_online_walking_module_msgs::StepData& src, robotis_framework::StepData& des)
{
  int copy_result = diana_online_walking_module_msgs::AddStepDataArray::Response::NO_ERROR;
  des.time_data.   walking_state        = src.time_data.walking_state;
  des.time_data.abs_step_time           = src.time_data.abs_step_time;
  des.time_data.dsp_ratio               = src.time_data.dsp_ratio;

  des.position_data.moving_foot         = src.position_data.moving_foot;
  des.position_data.shoulder_swing_gain = 0;
  des.position_data.elbow_swing_gain    = 0;
  des.position_data.foot_z_swap         = src.position_data.foot_z_swap;
  des.position_data.waist_pitch_angle   = 0;
  des.position_data.waist_yaw_angle     = src.position_data.torso_yaw_angle_rad;
  des.position_data.body_z_swap         = src.position_data.body_z_swap;

  des.position_data.body_pose.z          = src.position_data.body_pose.z;
  des.position_data.body_pose.roll       = src.position_data.body_pose.roll;
  des.position_data.body_pose.pitch      = src.position_data.body_pose.pitch;
  des.position_data.body_pose.yaw        = src.position_data.body_pose.yaw;
  des.position_data.right_foot_pose.x     = src.position_data.right_foot_pose.x;
  des.position_data.right_foot_pose.y     = src.position_data.right_foot_pose.y;
  des.position_data.right_foot_pose.z     = src.position_data.right_foot_pose.z;
  des.position_data.right_foot_pose.roll  = src.position_data.right_foot_pose.roll;
  des.position_data.right_foot_pose.pitch = src.position_data.right_foot_pose.pitch;
  des.position_data.right_foot_pose.yaw   = src.position_data.right_foot_pose.yaw;
  des.position_data.left_foot_pose.x      = src.position_data.left_foot_pose.x;
  des.position_data.left_foot_pose.y      = src.position_data.left_foot_pose.y;
  des.position_data.left_foot_pose.z      = src.position_data.left_foot_pose.z;
  des.position_data.left_foot_pose.roll   = src.position_data.left_foot_pose.roll;
  des.position_data.left_foot_pose.pitch  = src.position_data.left_foot_pose.pitch;
  des.position_data.left_foot_pose.yaw    = src.position_data.left_foot_pose.yaw;

  des.time_data.start_time_delay_ratio_x         = src.time_data.start_time_delay_ratio_x;
  des.time_data.start_time_delay_ratio_y         = src.time_data.start_time_delay_ratio_y;
  des.time_data.start_time_delay_ratio_z         = src.time_data.start_time_delay_ratio_z;
  des.time_data.start_time_delay_ratio_roll      = src.time_data.start_time_delay_ratio_roll;
  des.time_data.start_time_delay_ratio_pitch     = src.time_data.start_time_delay_ratio_pitch;
  des.time_data.start_time_delay_ratio_yaw       = src.time_data.start_time_delay_ratio_yaw;

  des.time_data.finish_time_advance_ratio_x     = src.time_data.finish_time_advance_ratio_x;
  des.time_data.finish_time_advance_ratio_y     = src.time_data.finish_time_advance_ratio_y;
  des.time_data.finish_time_advance_ratio_z     = src.time_data.finish_time_advance_ratio_z;
  des.time_data.finish_time_advance_ratio_roll  = src.time_data.finish_time_advance_ratio_roll;
  des.time_data.finish_time_advance_ratio_pitch = src.time_data.finish_time_advance_ratio_pitch;
  des.time_data.finish_time_advance_ratio_yaw   = src.time_data.finish_time_advance_ratio_yaw;

  if((src.time_data.walking_state != diana_online_walking_module_msgs::StepTimeData::IN_WALKING_STARTING)
      && (src.time_data.walking_state != diana_online_walking_module_msgs::StepTimeData::IN_WALKING)
      && (src.time_data.walking_state != diana_online_walking_module_msgs::StepTimeData::IN_WALKING_ENDING) )
    copy_result |= diana_online_walking_module_msgs::AddStepDataArray::Response::PROBLEM_IN_TIME_DATA;

  if((src.time_data.start_time_delay_ratio_x     < 0)
      || (src.time_data.start_time_delay_ratio_y     < 0)
      || (src.time_data.start_time_delay_ratio_z     < 0)
      || (src.time_data.start_time_delay_ratio_roll  < 0)
      || (src.time_data.start_time_delay_ratio_pitch < 0)
      || (src.time_data.start_time_delay_ratio_yaw   < 0) )
    copy_result |= diana_online_walking_module_msgs::AddStepDataArray::Response::PROBLEM_IN_TIME_DATA;

  if((src.time_data.finish_time_advance_ratio_x     < 0)
      || (src.time_data.finish_time_advance_ratio_y     < 0)
      || (src.time_data.finish_time_advance_ratio_z     < 0)
      || (src.time_data.finish_time_advance_ratio_roll  < 0)
      || (src.time_data.finish_time_advance_ratio_pitch < 0)
      || (src.time_data.finish_time_advance_ratio_yaw   < 0) )
    copy_result |= diana_online_walking_module_msgs::AddStepDataArray::Response::PROBLEM_IN_TIME_DATA;

  if(((src.time_data.start_time_delay_ratio_x + src.time_data.finish_time_advance_ratio_x) > 1.0)
      || ((src.time_data.start_time_delay_ratio_y      + src.time_data.finish_time_advance_ratio_y     ) > 1.0)
      || ((src.time_data.start_time_delay_ratio_z      + src.time_data.finish_time_advance_ratio_z     ) > 1.0)
      || ((src.time_data.start_time_delay_ratio_roll   + src.time_data.finish_time_advance_ratio_roll  ) > 1.0)
      || ((src.time_data.start_time_delay_ratio_pitch  + src.time_data.finish_time_advance_ratio_pitch ) > 1.0)
      || ((src.time_data.start_time_delay_ratio_yaw    + src.time_data.finish_time_advance_ratio_yaw   ) > 1.0) )
    copy_result |= diana_online_walking_module_msgs::AddStepDataArray::Response::PROBLEM_IN_TIME_DATA;

  if((src.position_data.moving_foot != diana_online_walking_module_msgs::StepPositionData::STANDING)
      && (src.position_data.moving_foot != diana_online_walking_module_msgs::StepPositionData::RIGHT_FOOT_SWING)
      && (src.position_data.moving_foot != diana_online_walking_module_msgs::StepPositionData::LEFT_FOOT_SWING))
    copy_result |= diana_online_walking_module_msgs::AddStepDataArray::Response::PROBLEM_IN_POSITION_DATA;

  if(src.position_data.foot_z_swap < 0)
    copy_result |= diana_online_walking_module_msgs::AddStepDataArray::Response::PROBLEM_IN_POSITION_DATA;

  return copy_result;
}


int OnlineWalkingModule::convertStepDataToStepDataMsg(robotis_framework::StepData& src, diana_online_walking_module_msgs::StepData& des)


{
  des.time_data.walking_state   = src.time_data.walking_state;
  des.time_data.abs_step_time   = src.time_data.abs_step_time;
  des.time_data.dsp_ratio       = src.time_data.dsp_ratio;

  des.time_data.start_time_delay_ratio_x     = des.time_data.finish_time_advance_ratio_x     = 0;
  des.time_data.start_time_delay_ratio_y     = des.time_data.finish_time_advance_ratio_y     = 0;
  des.time_data.start_time_delay_ratio_z     = des.time_data.finish_time_advance_ratio_z     = 0;
  des.time_data.start_time_delay_ratio_roll  = des.time_data.finish_time_advance_ratio_roll  = 0;
  des.time_data.start_time_delay_ratio_pitch = des.time_data.finish_time_advance_ratio_pitch = 0;
  des.time_data.start_time_delay_ratio_yaw   = des.time_data.finish_time_advance_ratio_yaw   = 0;

  des.position_data.moving_foot         = src.position_data.moving_foot;
  des.position_data.foot_z_swap         = src.position_data.foot_z_swap;
  des.position_data.torso_yaw_angle_rad = src.position_data.waist_yaw_angle;
  des.position_data.body_z_swap         = src.position_data.body_z_swap;

  des.position_data.body_pose.z           = src.position_data.body_pose.z;
  des.position_data.body_pose.roll        = src.position_data.body_pose.roll;
  des.position_data.body_pose.pitch       = src.position_data.body_pose.pitch;
  des.position_data.body_pose.yaw         = src.position_data.body_pose.yaw;
  des.position_data.right_foot_pose.x     = src.position_data.right_foot_pose.x;
  des.position_data.right_foot_pose.y     = src.position_data.right_foot_pose.y;
  des.position_data.right_foot_pose.z     = src.position_data.right_foot_pose.z;
  des.position_data.right_foot_pose.roll  = src.position_data.right_foot_pose.roll;
  des.position_data.right_foot_pose.pitch = src.position_data.right_foot_pose.pitch;
  des.position_data.right_foot_pose.yaw   = src.position_data.right_foot_pose.yaw;
  des.position_data.left_foot_pose.x      = src.position_data.left_foot_pose.x;
  des.position_data.left_foot_pose.y      = src.position_data.left_foot_pose.y;
  des.position_data.left_foot_pose.z      = src.position_data.left_foot_pose.z;
  des.position_data.left_foot_pose.roll   = src.position_data.left_foot_pose.roll;
  des.position_data.left_foot_pose.pitch  = src.position_data.left_foot_pose.pitch;
  des.position_data.left_foot_pose.yaw    = src.position_data.left_foot_pose.yaw;

  return 0;
}

bool OnlineWalkingModule::getReferenceStepDataServiceCallback(diana_online_walking_module_msgs::GetReferenceStepData::Request &req,
    diana_online_walking_module_msgs::GetReferenceStepData::Response &res)
{
  DIANAOnlineWalking *online_walking = DIANAOnlineWalking::getInstance();

  robotis_framework::StepData refStepData;

  online_walking->getReferenceStepDatafotAddition(&refStepData);

  convertStepDataToStepDataMsg(refStepData, res.reference_step_data);

  return true;
}

bool OnlineWalkingModule::addStepDataServiceCallback(diana_online_walking_module_msgs::AddStepDataArray::Request &req,
    diana_online_walking_module_msgs::AddStepDataArray::Response &res)
{
  DIANAOnlineWalking *online_walking = DIANAOnlineWalking::getInstance();
  res.result = diana_online_walking_module_msgs::AddStepDataArray::Response::NO_ERROR;

  if(enable_ == false)
  {
    res.result |= diana_online_walking_module_msgs::AddStepDataArray::Response::NOT_ENABLED_WALKING_MODULE;
    std::string status_msg = WalkingStatusMSG::FAILED_TO_ADD_STEP_DATA_MSG;
    publishStatusMsg(robotis_controller_msgs::StatusMsg::STATUS_ERROR, status_msg);
    return true;
  }

  if((req.step_data_array.size() > 100)
      && (req.remove_existing_step_data == true)
      && ((online_walking->isRunning() == true)))
  {
    res.result |= diana_online_walking_module_msgs::AddStepDataArray::Response::TOO_MANY_STEP_DATA;
    std::string status_msg  = WalkingStatusMSG::FAILED_TO_ADD_STEP_DATA_MSG;
    publishStatusMsg(robotis_controller_msgs::StatusMsg::STATUS_ERROR, status_msg);
    return true;
  }

  robotis_framework::StepData step_data, ref_step_data;
  std::vector<robotis_framework::StepData> req_step_data_array;

  online_walking->getReferenceStepDatafotAddition(&ref_step_data);

  for(int i = 0; i < req.step_data_array.size(); i++)
  {
    res.result |= convertStepDataMsgToStepData(req.step_data_array[i], step_data);

    if(step_data.time_data.abs_step_time <= 0)
    {
      res.result |= diana_online_walking_module_msgs::AddStepDataArray::Response::PROBLEM_IN_TIME_DATA;
    }

    if(i != 0)
    {
      if(step_data.time_data.abs_step_time <= req_step_data_array[req_step_data_array.size() - 1].time_data.abs_step_time)
      {
        res.result |= diana_online_walking_module_msgs::AddStepDataArray::Response::PROBLEM_IN_TIME_DATA;
      }
    }
    else
    {
      if(step_data.time_data.abs_step_time <= ref_step_data.time_data.abs_step_time)
      {
        res.result |= diana_online_walking_module_msgs::AddStepDataArray::Response::PROBLEM_IN_TIME_DATA;
      }
    }

    if(res.result != diana_online_walking_module_msgs::AddStepDataArray::Response::NO_ERROR)
    {
      std::string status_msg = WalkingStatusMSG::FAILED_TO_ADD_STEP_DATA_MSG;
      publishStatusMsg(robotis_controller_msgs::StatusMsg::STATUS_ERROR, status_msg);
      return true;
    }

    req_step_data_array.push_back(step_data);
  }

  if(req.remove_existing_step_data == true)
  {
    int exist_num_of_step_data = online_walking->getNumofRemainingUnreservedStepData();
    if(exist_num_of_step_data != 0)
      for(int remove_count  = 0; remove_count < exist_num_of_step_data; remove_count++)
        online_walking->eraseLastStepData();
  }
  else
  {
    if(online_walking->isRunning() == true)
    {
      res.result |= diana_online_walking_module_msgs::AddStepDataArray::Response::ROBOT_IS_WALKING_NOW;
      std::string status_msg  = WalkingStatusMSG::FAILED_TO_ADD_STEP_DATA_MSG;
      publishStatusMsg(robotis_controller_msgs::StatusMsg::STATUS_ERROR, status_msg);
      return true;
    }
  }

//  if(checkBalanceOnOff() == false)
//  {
//    std::string status_msg  = WalkingStatusMSG::BALANCE_HAS_BEEN_TURNED_OFF;
//    publishStatusMsg(robotis_controller_msgs::StatusMsg::STATUS_ERROR, status_msg);
//    return true;
//  }

  for(unsigned int i = 0; i < req_step_data_array.size() ; i++)
    online_walking->addStepData(req_step_data_array[i]);

  if( req.auto_start == true)
  {
    online_walking->start();
  }

  return true;
}

bool OnlineWalkingModule::startWalkingServiceCallback(diana_online_walking_module_msgs::StartWalking::Request &req,
    diana_online_walking_module_msgs::StartWalking::Response &res)
{
  DIANAOnlineWalking *prev_walking = DIANAOnlineWalking::getInstance();
  res.result = diana_online_walking_module_msgs::StartWalking::Response::NO_ERROR;

  if(enable_ == false)
  {
    res.result |= diana_online_walking_module_msgs::StartWalking::Response::NOT_ENABLED_WALKING_MODULE;
    return true;
  }

  if(prev_walking->isRunning() == true)
  {
    res.result |= diana_online_walking_module_msgs::StartWalking::Response::ROBOT_IS_WALKING_NOW;
    return true;
  }

//  if(checkBalanceOnOff() == false)
//  {
//    std::string status_msg  = WalkingStatusMSG::BALANCE_HAS_BEEN_TURNED_OFF;
//    publishStatusMsg(robotis_controller_msgs::StatusMsg::STATUS_ERROR, status_msg);
//    return true;
//  }

  if(prev_walking->getNumofRemainingUnreservedStepData() == 0)
  {
    res.result |= diana_online_walking_module_msgs::StartWalking::Response::NO_STEP_DATA;
    return true;
  }

  if(res.result == diana_online_walking_module_msgs::StartWalking::Response::NO_ERROR)
  {
    prev_walking->start();
  }

  return true;
}

bool OnlineWalkingModule::IsRunningServiceCallback(diana_online_walking_module_msgs::IsRunning::Request &req,
    diana_online_walking_module_msgs::IsRunning::Response &res)
{
  bool is_running = isRunning();
  res.is_running = is_running;

  return true;
}

bool OnlineWalkingModule::isRunning()
{
  return DIANAOnlineWalking::getInstance()->isRunning();
}

bool OnlineWalkingModule::removeExistingStepDataServiceCallback(diana_online_walking_module_msgs::RemoveExistingStepData::Request  &req,
    diana_online_walking_module_msgs::RemoveExistingStepData::Response &res)
{
  DIANAOnlineWalking *online_walking = DIANAOnlineWalking::getInstance();

  res.result = diana_online_walking_module_msgs::RemoveExistingStepData::Response::NO_ERROR;

  if(isRunning())
  {
    res.result |= diana_online_walking_module_msgs::RemoveExistingStepData::Response::ROBOT_IS_WALKING_NOW;
  }
  else
  {
    int exist_num_of_step_data = online_walking->getNumofRemainingUnreservedStepData();
    if(exist_num_of_step_data != 0)
      for(int remove_count  = 0; remove_count < exist_num_of_step_data; remove_count++)
        online_walking->eraseLastStepData();
  }
  return true;
}

void OnlineWalkingModule::onModuleEnable()
{
  std::string status_msg = WalkingStatusMSG::WALKING_MODULE_IS_ENABLED_MSG;
  publishStatusMsg(robotis_controller_msgs::StatusMsg::STATUS_INFO, status_msg);
}

void OnlineWalkingModule::onModuleDisable()
{
  previous_running_ = present_running = false;

  DIANAOnlineWalking *online_walking = DIANAOnlineWalking::getInstance();
  std::string status_msg = WalkingStatusMSG::WALKING_MODULE_IS_DISABLED_MSG;
  balance_update_with_loop_ = false;


  online_walking->leg_angle_feed_back_[0].p_gain_ = 0;
  online_walking->leg_angle_feed_back_[0].d_gain_ = 0;
  online_walking->leg_angle_feed_back_[1].p_gain_ = 0;
  online_walking->leg_angle_feed_back_[1].d_gain_ = 0;
  online_walking->leg_angle_feed_back_[2].p_gain_ = 0;
  online_walking->leg_angle_feed_back_[2].d_gain_ = 0;
  online_walking->leg_angle_feed_back_[3].p_gain_ = 0;
  online_walking->leg_angle_feed_back_[3].d_gain_ = 0;
  online_walking->leg_angle_feed_back_[4].p_gain_ = 0;
  online_walking->leg_angle_feed_back_[4].d_gain_ = 0;
  online_walking->leg_angle_feed_back_[5].p_gain_ = 0;
  online_walking->leg_angle_feed_back_[5].d_gain_ = 0;

  online_walking->leg_angle_feed_back_[6].p_gain_ = 0;
  online_walking->leg_angle_feed_back_[6].d_gain_ = 0;
  online_walking->leg_angle_feed_back_[7].p_gain_ = 0;
  online_walking->leg_angle_feed_back_[7].d_gain_ = 0;
  online_walking->leg_angle_feed_back_[8].p_gain_ = 0;
  online_walking->leg_angle_feed_back_[8].d_gain_ = 0;
  online_walking->leg_angle_feed_back_[9].p_gain_ = 0;
  online_walking->leg_angle_feed_back_[9].d_gain_ = 0;
  online_walking->leg_angle_feed_back_[10].p_gain_ = 0;
  online_walking->leg_angle_feed_back_[10].d_gain_ = 0;
  online_walking->leg_angle_feed_back_[11].p_gain_ = 0;
  online_walking->leg_angle_feed_back_[11].d_gain_ = 0;

  online_walking->balance_ctrl_.foot_roll_gyro_ctrl_.p_gain_           = 0;
  online_walking->balance_ctrl_.foot_roll_gyro_ctrl_.d_gain_           = 0;
  online_walking->balance_ctrl_.foot_pitch_gyro_ctrl_.p_gain_          = 0;
  online_walking->balance_ctrl_.foot_pitch_gyro_ctrl_.d_gain_          = 0;
  online_walking->balance_ctrl_.foot_roll_angle_ctrl_.p_gain_          = 0;
  online_walking->balance_ctrl_.foot_roll_angle_ctrl_.d_gain_          = 0;
  online_walking->balance_ctrl_.foot_pitch_angle_ctrl_.p_gain_         = 0;
  online_walking->balance_ctrl_.foot_pitch_angle_ctrl_.d_gain_         = 0;
  online_walking->balance_ctrl_.right_foot_force_x_ctrl_.p_gain_       = 0;
  online_walking->balance_ctrl_.right_foot_force_y_ctrl_.p_gain_       = 0;
  online_walking->balance_ctrl_.right_foot_force_z_ctrl_.p_gain_       = 0;
  online_walking->balance_ctrl_.right_foot_torque_roll_ctrl_.p_gain_   = 0;
  online_walking->balance_ctrl_.right_foot_torque_pitch_ctrl_.p_gain_  = 0;
  online_walking->balance_ctrl_.right_foot_force_x_ctrl_.d_gain_       = 0;
  online_walking->balance_ctrl_.right_foot_force_y_ctrl_.d_gain_       = 0;
  online_walking->balance_ctrl_.right_foot_force_z_ctrl_.d_gain_       = 0;
  online_walking->balance_ctrl_.right_foot_torque_roll_ctrl_.d_gain_   = 0;
  online_walking->balance_ctrl_.right_foot_torque_pitch_ctrl_.d_gain_  = 0;
  online_walking->balance_ctrl_.left_foot_force_x_ctrl_.p_gain_        = 0;
  online_walking->balance_ctrl_.left_foot_force_y_ctrl_.p_gain_        = 0;
  online_walking->balance_ctrl_.left_foot_force_z_ctrl_.p_gain_        = 0;
  online_walking->balance_ctrl_.left_foot_torque_roll_ctrl_.p_gain_    = 0;
  online_walking->balance_ctrl_.left_foot_torque_pitch_ctrl_.p_gain_   = 0;
  online_walking->balance_ctrl_.left_foot_force_x_ctrl_.d_gain_        = 0;
  online_walking->balance_ctrl_.left_foot_force_y_ctrl_.d_gain_        = 0;
  online_walking->balance_ctrl_.left_foot_force_z_ctrl_.d_gain_        = 0;
  online_walking->balance_ctrl_.left_foot_torque_roll_ctrl_.d_gain_    = 0;
  online_walking->balance_ctrl_.left_foot_torque_pitch_ctrl_.d_gain_   = 0;

  publishStatusMsg(robotis_controller_msgs::StatusMsg::STATUS_INFO, status_msg);
}

void OnlineWalkingModule::process(std::map<std::string, robotis_framework::Dynamixel *> dxls, std::map<std::string, double> sensors)
{
  if(enable_ == false)
    return;

  DIANAOnlineWalking *online_walking = DIANAOnlineWalking::getInstance();

//  r_foot_fx_N_  = sensors["r_foot_fx_scaled_N"];
//  r_foot_fy_N_  = sensors["r_foot_fy_scaled_N"];
//  r_foot_fz_N_  = sensors["r_foot_fz_scaled_N"];
//  r_foot_Tx_Nm_ = sensors["r_foot_tx_scaled_Nm"];
//  r_foot_Ty_Nm_ = sensors["r_foot_ty_scaled_Nm"];
//  r_foot_Tz_Nm_ = sensors["r_foot_tz_scaled_Nm"];
//
//  l_foot_fx_N_  = sensors["l_foot_fx_scaled_N"];
//  l_foot_fy_N_  = sensors["l_foot_fy_scaled_N"];
//  l_foot_fz_N_  = sensors["l_foot_fz_scaled_N"];
//  l_foot_Tx_Nm_ = sensors["l_foot_tx_scaled_Nm"];
//  l_foot_Ty_Nm_ = sensors["l_foot_ty_scaled_Nm"];
//  l_foot_Tz_Nm_ = sensors["l_foot_tz_scaled_Nm"];
//
//
//  r_foot_fx_N_ = robotis_framework::sign(r_foot_fx_N_) * fmin( fabs(r_foot_fx_N_), 2000.0);
//  r_foot_fy_N_ = robotis_framework::sign(r_foot_fy_N_) * fmin( fabs(r_foot_fy_N_), 2000.0);
//  r_foot_fz_N_ = robotis_framework::sign(r_foot_fz_N_) * fmin( fabs(r_foot_fz_N_), 2000.0);
//  r_foot_Tx_Nm_ = robotis_framework::sign(r_foot_Tx_Nm_) *fmin(fabs(r_foot_Tx_Nm_), 300.0);
//  r_foot_Ty_Nm_ = robotis_framework::sign(r_foot_Ty_Nm_) *fmin(fabs(r_foot_Ty_Nm_), 300.0);
//  r_foot_Tz_Nm_ = robotis_framework::sign(r_foot_Tz_Nm_) *fmin(fabs(r_foot_Tz_Nm_), 300.0);
//
//  l_foot_fx_N_ = robotis_framework::sign(l_foot_fx_N_) * fmin( fabs(l_foot_fx_N_), 2000.0);
//  l_foot_fy_N_ = robotis_framework::sign(l_foot_fy_N_) * fmin( fabs(l_foot_fy_N_), 2000.0);
//  l_foot_fz_N_ = robotis_framework::sign(l_foot_fz_N_) * fmin( fabs(l_foot_fz_N_), 2000.0);
//  l_foot_Tx_Nm_ = robotis_framework::sign(l_foot_Tx_Nm_) *fmin(fabs(l_foot_Tx_Nm_), 300.0);
//  l_foot_Ty_Nm_ = robotis_framework::sign(l_foot_Ty_Nm_) *fmin(fabs(l_foot_Ty_Nm_), 300.0);
//  l_foot_Tz_Nm_ = robotis_framework::sign(l_foot_Tz_Nm_) *fmin(fabs(l_foot_Tz_Nm_), 300.0);


//  if(balance_update_with_loop_ == true)
//  {
//    balance_update_sys_time_ += control_cycle_msec_ * 0.001;
//    if(balance_update_sys_time_ >= balance_update_duration_ )
//    {
//      balance_update_sys_time_ = balance_update_duration_;
//      balance_update_with_loop_ = false;
//      setBalanceParam(desired_balance_param_);
//      std::string status_msg = WalkingStatusMSG::BALANCE_PARAM_SETTING_FINISHED_MSG;
//      publishStatusMsg(robotis_controller_msgs::StatusMsg::STATUS_INFO, status_msg);
//      publishDoneMsg("walking_balance");
//    }
//    else
//    {
//      updateBalanceParam();
//    }
//  }
//
//  if(joint_feedback_update_with_loop_ == true)
//  {
//    joint_feedback_update_sys_time_ += control_cycle_msec_ * 0.001;
//    if(joint_feedback_update_sys_time_ >= joint_feedback_update_duration_ )
//    {
//      joint_feedback_update_sys_time_ = joint_feedback_update_duration_;
//      joint_feedback_update_with_loop_ = false;
//      setJointFeedBackGain(desired_joint_feedback_gain_);
//      publishStatusMsg(robotis_controller_msgs::StatusMsg::STATUS_INFO, WalkingStatusMSG::JOINT_FEEDBACK_GAIN_UPDATE_FINISHED_MSG);
//      publishDoneMsg("walking_joint_feedback");
//    }
//    else
//    {
//      updateJointFeedBackGain();
//    }
//  }

//  online_walking->current_right_fx_N_  = r_foot_fx_N_;
//  online_walking->current_right_fy_N_  = r_foot_fy_N_;
//  online_walking->current_right_fz_N_  = r_foot_fz_N_;
//  online_walking->current_right_tx_Nm_ = r_foot_Tx_Nm_;
//  online_walking->current_right_ty_Nm_ = r_foot_Ty_Nm_;
//  online_walking->current_right_tz_Nm_ = r_foot_Tz_Nm_;
//
//  online_walking->current_left_fx_N_  = l_foot_fx_N_;
//  online_walking->current_left_fy_N_  = l_foot_fy_N_;
//  online_walking->current_left_fz_N_  = l_foot_fz_N_;
//  online_walking->current_left_tx_Nm_ = l_foot_Tx_Nm_;
//  online_walking->current_left_ty_Nm_ = l_foot_Ty_Nm_;
//  online_walking->current_left_tz_Nm_ = l_foot_Tz_Nm_;

//  online_walking->curr_angle_rad_[0]  = result_["r_leg_hip_y"]->goal_position_;
//  online_walking->curr_angle_rad_[1]  = result_["r_leg_hip_r"]->goal_position_;
//  online_walking->curr_angle_rad_[2]  = result_["r_leg_hip_p"]->goal_position_;
//  online_walking->curr_angle_rad_[3]  = result_["r_leg_kn_p" ]->goal_position_;
//  online_walking->curr_angle_rad_[4]  = result_["r_leg_an_p" ]->goal_position_;
//  online_walking->curr_angle_rad_[5]  = result_["r_leg_an_r" ]->goal_position_;
//
//  online_walking->curr_angle_rad_[6]  = result_["l_leg_hip_y"]->goal_position_;
//  online_walking->curr_angle_rad_[7]  = result_["l_leg_hip_r"]->goal_position_;
//  online_walking->curr_angle_rad_[8]  = result_["l_leg_hip_p"]->goal_position_;
//  online_walking->curr_angle_rad_[9]  = result_["l_leg_kn_p" ]->goal_position_;
//  online_walking->curr_angle_rad_[10] = result_["l_leg_an_p" ]->goal_position_;
//  online_walking->curr_angle_rad_[11] = result_["l_leg_an_r" ]->goal_position_;
//
//  for(std::map<std::string, robotis_framework::DynamixelState*>::iterator result_it = result_.begin();
//      result_it != result_.end();
//      result_it++)
//  {
//    std::map<std::string, robotis_framework::Dynamixel*>::iterator dxls_it = dxls.find(result_it->first);
//    if(dxls_it != dxls.end())
//      online_walking->curr_angle_rad_[joint_name_to_index_[result_it->first]] = dxls_it->second->dxl_state_->present_position_;
//  }

  process_mutex_.lock();
  online_walking->process();

  desired_matrix_g_to_pelvis_   = online_walking->mat_g_to_pelvis_;
  desired_matrix_g_to_rfoot_ = online_walking->mat_g_to_rfoot_;
  desired_matrix_g_to_lfoot_ = online_walking->mat_g_to_lfoot_;
  process_mutex_.unlock();

  //publishRobotPose();

  result_["r_hip_pitch"  ]->goal_position_ = online_walking->out_angle_rad_[0];
  result_["r_hip_roll"   ]->goal_position_ = online_walking->out_angle_rad_[1];
  result_["r_hip_yaw"    ]->goal_position_ = online_walking->out_angle_rad_[2];
  result_["r_knee_pitch" ]->goal_position_  = online_walking->out_angle_rad_[3];
  result_["r_ankle_pitch"]->goal_position_  = online_walking->out_angle_rad_[4];
  result_["r_ankle_roll" ]->goal_position_  = online_walking->out_angle_rad_[5];

  result_["l_hip_pitch"  ]->goal_position_ = online_walking->out_angle_rad_[6];
  result_["l_hip_roll"   ]->goal_position_ = online_walking->out_angle_rad_[7];
  result_["l_hip_yaw"    ]->goal_position_ = online_walking->out_angle_rad_[8];
  result_["l_knee_pitch" ]->goal_position_ = online_walking->out_angle_rad_[9];
  result_["l_ankle_pitch"]->goal_position_ = online_walking->out_angle_rad_[10];
  result_["l_ankle_roll" ]->goal_position_ = online_walking->out_angle_rad_[11];

//  walking_joint_states_msg_.header.stamp = ros::Time::now();
//  walking_joint_states_msg_.r_goal_hip_y = online_walking->r_leg_out_angle_rad_[0];
//  walking_joint_states_msg_.r_goal_hip_r = online_walking->r_leg_out_angle_rad_[1];
//  walking_joint_states_msg_.r_goal_hip_p = online_walking->r_leg_out_angle_rad_[2];
//  walking_joint_states_msg_.r_goal_kn_p  = online_walking->r_leg_out_angle_rad_[3];
//  walking_joint_states_msg_.r_goal_an_p  = online_walking->r_leg_out_angle_rad_[4];
//  walking_joint_states_msg_.r_goal_an_r  = online_walking->r_leg_out_angle_rad_[5];
//  walking_joint_states_msg_.l_goal_hip_y = online_walking->l_leg_out_angle_rad_[0];
//  walking_joint_states_msg_.l_goal_hip_r = online_walking->l_leg_out_angle_rad_[1];
//  walking_joint_states_msg_.l_goal_hip_p = online_walking->l_leg_out_angle_rad_[2];
//  walking_joint_states_msg_.l_goal_kn_p  = online_walking->l_leg_out_angle_rad_[3];
//  walking_joint_states_msg_.l_goal_an_p  = online_walking->l_leg_out_angle_rad_[4];
//  walking_joint_states_msg_.l_goal_an_r  = online_walking->l_leg_out_angle_rad_[5];
//
//  walking_joint_states_msg_.r_present_hip_y = online_walking->curr_angle_rad_[0];
//  walking_joint_states_msg_.r_present_hip_r = online_walking->curr_angle_rad_[1];
//  walking_joint_states_msg_.r_present_hip_p = online_walking->curr_angle_rad_[2];
//  walking_joint_states_msg_.r_present_kn_p  = online_walking->curr_angle_rad_[3];
//  walking_joint_states_msg_.r_present_an_p  = online_walking->curr_angle_rad_[4];
//  walking_joint_states_msg_.r_present_an_r  = online_walking->curr_angle_rad_[5];
//  walking_joint_states_msg_.l_present_hip_y = online_walking->curr_angle_rad_[6];
//  walking_joint_states_msg_.l_present_hip_r = online_walking->curr_angle_rad_[7];
//  walking_joint_states_msg_.l_present_hip_p = online_walking->curr_angle_rad_[8];
//  walking_joint_states_msg_.l_present_kn_p  = online_walking->curr_angle_rad_[9];
//  walking_joint_states_msg_.l_present_an_p  = online_walking->curr_angle_rad_[10];
//  walking_joint_states_msg_.l_present_an_r  = online_walking->curr_angle_rad_[11];
//  walking_joint_states_pub_.publish(walking_joint_states_msg_);


  present_running = isRunning();
  if(previous_running_ != present_running)
  {
    if(present_running == true)
    {
      std::string status_msg = WalkingStatusMSG::WALKING_START_MSG;
      publishStatusMsg(robotis_controller_msgs::StatusMsg::STATUS_INFO, status_msg);
    }
    else
    {
      std::string status_msg = WalkingStatusMSG::WALKING_FINISH_MSG;
      publishStatusMsg(robotis_controller_msgs::StatusMsg::STATUS_INFO, status_msg);
      publishDoneMsg("walking_completed");
    }
  }
  previous_running_ = present_running;
}

void OnlineWalkingModule::stop()
{
  return;
}

