/*
 * diana_online_walking.cpp
 *
 *  Created on: 2018. 2. 27.
 *      Author: Crowban
 */

#include "diana_online_walking_module/diana_online_walking.h"

using namespace diana;


DIANAOnlineWalking::DIANAOnlineWalking()
{
  mat_pelvis_to_rhip_ = robotis_framework::getTransformationXYZRPY(0, -0.105, 0, 0, 0, 0);
  mat_pelvis_to_lhip_ = robotis_framework::getTransformationXYZRPY(0,  0.105, 0, 0, 0, 0);

  mat_rhip_to_pelvis_ = robotis_framework::getTransformationXYZRPY(0,  0.105, 0, 0, 0, 0);
  mat_lhip_to_pelvis_ = robotis_framework::getTransformationXYZRPY(0, -0.105, 0, 0, 0, 0);

  balance_error_ = heroehs::BalanceControlError::NoError;

  mat_imu_frame_ref_ = robotis_framework::getRotationX(M_PI) * robotis_framework::getRotationZ(-0.5*M_PI);
  mat_imu_frame_ref_inv_ = mat_imu_frame_ref_.transpose();

  right_dsp_fz_N_ = -0.5*30.0 * 9.8;
  left_dsp_fz_N_  = -0.5*30.0 * 9.8;
  right_ssp_fz_N_ = -30.0 * 9.8;
  left_ssp_fz_N_  = -30.0 * 9.8;
}

DIANAOnlineWalking::~DIANAOnlineWalking()
{

}

void DIANAOnlineWalking::initialize(double control_cycle_sec)
{
  diana_kd_ = new DIANAKinematicsDynamics(WholeBody);

  robotis_framework::Pose3D r_foot, l_foot, pelvis;
  r_foot.x = 0.0;    r_foot.y = -0.12;  r_foot.z = -0.55;
  r_foot.roll = 0.0; r_foot.pitch = 0.0; r_foot.yaw = 0.0;

  l_foot.x = 0.0;    l_foot.y = 0.12;   l_foot.z = -0.55;
  l_foot.roll = 0.0; l_foot.pitch = 0.0; l_foot.yaw = 0.0;

  pelvis.x = 0.0;    pelvis.y = 0.0;     pelvis.z = 0.0;
  pelvis.roll = 0.0; pelvis.pitch = 0.0; pelvis.yaw = 0;

  walking_pattern_.setInitialPose(r_foot, l_foot, pelvis);
  walking_pattern_.initialize(0.7, 1.6, control_cycle_sec);

  // initialize balance
  balance_ctrl_.initialize(control_cycle_sec);
  balance_ctrl_.setGyroBalanceEnable(true);
  balance_ctrl_.setOrientationBalanceEnable(true);
  balance_ctrl_.setForceTorqueBalanceEnable(true);

  mat_right_force_.resize(4,1);  mat_left_force_.resize(4,1);
  mat_right_torque_.resize(4,1); mat_left_torque_.resize(4,1);

  mat_right_force_.fill(0);  mat_left_force_.fill(0);
  mat_right_torque_.fill(0); mat_left_torque_.fill(0);

  for(int feed_forward_idx = 0; feed_forward_idx < 12; feed_forward_idx++)
  {
    leg_angle_feed_back_[feed_forward_idx].p_gain_ = 0;
    leg_angle_feed_back_[feed_forward_idx].d_gain_ = 0;
  }
}

void DIANAOnlineWalking::start()
{
  walking_pattern_.start();
}

void DIANAOnlineWalking::addStepData(robotis_framework::StepData& step_data)
{
  walking_pattern_.addStepData(step_data);
}

void DIANAOnlineWalking::eraseLastStepData()
{
  walking_pattern_.eraseLastStepData();
}

int  DIANAOnlineWalking::getNumofRemainingUnreservedStepData()
{
  return walking_pattern_.getNumofRemainingUnreservedStepData();
}

void DIANAOnlineWalking::getReferenceStepDatafotAddition(robotis_framework::StepData *ref_step_data_for_addition)
{
  walking_pattern_.getReferenceStepDatafotAddition(ref_step_data_for_addition);
}

