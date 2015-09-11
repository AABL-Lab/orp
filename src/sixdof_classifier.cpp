#include "orp/classifier/sixdof_classifier.h"

/**
 * Starts up the name and handles command-line arguments.
 * @param  argc num args
 * @param  argv args
 * @return      1 if all is well.
 */
int main(int argc, char **argv)
{
  srand (static_cast <unsigned> (time(0)));

  ros::init(argc, argv, "sixdof_classifier");
  ros::NodeHandle n;

  if(argc < 3) {
    ROS_FATAL("proper usage is 'sixdof_classifier data_directory object_list_file");
    return -1;
  }
  std::string directory = argv[1];
  std::string listFile = argv[2];

  ROS_INFO("Starting SixDOF Classifier");
  SixDOFClassifier v(n, directory, listFile);
  v.init();

  ros::spin();
  return 1;
} //main

SixDOFClassifier::SixDOFClassifier(ros::NodeHandle nh, std::string dataFolder, std::string path):
  Classifier(nh, 10000, "sixdof", path, dataFolder, ".cvfh")
{
} //SixDOFClassifier

bool SixDOFClassifier::loadHist(const boost::filesystem::path &path, FeatureVector &sixdof) {
  //ROS_INFO("Loading histogram %s", path.string().c_str());
  //path is the location of the file being read.
  int sixdof_idx;
  KnownPose kp;
  // Load the file as a PCD

  // Treat the SixDOF signature as a single Point Cloud
  pcl::PointCloud <pcl::VFHSignature308> point;
  if(pcl::io::loadPCDFile(path.string(), point) == -1) {
    ROS_ERROR("Couldn't read file %s", path.string().c_str());
    return false;
  }
  sixdof.second.resize(308);

  for (size_t i = 0; i < sixdof.second.size(); ++i)
  {
    sixdof.second[i] = point.points[0].histogram[i];
  }

  kp.name = path.filename().string();
  kp.dataName.assign(kp.name.begin()+kp.name.rfind("/")+1, kp.name.end());
  kp.name.assign(kp.name.begin()+kp.name.rfind("/")+1, kp.name.begin()+kp.name.rfind("_"));
  std::string cloud_name = path.string();

  std::string cloudName;
  cloudName.assign(cloud_name.begin(), cloud_name.begin()+cloud_name.rfind("."));
  cloudName += ".pcd";
  //ROS_INFO("loading cloud from file %s", cloudName.c_str());
  if (pcl::io::loadPCDFile<ORPPoint> (cloudName, *(kp.cloud)) == -1) //* load the file
  {
    ROS_ERROR ("Couldn't read file %s", cloudName.c_str());
    return false;
  }

  std::string matName;
  matName.assign(cloud_name.begin(), cloud_name.begin()+cloud_name.rfind("."));
  matName += ".mat4";
  //ROS_INFO("loading mat from file %s", matName.c_str());
  kp.pose = ORPUtils::loadEigenMatrix4f(matName.c_str()).cast<double>();

  //ROS_INFO("loading centroid");
  kp.centroid = Eigen::Vector4f(kp.pose(0,3), kp.pose(1,3), kp.pose(2,3), kp.pose(3,3));
  //pcl::compute3DCentroid(*(kp.cloud), kp.centroid);

  std::string crhName;
  crhName.assign(cloud_name.begin(), cloud_name.begin()+cloud_name.rfind("."));
  crhName += ".crh";
  //ROS_INFO("loading CRH from file %s", cloudName.c_str());
  if(pcl::io::loadPCDFile(crhName, *(kp.crh)) == -1) {
    ROS_ERROR("Couldn't read file %s", crhName.c_str());
    return false;
  }

  sixdof.first = kp;
  return true;
} //loadHist

double testLast = 0; // used for debug testing

