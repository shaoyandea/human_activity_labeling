/*
Copyright (C) 2012 Hema Koppula
*/

#include "./frame.h"
#include "./pointcloudClustering.h"
#include "featuresRGBD_skel.cpp"
#include "features.cpp"


ObjectProfile::ObjectProfile(vector<double> &feats,
                             pcl::PointCloud<PointT> &fullCloud,
                             map<int, int> &tablePoints, int id,
                             string transFile) {
  features = feats;
  transformfile = transFile;
  minX = features.at(2);
  minY = features.at(3);
  maxX = features.at(4);
  maxY = features.at(5);
  objID = id;
  getObjectPointCloud(fullCloud, tablePoints);
  initialize();
}

ObjectProfile::ObjectProfile(vector<double> &feats,
                             pcl::PointCloud<PointT> &fullCloud,
                             map<int, int> &tablePoints, int id) {
  features = feats;
  transformfile = "";
  minX = features.at(2);
  minY = features.at(3);
  maxX = features.at(4);
  maxY = features.at(5);
  objID = id;
  getObjectPointCloud(fullCloud, tablePoints);
  initialize();
}

ObjectProfile::ObjectProfile(vector<double> &feats,
                             pcl::PointCloud<PointT> &fullCloud,
                             map<int, int> &tablePoints, int id,
                             string transFile, vector<int> &PCInds) {
  features = feats;
  transformfile = transFile;
  minX = features.at(2);
  minY = features.at(3);
  maxX = features.at(4);
  maxY = features.at(5);
  objID = id;
  if (PCInds.size() > 10) {
    getObjectPointCloud(fullCloud, PCInds);
    initialize();
    pcInds = PCInds;
  }
}

void ObjectProfile::initialize() {
  Eigen::Matrix3d eigen_vectors;
  Eigen::Vector3d eigen_values;
  computeCentroid();
  computeCenter();
  Eigen::Matrix3d covariance_matrix;
  computeCovarianceMatrix(cloud, covariance_matrix, centroid);
  for (unsigned int i = 0; i < 3; i++) {
    for (unsigned int j = 0; j < 3; j++) {
      covariance_matrix(i, j) /= static_cast<double> (cloud.points.size());
    }
  }
  Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> ei_symm(covariance_matrix);
  eigen_values = ei_symm.eigenvalues();
  eigen_vectors = ei_symm.eigenvectors();

  setEigValues(eigen_values);
  float minEigV = FLT_MAX;

  for (int i = 0; i < 3; i++) {
    if (minEigV > eigen_values(i)) {
      minEigV = eigen_values(i);
      normal = eigen_vectors.col(i);
      // check the angle with line joining the centroid to origin
      VectorG centroid_(centroid.x, centroid.y, centroid.z);
      VectorG camera(0, 0, 0);
      VectorG cent2cam = camera.subtract(centroid_);
      VectorG normal_(normal[0], normal[1], normal[2]);
      if (normal_.dotProduct(cent2cam) < 0) {
          // flip the sign of the normal
        normal[0] = -normal[0];
        normal[1] = -normal[1];
        normal[2] = -normal[2];
      }
    }
  }
  assert(minEigV == getDescendingLambda(2));
}

void ObjectProfile::setObjectType(string type) {
  objectType = type;
}

string ObjectProfile::getObjectType() {
  return objectType;
}

void ObjectProfile::getObjectPointCloud(pcl::PointCloud<PointT> &fullCloud,
                                        vector<int> &PCInds) {
  int index = 0;
  cloud.height = 1;
  cloud.width = PCInds.size();
  cloud.points.resize(cloud.height * cloud.width);

  int objIndex = 0;
  for (size_t i = 0; i < PCInds.size(); i++) {
    index = PCInds.at(i);
    cloud.points.at(i) = fullCloud.points.at(index);
  }
}

