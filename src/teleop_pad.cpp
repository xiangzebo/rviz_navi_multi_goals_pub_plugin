#include <stdio.h>

#include <QPainter>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>

#include <math.h>
#include <QDebug>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QTableWidgetItem>
#include <tiff.h>
#include <zconf.h>

#include "teleop_pad.h"

namespace navi_multi_goals_pub_rviz_plugin {

// 构造函数，初始化变量
    TeleopPanel::TeleopPanel(QWidget *parent)
            : rviz::Panel(parent), nh_(), maxNumGoal_(1) {

        goal_sub_ = nh_.subscribe<geometry_msgs::PoseStamped>("move_base_simple/goal_temp", 1,
                                                              boost::bind(&TeleopPanel::goalCntCB, this, _1));

        status_sub_ = nh_.subscribe<actionlib_msgs::GoalStatusArray>("move_base/status", 1,
                                                                     boost::bind(&TeleopPanel::statusCB, this, _1));

        goal_pub_ = nh_.advertise<geometry_msgs::PoseStamped>("move_base_simple/goal", 1);

        cancel_pub_ = nh_.advertise<actionlib_msgs::GoalID>("move_base/cancel", 1);
        QVBoxLayout *root_layout = new QVBoxLayout;
        // 创建一个输入maxNumGoal命名的窗口
        QHBoxLayout *maxNumGoal_layout = new QHBoxLayout;
        maxNumGoal_layout->addWidget(new QLabel("目标最大数量"));
        output_maxNumGoal_editor_ = new QLineEdit;
        maxNumGoal_layout->addWidget(output_maxNumGoal_editor_);
        output_maxNumGoal_button_ = new QPushButton("确定");
        maxNumGoal_layout->addWidget(output_maxNumGoal_button_);
        root_layout->addLayout(maxNumGoal_layout);

        cycle_checkbox_ = new QCheckBox("循环");
        root_layout->addWidget(cycle_checkbox_);
        // creat a QTable to contain the poseArray
        poseArray_table_ = new QTableWidget;
        initPoseTable();
        root_layout->addWidget(poseArray_table_);
        //creat a manipulate layout
        QHBoxLayout *manipulate_layout = new QHBoxLayout;
        output_reset_button_ = new QPushButton("重置");
        manipulate_layout->addWidget(output_reset_button_);
        output_cancel_button_ = new QPushButton("取消");
        manipulate_layout->addWidget(output_cancel_button_);
        output_startNavi_button_ = new QPushButton("开始导航!");
        manipulate_layout->addWidget(output_startNavi_button_);
        root_layout->addLayout(manipulate_layout);

        setLayout(root_layout);

        QTimer *output_timer = new QTimer(this);
        output_timer->start(200);

        // 设置信号与槽的连接
        connect(output_maxNumGoal_button_, SIGNAL(clicked()), this,
                SLOT(updateMaxNumGoal()));
        connect(output_maxNumGoal_button_, SIGNAL(clicked()), this,
                SLOT(updatePoseTable()));
        connect(output_reset_button_, SIGNAL(clicked()), this, SLOT(initPoseTable()));
        connect(output_cancel_button_, SIGNAL(clicked()), this, SLOT(cancelNavi()));
        connect(output_startNavi_button_, SIGNAL(clicked()), this, SLOT(startNavi()));
        connect(cycle_checkbox_, SIGNAL(clicked(bool)), this, SLOT(checkCycle()));
        connect(output_timer, SIGNAL(timeout()), this, SLOT(startSpin()));

    }


// 更新maxNumGoal命名
    void TeleopPanel::updateMaxNumGoal() {
        setMaxNumGoal(output_maxNumGoal_editor_->text());
    }

// 设置maxNumGoal命名
    void TeleopPanel::setMaxNumGoal(const QString &new_maxNumGoal) {
        // 检查maxNumGoal是否发生改变.
        if (new_maxNumGoal != output_maxNumGoal_) {
            output_maxNumGoal_ = new_maxNumGoal;

            // 如果命名为空，不发布任何信息
            if (output_maxNumGoal_ == "") {
                nh_.setParam("maxNumGoal_", 1);
                maxNumGoal_ = 1;
            } else {
//                velocity_publisher_ = nh_.advertise<geometry_msgs::Twist>(output_maxNumGoal_.toStdString(), 1);
                nh_.setParam("maxNumGoal_", output_maxNumGoal_.toInt());
                maxNumGoal_ = output_maxNumGoal_.toInt();
            }
            Q_EMIT configChanged();
        }
    }

