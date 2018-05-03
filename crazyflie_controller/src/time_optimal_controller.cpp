#include <ros/ros.h>
#include <tf/transform_listener.h>
#include <std_srvs/Empty.h>
#include <geometry_msgs/Twist.h>


#include "pid.hpp"

double get(
    const ros::NodeHandle& n,
    const std::string& name) {
    double value;
    n.getParam(name, value);
    return value;
}

class Controller
{
public:

    Controller(
        const std::string& worldFrame,
        const std::string& frame,
        const ros::NodeHandle& n)
        : m_worldFrame(worldFrame)
        , m_frame(frame)
        , m_pubNav()
        , m_listener()
        , m_pidX(
            get(n, "PIDs/X/kp"),
            get(n, "PIDs/X/kd"),
            get(n, "PIDs/X/ki"),
            get(n, "PIDs/X/minOutput"),
            get(n, "PIDs/X/maxOutput"),
            get(n, "PIDs/X/integratorMin"),
            get(n, "PIDs/X/integratorMax"),
            "x")
        , m_pidY(
            get(n, "PIDs/Y/kp"),
            get(n, "PIDs/Y/kd"),
            get(n, "PIDs/Y/ki"),
            get(n, "PIDs/Y/minOutput"),
            get(n, "PIDs/Y/maxOutput"),
            get(n, "PIDs/Y/integratorMin"),
            get(n, "PIDs/Y/integratorMax"),
            "y")
        , m_pidZ(
            get(n, "PIDs/Z/kp"),
            get(n, "PIDs/Z/kd"),
            get(n, "PIDs/Z/ki"),
            get(n, "PIDs/Z/minOutput"),
            get(n, "PIDs/Z/maxOutput"),
            get(n, "PIDs/Z/integratorMin"),
            get(n, "PIDs/Z/integratorMax"),
            "z")
        , m_pidYaw(
            get(n, "PIDs/Yaw/kp"),
            get(n, "PIDs/Yaw/kd"),
            get(n, "PIDs/Yaw/ki"),
            get(n, "PIDs/Yaw/minOutput"),
            get(n, "PIDs/Yaw/maxOutput"),
            get(n, "PIDs/Yaw/integratorMin"),
            get(n, "PIDs/Yaw/integratorMax"),
            "yaw")
        , m_state(Idle)
        , m_goal()
        , m_subscribeGoal()
        , m_subscribeTwist()
        , m_serviceTakeoff()
        , m_serviceLand()
        , m_thrust(0)
        , m_startZ(0)
    {
        ros::NodeHandle nh;
        m_listener.waitForTransform(m_worldFrame, m_frame, ros::Time(0), ros::Duration(10.0)); 
        m_pubNav = nh.advertise<geometry_msgs::Twist>("cmd_vel", 1);
        m_subscribeGoal = nh.subscribe("goal", 1, &Controller::goalChanged, this);
        m_subscribeTwist = nh.subscribe("twist", 1, &Controller::getTwistData, this);
        m_serviceTakeoff = nh.advertiseService("takeoff", &Controller::takeoff, this);
        m_serviceLand = nh.advertiseService("land", &Controller::land, this);
    }

    void run(double frequency)
    {
        ros::NodeHandle node;
        ros::Timer timer = node.createTimer(ros::Duration(1.0/frequency), &Controller::iteration, this);
        ros::spin();
    }

private:
    void goalChanged(
        const geometry_msgs::PoseStamped::ConstPtr& msg)
    {
        m_goal = *msg;
    }

    void getTwistData(
        const geometry_msgs::TwistStamped::ConstPtr& msg)
    {
        m_twistData = *msg;
    }

    bool takeoff(
        std_srvs::Empty::Request& req,
        std_srvs::Empty::Response& res)
    {
        ROS_INFO("Takeoff requested!");
        m_state = TakingOff;

        tf::StampedTransform transform;
        m_listener.lookupTransform(m_worldFrame, m_frame, ros::Time(0), transform);

        /*ROS_INFO("Goal1 x = %f", m_goal.pose.position.x);
        ROS_INFO("Goal1 y = %f", m_goal.pose.position.y);
        ROS_INFO("Goal1 z = %f", m_goal.pose.position.z);*/

        m_startZ = transform.getOrigin().z();

        m_startX = transform.getOrigin().x();
        m_startY = transform.getOrigin().y();

        return true;
    }

    bool land(
        std_srvs::Empty::Request& req,
        std_srvs::Empty::Response& res)
    {
        ROS_INFO("Landing requested!");
        m_state = Landing;

        return true;
    }