void ObjectProfile::getObjectPointCloud(pcl::PointCloud<PointT> &fullCloud,
                                        map<int, int> &tablePoints) {
  int index = 0;
  cloud.height = 1;
  cloud.width = (maxY - minY + 1)*(maxX - minX + 1);
  cloud.points.resize(cloud.height * cloud.width);
  map<int, int> localTablePoints;
  int objIndex = 0;
  for (int y = minY; y <= maxY; y++) {
    for (int x = minX; x <= maxX; x++) {
      index = y * X_RES + x;
      cloud.points.at(objIndex) = fullCloud.points.at(index);
      if (tablePoints.find(index) != tablePoints.end()) {
        localTablePoints[objIndex] = 1;
      }
      objIndex++;
    }
  }
  filterCloud(localTablePoints);
  char filename[20];
  sprintf(filename, "obj_%d.pcd", objID);
  pcl::io::savePCDFileBinary(filename, cloud);
}

void ObjectProfile::filterCloud(map<int, int> &tablePoints) {
  // remove points too far from the camera (eg walls)
  PointT origin;
  origin.x = 0;
  origin.y = 0;
  origin.z = 0;
  TransformG globalTransform;
  if (transformfile != "") {
    globalTransform = readTranform(transformfile);
    globalTransform.transformPointInPlace(origin);
  }
  vector<int> indices;
  for (int i = 0; i < cloud.size(); i++) {
    double dist_from_cam = sqrt(sqr(origin.x - cloud.points[i].x) +
                                sqr(origin.y - cloud.points[i].y) +
                                sqr(origin.z - cloud.points[i].z));
    if (dist_from_cam < 2500 && dist_from_cam > 500 &&
        tablePoints.find(i) == tablePoints.end()) {
      indices.push_back(i);
    }
  }
  pcl::PointCloud<PointT> temp_cloud;
  temp_cloud = cloud;
  cloud.points.resize(indices.size());
  cloud.width = indices.size();
  for (int i = 0; i < indices.size(); i++) {
    if (i != indices[i])
      cloud.points[i] = temp_cloud.points[indices[i]];
  }
  // cluster and then retain the biggest cluster
  getMaxCluster(cloud);
}

void ObjectProfile::setEigValues(Eigen::Vector3d eigenValues_) {
  eigenValues.clear();
  // Assuming the values are sorted
  assert(eigenValues_(0) <= eigenValues_(1));
  assert(eigenValues_(1) <= eigenValues_(2));

  for (int i = 0; i < 3; i++)
    eigenValues.push_back(eigenValues_(i));
}

float ObjectProfile::getDescendingLambda(int index)  {
  return eigenValues[2 - index];
}

void ObjectProfile::computeCentroid() {
  centroid.x = 0;
  centroid.y = 0;
  centroid.z = 0;
  for (size_t i = 0; i < cloud.points.size(); i++) {
    centroid.x += cloud.points.at(i).x;
    centroid.y += cloud.points.at(i).y;
    centroid.z += cloud.points.at(i).z;
  }
  centroid.x = centroid.x / cloud.points.size();
  centroid.y = centroid.y / cloud.points.size();
  centroid.z = centroid.z / cloud.points.size();
}

double ObjectProfile::getMinDistanceTo(pcl::PointXYZ p) {
  double minDist = 100000000;
  double dist = 0;
  for (size_t i = 0; i < cloud.points.size(); i++) {
    dist = pow(p.x - cloud.points.at(i).x, 2) +
           pow(p.y - cloud.points.at(i).y, 2) +
           pow(p.z - cloud.points.at(i).z, 2);
    if (dist< minDist) {
      minDist = dist;
    }
  }
  return minDist;
}

double ObjectProfile::getDistanceToCentroid(pcl::PointXYZ p) {
  return pow((p.x-centroid.x), 2) + pow((p.y-centroid.y), 2)
         + pow((p.z-centroid.z), 2);
}

pcl::PointXYZ ObjectProfile::getCentroid() {
  pcl::PointXYZ ret;
  ret.x = centroid.x;
  ret.y = centroid.y;
  ret.z = centroid.z;
  return ret;
}

void ObjectProfile::setCentroid(pcl::PointXYZ p) {
  centroid.x = p.x;
  centroid.y = p.y;
  centroid.z = p.z;
}