    void TeleopPanel::initPoseTable() {
        ROS_INFO("Initialize");
        curGoalIdx_ = 0, cycleCnt_ = 0;
        permit_ = false, cycle_ = false;
        poseArray_table_->clear();
        pose_array_.poses.clear();
        poseArray_table_->setRowCount(maxNumGoal_);
        poseArray_table_->setColumnCount(3);
        poseArray_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        QStringList pose_header;
        pose_header << "x" << "y" << "yaw";
        poseArray_table_->setHorizontalHeaderLabels(pose_header);
        cycle_checkbox_->setCheckState(Qt::Unchecked);

    }

    void TeleopPanel::updatePoseTable() {
        poseArray_table_->setRowCount(maxNumGoal_);
//        pose_array_.poses.resize(maxNumGoal_);
        QStringList pose_header;
        pose_header << "x" << "y" << "yaw";
        poseArray_table_->setHorizontalHeaderLabels(pose_header);
        poseArray_table_->show();
    }


    void TeleopPanel::goalCntCB(const geometry_msgs::PoseStamped::ConstPtr &pose) {
        if (pose_array_.poses.size() < maxNumGoal_) {
            pose_array_.poses.push_back(pose->pose);
            pose_array_.header.frame_id = pose->header.frame_id;
            writePose(pose->pose);
        } else {
            ROS_ERROR("Beyond the maximum number of goals: %d", maxNumGoal_);
        }
    }


    void TeleopPanel::writePose(geometry_msgs::Pose pose) {

        poseArray_table_->setItem(pose_array_.poses.size() - 1, 0,
                                  new QTableWidgetItem(QString::number(pose.position.x, 'f', 2)));
        poseArray_table_->setItem(pose_array_.poses.size() - 1, 1,
                                  new QTableWidgetItem(QString::number(pose.position.y, 'f', 2)));
        poseArray_table_->setItem(pose_array_.poses.size() - 1, 2,
                                  new QTableWidgetItem(
                                          QString::number(tf::getYaw(pose.orientation) * 180.0 / 3.14, 'f', 2)));
    }

    void TeleopPanel::checkCycle() {
        cycle_ = true;
    }

    void TeleopPanel::startNavi() {
        if (!pose_array_.poses.empty() && curGoalIdx_ < maxNumGoal_) {
            geometry_msgs::PoseStamped goal;
            goal.header = pose_array_.header;
            goal.pose = pose_array_.poses.at(curGoalIdx_);
            goal_pub_.publish(goal);
            ROS_INFO("Navi to the Goal%d", curGoalIdx_ + 1);
            poseArray_table_->item(curGoalIdx_, 0)->setBackgroundColor(QColor(255, 0, 0));
            poseArray_table_->item(curGoalIdx_, 1)->setBackgroundColor(QColor(255, 0, 0));
            poseArray_table_->item(curGoalIdx_, 2)->setBackgroundColor(QColor(255, 0, 0));
            curGoalIdx_ += 1;
            permit_ = true;
        } else {
            ROS_ERROR("Something Wrong");
        }
    }

    void TeleopPanel::completeNavi() {
        if (curGoalIdx_ < pose_array_.poses.size()) {
            geometry_msgs::PoseStamped goal;
            goal.header = pose_array_.header;
            goal.pose = pose_array_.poses.at(curGoalIdx_);
            goal_pub_.publish(goal);
            ROS_INFO("Navi to the Goal%d", curGoalIdx_ + 1);
            poseArray_table_->item(curGoalIdx_, 0)->setBackgroundColor(QColor(255, 0, 0));
            poseArray_table_->item(curGoalIdx_, 1)->setBackgroundColor(QColor(255, 0, 0));
            poseArray_table_->item(curGoalIdx_, 2)->setBackgroundColor(QColor(255, 0, 0));
            curGoalIdx_ += 1;
            permit_ = true;
        } else {
            ROS_ERROR("All goals are completed");
            permit_ = false;
        }
    }

