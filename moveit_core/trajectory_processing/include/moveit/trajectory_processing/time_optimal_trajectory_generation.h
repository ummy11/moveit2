/*
 * Copyright (c) 2011-2012, Georgia Tech Research Corporation
 * All rights reserved.
 *
 * Author: Tobias Kunz <tobias@gatech.edu>
 * Date: 05/2012
 *
 * Humanoid Robotics Lab      Georgia Institute of Technology
 * Director: Mike Stilman     http://www.golems.org
 *
 * Algorithm details and publications:
 * http://www.golems.org/node/1570
 *
 * This file is provided under the following "BSD-style" License:
 *   Redistribution and use in source and binary forms, with or
 *   without modification, are permitted provided that the following
 *   conditions are met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 *   CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *   INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *   MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 *   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 *   USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 *   AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *   LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *   ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *   POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <Eigen/Core>
#include <list>
#include <moveit/robot_trajectory/robot_trajectory.h>
#include <moveit/trajectory_processing/time_parameterization.h>

namespace trajectory_processing
{
class PathSegment
{
public:
  PathSegment(double length = 0.0) : length_(length)
  {
  }
  virtual ~PathSegment()  // is required for destructing derived classes
  {
  }
  double getLength() const
  {
    return length_;
  }
  virtual Eigen::VectorXd getConfig(double s) const = 0;
  virtual Eigen::VectorXd getTangent(double s) const = 0;
  virtual Eigen::VectorXd getCurvature(double s) const = 0;
  virtual std::list<double> getSwitchingPoints() const = 0;
  virtual PathSegment* clone() const = 0;

  double position_;

protected:
  double length_;
};

class Path
{
public:
  Path(const std::list<Eigen::VectorXd>& path, double max_deviation = 0.0);
  Path(const Path& path);
  double getLength() const;
  Eigen::VectorXd getConfig(double s) const;
  Eigen::VectorXd getTangent(double s) const;
  Eigen::VectorXd getCurvature(double s) const;
  double getNextSwitchingPoint(double s, bool& discontinuity) const;
  std::list<std::pair<double, bool>> getSwitchingPoints() const;

private:
  PathSegment* getPathSegment(double& s) const;
  double length_;
  std::list<std::pair<double, bool>> switching_points_;
  std::list<std::unique_ptr<PathSegment>> path_segments_;
};

class Trajectory
{
public:
  /// @brief Generates a time-optimal trajectory
  Trajectory(const Path& path, const Eigen::VectorXd& max_velocity, const Eigen::VectorXd& max_acceleration,
             double time_step = 0.001);

  ~Trajectory();

  /** @brief Call this method after constructing the object to make sure the
     trajectory generation succeeded without errors. If this method returns
     false, all other methods have undefined behavior. **/
  bool isValid() const;

  /// @brief Returns the optimal duration of the trajectory
  double getDuration() const;

  /** @brief Return the position/configuration vector for a given point in time
   */
  Eigen::VectorXd getPosition(double time) const;
  /** @brief Return the velocity vector for a given point in time */
  Eigen::VectorXd getVelocity(double time) const;
  /** @brief Return the acceleration vector for a given point in time */
  Eigen::VectorXd getAcceleration(double time) const;

private:
  struct TrajectoryStep
  {
    TrajectoryStep()
    {
    }
    TrajectoryStep(double path_pos, double path_vel) : path_pos_(path_pos), path_vel_(path_vel)
    {
    }
    double path_pos_;
    double path_vel_;
    double time_;
  };

  bool getNextSwitchingPoint(double path_pos, TrajectoryStep& next_switching_point, double& before_acceleration,
                             double& after_acceleration);
  bool getNextAccelerationSwitchingPoint(double path_pos, TrajectoryStep& next_switching_point,
                                         double& before_acceleration, double& after_acceleration);
  bool getNextVelocitySwitchingPoint(double path_pos, TrajectoryStep& next_switching_point, double& before_acceleration,
                                     double& after_acceleration);
  bool integrateForward(std::list<TrajectoryStep>& trajectory, double acceleration);
  void integrateBackward(std::list<TrajectoryStep>& start_trajectory, double path_pos, double path_vel,
                         double acceleration);
  double getMinMaxPathAcceleration(double path_position, double path_velocity, bool max);
  double getMinMaxPhaseSlope(double path_position, double path_velocity, bool max);
  double getAccelerationMaxPathVelocity(double path_pos) const;
  double getVelocityMaxPathVelocity(double path_pos) const;
  double getAccelerationMaxPathVelocityDeriv(double path_pos);
  double getVelocityMaxPathVelocityDeriv(double path_pos);

  std::list<TrajectoryStep>::const_iterator getTrajectorySegment(double time) const;

  Path path_;
  Eigen::VectorXd max_velocity_;
  Eigen::VectorXd max_acceleration_;
  unsigned int joint_num_;
  bool valid_;
  std::list<TrajectoryStep> trajectory_;
  std::list<TrajectoryStep> end_trajectory_;  // non-empty only if the trajectory generation failed.

  const double time_step_;

  mutable double cached_time_;
  mutable std::list<TrajectoryStep>::const_iterator cached_trajectory_segment_;
};