void ObjectProfile::computeCenter() {
  center.x =(maxX+minX)/2;
  center.y = (maxY+minY)/2;
  center.z = 0;
}
pcl::PointXYZ ObjectProfile::getCenter() {
  pcl::PointXYZ ret;
  ret.x =(maxX+minX)/2;
  ret.y = (maxY+minY)/2;
  ret.z = 0;
  return ret;
}

float ObjectProfile::getScatter() {
  return getDescendingLambda(0);
}

float ObjectProfile::getLinearNess()  {
  return (getDescendingLambda(0) - getDescendingLambda(1));
}

float ObjectProfile::getPlanarNess()  {
  return (getDescendingLambda(1) - getDescendingLambda(2));
}

float ObjectProfile::getNormalZComponent() {
  return normal[2];
}

float ObjectProfile::getAngleWithVerticalInRadians()  {
  return acos(getNormalZComponent());
}

float ObjectProfile::getHorzDistanceBwCentroids(const ObjectProfile &other) {
  return sqrt(pow(this->centroid.x - other.centroid.x, 2) +
              pow(this->centroid.y - other.centroid.y, 2));
}

float ObjectProfile::getDistanceSqrBwCentroids(const ObjectProfile &other) {
  return pow(centroid.x - other.centroid.x, 2) +
         pow(centroid.y - other.centroid.y, 2) +
         pow(centroid.z - other.centroid.z, 2);
}
float ObjectProfile::getDistanceSqrBwCenters(const ObjectProfile &other) {
  return pow(center.x - other.center.x, 2) +
         pow(center.y - other.center.y, 2);
}

float ObjectProfile::getVertDispCentroids(const ObjectProfile &other) {
  return (centroid.z - other.centroid.z);
}
float ObjectProfile::getXDispCentroids(const ObjectProfile & other) {
  return (centroid.x - other.centroid.x);
}
float ObjectProfile::getYDispCentroids(const ObjectProfile & other) {
  return (centroid.y - other.centroid.y);
}

float ObjectProfile::getVertDispCenters(const ObjectProfile & other) {
  return (center.y - other.center.y);
}

float ObjectProfile::getHDiffAbs(const ObjectProfile & other) {
  return fabs(avgH - other.avgH);
}

float ObjectProfile::getSDiff(const ObjectProfile & other) {
  return (avgS - other.avgS);
}

float ObjectProfile::getVDiff(const ObjectProfile & other) {
  return (avgV - other.avgV);
}


float ObjectProfile::getNormalDotProduct(const ObjectProfile & other) {
  return fabs(normal(0) * other.normal(0) +
              normal(1) * other.normal(1) +
              normal(2) * other.normal(2));
}

float ObjectProfile::getInnerness(const ObjectProfile &other) {
  float r1 = sqrt(centroid.x * centroid.x + centroid.y * centroid.y);
  float r2 = sqrt(other.centroid.x * other.centroid.x +
                  other.centroid.y * other.centroid.y);
  return r1 - r2;
}

float ObjectProfile::pushHogDiffFeats(const ObjectProfile &other,
                                      vector<float> &feats) {
  avgHOGFeatsOfObject.pushBackAllDiffFeats(other.avgHOGFeatsOfObject, feats);
}

float ObjectProfile::getCoplanarity(const ObjectProfile &other) {
  float dotproduct = getNormalDotProduct(other);
  // if the segments are coplanar return the displacement
  // between centroids in the direction of the normal
  if (fabs(dotproduct) > 0.9) {
    float distance = (centroid.x - other.centroid.x) * normal[0] +
                     (centroid.y - other.centroid.y) * normal[1] +
                     (centroid.z - other.centroid.z) * normal[2];
    if (distance == 0 || fabs(distance) < (1 / 1000)) {
      return 1000;
    }
    return fabs(1 / distance);
  } else {
    return -1;
  }
}

void ObjectProfile::printFeatures() {
  for (size_t i = 0; i < features.size(); i++) {
    cout << features.at(i) << ",";
  }
  cout << endl;
}

void  ObjectProfile::setFeatures(vector<double> &feat) {
  features.clear();
  features = feat;
}