void DIANAOnlineWalking::process()
{
  walking_pattern_.process();
  mat_g_to_pelvis_ = walking_pattern_.mat_g_to_pelvis_;
  mat_pelvis_to_g_ = robotis_framework::getInverseTransformation(mat_g_to_pelvis_);
  mat_g_to_rfoot_ = walking_pattern_.mat_g_to_rfoot_;
  mat_g_to_lfoot_ = walking_pattern_.mat_g_to_lfoot_;
  mat_robot_to_pelvis_ = walking_pattern_.mat_robot_to_pelvis_;
  mat_robot_to_rfoot_ = (mat_robot_to_pelvis_*mat_pelvis_to_g_)*mat_g_to_rfoot_;
  mat_robot_to_lfoot_ = (mat_robot_to_pelvis_*mat_pelvis_to_g_)*mat_g_to_lfoot_;

  //balance
  imu_data_mutex_lock_.lock();
  balance_ctrl_.setCurrentGyroSensorOutput(current_gyro_roll_rad_per_sec_, current_gyro_pitch_rad_per_sec_);
  balance_ctrl_.setCurrentOrientationSensorOutput(current_imu_roll_rad_, current_imu_pitch_rad_);
  imu_data_mutex_lock_.unlock();
  ft_data_mutex_lock_.lock();
  balance_ctrl_.setCurrentFootForceTorqueSensorOutput(mat_right_force_.coeff(0,0),  mat_right_force_.coeff(1,0),  mat_right_force_.coeff(2,0),
                                                      mat_right_torque_.coeff(0,0), mat_right_torque_.coeff(1,0), mat_right_torque_.coeff(2,0),
                                                      mat_left_force_.coeff(0,0),   mat_left_force_.coeff(1,0),   mat_left_force_.coeff(2,0),
                                                      mat_left_torque_.coeff(0,0),  mat_left_torque_.coeff(1,0),  mat_left_torque_.coeff(2,0));
  ft_data_mutex_lock_.unlock();

  balance_index_ = walking_pattern_.current_balancing_index_;
  double r_target_fx_N = 0;
  double l_target_fx_N = 0;
  double r_target_fy_N = 0;
  double l_target_fy_N = 0;
  double r_target_fz_N = right_dsp_fz_N_;
  double l_target_fz_N = left_dsp_fz_N_;
  double target_fz_N  = 0;

  switch(balance_index_)
   {
   case 0:
     //fprintf(stderr, "DSP : START\n");
     r_target_fx_N = l_target_fx_N = 0;
     r_target_fy_N = l_target_fy_N = 0;
     r_target_fz_N = right_dsp_fz_N_;
     l_target_fz_N = left_dsp_fz_N_;
     target_fz_N = left_dsp_fz_N_ - right_dsp_fz_N_;
     break;
   case 1:
     //fprintf(stderr, "DSP : R--O->L\n");
     r_target_fx_N = l_target_fx_N = 0;//-0.5*total_mass_of_robot_*mat_robot_to_acc.coeff(0,0);
     r_target_fy_N = l_target_fy_N = 0;//-0.5*total_mass_of_robot_*mat_robot_to_acc.coeff(1,0);
     r_target_fz_N = right_dsp_fz_N_;
     l_target_fz_N = left_dsp_fz_N_;
     target_fz_N = left_dsp_fz_N_ - right_dsp_fz_N_;
     break;
   case 2:
     //fprintf(stderr, "SSP : L_BALANCING1\n");
     r_target_fx_N = 0;
     r_target_fy_N = 0;
     r_target_fz_N = 0;

     l_target_fx_N = 0;//-1.0*total_mass_of_robot_*mat_robot_to_acc.coeff(0,0);
     l_target_fy_N = 0;//-1.0*total_mass_of_robot_*mat_robot_to_acc.coeff(1,0);
     l_target_fz_N = left_ssp_fz_N_;
     target_fz_N = left_ssp_fz_N_;
     break;
   case 3:
     //fprintf(stderr, "SSP : L_BALANCING2\n");
     r_target_fx_N = 0;
     r_target_fy_N = 0;
     r_target_fz_N = 0;

     l_target_fx_N = 0;//-1.0*total_mass_of_robot_*mat_robot_to_acc.coeff(0,0);
     l_target_fy_N = 0;//-1.0*total_mass_of_robot_*mat_robot_to_acc.coeff(1,0);
     l_target_fz_N = left_ssp_fz_N_;
     target_fz_N = left_ssp_fz_N_;
     break;
   case 4:
     //fprintf(stderr, "DSP : R--O<-L\n");
     r_target_fx_N = l_target_fx_N = 0;//-0.5*total_mass_of_robot_*mat_robot_to_acc.coeff(0,0);
     r_target_fy_N = l_target_fy_N = 0;//-0.5*total_mass_of_robot_*mat_robot_to_acc.coeff(1,0);
     r_target_fz_N = right_dsp_fz_N_;
     l_target_fz_N = left_dsp_fz_N_;
     target_fz_N = left_dsp_fz_N_ - right_dsp_fz_N_;
     break;
   case 5:
     //fprintf(stderr, "DSP : R<-O--L\n");
     r_target_fx_N = l_target_fx_N = 0;//-0.5*total_mass_of_robot_*mat_robot_to_acc.coeff(0,0);
     r_target_fy_N = l_target_fy_N = 0;//-0.5*total_mass_of_robot_*mat_robot_to_acc.coeff(1,0);
     r_target_fz_N = right_dsp_fz_N_;
     l_target_fz_N = left_dsp_fz_N_;
     target_fz_N = left_dsp_fz_N_ - right_dsp_fz_N_;
     break;
   case 6:
     //fprintf(stderr, "SSP : R_BALANCING1\n");
     r_target_fx_N = 0;//-1.0*total_mass_of_robot_*mat_robot_to_acc.coeff(0,0);
     r_target_fy_N = 0;//-1.0*total_mass_of_robot_*mat_robot_to_acc.coeff(1,0);
     r_target_fz_N = right_ssp_fz_N_;

     l_target_fx_N = 0;
     l_target_fy_N = 0;
     l_target_fz_N = 0;
     target_fz_N = -right_ssp_fz_N_;
     break;
   case 7:
     //fprintf(stderr, "SSP : R_BALANCING2\n");
     r_target_fx_N = 0;//-1.0*total_mass_of_robot_*mat_robot_to_acc.coeff(0,0);
     r_target_fy_N = 0;//-1.0*total_mass_of_robot_*mat_robot_to_acc.coeff(1,0);
     r_target_fz_N = right_ssp_fz_N_;

     l_target_fx_N = 0;
     l_target_fy_N = 0;
     l_target_fz_N = 0;
     target_fz_N =  -right_ssp_fz_N_;
     break;
   case 8:
     //fprintf(stderr, "DSP : R->O--L");
     r_target_fx_N = l_target_fx_N = 0;//-0.5*total_mass_of_robot_*mat_robot_to_acc.coeff(0,0);
     r_target_fy_N = l_target_fy_N = 0;//-0.5*total_mass_of_robot_*mat_robot_to_acc.coeff(1,0);
     r_target_fz_N = right_dsp_fz_N_;
     l_target_fz_N = left_dsp_fz_N_;
     target_fz_N = left_dsp_fz_N_ - right_dsp_fz_N_;
     break;
   case 9:
     //fprintf(stderr, "DSP : END");
     r_target_fx_N = l_target_fx_N = 0;//-0.5*total_mass_of_robot_*mat_robot_to_acc.coeff(0,0);
     r_target_fy_N = l_target_fy_N = 0;//-0.5*total_mass_of_robot_*mat_robot_to_acc.coeff(1,0);
     r_target_fz_N = right_dsp_fz_N_;
     l_target_fz_N = left_dsp_fz_N_;
     target_fz_N = left_dsp_fz_N_ - right_dsp_fz_N_;
     break;
   default:
     break;
   }

  balance_ctrl_.setDesiredCOBGyro(0,0);
  balance_ctrl_.setDesiredCOBOrientation(walking_pattern_.pose_g_to_pelvis_.roll, walking_pattern_.pose_g_to_pelvis_.pitch);
  balance_ctrl_.setDesiredFootForceTorque(r_target_fx_N*1.0, r_target_fy_N*1.0, r_target_fz_N, 0, 0, 0,
                                          l_target_fx_N*1.0, l_target_fy_N*1.0, l_target_fz_N, 0, 0, 0);
  balance_ctrl_.setDesiredPose(mat_robot_to_pelvis_, mat_robot_to_rfoot_, mat_robot_to_lfoot_);

  balance_ctrl_.process(&balance_error_, &mat_robot_to_pelvis_modified_, &mat_robot_to_rf_modified_, &mat_robot_to_lf_modified_);
  mat_pelvis_to_robot_modified_ = robotis_framework::getInverseTransformation(mat_robot_to_pelvis_modified_);


//  rhip_to_rfoot_pose_ = robotis_framework::getPose3DfromTransformMatrix((mat_rhip_to_pelvis_) * mat_pelvis_to_g_*mat_g_to_rfoot_);
//  lhip_to_lfoot_pose_ = robotis_framework::getPose3DfromTransformMatrix((mat_lhip_to_pelvis_) * mat_pelvis_to_g_*mat_g_to_lfoot_);

  rhip_to_rfoot_pose_ = robotis_framework::getPose3DfromTransformMatrix((mat_rhip_to_pelvis_ * mat_pelvis_to_robot_modified_) * mat_robot_to_rf_modified_);
  lhip_to_lfoot_pose_ = robotis_framework::getPose3DfromTransformMatrix((mat_lhip_to_pelvis_ * mat_pelvis_to_robot_modified_) * mat_robot_to_lf_modified_);


//  diana_kd_->calcInverseKinematicsForRightLeg(r_leg_out_angle_rad_, rhip_to_rfoot_pose_.x, rhip_to_rfoot_pose_.y, rhip_to_rfoot_pose_.z,
//      rhip_to_rfoot_pose_.roll, rhip_to_rfoot_pose_.pitch, rhip_to_rfoot_pose_.yaw);
//  diana_kd_->calcInverseKinematicsForLeftLeg(l_leg_out_angle_rad_, lhip_to_lfoot_pose_.x, lhip_to_lfoot_pose_.y, lhip_to_lfoot_pose_.z,
//      lhip_to_lfoot_pose_.roll, lhip_to_lfoot_pose_.pitch, lhip_to_lfoot_pose_.yaw);


  diana_kin_.InverseKinematics(rhip_to_rfoot_pose_.x, rhip_to_rfoot_pose_.y, rhip_to_rfoot_pose_.z,
      rhip_to_rfoot_pose_.yaw, rhip_to_rfoot_pose_.pitch, rhip_to_rfoot_pose_.roll);

  for(int i = 0; i < 6; i++)
  {
	  r_leg_out_angle_rad_[i] =  diana_kin_.joint_radian(i+1,0);
  }
  r_leg_out_angle_rad_[3] = -r_leg_out_angle_rad_[3];

  diana_kin_.InverseKinematics(lhip_to_lfoot_pose_.x, lhip_to_lfoot_pose_.y, lhip_to_lfoot_pose_.z,
		  lhip_to_lfoot_pose_.yaw, lhip_to_lfoot_pose_.pitch, lhip_to_lfoot_pose_.roll);

  for(int i = 0; i < 6; i++)
  {
	  l_leg_out_angle_rad_[i] =  diana_kin_.joint_radian(i+1,0);
  }
  l_leg_out_angle_rad_[0] = -l_leg_out_angle_rad_[0];
  l_leg_out_angle_rad_[4] = -l_leg_out_angle_rad_[4];

  for(int i = 0; i < 6; i++)
  {
	  out_angle_rad_[i+0] = r_leg_out_angle_rad_[i];
	  out_angle_rad_[i+6] = l_leg_out_angle_rad_[i];
  }

//  out_angle_rad_[0] = r_leg_out_angle_rad_[2];
//  out_angle_rad_[1] = r_leg_out_angle_rad_[1];
//  out_angle_rad_[2] = r_leg_out_angle_rad_[0];
//
//  out_angle_rad_[6] = l_leg_out_angle_rad_[2];
//  out_angle_rad_[7] = l_leg_out_angle_rad_[1];
//  out_angle_rad_[8] = l_leg_out_angle_rad_[0];

  //std::cout << out_angle_rad_[0] << "  " << curr_angle_rad_[0] << std::endl;

  for(int angle_idx = 0; angle_idx < 6; angle_idx++)
  {
    leg_angle_feed_back_[angle_idx+0].desired_ = out_angle_rad_[angle_idx];
    leg_angle_feed_back_[angle_idx+6].desired_ = out_angle_rad_[angle_idx+6];
    out_angle_rad_[angle_idx+0] = out_angle_rad_[angle_idx+0] + leg_angle_feed_back_[angle_idx+0].getFeedBack(curr_angle_rad_[angle_idx]);
    out_angle_rad_[angle_idx+6] = out_angle_rad_[angle_idx+6] + leg_angle_feed_back_[angle_idx+6].getFeedBack(curr_angle_rad_[angle_idx+6]);
//      out_angle_rad_[angle_idx+0] = r_leg_out_angle_rad_[angle_idx];
//      out_angle_rad_[angle_idx+6] = l_leg_out_angle_rad_[angle_idx];
  }
}

