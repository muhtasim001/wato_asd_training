#include <chrono>
#include <memory>

#include "map_memory_node.hpp"

MapMemoryNode::MapMemoryNode() : Node("map_memory"), map_memory_(robot::MapMemoryCore(this->get_logger())) {

  //initalize the global costmap
  global_map_ = std::make_shared<nav_msgs::msg::OccupancyGrid>();
  if (!global_map_) {
    RCLCPP_INFO(this->get_logger(),"failed to initalize map");
  }


  global_map_->info.height = GM_Var::y_size;
  global_map_->info.width = GM_Var::x_size;
  global_map_->info.resolution = GM_Var::resolution;
  global_map_->info.origin.position.x = -GM_Var::gg_ofset;
  global_map_->info.origin.position.y = -GM_Var::gg_ofset;
  global_map_->info.origin.orientation.w = 1.0;

  global_map_->data.assign(GM_Var::x_size*GM_Var::y_size,0);

  should_update_map_ = true;
  costmap_updated_ = true;


  RCLCPP_INFO(this->get_logger(),"initalized global map");


  // Initialize subscribers
  costmap_sub_ = this->create_subscription<nav_msgs::msg::OccupancyGrid>(
    "/costmap", 10, std::bind(&MapMemoryNode::costmapCallback, this, std::placeholders::_1));

  odom_sub_ = this->create_subscription<nav_msgs::msg::Odometry>(
    "/odom/filtered", 10, std::bind(&MapMemoryNode::odomCallback, this, std::placeholders::_1));

  // Initialize publisher
  map_pub_ = this->create_publisher<nav_msgs::msg::OccupancyGrid>("/map", 10);

  // Initialize timer
  timer_ = this->create_wall_timer(
      std::chrono::milliseconds(650), std::bind(&MapMemoryNode::updateMap, this));

  RCLCPP_INFO(this->get_logger(),"initalized all subs,pubs and timers for map memory");

  
}

void MapMemoryNode::costmapCallback(const nav_msgs::msg::OccupancyGrid::SharedPtr msg) {
  // Store the latest costmap
  latest_costmap_ = *msg;
  costmap_updated_ = true;
}

void MapMemoryNode::odomCallback(const nav_msgs::msg::Odometry::SharedPtr msg) {
  double x = msg->pose.pose.position.x;
  double y = msg->pose.pose.position.y;

  //keep track of current postion and orientaion
  tv.x = x;
  tv.y = y;
  tv.qw = msg->pose.pose.orientation.w;
  tv.qx = msg->pose.pose.orientation.x;
  tv.qy = msg->pose.pose.orientation.y;
  tv.qz = msg->pose.pose.orientation.z;

  //calculate yaw angle θ=arctan2(2(wz+xy), 1−2(y2+z2))
  //calcuate the sin and cos of angle for transformation
  // Calculate yaw θ from quaternion
  //yaw = math.atan2(2 * (q_w * q_z + q_x * q_y), 1 - 2 * (q_y**2 + q_z**2))
  tv.theta = std::atan2(2.0 * (tv.qw * tv.qz + tv.qx * tv.qy), 1.0 - 2.0 * (std::pow(tv.qy,2) + std::pow(tv.qz,2)));
  
 
  // Compute distance traveled
  double distance = std::sqrt(std::pow(x - last_x, 2) + std::pow(y - last_y, 2));
  if (distance >= distance_threshold) {
    last_x = x;
    last_y = y;
    should_update_map_ = true;
    RCLCPP_INFO(this->get_logger(),"ready to update cost map");
  }
}

void MapMemoryNode::updateMap() {
  if (should_update_map_ && costmap_updated_) {

    integrateCostmap();

    global_map_->header.stamp    = this->now();
    global_map_->header.frame_id = "sim_world";

    map_pub_->publish(*global_map_);
    should_update_map_ = false;
  }
}


void MapMemoryNode::integrateCostmap() {

  //find the size of local cost map
  int x_max = latest_costmap_.info.width;
  int y_max = latest_costmap_.info.height;

  //initalize a global cost map with all unknown
  global_map_2d = std::vector<std::vector<int8_t>>(GM_Var::y_size, 
    std::vector<int8_t>(GM_Var::x_size, -1));
  

  //loop though and convert the local cost map index to gloabl index
  //loop though and convert the local cost map index to gloabl index
  for (int i = 0 ; i < y_max; i++) {
    for (int j = 0; j < x_max; j++) {

      int new_x, new_y;
      convertRobotToWorldIndex(j,i,new_x,new_y);

      //place them in temp global map if they are in the range of global map
      if (new_x < GM_Var::x_size && new_x >= 0 && new_y < GM_Var::y_size && new_y >= 0) {
        global_map_2d[new_y][new_x] = latest_costmap_.data[i * latest_costmap_.info.width + j];
      }

    }
  }

  //step 3 : loop though and temopray 2d vector of global in place 
  //into global cost map message being published
  
  for (int i = 0; i < GM_Var::y_size; i++) {
    for (int j = 0; j < GM_Var::x_size; j++) {

      int index = i * GM_Var::x_size + j;

      //checking if the new one if unknown,than skiping
      if (global_map_2d[i][j] > -1 && global_map_->data[index] < global_map_2d[i][j]) {
        global_map_->data[index] = global_map_2d[i][j];
      }

    }
  }

}

void MapMemoryNode::convertRobotToWorldIndex(int robot_x_index,int robot_y_index,int &world_x_index,int &world_y_index) {
  
  double x_cord =  ((robot_x_index + 0.5) * latest_costmap_.info.resolution) + latest_costmap_.info.origin.position.x;
  double y_cord =  ((robot_y_index + 0.5) * latest_costmap_.info.resolution) + latest_costmap_.info.origin.position.y;

  double world_x_cord = (std::cos(tv.theta) * x_cord - std::sin(tv.theta)* y_cord) + tv.x;
  double world_y_cord = (std::sin(tv.theta) * x_cord + std::cos(tv.theta)* y_cord) + tv.y;

  world_x_index = (int)std::round((world_x_cord - global_map_->info.origin.position.x)/GM_Var::resolution);
  world_y_index = (int)std::round((world_y_cord - global_map_->info.origin.position.y)/GM_Var::resolution);
}


int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<MapMemoryNode>());
  rclcpp::shutdown();
  return 0;
}