void Frame::createPointCloud(int ***IMAGE, string transformfile) {
  int index = 0;
  ColorRGB color(0, 0, 0);
  cloud.height = 1;
  cloud.width = X_RES*Y_RES;
  cloud.points.resize(cloud.height * cloud.width);

  for (int y = 0; y < Y_RES; y++) {
    for (int x = 0; x < X_RES; x++) {
      color.assignColor(static_cast<float>(IMAGE[x][y][0]) / 255.0,
                        static_cast<float>(IMAGE[x][y][1]) / 255,
                        static_cast<float>(IMAGE[x][y][2]) / 255);
      cloud.points.at(index).y = IMAGE[x][y][3];
      cloud.points.at(index).x = (x - 640 * 0.5) *
                                   cloud.points.at(index).y * 1.1147 / 640;
      cloud.points.at(index).z = (480 * 0.5 - y) *
                                   cloud.points.at(index).y * 0.8336 / 480;
      cloud.points.at(index).rgb = color.getFloatRep();
      index++;
    }
  }
  // find table indices
  if (findTable) {
    pcl::PointIndices tablePointInds;
    getTableInds(cloud, tablePointInds);
    cout << "size of table :" << tablePointInds.indices.size() << endl;
    for (size_t i = 0; i < tablePointInds.indices.size(); i++)
      tablePoints[tablePointInds.indices.at(i)] = 1;
  }
  TransformG globalTransform;
  globalTransform = readTranform(transformfile);
  globalTransform.transformPointCloudInPlaceAndSetOrigin(cloud);
}

void Frame::createPointCloud(int ***IMAGE) {
  int index = 0;
  ColorRGB color(0, 0, 0);

  cloud.height = 1;
  cloud.width = X_RES*Y_RES;
  cloud.points.resize(cloud.height * cloud.width);

  for (int y = 0; y < Y_RES; y++) {
    for (int x = 0; x < X_RES; x++) {
      color.assignColor(static_cast<float>(IMAGE[x][y][0]) / 255.0,
                        static_cast<float>(IMAGE[x][y][1]) / 255,
                        static_cast<float>(IMAGE[x][y][2]) / 255);
      cloud.points.at(index).y = IMAGE[x][y][3];
      cloud.points.at(index).x = (x - 640 * 0.5) *
                                    cloud.points.at(index).y * 1.1147 / 640;
      cloud.points.at(index).z = (480 * 0.5 - y) *
                                    cloud.points.at(index).y * 0.8336 / 480;
      cloud.points.at(index).rgb = color.getFloatRep();
      index++;
    }
  }
  // find table indices
  if (findTable) {
      pcl::PointIndices tablePointInds;
      getTableInds(cloud, tablePointInds);
      cout << "size of table :" << tablePointInds.indices.size() << endl;
      for (size_t i = 0; i < tablePointInds.indices.size(); i++)
        tablePoints[tablePointInds.indices.at(i)] = 1;
  }
}

/* This function takes a HOG object and aggregates the HOG
 features for each stripe in the chunk.
 It populates aggHogVec with one HOGFeaturesOfBlock
 object for each stripe in the image. */
void Frame::computeAggHogBlock(int numStripes, int minXBlock, int maxXBlock,
                               int minYBlock, int maxYBlock,
                               HOGFeaturesOfBlock &hogObject) {
  // The number of blocks in a single column of
  // blocks in one stripe in the image.
  double stripeSize = (static_cast<double>(maxYBlock - minYBlock)) / numStripes;

  for (int n = 0; n < numStripes; n++) {
    // For each stripe, create a new HOGFeaturesOfBlock vector hogvec
    // and fill it with the HOGFeaturesOfBlocks in this stripe.
    std::vector<HOGFeaturesOfBlock> hogvec;
    for (int j =  static_cast<int> (minYBlock + n * stripeSize);
          j <=  static_cast<int> (minYBlock + (n + 1) * stripeSize); j++) {
      for (int i = minXBlock; i <= maxXBlock; i++) {
        HOGFeaturesOfBlock hfob;
        hog.getFeatVec(j, i, hfob);
        hogvec.push_back(hfob);
      }
    }
    // Now compute the aggregate features for this stripe,
    // and store the aggregate as its own
    // HOGFeaturesOfBlock in the aggHogVec vector.
    HOGFeaturesOfBlock::aggregateFeatsOfBlocks(hogvec, hogObject);
  }
}