    void TeleopPanel::cycleNavi() {
        if (permit_) {
            geometry_msgs::PoseStamped goal;
            goal.header = pose_array_.header;
            std::cout << curGoalIdx_ % pose_array_.poses.size() << std::endl;
            goal.pose = pose_array_.poses.at(curGoalIdx_ % pose_array_.poses.size());
            goal_pub_.publish(goal);
            ROS_INFO("Navi to the Goal%d, in the %dth cycle", curGoalIdx_ % pose_array_.poses.size() + 1,
                     cycleCnt_ + 1);
            bool even = ((cycleCnt_ + 1) % 2 != 0);
            QColor color_table;
            if (even) color_table = QColor(255, 0, 0); else color_table = QColor(0, 0, 255);
            poseArray_table_->item(curGoalIdx_ % pose_array_.poses.size(), 0)->setBackgroundColor(color_table);
            poseArray_table_->item(curGoalIdx_ % pose_array_.poses.size(), 1)->setBackgroundColor(color_table);
            poseArray_table_->item(curGoalIdx_ % pose_array_.poses.size(), 2)->setBackgroundColor(color_table);
            std::cout << "check" << std::endl;
            curGoalIdx_ += 1;
            cycleCnt_ = curGoalIdx_ / pose_array_.poses.size();
        }
    }

    void TeleopPanel::cancelNavi() {
        if (!cur_goalid_.id.empty()) {
            cancel_pub_.publish(cur_goalid_);
            ROS_ERROR("Navigation have been canceled");
        }
    }

    void TeleopPanel::statusCB(const actionlib_msgs::GoalStatusArray::ConstPtr &statuses) {

        if (checkGoal(statuses->status_list) && ros::ok() && permit_) {
            if (cycle_) cycleNavi();
            else completeNavi();
        }
    }

    bool TeleopPanel::checkGoal(std::vector<actionlib_msgs::GoalStatus> status_list) {
        bool done;
        if (!status_list.empty()) {
            for (int i = 0; i < status_list.size(); ++i) {
                if (status_list[i].status == 3) {
                    done = true;
                    ROS_INFO("completed Goal%d", curGoalIdx_);
                } else if (status_list[i].status == 4) {
                    ROS_ERROR("Goal%d is Invalid, Navi to Next Goal%d", curGoalIdx_, curGoalIdx_ + 1);
                    return true;
                } else if (status_list[i].status == 0) {
                    done = true;
                } else if (status_list[i].status == 1) {
                    cur_goalid_ = status_list[i].goal_id;
                    done = false;
                } else done = false;
            }

        } else {
            ROS_INFO("Please input the Navi Goal");
            done = false;
        }
        return done;
    }

    void TeleopPanel::startSpin() {
        if (ros::ok()) {
            ros::spinOnce();
        }
    }



//// 重载父类的功能
//    void TeleopPanel::save(rviz::Config config) const {
//        rviz::Panel::save(config);
//        config.mapSetValue("Topic", output_topic_);
//    }
//
//
//// 重载父类的功能，加载配置数据
//    void TeleopPanel::load(const rviz::Config &config) {
//        rviz::Panel::load(config);
//        QString topic;
//        if (config.mapGetString("Topic", &topic)) {
//            output_topic_editor_->setText(topic);
//            updateTopic();
//        }
//    }

} // end namespace navi-multi-goals-pub-rviz-plugin

// 声明此类是一个rviz的插件
#include <pluginlib/class_list_macros.h>

PLUGINLIB_EXPORT_CLASS(navi_multi_goals_pub_rviz_plugin::TeleopPanel, rviz::Panel
)
// END_TUTORIAL