void SixDOFClassifier::cb_classify(sensor_msgs::PointCloud2 cloud) {
  //ROS_INFO("camera callback");
  orp::Segmentation seg_srv;
  seg_srv.request.scene = cloud;
  //ROS_INFO("SixDOF classifier calling segmentation");
  segmentationClient.call(seg_srv);
  std::vector<sensor_msgs::PointCloud2> clouds = seg_srv.response.clusters;
  //ROS_INFO("SixDOF classifier finished calling segmentation");

  orp::ClassificationResult classRes;

  //ROS_INFO("data size: %d x %d", kData->rows, kData->cols);

  for(std::vector<sensor_msgs::PointCloud2>::iterator eachCloud = clouds.begin(); eachCloud != clouds.end(); eachCloud++) {
    pcl::PointCloud<ORPPoint>::Ptr thisCluster (new pcl::PointCloud<ORPPoint>);
    pcl::fromROSMsg(*eachCloud, *thisCluster);

    //Compute sixdof:
    pcl::CVFHEstimation<ORPPoint, pcl::Normal, pcl::VFHSignature308> cvfh;
    cvfh.setInputCloud (thisCluster);
    //Estimate normals:
    pcl::NormalEstimation<ORPPoint, pcl::Normal> ne;
    ne.setInputCloud (thisCluster);
    pcl::search::KdTree<ORPPoint>::Ptr treeNorm (new pcl::search::KdTree<ORPPoint> ());
    ne.setSearchMethod (treeNorm);
    pcl::PointCloud<pcl::Normal>::Ptr cloud_normals (new pcl::PointCloud<pcl::Normal>);
    ne.setRadiusSearch (0.03);
    ne.compute (*cloud_normals);

    //SixDOF estimation
    cvfh.setInputNormals(cloud_normals);
    cvfh.setSearchMethod(treeNorm);
    pcl::PointCloud<pcl::VFHSignature308>::Ptr cvfhs(new pcl::PointCloud<pcl::VFHSignature308> ());
    cvfh.compute(*cvfhs);

    //Nearest neighbor algorigthm
    FeatureVector histogram;
    histogram.second.resize(308);

    for (size_t i = 0; i < 308; ++i) {
      histogram.second[i] = cvfhs->points[0].histogram[i];
      //does the fact that we only look at index 0 here matter? I think it might.
    }

    int numNeighbors = 5;
    //KNN classification find nearest neighbors based on histogram
    int numFound = nearestKSearch (*kIndex, histogram, numNeighbors, kIndices, kDistances);

    int limit = std::min<int>(numNeighbors, numFound);
    
    for(int j=0; j<limit; j++) {
      // ROS_INFO("SixDOF: Dist(%s@%.2f): %f", subModels.at(kIndices[0][j]).first.dataName.c_str(),
        // subModels.at(kIndices[0][j]).first.angle, kDistances[0][j]);
      ROS_DEBUG("SixDOF: Dist(%s@%.2f): %f", subModels.at(kIndices[0][j]).first.name.c_str(),
        subModels.at(kIndices[0][j]).first.angle, kDistances[0][j]);
    }

    Eigen::Vector4f clusterCentroid;
    pcl::compute3DCentroid(*thisCluster, clusterCentroid);

    pcl::PointCloud<CRH90>::Ptr clusterCRH(new pcl::PointCloud<CRH90>);
    pcl::CRHEstimation<ORPPoint, pcl::Normal, CRH90> clusterCRHGen;
    clusterCRHGen.setInputCloud(thisCluster);
    clusterCRHGen.setInputNormals(cloud_normals);
    clusterCRHGen.setCentroid(clusterCentroid);
    clusterCRHGen.compute(*clusterCRH);

    Eigen::Vector4f viewCentroid = subModels.at(kIndices[0][0]).first.centroid;
    //pcl::compute3DCentroid(*(subModels.at(kIndices[0][0]).first.cloud), viewCentroid);

    pcl::PointCloud<CRH90>::Ptr viewCRH = subModels.at(kIndices[0][0]).first.crh;

    pcl::CRHAlignment<ORPPoint, 90> alignment;
    alignment.setInputAndTargetView(thisCluster, subModels.at(kIndices[0][0]).first.cloud);
    // CRHAlignment works with Vector3f, not Vector4f.
    Eigen::Vector3f viewCentroid3f(viewCentroid[0], viewCentroid[1], viewCentroid[2]);
    Eigen::Vector3f clusterCentroid3f(clusterCentroid[0], clusterCentroid[1], clusterCentroid[2]);
    alignment.setInputAndTargetCentroids(clusterCentroid3f, viewCentroid3f);
   
    // Compute the roll angle(s).
    std::vector<float> angles;
    alignment.computeRollAngle(*clusterCRH, *viewCRH, angles);
   
    if (angles.size() == 0)
    {
      ROS_ERROR("No angles correlated! not detecting this object.");
    }
    else {
      //ROS_INFO("list of correlations:");
      for(int k = 0; k < angles.size(); k++) {
        //ROS_INFO_STREAM("\t" << angles[k] << "degrees");
      }
      Eigen::Affine3d finalPose;
      finalPose(0,3) = clusterCentroid(0)+viewCentroid(0);
      finalPose(1,3) = clusterCentroid(1)+viewCentroid(1);
      finalPose(2,3) = clusterCentroid(2)+viewCentroid(2);

      // CRH rotation:
      
      // get the rotation vector - just the vector to the centroid in object space
      //Eigen::Vector3d rotVec = Eigen::Vector3d(finalPose(0,3), finalPose(1,3), finalPose(2,3));
      Eigen::Vector3d rotVec = Eigen::Vector3d(0.0f, 0.0f, 1.0f);
      rotVec.normalize();
      //ROS_INFO_STREAM("rotation vector for CRH: " << finalPose(0,3) << ", " << finalPose(1,3) << ", " << finalPose(2,3));
      
      // get the rotation amount from CRH and convert from degrees to radians
      double radRotationAmount = 2*M_PI - angles.at(0) * M_PI/180;
      //radRotationAmount = testLast;
      //testLast -= 0.2;
      //ORPUtils::attemptToReloadDoubleParam(n, "/testrotation", radRotationAmount);
      //ROS_INFO_STREAM("rotation amount = " << radRotationAmount);
      
      // create a quaternion that represents rotation around an axis
      Eigen::Quaterniond quat;
      quat = Eigen::AngleAxisd(radRotationAmount, rotVec);
      quat.normalize();
      Eigen::Matrix3d rotMat; rotMat = quat;

      // rotate the object using the quaternion.
      Eigen::Affine3d crhRot; crhRot = Eigen::Affine3d(Eigen::AngleAxisd(radRotationAmount, rotVec));
      finalPose.linear() = rotMat;
      finalPose.linear() *= subModels.at(kIndices[0][0]).first.pose.linear();

      classRes.result.label = subModels.at(kIndices[0][0]).first.name;
      tf::poseEigenToMsg(finalPose, classRes.result.pose.pose);
      

      /*}
      else { //if nothing matches well enough, return "unknown"
        ROS_ERROR("not within threshold, distance was %f", kDistances[0][0]);
        classRes.result.label = "unknown";
        classRes.result.pose.pose.position.x = clusterCentroid(0);
        classRes.result.pose.pose.position.y = clusterCentroid(1);
        classRes.result.pose.pose.position.z = clusterCentroid(2);
      }*/

      classRes.method = "sixdof";
      // ROS_INFO("SixDOF: position is %f %f %f",
      //   classRes.result.pose.pose.position.x,
      //   classRes.result.pose.pose.position.y,
      //   classRes.result.pose.pose.position.z);
      classificationPub.publish(classRes);
    }
    delete[] kIndices.ptr();
    delete[] kDistances.ptr();
  }
} //classify*/