void Frame::computeObjectHog() {
  const int numStripes = 1;
  // for each object
  int count = 0;
  for (vector<ObjectProfile>::iterator it = objects.begin();
        it != objects.end(); it++) {
    count++;
    int minXBlock = static_cast<int> ((*it).minX / BLOCK_SIDE);
    int minYBlock = static_cast<int> ((*it).minY / BLOCK_SIDE);
    int maxXBlock = static_cast<int> ((*it).maxX / BLOCK_SIDE);
    int maxYBlock = static_cast<int> ((*it).maxY / BLOCK_SIDE);
    computeAggHogBlock(numStripes, minXBlock, maxXBlock, minYBlock, maxYBlock,
                       it->avgHOGFeatsOfObject);
  }
}

void Frame::computeHogDescriptors() {
  CvSize size;
  size.height = Y_RES;
  size.width = X_RES;
  IplImage * image = cvCreateImage(size, IPL_DEPTH_32F, 3);
  assert(cloud.size() == size.width * size.height);
  PointT tmp;
  for (int x = 0; x < size.width; x++)
    for (int y = 0; y < size.height; y++) {
      int index = x + y * size.width;
      tmp = cloud.points[index];
      ColorRGB tmpColor(tmp.rgb);
      CV_IMAGE_ELEM(image, float, y, 3 * x) = tmpColor.b;
      CV_IMAGE_ELEM(image, float, y, 3 * x + 1) = tmpColor.g;
      CV_IMAGE_ELEM(image, float, y, 3 * x + 2) = tmpColor.r;
    }
  hog.computeHog(image);
  cvReleaseImage(&image);
}



void Frame::printHOGFeats() {
  int count = 0;
  for (std::vector<ObjectProfile>::iterator it = objects.begin();
        it != objects.end(); it++) {
    count++;
    cout << "HOG feats for obj " << count << ":" << endl;
    for (int i = 0; i < HOGFeaturesOfBlock::numFeats; i++) {
      cout << it->avgHOGFeatsOfObject.feats[i] << ",";
    }
    cout << endl;
  }
}

void Frame::savePointCloud() {
  pcl::io::savePCDFileBinary("test_pcd.pcd", cloud);
}

void Frame::saveObjImage(ObjectProfile & obj, int ***IMAGE) {
  CvSize size;
  size.height = obj.maxY - obj.minY;
  size.width = obj.maxX - obj.minX;
  IplImage * image = cvCreateImage(size, IPL_DEPTH_32F, 3);
  PointT tmp;
  for (int x = 0; x < size.width; x++)
    for (int y = 0; y < size.height; y++) {
      int index = (obj.minX + x) + (obj.minY + y) * X_RES;
      tmp = cloud.points[index];
      ColorRGB tmpColor(tmp.rgb);
      CV_IMAGE_ELEM(image, float, y, 3 * x) = tmpColor.b;
      CV_IMAGE_ELEM(image, float, y, 3 * x + 1) = tmpColor.g;
      CV_IMAGE_ELEM(image, float, y, 3 * x + 2) = tmpColor.r;
    }

  char filename[30];
  sprintf(filename, "image_obj_%d.png", obj.objID);
  HOG::saveFloatImage(filename, image);
  cvReleaseImage(&image);
}

void Frame::saveImage() {
  CvSize size;
  size.height = Y_RES;
  size.width = X_RES;
  IplImage * image = cvCreateImage(size, IPL_DEPTH_32F, 3);
  assert(cloud.size() == size.width * size.height);
  PointT tmp;
  for (int x = 0; x < size.width; x++)
    for (int y = 0; y < size.height; y++) {
      int index = x + y * size.width;
      tmp = cloud.points[index];
      ColorRGB tmpColor(tmp.rgb);
      CV_IMAGE_ELEM(image, float, y, 3 * x) = tmpColor.b;
      CV_IMAGE_ELEM(image, float, y, 3 * x + 1) = tmpColor.g;
      CV_IMAGE_ELEM(image, float, y, 3 * x + 2) = tmpColor.r;
    }

  char filename[30];
  sprintf(filename, "image.png");
  HOG::saveFloatImage(filename, image);
  cvReleaseImage(&image);
}