    void getTransform(
        const std::string& sourceFrame,
        const std::string& targetFrame,
        tf::StampedTransform& result)
    {
        m_listener.lookupTransform(sourceFrame, targetFrame, ros::Time(0), result);
    }

    void pidReset()
    {
        m_pidX.reset();
        m_pidY.reset();
        m_pidZ.reset();
        m_pidYaw.reset();
    }

    void iteration(const ros::TimerEvent& e)
    {
        float dt = e.current_real.toSec() - e.last_real.toSec();

        /*tf::StampedTransform transform;
        m_listener.lookupTransform(m_worldFrame, m_frame, ros::Time(0), transform);

        ROS_INFO("Origin x = %f", transform.getOrigin().x());
        ROS_INFO("Origin y = %f", transform.getOrigin().y());
        ROS_INFO("Origin z = %f", transform.getOrigin().z());*/

        /*ROS_INFO("Goal x = %f", m_goal.pose.position.x);
        ROS_INFO("Goal y = %f", m_goal.pose.position.y);
        ROS_INFO("Goal z = %f", m_goal.pose.position.z);*/

        /*ROS_INFO("Vel x = %f", m_twistData.twist.linear.x);
        ROS_INFO("Vel y = %f", m_twistData.twist.linear.y);
        ROS_INFO("Vel z = %f", m_twistData.twist.linear.z);*/

        switch(m_state)
        {
        case TakingOff:
            {
                tf::StampedTransform transform;
                m_listener.lookupTransform(m_worldFrame, m_frame, ros::Time(0), transform);

                if (transform.getOrigin().z() > m_startZ + 0.05 || m_thrust > 50000)
                {
                    pidReset();
                    m_pidZ.setIntegral(m_thrust / m_pidZ.ki());

                    m_goal_temp = m_goal;

                    m_goal_temp.pose.position.x = m_startX;
                    m_goal_temp.pose.position.y = m_startY;

                    /*ROS_INFO("Goal2 x = %f", m_goal_temp.pose.position.x);
                    ROS_INFO("Goal2 y = %f", m_goal_temp.pose.position.y);
                    ROS_INFO("Goal2 z = %f", m_goal_temp.pose.position.z);*/

                    m_state = GoToZDesired;
                    m_thrust = 0;
                }
                else
                {
                    m_thrust += 14500 * dt;
                    geometry_msgs::Twist msg;
                    msg.linear.z = m_thrust;
                    m_pubNav.publish(msg);
                }

            }
            break;
        case GoToZDesired:
            {
                tf::StampedTransform transform;
                m_listener.lookupTransform(m_worldFrame, m_frame, ros::Time(0), transform);

                geometry_msgs::PoseStamped targetWorld;
                targetWorld.header.stamp = transform.stamp_;
                targetWorld.header.frame_id = m_worldFrame;
                targetWorld.pose = m_goal_temp.pose;

                geometry_msgs::PoseStamped targetDrone;
                m_listener.transformPose(m_frame, targetWorld, targetDrone);

                tfScalar roll, pitch, yaw;
                tf::Matrix3x3(
                    tf::Quaternion(
                        targetDrone.pose.orientation.x,
                        targetDrone.pose.orientation.y,
                        targetDrone.pose.orientation.z,
                        targetDrone.pose.orientation.w
                    )).getRPY(roll, pitch, yaw);

                geometry_msgs::Twist msg;
                msg.linear.x = m_pidX.update(0, targetDrone.pose.position.x);
                msg.linear.y = m_pidY.update(0.0, targetDrone.pose.position.y);
                msg.linear.z = m_pidZ.update(0.0, targetDrone.pose.position.z);
                msg.angular.z = m_pidYaw.update(0.0, yaw);
                m_pubNav.publish(msg);

                m_HoverRMSEX = targetDrone.pose.position.x * targetDrone.pose.position.x;
                m_HoverRMSEY = targetDrone.pose.position.y * targetDrone.pose.position.y;
                m_HoverRMSEZ = targetDrone.pose.position.z * targetDrone.pose.position.z;

                //ROS_INFO("Error x = %f, y = %f, z = %f", m_HoverRMSEX, m_HoverRMSEY, m_HoverRMSEZ);

                if (m_HoverRMSEX < 0.05 && m_HoverRMSEY < 0.05 && m_HoverRMSEZ < 0.05)
                    m_state = Automatic;
                
                /*tf::StampedTransform transform1;
                m_listener.lookupTransform(m_worldFrame, m_frame, ros::Time(0), transform1);

                geometry_msgs::PoseStamped targetWorld1;
                targetWorld1.header.stamp = transform1.stamp_;
                targetWorld1.header.frame_id = m_worldFrame;
                targetWorld1.pose = m_goal.pose;

                geometry_msgs::PoseStamped targetDrone1;
                m_listener.transformPose(m_frame, targetWorld1, targetDrone1);

                tfScalar roll1, pitch1, yaw1;
                tf::Matrix3x3(
                    tf::Quaternion(
                        targetDrone1.pose.orientation.x,
                        targetDrone1.pose.orientation.y,
                        targetDrone1.pose.orientation.z,
                        targetDrone1.pose.orientation.w
                    )).getRPY(roll1, pitch1, yaw1);

                float s_x = targetDrone1.pose.position.x*10 + 1910*m_twistData.twist.linear.x*fabs(m_twistData.twist.linear.x);
                float s_y = targetDrone1.pose.position.y*10 + 1910*m_twistData.twist.linear.y*fabs(m_twistData.twist.linear.y);

                ROS_INFO("%f %f %f %f %f %f", s_x, s_y, targetDrone1.pose.position.x, m_twistData.twist.linear.x, targetDrone1.pose.position.y, m_twistData.twist.linear.y);*/

                //ROS_INFO("roll = %f, pitch = %f, yaw = %f", roll1*180/3.14, pitch1*180/3.14, yaw1*180/3.14);
                /*ROS_INFO("Error x = %f", targetDrone1.pose.position.x);
                ROS_INFO("Error y = %f", targetDrone1.pose.position.y);
                ROS_INFO("Error z = %f", targetDrone1.pose.position.z);*/
            }
            break;
        case Landing:
            {
                m_goal.pose.position.z = m_startZ + 0.05;
                tf::StampedTransform transform;
                m_listener.lookupTransform(m_worldFrame, m_frame, ros::Time(0), transform);
                if (transform.getOrigin().z() <= m_startZ + 0.05) {
                    m_state = Idle;
                    geometry_msgs::Twist msg;
                    m_pubNav.publish(msg);
                }

                geometry_msgs::PoseStamped targetWorld;
                targetWorld.header.stamp = transform.stamp_;
                targetWorld.header.frame_id = m_worldFrame;
                targetWorld.pose = m_goal.pose;

                geometry_msgs::PoseStamped targetDrone;
                m_listener.transformPose(m_frame, targetWorld, targetDrone);

                tfScalar roll, pitch, yaw;
                tf::Matrix3x3(
                    tf::Quaternion(
                        targetDrone.pose.orientation.x,
                        targetDrone.pose.orientation.y,
                        targetDrone.pose.orientation.z,
                        targetDrone.pose.orientation.w
                    )).getRPY(roll, pitch, yaw);

                geometry_msgs::Twist msg;
                msg.linear.x = m_pidX.update(0, targetDrone.pose.position.x);
                msg.linear.y = m_pidY.update(0.0, targetDrone.pose.position.y);
                msg.linear.z = m_pidZ.update(0.0, targetDrone.pose.position.z);
                msg.angular.z = m_pidYaw.update(0.0, yaw);
                m_pubNav.publish(msg);
            }
            break;
        case Automatic:
            {
                ROS_INFO("Automatic mode initiated");
                tf::StampedTransform transform;
                m_listener.lookupTransform(m_worldFrame, m_frame, ros::Time(0), transform);

                geometry_msgs::PoseStamped targetWorld;
                targetWorld.header.stamp = transform.stamp_;
                targetWorld.header.frame_id = m_worldFrame;
                targetWorld.pose = m_goal.pose;

                geometry_msgs::PoseStamped targetDrone;
                m_listener.transformPose(m_frame, targetWorld, targetDrone);

                tfScalar roll, pitch, yaw;
                tf::Matrix3x3(
                    tf::Quaternion(
                        targetDrone.pose.orientation.x,
                        targetDrone.pose.orientation.y,
                        targetDrone.pose.orientation.z,
                        targetDrone.pose.orientation.w
                    )).getRPY(roll, pitch, yaw);

                geometry_msgs::Twist msg;

                float s_x = targetDrone.pose.position.x*10 + 1910*m_twistData.twist.linear.x*fabs(m_twistData.twist.linear.x);
                float s_y = targetDrone.pose.position.y*10 + 1910*m_twistData.twist.linear.y*fabs(m_twistData.twist.linear.y);

                //ROS_INFO("s_x = %f, s_y = %f", s_x, s_y);

                if (targetDrone.pose.position.x > 0.5 || targetDrone.pose.position.y > 0.5)
                {
                    ROS_INFO("Error present");
                    //msg.linear.x = m_pidX.update(0, targetDrone.pose.position.x);
                    //msg.linear.y = m_pidY.update(0.0, targetDrone.pose.position.y);
                    if (fabs(s_x) > 0.2 || fabs (s_y) > 0.2)
                    {
                        if (s_x > 0.2 && s_y > 0.2)
                        {
                            ROS_INFO("Case 1");
                            msg.linear.x = -10.0;
                            msg.linear.y = -10.0;
                        }
                        else if (s_x < -0.2 && s_y > 0.2)
                        {
                            ROS_INFO("Case 2");
                            msg.linear.x = 10.0;
                            msg.linear.y = -10.0;
                        }
                        else if (s_x < -0.2 && s_y < -0.2)
                        {
                            ROS_INFO("Case 3");
                            msg.linear.x = 10.0;
                            msg.linear.y = 10.0;
                        }
                        else if (s_x > 0.2 && s_y < -0.2)
                        {
                            ROS_INFO("Case 4");
                            msg.linear.x = -10.0;
                            msg.linear.y = 10.0;
                        }
                    }
                    
                    else
                    {
                        ROS_INFO("Case 5");
                        msg.linear.x = 0.0;
                        msg.linear.y = 0.0;
                    }
                    msg.linear.z = m_pidZ.update(0.0, targetDrone.pose.position.z);
                    msg.angular.z = m_pidYaw.update(0.0, yaw);
                    m_pubNav.publish(msg);    
                }

                /*if (targetDrone.pose.position.y > 0.1)
                {
                    //ROS_INFO("Error along Y");
                    
                    if (s_y > 0.2)
                    {
                        //ROS_INFO("-ve roll");
                        msg.linear.y = -10.0;
                    }
                    else if (s_y < -0.2)
                    {
                        //ROS_INFO("+ve roll");
                        msg.linear.y = 10.0;
                    }
                    else
                        msg.linear.y = 0.0;
                }*/
                else
                { 
                    ROS_INFO("No error present");
                    msg.linear.x = m_pidX.update(0, targetDrone.pose.position.x);
                    msg.linear.y = m_pidY.update(0.0, targetDrone.pose.position.y);
                    msg.linear.z = m_pidZ.update(0.0, targetDrone.pose.position.z);
                    msg.angular.z = m_pidYaw.update(0.0, yaw);
                    m_pubNav.publish(msg);
                }
                /*ROS_INFO("x = %f",targetDrone.pose.position.x);
                ROS_INFO("y = %f",targetDrone.pose.position.y);
                ROS_INFO("roll = %f", roll);
                ROS_INFO("pitch = %f", pitch);*/    

            }
            break;
        case Idle:
            {
                geometry_msgs::Twist msg;
                m_pubNav.publish(msg);
            }
            break;
        }
    }

private:

    enum State
    {
        Idle = 0,
        Automatic = 1,
        TakingOff = 2,
        Landing = 4,
        GoToZDesired = 3,
    };

private:
    std::string m_worldFrame;
    std::string m_frame;
    ros::Publisher m_pubNav;
    tf::TransformListener m_listener;
    PID m_pidX;
    PID m_pidY;
    PID m_pidZ;
    PID m_pidYaw;
    State m_state;
    geometry_msgs::PoseStamped m_goal;
    geometry_msgs::PoseStamped m_goal_temp;
    geometry_msgs::TwistStamped m_twistData;
    ros::Subscriber m_subscribeGoal;
    ros::Subscriber m_subscribeTwist;
    ros::ServiceServer m_serviceTakeoff;
    ros::ServiceServer m_serviceLand;
    float m_thrust;
    float m_startZ, m_startX, m_startY;
    float m_HoverRMSEX, m_HoverRMSEY, m_HoverRMSEZ;
};

int main(int argc, char **argv)
{
  ros::init(argc, argv, "controller");

  // Read parameters
  ros::NodeHandle n("~");
  std::string worldFrame;
  n.param<std::string>("worldFrame", worldFrame, "/world");
  std::string frame;
  n.getParam("frame", frame);
  double frequency;
  n.param("frequency", frequency, 50.0);

  Controller controller(worldFrame, frame, n);
  controller.run(frequency);

  return 0;
}
