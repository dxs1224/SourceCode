//serial_imu.cpp
#include <ros/ros.h>
#include <serial/serial.h>
#include <iostream>
#include <stdio.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include <sensor_msgs/Imu.h>
#include <tf/tf.h>
#include <tf/transform_broadcaster.h>
#include <signal.h>


#include "std_msgs/Float64MultiArray.h"

#ifdef __cplusplus 
extern "C"{
#endif

#include <stdint.h>
#include <stdbool.h>
#include "packet.h"
#include "imu_data_decode.h"

#define IMU_SERIAL   "/dev/imu"
#define BAUD         (115200)
#define GRA_ACC      (9.8)
#define DEG_TO_RAD   (0.01745329)

int imu_data_decode_init(void);
typedef void (*on_data_received_event)(packet_t *ptr);
void packet_decode_init(packet_t *pkt, on_data_received_event rx_handler);
uint32_t packet_decode(uint8_t);

#ifdef __cplusplus
}
#endif

bool isQuaternionValid(const geometry_msgs::Quaternion& q);
bool publish_imu_data(receive_imusol_packet_t *data, sensor_msgs::Imu *imu_data, std_msgs::Float64MultiArray *RPY_msg);

void dump_data_packet(receive_imusol_packet_t *data);

static int frame_rate;

static uint8_t buf[2048];

void timer(int sig)
{
	if(SIGALRM == sig)
	{
		frame_rate = frame_count;
		frame_count = 0;
		alarm(1);
	}
}

int main(int argc, char** argv)
{
	ros::init(argc, argv, "serial_imu");
	ros::NodeHandle n;

	ros::Publisher IMU_pub = n.advertise<sensor_msgs::Imu>("imu_data", 20);
	ros::Publisher RPY_pub = n.advertise<std_msgs::Float64MultiArray>("RPY", 20);

	serial::Serial sp;

	serial::Timeout to = serial::Timeout::simpleTimeout(100);

	sp.setPort(IMU_SERIAL);

	sp.setBaudrate(BAUD);

	sp.setTimeout(to);
	
	
	imu_data_decode_init();
	signal(SIGALRM,timer);

	try
	{
		sp.open();
	}
	catch(serial::IOException& e)
	{
		ROS_ERROR_STREAM("Unable to open port.");
		return -1;
	}
    
	if(sp.isOpen())
	{
		ROS_INFO_STREAM("/dev/imu is opened.");
	}
	else
	{
		return -1;
	}
	
	alarm(1);
	
	ros::Rate loop_rate(500);
	sensor_msgs::Imu imu_data;
	std_msgs::Float64MultiArray RPY_msg;
      
        sp.write("AT+SETPTL=A0,B0,D0,D1\r\n");
//	sp.write("AT+URFR=1,0,0,0,1,0,0,0,1\r\n");
	sp.write("AT+RST\r\n");
        sp.write("AT+URFR=1,0,0,0,1,0,0,0,1\r\n");
	while(ros::ok())
	{
		size_t num = sp.available();
		if(num!=0)
		{
			uint8_t buffer[1024];

			num = sp.read(buffer, num);
			if(num > 0)
			{
				for(int i = 0; i < num; i++)
					packet_decode(buffer[i]);

				imu_data.header.stamp = ros::Time::now();
				imu_data.header.frame_id = "imu_link";

//				puts("\033c");
				if(receive_gwsol.tag != KItemGWSOL)
				{
					dump_data_packet(&receive_imusol);
					if(publish_imu_data(&receive_imusol, &imu_data, &RPY_msg))
					{
					IMU_pub.publish(imu_data);
					RPY_pub.publish(RPY_msg);
					RPY_msg.data.clear();
					}
//					puts("Pleaes enter ctrl + 'c' to quit....");
				}
				else
				{
					printf("       GW ID: %4d\n", receive_gwsol.gw_id);
					for(int i = 0; i < receive_gwsol.n; i++)
					{
						dump_data_packet(&receive_gwsol.receive_imusol[i]);
						publish_imu_data(&receive_gwsol.receive_imusol[i], &imu_data, &RPY_msg);
						IMU_pub.publish(imu_data);
						puts("");
					}
					puts("Please enter ctrl + 'c' to quit...");
				}
			}
		}
		loop_rate.sleep();
	}
    
	sp.close();
 
	return 0;
}