bool DIANAOnlineWalking::isRunning()
{
  return walking_pattern_.isRunning();
}


void DIANAOnlineWalking::setCurrentIMUSensorOutput(double gyro_x, double gyro_y, double quat_x, double quat_y, double quat_z, double quat_w)
{
  imu_data_mutex_lock_.lock();

  current_gyro_roll_rad_per_sec_  = gyro_x;
  current_gyro_pitch_rad_per_sec_ = gyro_y;

  quat_current_imu_ = Eigen::Quaterniond(quat_w, quat_x, quat_y, quat_z);

  mat_current_imu_ = (mat_imu_frame_ref_ * quat_current_imu_.toRotationMatrix()) * mat_imu_frame_ref_inv_;

  current_imu_roll_rad_  = atan2( mat_current_imu_.coeff(2,1), mat_current_imu_.coeff(2,2));
  current_imu_pitch_rad_ = atan2(-mat_current_imu_.coeff(2,0), sqrt(robotis_framework::powDI(mat_current_imu_.coeff(2,1), 2) + robotis_framework::powDI(mat_current_imu_.coeff(2,2), 2)));

  //std::cout << "gx : " << current_gyro_roll_rad_per_sec_ << " gy : " << current_gyro_pitch_rad_per_sec_
      //<< " x : " << current_imu_roll_rad_ << " y : " << current_imu_pitch_rad_ << std::endl;

  imu_data_mutex_lock_.unlock();
}

