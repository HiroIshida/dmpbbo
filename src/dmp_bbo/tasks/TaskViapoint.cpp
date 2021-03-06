/**
 * @file   TaskViapoint.cpp
 * @brief  TaskViapoint class source file.
 * @author Freek Stulp
 *
 * This file is part of DmpBbo, a set of libraries and programs for the 
 * black-box optimization of dynamical movement primitives.
 * Copyright (C) 2014 Freek Stulp, ENSTA-ParisTech
 * 
 * DmpBbo is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 * 
 * DmpBbo is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public License
 * along with DmpBbo.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "dmp_bbo/tasks/TaskViapoint.hpp"

#include <iostream>
#include <fstream>
#include <iomanip>
#include <eigen3/Eigen/Core>

#include "dmpbbo_io/EigenFileIO.hpp"

using namespace std;
using namespace Eigen;

namespace DmpBbo {

TaskViapoint::TaskViapoint(const Eigen::VectorXd& viapoint, double  viapoint_time, double viapoint_radius)
: viapoint_(viapoint), viapoint_time_(viapoint_time), viapoint_radius_(viapoint_radius), 
  goal_(VectorXd::Ones(viapoint.size())), goal_time_(-1),
  viapoint_weight_(1.0), acceleration_weight_(0.0001),  goal_weight_(0.0)
{
  assert(viapoint_radius_>=0.0);
}

TaskViapoint::TaskViapoint(const Eigen::VectorXd& viapoint, double  viapoint_time, const Eigen::VectorXd& goal,  double goal_time)
: viapoint_(viapoint), viapoint_time_(viapoint_time), viapoint_radius_(0.0),
  goal_(goal), goal_time_(goal_time),
  viapoint_weight_(1.0), acceleration_weight_(0.0001),  goal_weight_(1.0)
{
  assert(viapoint_.size()==goal.size());
}

TaskViapoint::TaskViapoint(const Eigen::VectorXd& viapoint, double  viapoint_time, double viapoint_radius, const Eigen::VectorXd& goal,  double goal_time, double viapoint_weight, double acceleration_weight, double goal_weight)
: viapoint_(viapoint), viapoint_time_(viapoint_time), viapoint_radius_(viapoint_radius),
  goal_(goal), goal_time_(goal_time),
  viapoint_weight_(viapoint_weight), acceleration_weight_(acceleration_weight),  goal_weight_(goal_weight)
{
  assert(viapoint_radius_>=0.0);
  assert(viapoint.size()==goal.size());
}


void TaskViapoint::computeCosts(const VectorXd& ts, const MatrixXd& y, const MatrixXd& ydd, VectorXd& costs) const
{
  int n_time_steps = ts.size();

  double dist_to_viapoint = 0.0;
  if (viapoint_weight_!=0.0)
  {
    if (viapoint_time_ == TIME_AT_MINIMUM_DIST)
    {
      // Don't compute the distance at some time, but rather get the minimum distance
      dist_to_viapoint = (y.rowwise() - viapoint_.transpose()).rowwise().squaredNorm().minCoeff();
    }
    else
    {
      // Compute the minimum distance at a specific time
      
      // Get the time_step at which viapoint_time_step approx ts[time_step]
      int viapoint_time_step = 0;
      while (viapoint_time_step < ts.size() && ts[viapoint_time_step] < viapoint_time_)
        viapoint_time_step++;

      assert(viapoint_time_step < ts.size());

      VectorXd y_via = y.row(viapoint_time_step);
      dist_to_viapoint = sqrt((y_via-viapoint_).array().pow(2).sum());
    }
    
    if (viapoint_radius_>0.0)
    {
      // The viapoint_radius defines a radius within which the cost is always 0
      dist_to_viapoint -= viapoint_radius_;
      if (dist_to_viapoint<0.0)
        dist_to_viapoint = 0.0;
    }
  }
  
  double mean_ydd = 0.0;
  if (acceleration_weight_!=0.0)
  {
    // ydd = n_time_steps x n_dims
    mean_ydd = ydd.array().pow(2).sum()/n_time_steps;
  }

  double delay_cost = 0.0;
  if (goal_weight_!=0.0)
  {
    int goal_time_step = 0;
    while (goal_time_step < ts.size() && ts[goal_time_step] < goal_time_)
      goal_time_step++;

    MatrixXd y_after_goal = y.bottomRows(n_time_steps - goal_time_step);
    //cost_vars.block(goal_time_step, 0,
    //    n_time_steps - goal_time_step, n_dims);

    delay_cost = (y_after_goal.rowwise() - goal_.transpose()).rowwise().squaredNorm().sum();
  }
  
  int n_cost_components = 3;
  costs.resize(1+n_cost_components); // costs[0] = sum(costs[1:end])
  costs[1] = viapoint_weight_*dist_to_viapoint;
  costs[2] = acceleration_weight_*mean_ydd;
  costs[3] = goal_weight_*delay_cost;
  costs[0] = costs[1] +  costs[2] +  costs[3];  
}

void TaskViapoint::evaluateRollout(const MatrixXd& cost_vars, const Eigen::VectorXd& sample, const VectorXd& task_parameters, VectorXd& costs) const
{
  // cost_vars is assumed to have following structure
  // t=0  y_1..y_D  yd_1..yd_D  ydd_1..ydd_D  forcing_1..forcing_D
  // t=1  y_1..y_D  yd_1..yd_D  ydd_1..ydd_D  forcing_1..forcing_D
  //  |    |    |     |     |      |     |       |          |
  // t=T  y_1..y_D  yd_1..yd_D  ydd_1..ydd_D  forcing_1..forcing_D
  int n_dims = viapoint_.size();
  int n_time_steps = cost_vars.rows();
  int n_cost_vars = cost_vars.cols();
  assert(n_cost_vars==(1+4*n_dims));
  
  // rollout is of size   n_time_steps x n_cost_vars
  VectorXd ts = cost_vars.col(0);
  MatrixXd y = cost_vars.block(0, 1, n_time_steps, n_dims);
  MatrixXd ydd = cost_vars.block(0,1+2*n_dims,n_time_steps,n_dims);
  computeCosts(ts,y,ydd,costs);         
}

unsigned int TaskViapoint::getNumberOfCostComponents(void) const
{ 
  return 3;
};

void TaskViapoint::setCostFunctionWeighting(double viapoint_weight, double acceleration_weight, double goal_weight)
{
  viapoint_weight_      = viapoint_weight;
  acceleration_weight_  = acceleration_weight;
  goal_weight_          = goal_weight;            
}

void TaskViapoint::generateDemonstration(const MatrixXd& task_parameters, const VectorXd& ts, Trajectory& demonstration) const
{
  int n_dims = viapoint_.size();
  
  assert(task_parameters.rows()==1);
  assert(task_parameters.cols()==n_dims);	
	
	VectorXd y_from    = VectorXd::Constant(n_dims,0.0);
	VectorXd y_to      = goal_;

	VectorXd y_yd_ydd_viapoint(3*n_dims);
	y_yd_ydd_viapoint << task_parameters.row(0), VectorXd::Constant(n_dims,1.0), VectorXd::Constant(n_dims,0.0);
  
  demonstration = Trajectory::generatePolynomialTrajectoryThroughViapoint(ts, y_from, y_yd_ydd_viapoint, viapoint_time_, y_to);

}


string TaskViapoint::toString(void) const {
  return string("TaskViapoint");
}

TaskViapoint TaskViapoint::readFromFile(std::string filename)
{
  MatrixXd matrix;
  loadMatrix(filename, matrix);
  VectorXd vector = matrix.row(0);
  
  // 6 doubles and two vectors of size n_dims 
  int n_dims = (vector.size()-6)/2;
  
  VectorXd viapoint = vector.head(n_dims);
  double viapoint_time = vector[n_dims];
  if (viapoint_time<0.0)
    viapoint_time = TIME_AT_MINIMUM_DIST;
  double viapoint_radius = vector[n_dims+1];    
  VectorXd goal = vector.segment(n_dims+2,n_dims);
  double goal_time = vector[2*n_dims+2];
  double viapoint_weight = vector[2*n_dims+3];    
  double acceleration_weight = vector[2*n_dims+4];
  double goal_weight = vector[2*n_dims+5];
  
  return TaskViapoint(viapoint,viapoint_time,viapoint_radius,goal,goal_time,viapoint_weight,acceleration_weight,goal_weight);
}

bool TaskViapoint::writeToFile(std::string filename) const
{
  ofstream myfile;
  myfile.open(filename, ios::out);
  for (int ii=0; ii<viapoint_.size(); ii++)
    myfile << viapoint_[ii] << " ";
  myfile << viapoint_time_ << " ";    
  myfile << viapoint_radius_ << " ";    
  for (int ii=0; ii<goal_.size(); ii++)
    myfile << goal_[ii] << " ";
  myfile << goal_time_ << " ";
  myfile << viapoint_weight_ << " ";    
  myfile << acceleration_weight_ << " ";
  myfile << goal_weight_ << " ";
  myfile.close();
  return true;
}


bool TaskViapoint::savePlotRolloutScript(string directory) const
{
  string filename = directory + "/plotRollout.py";
  
  std::ofstream file;
  file.open(filename.c_str());
  if (!file.is_open())
  {
    std::cerr << "Couldn't open file '" << filename << "' for writing." << std::endl;
    return false;
  }
  
  file << "import numpy as np" << endl;
  file << "import matplotlib.pyplot as plt" << endl;
  file << "import sys, os" << endl;
  file << "def plotRollout(cost_vars,ax):" << endl;
  file << "    viapoint = [";
  file << fixed;
  for (int ii=0; ii<viapoint_.size(); ii++)
  {
    if (ii>0) file << ", ";
    file << viapoint_[ii];
  }
  file << "]" << endl;
  file << "    viapoint_time = "<< viapoint_time_<< endl;
  file << "    # y      yd     ydd    1  forcing" << endl;
  file << "    # n_dofs n_dofs n_dofs ts n_dofs" << endl;
  file << "    n_dofs = len(viapoint)" << endl;
  file << "    y = cost_vars[:,0:n_dofs]" << endl;
  file << "    t = cost_vars[:,3*n_dofs]" << endl;
  if (viapoint_.size()==1)
  {
    file << "    line_handles = ax.plot(t,y,linewidth=0.5)" << endl;
    file << "    ax.plot(viapoint_time,viapoint,'ok')" << endl;
  }
  else
  {
    file << "    line_handles = ax.plot(y[:,0],y[:,1],linewidth=0.5)" << endl;
    file << "    ax.plot(viapoint[0],viapoint[1],'ok')" << endl;
  }
  file << "    return line_handles" << endl;
  file << "" << endl;
  file << "if __name__=='__main__':" << endl;
  file << "    # See if input directory was passed" << endl;
  file << "    if (len(sys.argv)==2):" << endl;
  file << "      directory = str(sys.argv[1])" << endl;
  file << "    else:" << endl;
  file << "      print 'Usage: '+sys.argv[0]+' <directory>';" << endl;
  file << "      sys.exit()" << endl;
  file << "    cost_vars = np.loadtxt(directory+\"/cost_vars.txt\")" << endl;
  file << "    fig = plt.figure()" << endl;
  file << "    ax = fig.gca()" << endl;
  file << "    plotRollout(cost_vars,ax)" << endl;
  file << "    plt.show()" << endl;
  
  file.close();
  
  return true;
}


}