MOVEIT_CLASS_FORWARD(TimeOptimalTrajectoryGeneration);
class TimeOptimalTrajectoryGeneration : public TimeParameterization
{
public:
  TimeOptimalTrajectoryGeneration(const double path_tolerance = 0.1, const double resample_dt = 0.1,
                                  const double min_angle_change = 0.001);

  // clang-format off
/**
  * \brief Compute a trajectory with waypoints spaced equally in time (according to resample_dt_).
  * Resampling the trajectory doesn't change the start and goal point,
  * and all re-sampled waypoints will be on the path of the original trajectory (within path_tolerance_).
  * However, controller execution is separate from MoveIt and may deviate from the intended path between waypoints.
  * path_tolerance_ is defined in configuration space, so the unit is rad for revolute joints,
  * meters for prismatic joints.
  * \param[in,out] trajectory A path which needs time-parameterization. It's OK if this path has already been
  * time-parameterized; this function will re-time-parameterize it.
  * \param max_velocity_scaling_factor A factor in the range [0,1] which can slow down the trajectory.
  * \param max_acceleration_scaling_factor A factor in the range [0,1] which can slow down the trajectory.
  */
  // clang-format on
  bool computeTimeStamps(robot_trajectory::RobotTrajectory& trajectory, const double max_velocity_scaling_factor = 1.0,
                         const double max_acceleration_scaling_factor = 1.0) const override;

  // clang-format off
/**
  * \brief Compute a trajectory with waypoints spaced equally in time (according to resample_dt_).
  * Resampling the trajectory doesn't change the start and goal point,
  * and all re-sampled waypoints will be on the path of the original trajectory (within path_tolerance_).
  * However, controller execution is separate from MoveIt and may deviate from the intended path between waypoints.
  * path_tolerance_ is defined in configuration space, so the unit is rad for revolute joints,
  * meters for prismatic joints.
  * \param[in,out] trajectory A path which needs time-parameterization. It's OK if this path has already been
  * time-parameterized; this function will re-time-parameterize it.
  * \param velocity_limits Joint names and velocity limits in rad/s
  * \param acceleration_limits Joint names and acceleration limits in rad/s^2
  */
  // clang-format on
  bool computeTimeStamps(robot_trajectory::RobotTrajectory& trajectory,
                         const std::unordered_map<std::string, double>& velocity_limits,
                         const std::unordered_map<std::string, double>& acceleration_limits) const override;

private:
  bool doTimeParameterizationCalculations(robot_trajectory::RobotTrajectory& trajectory,
                                          const Eigen::VectorXd& max_velocity,
                                          const Eigen::VectorXd& max_acceleration) const;

  /**
   * @brief Check if a combination of revolute and prismatic joints is used. path_tolerance_ is not valid, if so.
   * \param group The JointModelGroup to check.
   * \return true if there are mixed joints.
   */
  bool hasMixedJointTypes(const moveit::core::JointModelGroup* group) const;

  const double path_tolerance_;
  const double resample_dt_;
  const double min_angle_change_;
};

// clang-format off
/**
  * \brief Compute a trajectory with the desired number of waypoints.
  * Resampling the trajectory doesn't change the start and goal point,
  * and all re-sampled waypoints will be on the path of the original trajectory (within path_tolerance_).
  * However, controller execution is separate from MoveIt and may deviate from the intended path between waypoints.
  * path_tolerance_ is defined in configuration space, so the unit is rad for revolute joints,
  * meters for prismatic joints.
  * This is a free function because it needs to modify the const resample_dt_ member of TimeOptimalTrajectoryGeneration class.
  * \param num_waypoints The desired number of waypoints (plus or minus one due to numerical rounding).
  * \param[in,out] trajectory A path which needs time-parameterization. It's OK if this path has already been
  * time-parameterized; this function will re-time-parameterize it.
  * \param max_velocity_scaling_factor A factor in the range [0,1] which can slow down the trajectory.
  * \param max_acceleration_scaling_factor A factor in the range [0,1] which can slow down the trajectory.
  */
// clang-format on
bool totgComputeTimeStamps(const size_t num_waypoints, robot_trajectory::RobotTrajectory& trajectory,
                           const double max_velocity_scaling_factor = 1.0,
                           const double max_acceleration_scaling_factor = 1.0);
}  // namespace trajectory_processing
