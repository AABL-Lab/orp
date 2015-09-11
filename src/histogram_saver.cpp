#include "orp/collector/histogram_saver.h"

HistogramSaver::HistogramSaver(ros::NodeHandle nh, std::string location) : n(nh), feature(),
  savePCD(false), saveCPH(false), saveVFH(false), saveCVFH(false)
{
  //Offer services that can be called from the terminal:
  SaveCloud_serv = n.advertiseService("save_cloud", &HistogramSaver::cloud_cb, this );
  seg_client = n.serviceClient<orp::Segmentation>("segmentation");
  outDir = location + "/";

  reconfigureCallbackType = boost::bind(&HistogramSaver::paramsChanged, this, _1, _2);
  reconfigureServer.setCallback(reconfigureCallbackType);
}

void HistogramSaver::paramsChanged(
  orp::HistogramSaverConfig &config, uint32_t level)
{

  savePCD = config.save_pcd;
  saveCPH = config.save_cph;
  saveVFH = config.save_vfh;
  saveCVFH = config.save_cvfh;
  save6DOF = config.save_sixdof;

  cvfhRadiusSearch = config.cvfh_radius_search;
  vfhRadiusSearch = config.vfh_radius_search;
  cphVerticalBins = config.cph_vertical_bins;
  cphRadialBins = config.cph_radial_bins;
} //paramsChanaged

bool HistogramSaver::cloud_cb(orp::SaveCloud::Request &req,
  orp::SaveCloud::Response &res)   
{
  //Segment from cloud:
  std::vector<pcl::PointCloud<ORPPoint>::Ptr> clouds;
  orp::Segmentation seg_srv;
  seg_srv.request.scene = req.in_cloud;

  seg_client.call(seg_srv);
  
  if(seg_srv.response.clusters.size() < 1) {
    ROS_ERROR("no points returned from segmentation node.");
    return true;
  }

  pcl::PointCloud<ORPPoint>::Ptr cluster (new pcl::PointCloud<ORPPoint>);
  //cluster = clouds.at(0);
  pcl::fromROSMsg(seg_srv.response.clusters.at(0), *cluster);
  ROS_INFO_STREAM(req.objectName << " cluster has " << cluster->height*cluster->width << " points.");


  if(!savePCD && !saveCPH && !saveVFH && !saveCVFH && !save6DOF) {
    ROS_WARN("Not saving any types of output files. Use rqt_reconfigure to turn on output.");
  }

  if(savePCD) {
    writeRawCloud(cluster, req.objectName, req.angle);
  }
  if(saveCPH) {
    writeCPH(cluster, req.objectName, req.angle);
  }
  if(saveVFH) {
    writeVFH(cluster, req.objectName, req.angle);
  }
  if(saveCVFH) {
    writeCVFH(cluster, req.objectName, req.angle);
  }
  if(save6DOF) {
    write6DOF(cluster, req.objectName, req.angle);
  }
} //cloud_cb

void HistogramSaver::writeRawCloud(pcl::PointCloud<ORPPoint>::Ptr cluster, std::string name,
  int angle)
{
  std::stringstream fileName_ss;
  fileName_ss << outDir << name.c_str() << "_" << angle << ".pcd";

  char thepath3[200];
  realpath(fileName_ss.str().c_str(), thepath3);

  //Write raw pcd file (objecName_angle.pcd)
  ROS_INFO_STREAM("writing raw cloud to file '" << thepath3 << "'");
  pcl::io::savePCDFile(thepath3, *cluster);
  ROS_DEBUG("done");
}

void HistogramSaver::writeVFH(pcl::PointCloud<ORPPoint>::Ptr cluster, std::string name,
  int angle)
{
  pcl::VFHEstimation<ORPPoint, pcl::Normal, pcl::VFHSignature308> vfh;
  vfh.setInputCloud (cluster);

  //Estimate normals:
  pcl::NormalEstimation<ORPPoint, pcl::Normal> ne;
  ne.setInputCloud (cluster);
  pcl::search::KdTree<ORPPoint>::Ptr tree (new pcl::search::KdTree<ORPPoint> ());
  ne.setSearchMethod (tree);
  pcl::PointCloud<pcl::Normal>::Ptr cloud_normals (new pcl::PointCloud<pcl::Normal>);
  ne.setRadiusSearch (vfhRadiusSearch);
  ne.compute (*cloud_normals);
  vfh.setInputNormals (cloud_normals);

  //Estimate vfh:
  vfh.setSearchMethod (tree);
  pcl::PointCloud<pcl::VFHSignature308>::Ptr vfhs (new pcl::PointCloud<pcl::VFHSignature308> ());

  // Compute the feature
  vfh.compute (*vfhs);

  //Write to file: (objectName_angle_vfh.pcd)
  std::stringstream fileName_ss;
  fileName_ss << outDir << name.c_str() << "_" << angle << ".vfh";

  char thepath[200];
  realpath(fileName_ss.str().c_str(), thepath);

  ROS_INFO_STREAM("Writing VFH to file '" << thepath << "'...");
  pcl::io::savePCDFile(thepath, *vfhs);
  ROS_DEBUG("done");
} //writeVFH