Frame::Frame() {
}

Frame::Frame(int ***IMAGE, double** data, double **pos_data,
             vector<vector<double> > &objFeats, string seqId, int fnum,
             string transformfile) {
  frameNum = fnum;
  sequenceId = seqId;
  findTable = true;
  createPointCloud(IMAGE, transformfile);
  savePointCloud();
  skeleton.initialize(data, pos_data, transformfile);
  int count = 0;
  for (vector<vector<double> >::iterator it = objFeats.begin();
        it != objFeats.end(); it++) {
    count++;
    ObjectProfile tmp(*it, cloud, tablePoints, count, transformfile);
    objects.push_back(tmp);
  }
  computeHogDescriptors();
  computeObjectHog();
}

Frame::Frame(int ***IMAGE, double** data, double **pos_data,
             vector<vector<double> > &objFeats, string seqId, int fnum) {
  frameNum = fnum;
  sequenceId = seqId;
  findTable = false;
  createPointCloud(IMAGE);
  skeleton.initialize(data, pos_data);
  FeaturesSkelRGBD *features_skeleton_rgbd = new FeaturesSkelRGBD(false);
  int numFeats;
  rgbdskel_feats = features_skeleton_rgbd->computeFeatures(IMAGE, data,
                                                           pos_data, numFeats,
                                                           true, true, true,
                                                           true, true, true,
                                                           true, true, true);
  int count = 0;
  for (vector<vector<double> >::iterator it = objFeats.begin();
       it != objFeats.end(); it++) {
    count++;
    ObjectProfile tmp(*it, cloud, tablePoints, count);
    objects.push_back(tmp);
  }
  computeHogDescriptors();
  computeObjectHog();
}

Frame::Frame(int ***IMAGE, double** data, double **pos_data,
             vector<vector<double> > &objFeats, string seqId, int fnum,
             string transformfile, vector<vector<int> > &objPCInds) {
  frameNum = fnum;
  sequenceId = seqId;
  findTable = false;
  createPointCloud(IMAGE, transformfile);
  skeleton.initialize(data, pos_data, transformfile);
  int count = 0;
  for (size_t i = 0; i < objFeats.size(); i++) {
    count++;
    ObjectProfile tmp(objFeats.at(i), cloud, tablePoints, count, transformfile,
                      objPCInds.at(i));
    objects.push_back(tmp);
  }
  computeHogDescriptors();
  computeObjectHog();
}

Frame::Frame(int ***IMAGE, double** data, double **pos_data,
             vector<vector<double> > &objFeats, string seqId, int fnum,
             string transformfile, vector<vector<int> > &objPCInds,
             bool partial) {
  frameNum = fnum;
  sequenceId = seqId;
  findTable = false;
  createPointCloud(IMAGE, transformfile);
  if (partial) {
    skeleton.initialize_partial(data, pos_data, transformfile);
  } else {
    skeleton.initialize(data, pos_data, transformfile);
  }
  int count = 0;
  for (size_t i = 0; i < objFeats.size(); i++) {
    count++;
    ObjectProfile tmp(objFeats.at(i), cloud, tablePoints, count, transformfile,
                      objPCInds.at(i));
    objects.push_back(tmp);
  }

  computeHogDescriptors();
  computeObjectHog();
}

Frame::Frame(int ***IMAGE, double** data, double **pos_data,
             vector<vector<double> > &objFeats, string seqId, int fnum,
             string transformfile, vector<vector<int> > &objPCInds,
             vector<string> types) {
  frameNum = fnum;
  sequenceId = seqId;
  findTable = false;
  createPointCloud(IMAGE, transformfile);
  skeleton.initialize_partial(data, pos_data, transformfile);
  int count = 0;
  for (size_t i = 0; i < objFeats.size(); i++) {
    count++;
    ObjectProfile tmp(objFeats.at(i), cloud, tablePoints, count,
                      transformfile, objPCInds.at(i));
    tmp.setObjectType(types.at(i));
    objects.push_back(tmp);
  }
  computeHogDescriptors();
  computeObjectHog();
}

Frame::~Frame() {}