void DIANAOnlineWalking::setCurrentFTSensorOutput(double rfx, double rfy, double rfz, double rtx, double rty, double rtz,
    double lfx, double lfy, double lfz, double ltx, double lty, double ltz)
{
  ft_data_mutex_lock_.lock();
  current_right_fx_N_ = rfx;  current_right_fy_N_ = rfy;  current_right_fz_N_ = rfz;
  current_right_tx_Nm_= rtx; current_right_ty_Nm_= rty; current_right_tz_Nm_= rtz;

  current_left_fx_N_ = lfx;  current_left_fy_N_ = lfy;  current_left_fz_N_ = lfz;
  current_left_tx_Nm_ = ltx; current_left_ty_Nm_ = lty; current_left_tz_Nm_ = ltz;

  mat_right_force_(0,0) = current_right_fx_N_;
  mat_right_force_(1,0) = current_right_fy_N_;
  mat_right_force_(2,0) = current_right_fz_N_;
  mat_left_force_(0,0)  = current_left_fx_N_;
  mat_left_force_(1,0)  = current_left_fy_N_;
  mat_left_force_(2,0)  = current_left_fz_N_;

  mat_right_torque_(0,0) = current_right_tx_Nm_;
  mat_right_torque_(1,0) = current_right_ty_Nm_;
  mat_right_torque_(2,0) = current_right_tz_Nm_;
  mat_left_torque_(0,0) = current_left_tx_Nm_;
  mat_left_torque_(1,0) = current_left_ty_Nm_;
  mat_left_torque_(2,0) = current_left_tz_Nm_;

  mat_right_force_  = mat_robot_to_rfoot_*mat_right_force_;
  mat_right_torque_ = mat_robot_to_rfoot_*mat_right_torque_;

  mat_left_force_  = mat_robot_to_lfoot_*mat_left_force_;
  mat_left_torque_ = mat_robot_to_lfoot_*mat_left_torque_;
  ft_data_mutex_lock_.unlock();
}



