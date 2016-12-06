#include <vector>
#include <algorithm>
#include <string>
#include <fstream>
#include <iomanip>

#include <opencv2/opencv.hpp>
#include <opencv/cv.h>
#include <opencv2/highgui/highgui.hpp>

#include <yarp/os/all.h>
#include <yarp/sig/all.h>
#include <yarp/math/Math.h>
#include <yarp/dev/Drivers.h>
#include <iCub/iKin/iKinFwd.h>

#include <yarp/dev/CartesianControl.h>
#include <yarp/dev/PolyDriver.h>
#include <yarp/dev/IAnalogSensor.h>

using namespace std;
using namespace yarp::os;
using namespace yarp::sig;
using namespace yarp::dev;
using namespace yarp::math;
using namespace iCub::iKin;

#include "inHandSegmentation_IDL.h"

class inHandSegmentation : public RFModule,
                           public inHandSegmentation_IDL
{
protected:
    vector<cv::Point> blobPoints;
    vector<Vector> pointsIn;
    vector<Vector> pointsOut;
    vector<Vector> fingers;
    Vector tip_x_init, tip_o_init;
    Vector orientation;
    Vector position;
    Matrix H_hand;

    Mutex mutex;

    string module_name;
    string homeContextPath;
    string savename;
    string fileOutFormat;
    string visionFileName;
    string fingersFileName;
    string poseFileName;
    string poseOutFileName;
    string fingersOutFileName;
    string robot;
    string left_or_right;
    string frame;
    string info;
    string color_space;

    int fileCount;
    int startup_context_id;
    int down;

    int nnThreshold;
    int numVertices;
    int nnThreshold_color;
    int diff_rgb;
    int diff_ycbcr;
    double radius;
    double radius_color;
    double x_lim_up, x_lim_down, y_lim_up, y_lim_down, z_lim_up, z_lim_down;
    double radius_volume;
    double a,b;
    double radius_volume_offset;
    double a_offset, b_offset;
    Vector center_ellips;
    Vector center_volume;

    bool saving;
    bool acquire;
    bool online;
    bool tactile_on;
    bool filter;
    bool change_frame;
    bool coarse_filter;
    bool hand_filter;
    bool density_filter;
    bool cylinder_filter;
    bool ellipse_filter;

    PolyDriver robotDevice;
    PolyDriver robotDevice2;
    PolyDriver analogDevice;

    ICartesianControl *icart_arm;
    ICartesianControl *icart_arm2;
    IEncoders *enc;
    IAnalogSensor *analog;
    Vector encoders;

    Bottle limits;

    Vector contactPoint_index;
    Vector contactPoint_middle;
    Vector contactPoint_thumb;

    IControlLimits* lim;
    deque<IControlLimits*> lim_deque;

    iCubFinger finger_thumb, finger_index, finger_middle;

    BufferedPort<ImageOf<PixelMono> > portDispIn;
    BufferedPort<ImageOf<PixelMono> > portBlobIn;
    BufferedPort<ImageOf<PixelRgb> > portImgIn;

    BufferedPort<ImageOf<PixelRgb> > portDispOut;

    RpcClient portSFM;
    RpcServer portRpc;

    /************************************************************************/
    bool attach(RpcServer &source)
    {
        return this->yarp().attachAsServer(source);
    }

    /*******************************************************************************/
    bool clear_points()
    {
        pointsIn.clear();
        return true;
    }

    /*******************************************************************************/
    bool set_saving(const string &entry)
    {
        if (entry == "yes")
            saving=true;
        else if (entry == "no")
            saving=false;
        else
            return false;

        return true;
    }

    /*******************************************************************************/
    string get_saving()
    {
        if (saving==true)
            return "yes";
        else
            return "no";
    }

    /*******************************************************************************/
    bool set_format(const string &entry)
    {
        if (entry == "off" || entry == "ply")
        {
            fileOutFormat=entry;

            return true;
        }
        else
        {
            return false;
        }
    }

    /*******************************************************************************/
    string get_format()
    {
        return fileOutFormat;
    }

    /*******************************************************************************/
    bool set_filename(const string &entry)
    {
        savename=entry;
        return true;
    }

    /*******************************************************************************/
    string get_filename()
    {
        return savename;
    }

    /*******************************************************************************/
    bool go_acquire()
    {
        acquire=true;
        return true;
    }

    /*******************************************************************************/
    Bottle get_2D_blob_points()
    {
        Bottle bblobs;

        if (blobPoints.size()>0)
        {
            for (size_t i=0; i<blobPoints.size(); i++)
            {
                Bottle &bblob=bblobs.addList();
                bblob.addInt(blobPoints[i].x); bblob.addInt(blobPoints[i].y);
            }
        }

        return bblobs;
    }

    /*******************************************************************************/
    Bottle get_3D_blob_points()
    {
        Bottle bpoints;
        if (pointsIn.size()>0)
        {
            for (size_t i=0; i<pointsIn.size(); i++)
            {
                Vector point=pointsIn[i];
                Bottle &bpoint=bpoints.addList();
                bpoint.addDouble(point[0]); bpoint.addDouble(point[1]); bpoint.addDouble(point[2]); bpoint.addInt(point[3]); bpoint.addInt(point[4]);bpoint.addInt(point[5]);
            }
        }

        return bpoints;
    }

    /***********************************************************************/
    bool set_frame(const string &entry)
    {
        if (entry=="hand" || entry=="robot")
        {
            frame=entry;
            return true;
        }
        else
            return false;
    }

    /***********************************************************************/
    string get_frame()
    {
        return frame;
    }

    /***********************************************************************/
    Bottle get_pose()
    {
        icart_arm->getPose(position,orientation);

        Bottle poseInfo;
        Bottle &positionInfo=poseInfo.addList();
        positionInfo.addString("position");
        positionInfo.addDouble(position[0]); positionInfo.addDouble(position[1]); positionInfo.addDouble(position[2]);
        Bottle &orientationInfo=poseInfo.addList();
        orientationInfo.addString("orientation");
        orientationInfo.addDouble(orientation[0]); orientationInfo.addDouble(orientation[1]); orientationInfo.addDouble(orientation[2]); orientationInfo.addDouble(orientation[3]);
        Bottle &handb=poseInfo.addList();
        handb.addString("hand");
        handb.addString(left_or_right);

        return poseInfo;
    }

    /***********************************************************************/
    Bottle get_tactile_data()
    {
        giveContactPoint(contactPoint_thumb, "thumb");
        giveContactPoint(contactPoint_index, "index");
        giveContactPoint(contactPoint_middle, "middle");

        if (frame == "hand")
        {
            Vector aux(4,1.0);
            aux.setSubvector(0,contactPoint_thumb);           
            aux=H_hand*aux;
            contactPoint_thumb=aux.subVector(0,2);
            aux.setSubvector(0,contactPoint_index);
            aux=H_hand*aux;
            contactPoint_index=aux.subVector(0,2);
            aux.setSubvector(0,contactPoint_middle);
            aux=H_hand*aux;
            contactPoint_middle=aux.subVector(0,2);
        }

        Bottle tactileData;
        Bottle &thumbData=tactileData.addList();
        thumbData.addString("thumb");
        thumbData.addDouble(contactPoint_thumb[0]); thumbData.addDouble(contactPoint_thumb[1]); thumbData.addDouble(contactPoint_thumb[2]);
        Bottle &indexData=tactileData.addList();
        indexData.addString("index");
        indexData.addDouble(contactPoint_index[0]); indexData.addDouble(contactPoint_index[1]); indexData.addDouble(contactPoint_index[2]);
        Bottle &middleData=tactileData.addList();
        middleData.addString("middle");
        middleData.addDouble(contactPoint_middle[0]); middleData.addDouble(contactPoint_middle[1]); middleData.addDouble(contactPoint_middle[2]);
        Bottle &frameb=tactileData.addList();
        frameb.addString("frame");
        frameb.addString(frame);
        Bottle &handb=tactileData.addList();
        handb.addString("hand");
        handb.addString(left_or_right);


        return tactileData;
    }

    /***********************************************************************/
    string get_filters()
    {
        string filters;

        if (coarse_filter)
            filters="CF";
        if (hand_filter)
            filters+=" HF";
        if (density_filter)
            filters+=" DF";
        if (cylinder_filter)
            filters+=" CyF";
        if (ellipse_filter)
            filters+=" EF";


        return filters;
    }

    /***********************************************************************/
    bool set_coarse_filter(const string &entry)
    {
        if (entry=="on" || entry=="off")
        {
            coarse_filter=(entry=="on");
            return true;
        }
        else
            return false;
    }

    /***********************************************************************/
    bool set_hand_filter(const string &entry)
    {
        if (entry=="on" || entry=="off")
        {
            hand_filter=(entry=="on");
            return true;
        }
        else
            return false;
    }

    /***********************************************************************/
    bool set_density_filter(const string &entry)
    {
        if (entry=="on" || entry=="off")
        {
            density_filter=(entry=="on");
            return true;
        }
        else
            return false;
    }

    /***********************************************************************/
    bool set_cylinder_filter(const string &entry)
    {
        if (entry=="on" || entry=="off")
        {
            cylinder_filter=(entry=="on");
            return true;
        }
        else
            return false;
    }

    /***********************************************************************/
    bool set_ellipse_filter(const string &entry)
    {
        if (entry=="on" || entry=="off")
        {
            ellipse_filter=(entry=="on");
            return true;
        }
        else
            return false;
    }

    /***********************************************************************/
    bool set_all_filters(const string &entry)
    {
        if (entry=="on")
        {
            coarse_filter=true;
            hand_filter=true;
            density_filter=true;
            ellipse_filter=true;
            return true;
        }
        else
            return false;
    }

    /***********************************************************************/
    Bottle get_filtered_points()
    {
        Bottle bpoints;

        vector<Vector> pointsTobeSent;

        if ((density_filter==true || hand_filter==true || cylinder_filter==true || ellipse_filter==true)==true && pointsOut.size()>0)
            pointsTobeSent=pointsOut;
        else
            pointsTobeSent=pointsIn;

        if (pointsTobeSent.size()>0)
        {
            for (size_t i=0; i<pointsTobeSent.size(); i++)
            {
                Vector point=pointsTobeSent[i];
                Bottle &bpoint=bpoints.addList();
                bpoint.addDouble(point[0]); bpoint.addDouble(point[1]);bpoint.addDouble(point[2]);
                yDebug()<<"number of points sent "<<pointsTobeSent.size();
            }
        }
        else
            yError()<<"No points available!";

        return bpoints;
    }

    /***********************************************************************/
    bool go_filter()
    {
        filter=true;
        return true;
    }

    /***********************************************************************/
    bool set_parameter_coarse_filter(const string &entry, const double value)
    {
        if (entry=="x_lim_up")
        {
            x_lim_up=value;
            return true;
        }
        else if (entry=="x_lim_down")
        {
            x_lim_down=value;
            return true;
        }
        else if (entry=="y_lim_up")
        {
            y_lim_up=value;
            return true;
        }
        else if (entry=="y_lim_down")
        {
            y_lim_down=value;
            return true;
        }
        else if (entry=="z_lim_up")
        {
            z_lim_up=value;
            return true;
        }
        else if (entry=="z_lim_down")
        {
            z_lim_down=value;
            return true;
        }
        else
            return false;
    }

    /***********************************************************************/
    double get_parameter_coarse_filter(const string &entry)
    {
        if (entry=="x_lim_up")
            return x_lim_up;
        else if (entry=="x_lim_down")
            return x_lim_down;
        else if (entry=="y_lim_up")
            return y_lim_up;
        else if (entry=="y_lim_down")
            return y_lim_down;
        else if (entry=="z_lim_up")
            return z_lim_up;
        else if (entry=="z_lim_down")
            return z_lim_down;
    }

    /***********************************************************************/
    bool set_parameter_ellipse_filter(const string &entry, const double value)
    {
        if (entry=="a_offset")
        {
            a_offset=value;
            return true;
        }
        else if (entry=="b_offset")
        {
            b_offset=value;
            return true;
        }
        else
            return false;
    }

    /***********************************************************************/
    double get_parameter_ellipse_filter(const string &entry)
    {
        if (entry=="a_offset")
            return a_offset;
        else if (entry=="b_offset")
            return b_offset;
    }

public:
    /*******************************************************************************/
    bool configure(ResourceFinder &rf)
    {
        module_name=rf.check("module_name", Value("in-hand-segmentation"), "Getting module name").asString();
        homeContextPath=rf.getHomeContextPath().c_str();

        savename=rf.check("savename", Value("in-hand-point-cloud"), "Default file savename").asString();
        saving=(rf.check("saving", Value("yes"), "Toggle save clouds as file").asString()== "yes");

        fileOutFormat=rf.check("format", Value("off"), "Default file format").asString();
        visionFileName=rf.check("fileInName", Value("cloud.off"), "Default cloud name").asString();
        poseFileName=rf.check("poseFileName", Value("hand_pose.off"), "Default pose file name").asString();
        fingersFileName=rf.check("fingersFileName", Value("tactile_hand_pose.off"), "Default fingers positions file name").asString();

        online=(rf.check("online", Value("yes"), "online or offline processing").asString()== "yes");
        tactile_on=(rf.check("tactile_on", Value("yes"), "use or not finger positions").asString()== "yes");
        frame=rf.check("frame", Value("hand"), "in which frame save finger positions").asString();

        density_filter=(rf.check("density_filter", Value("no")).asString()=="yes");
        hand_filter=(rf.check("hand_filter", Value("yes")).asString()=="yes");
        change_frame=(rf.check("change_frame", Value("yes")).asString()=="yes");
        coarse_filter=(rf.check("coarse_filter", Value("no")).asString()=="yes");
        cylinder_filter=(rf.check("cylinder_filter", Value("no")).asString()=="yes");
        ellipse_filter=(rf.check("ellipse_filter", Value("no")).asString()=="yes");
        color_space=rf.check("color_code", Value("rgb")).asString();

        if (tactile_on==false)
        {
            cylinder_filter=ellipse_filter=false;
        }

        radius=rf.check("radius", Value(0.0002)).asDouble();
        nnThreshold=rf.check("nn-threshold", Value(40)).asInt();
        radius_color=rf.check("radius_color", Value(0.0003)).asDouble();
        nnThreshold_color=rf.check("nn-threshold_color", Value(10)).asInt();
        diff_rgb=rf.check("diff_rgb", Value(25)).asInt();
        diff_ycbcr=rf.check("diff_ycbcr", Value(2)).asInt();

        x_lim_up=rf.check("x_lim_up", Value(0.15)).asDouble();
        x_lim_down=rf.check("x_lim_down", Value(-0.1)).asDouble();
        y_lim_up=rf.check("y_lim_up", Value(0.05)).asDouble();
        y_lim_down=rf.check("y_lim_down", Value(-0.25)).asDouble();

        if (left_or_right == "right")
        {
            z_lim_up=rf.check("z_lim_up", Value(0.25)).asDouble();
            z_lim_down=rf.check("z_lim_down", Value(-0.08)).asDouble();
        }
        else
        {
            z_lim_up=rf.check("z_lim_up", Value(0.08)).asDouble();
            z_lim_down=rf.check("z_lim_down", Value(-0.25)).asDouble();
        }

        radius_volume_offset=rf.check("radius_volume_offset", Value(0.03)).asDouble();
        a_offset=rf.check("a_offset", Value(0.03)).asDouble();
        b_offset=rf.check("b_offset", Value(0.015)).asDouble();

        cout<<endl<<" Files will be saved in "<<homeContextPath<<" folder, as "<<savename<<"N."<<fileOutFormat<<", with increasing numeration N"<< endl<<endl;

        fileCount=0;
        down= rf.check("downsampling", Value(1)).asInt();

        cout<<endl<<" Opening ports"<<endl<<endl;

        portDispIn.open("/" + module_name + "/disp:i");
        portImgIn.open("/" + module_name + "/img:i");
        portBlobIn.open("/" + module_name + "/blob:i");

        portDispOut.open("/"+module_name+"/disp:o");

        portSFM.open("/"+module_name+"/SFM:rpc");
        portRpc.open("/"+module_name+"/rpc");

        cout<<endl<<" Ports opened"<<endl<<endl;

        attach(portRpc);

        orientation.resize(4,0.0);
        position.resize(3,0.0);
        center_volume.resize(2,0.0);
        center_ellips.resize(2,0.0);

        robot=rf.find("robot").asString().c_str();
        if(rf.find("robot").isNull())
            robot="icub";

        left_or_right=rf.find("which_hand").asString().c_str();
        if(rf.find("which_hand").isNull())
            left_or_right="left";

        if (online)
        {
            Property option_arm("(device cartesiancontrollerclient)");
            option_arm.put("remote","/"+robot+"/cartesianController/"+left_or_right+"_arm");
            option_arm.put("local","/"+module_name+"/cartesian/"+left_or_right+"_arm");

            robotDevice.open(option_arm);
            if (!robotDevice.isValid())
            {
                yError(" Device index not available!");
                return false;
            }

            robotDevice.view(icart_arm);

            Property option_arm2("(device remote_controlboard)");
            option_arm2.put("remote","/"+robot+"/"+left_or_right+"_arm");
            option_arm2.put("local","/"+module_name+"/joint/"+left_or_right+"_arm");

            robotDevice2.open(option_arm2);
            if (!robotDevice2.isValid())
            {
                yError(" Device 2 not available!");
                return false;
            }

            robotDevice2.view(enc);
            robotDevice2.view(lim);

            lim_deque.push_back(lim);

            finger_thumb=iCubFinger(left_or_right+"_thumb");
            finger_index=iCubFinger(left_or_right+"_index");
            finger_middle=iCubFinger(left_or_right+"_middle");

            if (!finger_thumb.alignJointsBounds(lim_deque))
                yError(" Problem in alignJoints!Bounds");
            if (!finger_index.alignJointsBounds(lim_deque))
                yError(" Problem in alignJoints!Bounds");
            if (!finger_middle.alignJointsBounds(lim_deque))
                yError(" Problem in alignJoints!Bounds");

            int jnts;
            enc->getAxes(&jnts);
            encoders.resize(jnts);
            enc->getEncoders(encoders.data());
            icart_arm->storeContext(&startup_context_id);
            icart_arm->getTipFrame(tip_x_init, tip_o_init);

            Property option_analog("(device analogsensorclient)");
            option_analog.put("remote","/"+robot+"/"+left_or_right+"_hand"+"/analog:o");
            option_analog.put("local","/"+module_name+"/analogsensorclient/"+left_or_right+"_hand");

            analogDevice.open(option_analog);
            if (!analogDevice.isValid())
            {
                yError(" Device index not available!");
                return false;
            }

            analogDevice.view(analog);

            H_hand=computePose();
        }
        else
            filter=true;

        return true;
    }

    /*******************************************************************************/
    bool interruptModule()
    {
        portDispIn.interrupt();
        portImgIn.interrupt();
        portBlobIn.interrupt();

        portDispOut.interrupt();

        portSFM.interrupt();
        portRpc.interrupt();

        return true;
    }

    /*******************************************************************************/
    bool close()
    {
        portDispIn.close();
        portImgIn.close();
        portBlobIn.close();

        portDispOut.close();

        portSFM.close();
        portRpc.close();

        return true;
    }

    /*******************************************************************************/
    double getPeriod()
    {
        return 0.1;
    }

    /*******************************************************************************/
    bool updateModule()
    {
        Vector colors(3,0.0);

        if (online)
        {
            acquirePoints();
            
            if (pointsIn.size()>0 && acquire==true)
            {
                if (change_frame)
                    fromRobotTohandFrame(pointsIn);

                if (tactile_on)
                    acquireFingers();

                cout<<" "<<pointsIn.size()<<" Points coming from vision ";
                if (tactile_on)
                    cout<<" and touch";

                cout<<" have been collected"<<endl<<endl;

                acquire=false;
            }
        }
        else
        {
            readPoints(visionFileName, "points");
            H_hand=readPose();

            if (change_frame)
                fromRobotTohandFrame(pointsIn);

            if (tactile_on)
                readPoints(fingersFileName, "fingers");
        }

        if (filter && pointsIn.size()>0)
        {            
            if (coarse_filter)
            {
                colors[1]=255;
                coarseFilter(pointsIn);
                info="_CF";
                saveNewCloud(colors, pointsIn, info);
            }

            if (hand_filter && color_space == "rgb" && pointsIn.size()>0)
            {
                colors[2]=255;
                colors[1]=0;
                handFilter(radius_color,nnThreshold_color,diff_rgb, pointsIn);
                info+="_HF_rgb";
                saveNewCloud(colors, pointsOut, info);
            }
            else if (hand_filter && color_space == "ycbcr" && pointsIn.size()>0)
            {
                colors[2]=255;
                colors[1]=0;
                handFilter(radius_color,nnThreshold_color,diff_ycbcr, pointsIn);
                info+="_HF_ycbcr";
                saveNewCloud(colors, pointsOut, info);
            }

            if (density_filter && pointsIn.size()>0)
            {
                colors[0]=255;
                colors[2]=0;
                if (hand_filter)
                    spatialDensityFilter(radius,nnThreshold+1, pointsOut);
                else
                    spatialDensityFilter(radius,nnThreshold+1, pointsIn);
                info+="_DF";
                saveNewCloud(colors, pointsOut, info);
            }

            if (cylinder_filter && pointsIn.size()>0 )
            {
                colors[1]=255;
                if ((density_filter==true || hand_filter==true)==true)
                    cylinderFilter(pointsOut);
                else
                    cylinderFilter(pointsIn);
                info+="_CyF";

                saveNewCloud(colors, pointsOut,info);
            }
            else if (ellipse_filter && pointsIn.size()>0 )
            {
                colors[1]=255;
                if ((density_filter==true || hand_filter==true)==true)
                    ellipseFilter(pointsOut);
                else
                    ellipseFilter(pointsIn);
                info+="_EF";

                saveNewCloud(colors, pointsOut,info);
            }

            if (info == "_CF_FF_rgb_DF_CyF" ||info == "_CF_GF_ycbcr_DF_CyF"||info == "_CF_HF_rgb_DF_CyF"|| info == "_CF_HF_ycbcr_DF_CyF")
            {
                info="_all_filters";
                saveNewCloud(colors, pointsOut,info);
            }

            filter=false;
            fileCount++;

            if (online)
                cout<<endl<<" All filters have been executed. Ready for new points filtering or acquisition "<<endl<<endl;
        }
        else
        {
            if (online)
                true;
            else
                false;
        }

        return true;
    }

    /*******************************************************************************/
    bool acquirePoints()
    {
        ImageOf<PixelMono> *imgDispIn=portDispIn.read();
        if (imgDispIn==NULL)
            return false;

        ImageOf<PixelRgb> *imgIn=portImgIn.read();
        if (imgIn==NULL)
            return false;

        ImageOf<PixelMono> *imgBlobIn=portBlobIn.read();
        if (imgBlobIn==NULL)
            return false;

        ImageOf<PixelRgb> &imgDispOut=portDispOut.prepare();
        imgDispOut.resize(imgDispIn->width(),imgDispIn->height());

        cv::Mat imgDispInMat=cv::cvarrToMat((IplImage*)imgDispIn->getIplImage());
        cv::Mat imgDispOutMat=cv::cvarrToMat((IplImage*)imgDispOut.getIplImage());
        cv::cvtColor(imgDispInMat,imgDispOutMat,CV_GRAY2RGB);

        cv::Mat imgBlobInMat=cv::cvarrToMat((IplImage*)imgBlobIn->getIplImage());

        if (acquire)
        {
            blobPoints.clear();
            pointsIn.clear();

            for (int j=0; j<imgBlobInMat.rows; j++)
            {
                for (int i=0; i<imgBlobInMat.cols; i++)
                {
                    if (static_cast<int>(imgBlobInMat.at<unsigned char>(j,i))!=0)
                    {
                        blobPoints.push_back(cv::Point(j,i));
                    }
                }
            }

            cout<<endl<<" Number of points belonging to the blob: "<<blobPoints.size()<<endl;

            LockGuard lg(mutex);

            Bottle cmdSFM,replySFM;
            cmdSFM.addString("Points");
            int count=0;

            if (blobPoints.size()>0)
            {
                for (size_t i=0; i<blobPoints.size(); i++)
                {
                        cv::Point single_point=blobPoints[i];
                        cmdSFM.addInt(single_point.y);
                        cmdSFM.addInt(single_point.x);
                }

                if (portSFM.write(cmdSFM,replySFM))
                {
                    for (int i=0; i<replySFM.size(); i+=3)
                    {
                        Vector point(6,0.0);
                        point[0]=replySFM.get(i+0).asDouble();
                        point[1]=replySFM.get(i+1).asDouble();
                        point[2]=replySFM.get(i+2).asDouble();

                        PixelRgb px=imgIn->pixel(blobPoints[count].y,blobPoints[count].x);
                        point[3]=px.r;
                        point[4]=px.g;
                        point[5]=px.b;

                        count++;

                        if ((norm(point)>0))
                        {
                            pointsIn.push_back(point);
                        }
                        }

                    cout <<endl<< " Retrieved " << pointsIn.size() << " 3D points" <<endl<<endl;
                }
                else
                {
                    cout << " SFM didn't reply!" << endl;
                    return true;
                }
            }
            else
                yError()<<" No blob received!";
        }

        if (pointsIn.size()>0 && acquire==true)
        {
            if (saving)
            {
                saveCloud(pointsIn);               
            }
        }

        if (blobPoints.size()>0)
        {
            PixelRgb color(255,255,0);
            for (size_t i=0; i<blobPoints.size(); i++)
            {
                if ((blobPoints[i].y<320) && (blobPoints[i].x<240) && (blobPoints[i].x>0) && (blobPoints[i].y>0))
                    imgDispOut.pixel(blobPoints[i].y,blobPoints[i].x)=color;
            }
        }

        portDispOut.write();

        return true;
    }

    /*******************************************************************************/
    bool readPoints(string &filename, string what)
    {
        int state=0;
        int nPoints;
        char line[255];
        deque<Vector> points_tmp;
        Vector point_tmp;
        point_tmp.resize(6,0.0);

        cout<< endl<<" In cloud file "<<homeContextPath+"/"+filename<<endl;

        ifstream cloudFile((homeContextPath+"/"+filename).c_str());
        if (!cloudFile.is_open())
        {
            yError()<<" Problem opening cloud file!";
            return false;
        }

        while (!cloudFile.eof())
        {
            cloudFile.getline(line,sizeof(line),'\n');
            Bottle b(line);
            Value firstItem=b.get(0);
            bool isNumber=firstItem.isInt() || firstItem.isDouble();

            if (state==0)
            {
                string tmp=firstItem.asString().c_str();
                std::transform(tmp.begin(),tmp.end(),tmp.begin(),::toupper);
                if (tmp=="COFF" || tmp=="OFF")
                    state++;
            }
            else if (state==1)
            {
                if (isNumber)
                {
                    nPoints=firstItem.asInt();
                    state++;
                }
            }
            else if (state==2)
            {
                if (isNumber && (b.size()>=3))
                {
                    point_tmp[0]=b.get(0).asDouble();
                    point_tmp[1]=b.get(1).asDouble();
                    point_tmp[2]=b.get(2).asDouble();
                    point_tmp[3]=b.get(3).asDouble();
                    point_tmp[4]=b.get(4).asDouble();
                    point_tmp[5]=b.get(5).asDouble();
                    points_tmp.push_back(point_tmp);

                    if (--nPoints<=0)
                    {
                        for( size_t i=0; i<points_tmp.size();i=i+down)
                        {
                            point_tmp=points_tmp[i];

                            if (what=="fingers")
                            {
                                fingers.push_back(point_tmp);
                            }
                            pointsIn.push_back(point_tmp);
                        }
                        return true;
                    }
                }
            }
        }
        return false;
    }

    /*******************************************************************************/
    Matrix readPose()
    {
        Matrix H;
        int state=0;
        char line[255];

        cout<<endl<< " In pose file "<<homeContextPath+"/"+poseFileName<<endl<<endl;

        ifstream poseFile((homeContextPath+"/"+poseFileName).c_str());
        if (!poseFile.is_open())
        {
            yError()<<" Problem opening pose file!";
        }

        while (!poseFile.eof())
        {
            poseFile.getline(line,sizeof(line),'\n');
            Bottle b(line);
            Value firstItem=b.get(0);
            bool isNumber=firstItem.isInt() || firstItem.isDouble();

            if (state==0)
            {
                string tmp=firstItem.asString().c_str();
                std::transform(tmp.begin(),tmp.end(),tmp.begin(),::toupper);
                if (tmp=="RIGHT" || tmp=="LEFT")
                    state++;
            }
            else if (state==1)
            {
                if (isNumber && (b.size()==3))
                {
                    position[0]=b.get(0).asDouble();
                    position[1]=b.get(1).asDouble();
                    position[2]=b.get(2).asDouble();
                }
                else if (isNumber && (b.size()==4))
                {
                    orientation[0]=b.get(0).asDouble();
                    orientation[1]=b.get(1).asDouble();
                    orientation[2]=b.get(2).asDouble();
                    orientation[3]=b.get(3).asDouble();
                }
            }
        }

        H.resize(4,0.0);

        H=axis2dcm(orientation);
        Vector x_aux(4,1.0);
        x_aux.setSubvector(0,position);
        H.setCol(3,x_aux);
        H=SE3inv(H);

        return H;
    }

    /*******************************************************************************/
    Matrix computePose()
    {
        Matrix H;

        icart_arm->getPose(position,orientation);

        H.resize(4,0.0);

        H=axis2dcm(orientation);
        Vector x_aux(4,1.0);
        x_aux.setSubvector(0,position);
        H.setCol(3,x_aux);
        H=SE3inv(H);

        if (saving)
        {
            ofstream fout;
            stringstream fileName;
            fileName<<homeContextPath + "/" + poseOutFileName<<savename<<"_"<<fileCount;
            string fileNameFormat;
            fileNameFormat=fileName.str()+".txt";
            fout.open(fileNameFormat.c_str());
            if (fout.is_open())
            {
                fout<<left_or_right+" hand pose"<<endl<<endl;
                fout<<position.toString()<<endl;
                fout<<orientation.toString()<<endl;

                cout<<endl<<" Pose saved in "<<fileNameFormat<<endl<<endl;
            }
            else
            {
                yError(" Problems in opening pose out file!");
            }
        }

        return H;
    }

    /***********************************************************************/
    void acquireFingers()
    {
        giveContactPoint(contactPoint_thumb, "thumb");
        giveContactPoint(contactPoint_index, "index");
        giveContactPoint(contactPoint_middle, "middle");

        if (frame =="robot" && change_frame==true)
            frame="hand";
        else if (frame =="hand" && change_frame==false)
            frame="robot";

        if (frame == "hand")
        {
            Vector aux(4,1.0);
            aux.setSubvector(0,contactPoint_thumb);
            aux=H_hand*aux;
            contactPoint_thumb=aux.subVector(0,2);
            aux.setSubvector(0,contactPoint_index);
            aux=H_hand*aux;
            contactPoint_index=aux.subVector(0,2);
            aux.setSubvector(0,contactPoint_middle);
            aux=H_hand*aux;
            contactPoint_middle=aux.subVector(0,2);
        }

        fingers.clear();
        fingers.push_back(contactPoint_thumb);
        fingers.push_back(contactPoint_index);
        fingers.push_back(contactPoint_middle);
        pointsIn.push_back(contactPoint_thumb);
        pointsIn.push_back(contactPoint_index);
        pointsIn.push_back(contactPoint_middle);

        if (saving)
        {
            ofstream fout;
            stringstream fileName;
            string fileNameFormat;
            fileName<<homeContextPath + "/" + fingersOutFileName.c_str() <<savename<< "_"+frame<<"_"+left_or_right+"_hand_"<<fileCount;
            fileNameFormat= fileName.str()+".off";
            fout.open(fileNameFormat.c_str());
            if (fout.is_open())
            {
                fout<<"COFF"<<endl;
                fout<<"3 0 0 "<<endl;
                fout<<contactPoint_thumb.toString()<<" 255 0 0 "<<endl;
                fout<<contactPoint_index.toString()<<" 255 0 0 "<<endl;
                fout<<contactPoint_middle.toString()<<" 255 0 0 "<<endl;

                cout<<endl<<" Tactile data saved in "<<fileNameFormat<<endl<<endl;
            }
            else
            {
                yError(" Problems in opening pose out file!");
            }
        }
    }

    /***********************************************************************/
    void giveContactPoint(Vector &contactPoint, const string finger_str)
    {
        contactPoint.resize(3,0.0);
        Vector joints, enc_from_port;
        enc_from_port.resize(10,0.0);
        Matrix tipFrame;

        enc->getEncoders(encoders.data());
        analog->read(enc_from_port);

        if (finger_str == "thumb")
        {
            finger_thumb.getChainJoints(encoders, joints);
            tipFrame=finger_thumb.getH((M_PI/180.0)*joints);
        }
        if (finger_str == "index")
        {
            finger_index.getChainJoints(encoders, joints);
            tipFrame=finger_index.getH((M_PI/180.0)*joints);
        }
        if (finger_str == "middle")
        {
            finger_middle.getChainJoints(encoders, joints);
            tipFrame=finger_middle.getH((M_PI/180.0)*joints);
        }

        Vector tip_x=tipFrame.getCol(3);
        Vector tip_o=dcm2axis(tipFrame);

        icart_arm->attachTipFrame(tip_x.subVector(0,2),tip_o);

        Time::delay(0.1);
        Vector o(4,0.0);
        icart_arm->getPose(contactPoint, o);
        icart_arm->removeTipFrame();
        Time::delay(0.1);
    }

    /*******************************************************************************/
    vector<int>  spatialDensityFilter(const double radius, const int maxResults, vector<Vector> &points)
    {
        numVertices=points.size();

        vector<Vector> pointsTmp=points;
        pointsOut.clear();

        cv:: Mat data(numVertices,3,CV_32F);
        for (int i=0; i<numVertices; i++)
        {
            Vector point=pointsTmp[i];
            data.at<float>(i,0)=(float)point[0];
            data.at<float>(i,1)=(float)point[1];
            data.at<float>(i,2)=(float)point[2];
        }

        cv::flann::KDTreeIndexParams indexParams;
        cv::flann::Index kdtree(data,indexParams);

        cv::Mat query(1,data.cols,CV_32F);
        cv::Mat indices,dists;

        vector<int> res(data.rows);

        for (size_t i=0; i<res.size(); i++)
        {
            for (int c=0; c<query.cols; c++)
                query.at<float>(0,c)=data.at<float>(i,c);

            res[i]=kdtree.radiusSearch(query,indices,dists,radius,maxResults,cv::flann::SearchParams(250));

            Vector point(3,0.0);
            if (res[i]>=maxResults)
            {
                point[0]=data.at<float>(i,0);
                point[1]=data.at<float>(i,1);
                point[2]=data.at<float>(i,2);
                pointsOut.push_back(point);
            }
        }

        return res;
    }

    /*******************************************************************************/
    vector<int> handFilter(const double radius, const int maxResults, const int diff_colors, vector<Vector> &pointsIn)
    {
        numVertices=pointsIn.size();
        pointsOut.clear();

        cv:: Mat data(numVertices,3,CV_32F);
        cv:: Mat datacolor(numVertices,3,CV_32F);

        if (color_space == "ycbcr")
            fromRGBtoYCbCr();

        for (int i=0; i<numVertices; i++)
        {
            Vector point=pointsIn[i];
            data.at<float>(i,0)=(float)point[0];
            data.at<float>(i,1)=(float)point[1];
            data.at<float>(i,2)=(float)point[2];

            datacolor.at<float>(i,0)=(float)point[3];
            datacolor.at<float>(i,1)=(float)point[4];
            datacolor.at<float>(i,2)=(float)point[5];
        }

        cv::flann::KDTreeIndexParams indexParams;
        cv::flann::Index kdtree(data,indexParams);

        cv::Mat query(1,data.cols,CV_32F);
        cv::Mat indices,dists;

        vector<int> res(data.rows);
        double average_diff, diff1, diff2, diff3;
        int count =0;
        average_diff=0;

        for (size_t i=0; i<res.size(); i++)
        {
            for (int c=0; c<query.cols; c++)
                query.at<float>(0,c)=data.at<float>(i,c);

            res[i]=kdtree.radiusSearch(query,indices,dists,radius,maxResults,cv::flann::SearchParams(128));

            int count_index=0;
            for (int j=0; j<indices.cols; j++)
            {
                if (abs(indices.at<int>(0,j)) <=datacolor.rows)
                {
                    count_index++;
                    if (color_space == "rgb")
                    {
                        diff1 = abs(datacolor.at<float>(indices.at<int>(0,j),0) - datacolor.at<float>(indices.at<int>(0,j),1));
                        diff2 = abs(datacolor.at<float>(indices.at<int>(0,j),2) - datacolor.at<float>(indices.at<int>(0,j),1));
                        diff3 = abs(datacolor.at<float>(indices.at<int>(0,j),0) - datacolor.at<float>(indices.at<int>(0,j),2));
                        average_diff += (diff1+diff2+diff3)/3;
                    }
                    else
                    {
                        diff1 = abs(datacolor.at<float>(indices.at<int>(0,j),1) - 128);
                        diff2 = abs(datacolor.at<float>(indices.at<int>(0,j),2) - 128);
                        average_diff += (diff1+diff2)/2;
                    }
                }
            }
            average_diff/=count_index;

            Vector point(3,0.0);
            if (res[i]>=maxResults && average_diff>=diff_colors)
            {
                point[0]=data.at<float>(i,0);
                point[1]=data.at<float>(i,1);
                point[2]=data.at<float>(i,2);
                pointsOut.push_back(point);
                count++;
                average_diff=0.0;
            }
        }
        return res;
    }

    /*******************************************************************************/
    void coarseFilter(vector<Vector> &points)
    {
        vector<Vector> pointsTmp=points;
        pointsIn.clear();

        for (size_t i=0; i<pointsTmp.size(); i++)
        {
            Vector point=pointsTmp[i];

            if ((point[0]<= x_lim_up) && (point[0]>= x_lim_down) && (point[1]<= y_lim_up) && (point[1]>= y_lim_down) && (point[2]<= z_lim_up) && (point[2]>= z_lim_down))
                pointsIn.push_back(point);
        }
    }

    /*******************************************************************************/
    void fromRGBtoYCbCr()
    {
        Vector *point;

        Matrix M_rgb2ycbcr(3,3);
        M_rgb2ycbcr(0,0)=0.299;
        M_rgb2ycbcr(0,1)=0.587;
        M_rgb2ycbcr(0,2)=0.144;
        M_rgb2ycbcr(1,0)=-0.168736;
        M_rgb2ycbcr(1,1)=-0.331264;
        M_rgb2ycbcr(1,2)=0.5;
        M_rgb2ycbcr(2,0)=0.5;
        M_rgb2ycbcr(2,1)=-0.418688;
        M_rgb2ycbcr(2,2)=-0.081312;

        Vector ycbcr(3,0.0);
        ycbcr[1]=128;
        ycbcr[2]=128;


        for (size_t i=0; i<pointsIn.size(); i++)
        {
            point=&pointsIn[i];
            yDebug()<<"point "<<point->subVector(3,5).toString();
            point->setSubvector(3,M_rgb2ycbcr*point->subVector(3,5) + ycbcr);

            yDebug()<<"point computed "<<point->subVector(3,5).toString();
        }
    }

    /*******************************************************************************/
    void fromRobotTohandFrame(vector<Vector> &pointstmp)
    {
        for (size_t i=0; i<pointstmp.size(); i++)
        {
            Vector aux(4,1.0);
            aux.setSubvector(0,pointstmp[i].subVector(0,2));
            aux=H_hand*(aux);
            pointstmp[i].setSubvector(0,aux.subVector(0,2));
        }
    }

    /*******************************************************************************/
    void graspableVolume(vector<Vector> &fingerPoses)
    {
        Vector thumb, index, middle;
        thumb=fingerPoses[0];
        index=fingerPoses[1];
        middle=fingerPoses[2];
        double ma, mb;
        ma=(index[2]-thumb[2])/(index[0]-thumb[0]);
        mb=(middle[2]-index[2])/(middle[0]-index[0]);
        center_volume[0]=(ma*mb * (thumb[2]-middle[2]) + mb * (index[0]+thumb[0]) - ma * (index[0]+middle[0]))/(2*(mb-ma));
        center_volume[1]=-1/ma * (center_volume[0] - (thumb[0]+index[0])/2) +(thumb[2]+index[2])/2;
        radius_volume=sqrt((center_volume[0]-thumb[0])*(center_volume[0]-thumb[0]) + (center_volume[1]-thumb[2])*(center_volume[1]-thumb[2]));
    }

    /*******************************************************************************/
    void cylinderFilter(vector<Vector> &points)
    {
        vector<Vector> pointsTmp=points;
        pointsOut.clear();

        graspableVolume(fingers);

        for (size_t i=0;i<pointsTmp.size(); i++)
        {
            Vector point=pointsTmp[i];
            if ((point[0]-center_volume[0])*(point[0]-center_volume[0]) + (point[2]-center_volume[1])*(point[2]-center_volume[1]) - (radius_volume+a_offset)*(radius_volume+b_offset )<0 )
                pointsOut.push_back(point);
        }
    }

    /*******************************************************************************/
    void graspableEllips(vector<Vector> &fingerPoses)
    {
        Vector thumb, index, middle;
        thumb=fingerPoses[0];
        index=fingerPoses[1];
        middle=fingerPoses[2];
        a=abs(thumb[0]-index[0])/2.0;
        b=sqrt(thumb[2]*thumb[2]+thumb[0]*thumb[0]);
        double ma, mb, ca, cb;
        ma=(index[2]-thumb[2])/(index[0]-thumb[0]);
        mb=-1/ma;

        ca=index[2]- index[0]*ma;
        cb=0;

        center_ellips[0]=(ca - cb)/(mb-ma);
        center_ellips[1]=ma*center_ellips[0]+ca;
    }

    /*******************************************************************************/
    void ellipseFilter(vector<Vector> &points)
    {
        vector<Vector> pointsTmp=points;
        pointsOut.clear();

        graspableEllips(fingers);

        for (size_t i=0;i<pointsTmp.size(); i++)
        {
            Vector point=pointsTmp[i];
            if ((point[0]-center_ellips[0])*(point[0]-center_ellips[0])*(b+b_offset)*(b+b_offset) + (point[2]-center_ellips[1])*(point[2]-center_ellips[1])*(a+a_offset)*(a+a_offset) - (a+a_offset)*(a+a_offset)*(b+b_offset)*(b+b_offset)<0 )
                pointsOut.push_back(point);
        }
    }

    /*******************************************************************************/
    bool saveCloud(const vector<Vector> &points)
    {
        ofstream fout;
        stringstream fileName;
        string fileNameFormat;
        fileName.str("");
        fileName << homeContextPath + "/" + savename.c_str() <<"_"<< fileCount;

        if (fileOutFormat == "ply")
        {
            fileNameFormat = fileName.str()+".ply";
            cout << " Saving as " << fileNameFormat << endl;
            fout.open(fileNameFormat.c_str());
            if (fout.is_open())
            {
                fout << "ply\n";
                fout << "format ascii 1.0\n";
                fout << "element vertex " << points.size() <<"\n";
                fout << "property float x\n";
                fout << "property float y\n";
                fout << "property float z\n";
                fout << "property uchar diffuse_red\n";
                fout << "property uchar diffuse_green\n";
                fout << "property uchar diffuse_blue\n";
                fout << "end_header\n";

                for (unsigned int i=0; i<points.size(); i++)
                {
                    fout << points[i][0] << " " <<      points[i][1] << " " <<      points[i][2] << " " << (int)points[i][3] << " " << (int)points[i][4] << " " << (int)points[i][5] << "\n";
                    //plyfile << cloud->at(i).x << " " << cloud->at(i).y << " " << cloud->at(i).z << " " << (int)cloud->at(i).r << " " << (int)cloud->at(i).g << " " << (int)cloud->at(i).b << "\n";
                }

                fout.close();
                cout <<endl<< " Points saved as " << fileNameFormat << endl;
                fileCount++;
            }

        }
        else if (fileOutFormat == "off")
        {
            fileNameFormat = fileName.str()+".off";
            cout << " Saving as " << fileNameFormat << endl;
            fout.open(fileNameFormat.c_str());
            if (fout.is_open())
            {

                fout<<"COFF"<<endl;
                fout<<points.size()<<" 0 0"<<endl;
                fout<<endl;
                for (size_t i=0; i<points.size(); i++)
                {
                    fout<<points[i].subVector(0,2).toString(3,4).c_str()<<" "<<
                          points[i].subVector(3,5).toString(0,3).c_str()<<endl;
                }
                fout<<endl;
            }

            fout.close();
            cout <<endl<< " Points saved as " << fileNameFormat << endl<<endl;
            fileCount++;
        }
        else if (fileOutFormat == "none")
        {
            yError( " Points not saved" );
        }
        return true;
    }

    /*******************************************************************************/
    bool saveNewCloud(const Vector &colors, vector<Vector> &pointsToBeSaved, string &info)
    {
        ofstream fout;
        stringstream fileName;
        string fileNameFormat;
        fileName.str("");
        fileName << homeContextPath + "/" + savename.c_str() <<"_"<<  fileCount<<info;

        if (fileOutFormat == "ply")
        {
            fileNameFormat = fileName.str()+".ply";
            cout << " Saving as " << fileNameFormat << endl;
            fout.open(fileNameFormat.c_str());
            if (fout.is_open())
            {
                fout << "ply\n";
                fout << "format ascii 1.0\n";
                fout << "element vertex " << pointsToBeSaved.size() <<"\n";
                fout << "property float x\n";
                fout << "property float y\n";
                fout << "property float z\n";
                fout << "property uchar diffuse_red\n";
                fout << "property uchar diffuse_green\n";
                fout << "property uchar diffuse_blue\n";
                fout << "end_header\n";

                for (unsigned int i=0; i<pointsToBeSaved.size(); i++)
                {
                    fout << pointsToBeSaved[i][0] << " " <<      pointsToBeSaved[i][1] << " " <<      pointsToBeSaved[i][2] << " " << (int)pointsToBeSaved[i][3] << " " << (int)pointsToBeSaved[i][4] << " " << (int)pointsToBeSaved[i][5] << "\n";
                    //plyfile << cloud->at(i).x << " " << cloud->at(i).y << " " << cloud->at(i).z << " " << (int)cloud->at(i).r << " " << (int)cloud->at(i).g << " " << (int)cloud->at(i).b << "\n";
                }

                fout.close();
                cout << endl<< " Points saved as " << fileNameFormat << endl<<endl;
            }

        }
        else if (fileOutFormat == "off")
        {
            fileNameFormat = fileName.str()+".off";
            cout << " Saving as " << fileNameFormat << endl;
            fout.open(fileNameFormat.c_str());
            if (fout.is_open())
            {

                fout<<"COFF"<<endl;
                fout<<pointsToBeSaved.size()<<" 0 0"<<endl;
                fout<<endl;
                for (size_t i=0; i<pointsToBeSaved.size(); i++)
                {
                    if (info!="_CF")
                    {
                        fout<<pointsToBeSaved[i].subVector(0,2).toString(3,4).c_str()<<" "<<
                              colors[0]<<" "<<colors[1]<<" "<<colors[2]<<endl;
                    }
                    else
                        fout<<pointsToBeSaved[i].subVector(0,2).toString(3,4).c_str()<<" "<<pointsToBeSaved[i].subVector(3,5).toString(0,4).c_str()<<endl;
                }
                fout<<endl;
            }

            fout.close();
            cout << endl<<" Points saved as " << fileNameFormat << endl<<endl;
        }
        else if (fileOutFormat == "none")
        {
            yError(" Points not saved");
        }
    }
};