void HistogramSaver::writeCPH(pcl::PointCloud<ORPPoint>::Ptr cluster, std::string name,
  int angle)
{
  //Extract cph
  feature.clear();

  CPH cph(cphVerticalBins, cphRadialBins);
  cph.setInputCloud(cluster);
  cph.compute(feature);
  std::stringstream fileName_ss;
  fileName_ss << outDir << name.c_str() << "_" << angle << ".cph";

  char thepath2[200];
  realpath(fileName_ss.str().c_str(), thepath2);

  //Write cph to file. (objectName_angle.csv)
  ROS_INFO_STREAM("Writing CPH to file '" << thepath2 << "'");
  std::ofstream outFile;
  outFile.open(thepath2);

  for(unsigned int j=0; j<feature.size(); j++){
    outFile << feature.at(j) << " "; 
  }
  outFile.close();
  fileName_ss.str("");
  ROS_DEBUG("done");
} //writeCPH

void HistogramSaver::writeCVFH(pcl::PointCloud<ORPPoint>::Ptr cluster, std::string name,
  int angle)
{
  pcl::CVFHEstimation<ORPPoint, pcl::Normal, pcl::VFHSignature308> cvfh;
  cvfh.setInputCloud (cluster);

  //ROS_INFO("normals");
  //Estimate normals:
  pcl::NormalEstimation<ORPPoint, pcl::Normal> ne;
  ne.setInputCloud (cluster);
  pcl::search::KdTree<ORPPoint>::Ptr tree (new pcl::search::KdTree<ORPPoint> ());
  ne.setSearchMethod (tree);
  pcl::PointCloud<pcl::Normal>::Ptr cloud_normals (new pcl::PointCloud<pcl::Normal>);
  ne.setRadiusSearch (cvfhRadiusSearch);
  ne.compute (*cloud_normals);
  cvfh.setInputNormals (cloud_normals);

  //ROS_INFO("prepare to cvfh");
  //Estimate cvfh:
  cvfh.setSearchMethod (tree);
  pcl::PointCloud<pcl::VFHSignature308>::Ptr cvfhs (new pcl::PointCloud<pcl::VFHSignature308> ());

  // Compute the feature
  //ROS_INFO("cvfh");
  cvfh.compute (*cvfhs);

  //ROS_INFO("prepare to write");
  //Write to file: (objectName_angle.cvfh)
  std::stringstream fileName_ss;
  fileName_ss << outDir.c_str() << name.c_str() << "_" << angle << ".cvfh";

  ROS_INFO_STREAM("Writing CVFH to file '" << fileName_ss.str().c_str() << "'...");
  pcl::io::savePCDFile(fileName_ss.str(), *cvfhs);
} //writeCVFH

//http://robotica.unileon.es/mediawiki/index.php/PCL/OpenNI_tutorial_5:_3D_object_recognition_(pipeline)
typedef pcl::Histogram<90> CRH90;
POINT_CLOUD_REGISTER_POINT_STRUCT (CRH90, (float[90], histogram, histogram) )

void HistogramSaver::write6DOF(pcl::PointCloud<ORPPoint>::Ptr cluster, std::string name, int num)
{
  pcl::CVFHEstimation<ORPPoint, pcl::Normal, pcl::VFHSignature308> cvfh;
  cvfh.setInputCloud (cluster);

  //ROS_INFO("normals");
  //Estimate normals:
  pcl::NormalEstimation<ORPPoint, pcl::Normal> ne;
  ne.setInputCloud (cluster);
  pcl::search::KdTree<ORPPoint>::Ptr tree (new pcl::search::KdTree<ORPPoint> ());
  ne.setSearchMethod (tree);
  pcl::PointCloud<pcl::Normal>::Ptr cloud_normals (new pcl::PointCloud<pcl::Normal>);
  ne.setRadiusSearch (cvfhRadiusSearch);
  ne.compute (*cloud_normals);
  cvfh.setInputNormals (cloud_normals);

  //ROS_INFO("prepare to cvfh");
  //Estimate cvfh:
  cvfh.setSearchMethod (tree);
  pcl::PointCloud<pcl::VFHSignature308>::Ptr cvfhs (new pcl::PointCloud<pcl::VFHSignature308> ());

  // Compute the feature
  //ROS_INFO("cvfh");
  cvfh.compute (*cvfhs);

  //ROS_INFO("prepare to write");
  //Write to file: (objectName_angle.cvfh)
  std::stringstream fileName_ss;
  fileName_ss << outDir.c_str() << "sixdof/" << name.c_str() << "_" << num << ".cvfh";

  ROS_INFO_STREAM("Writing 6DOF CVFH to file '" << fileName_ss.str().c_str() << "'...");
  pcl::io::savePCDFile(fileName_ss.str(), *cvfhs);

  //CRH/////////////////////////////////////////////////////////////
  // CRH estimation object.
  pcl::CRHEstimation<ORPPoint, pcl::Normal, CRH90> crh;
  crh.setInputCloud(cluster);
  crh.setInputNormals(cloud_normals);

  Eigen::Vector4f centroid;
  pcl::compute3DCentroid(*cluster, centroid);
  crh.setCentroid(centroid);

  pcl::PointCloud<CRH90>::Ptr histogram(new pcl::PointCloud<CRH90>);
  crh.compute(*histogram);

  //http://www.pcl-users.org/Save-pcl-Pointcloud-lt-pcl-Histogram-lt-N-gt-gt-td4035239.html
  //pcl::PointCloud< CRH90 > histogram_cloud; 
  //histogram_cloud.push_back(*histogram); 

  fileName_ss.str("");
  fileName_ss.clear();
  fileName_ss << outDir << "sixdof/" << name << "_" << num << ".crh";
  ROS_INFO_STREAM("Writing 6DOF CRH to '" << fileName_ss.str().c_str() << "'...");
  pcl::io::savePCDFile(fileName_ss.str(), *histogram); 
} //write6DOF