void dump_data_packet(receive_imusol_packet_t *data)
{
//	if(bitmap & BIT_VALID_ID)
//		printf("     Devie ID:%6d\n",data->id);

//	if(bitmap & BIT_VALID_TIMES)
//		printf("    Run times: %d days  %d:%d:%d:%d\n",data->times / 86400000, data->times / 3600000 % 24, data->times / 60000 % 60, data->times / 1000 % 60, data->times % 1000);

//	printf("  Frame Rate:  %4dHz\r\n",frame_rate);
//	if(bitmap & BIT_VALID_ACC)
//		printf("       Acc(G):%8.3f %8.3f %8.3f\r\n", data->acc[0], data->acc[1], data->acc[2]);

//	if(bitmap & BIT_VALID_GYR)
//		printf("   Gyr(deg/s):%8.2f %8.2f %8.2f\r\n", data->gyr[0], data->gyr[1], data->gyr[2]);

//	if(bitmap & BIT_VALID_MAG)
//		printf("      Mag(uT):%8.2f %8.2f %8.2f\r\n", data->mag[0], data->mag[1], data->mag[2]);

//	if(bitmap & BIT_VALID_EUL)
//		printf("   Eul(R P Y):%8.2f %8.2f %8.2f\r\n", data->eul[0], data->eul[1], data->eul[2]);

//	if(bitmap & BIT_VALID_QUAT)
//		printf("Quat(W X Y Z):%8.3f %8.3f %8.3f %8.3f\r\n", data->quat[0], data->quat[1], data->quat[2], data->quat[3]);
}

bool isQuaternionValid(const geometry_msgs::Quaternion& q)
{
    //first we need to check if the quaternion has nan's or infs
    if(!std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.z) || !std::isfinite(q.w)){
      ROS_ERROR("Quaternion has nans or infs... discarding as a navigation goal");
      return false;
    }
    return true;
}

bool publish_imu_data(receive_imusol_packet_t *data, sensor_msgs::Imu *imu_data, std_msgs::Float64MultiArray *RPY_msg)
{	
	imu_data->orientation.x = data->quat[1];
	imu_data->orientation.y = data->quat[2];
	imu_data->orientation.z = data->quat[3];
	imu_data->orientation.w = data->quat[0];
	imu_data->angular_velocity.x = data->gyr[0] * DEG_TO_RAD;
	imu_data->angular_velocity.y = data->gyr[1] * DEG_TO_RAD;
	imu_data->angular_velocity.z = data->gyr[2] * DEG_TO_RAD;
	imu_data->linear_acceleration.x = data->acc[0] * GRA_ACC;
	imu_data->linear_acceleration.y = data->acc[1] * GRA_ACC;
	imu_data->linear_acceleration.z = data->acc[2] * GRA_ACC;

	double roll, pitch, yaw;
    tf::Quaternion my_quat;
    geometry_msgs::Quaternion quat;
    quat.w = data->quat[0];
    quat.x = data->quat[1];
    quat.y = data->quat[2];
    quat.z = data->quat[3];
 
//   printf("%f  %f   %f   %f\r\n",quat.w,quat.x,quat.y,quat.z);

   if(quat.w == 0 && quat.x == 0 && quat.y == 0 && quat.z == 0) return false;   

    tf::quaternionMsgToTF(quat, my_quat);
    tf::Matrix3x3(my_quat).getRPY(roll, pitch, yaw);

	RPY_msg->data.push_back(roll*180/3.1415926);
	RPY_msg->data.push_back(pitch*180/3.1415926);
	RPY_msg->data.push_back(yaw*180/3.1415926);
   return true;
}



