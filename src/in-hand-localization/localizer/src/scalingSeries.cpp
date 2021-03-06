
/*
 * Copyright: (C) 2015 iCub Facility - Istituto Italiano di Tecnologia
 * Authors: Giulia Vezzani
 * CopyPolicy: Released under the terms of the GNU GPL v2.0.
 */

#include <iostream>
#include <fstream>
#include <string>
#include <algorithm>
#include <cmath>
#include <yarp/os/all.h>
#include <yarp/math/Math.h>
#include <yarp/math/Rand.h>
#include <CGAL/IO/Polyhedron_iostream.h>
#include <yarp/os/ResourceFinder.h>

#include "../headers/scalingSeries.h"

typedef CGAL::Aff_transformation_3<K> Affine;

using namespace std;
using namespace yarp::os;
using namespace yarp::sig;
using namespace yarp::math;


/*******************************************************************************/
ScalingSeries::ScalingSeries() : GeometryCGAL()
{
    parameters=new ParametersSSprune;   // owned by GeometryCGAL
}

/*******************************************************************************/
ParametersSSprune &ScalingSeries::get_parameters()
{
    return *static_cast<ParametersSSprune*>(parameters);
}

/*******************************************************************************/
void ScalingSeries::initialRandomize()
{
    for (size_t i=0; i<x.size(); i++)
    {
        Particle &particle=x[i];
        ParametersSSprune &params=get_parameters();

        particle.pos[0]=Rand::scalar(params.center0[0]-params.radius0[0],params.center0[0]+params.radius0[0]);
        particle.pos[1]=Rand::scalar(params.center0[1]-params.radius0[1],params.center0[1]+params.radius0[1]);
        particle.pos[2]=Rand::scalar(params.center0[2]-params.radius0[2],params.center0[2]+params.radius0[2]);
        particle.pos[3]=Rand::scalar(0,2*M_PI);
        particle.pos[4]=Rand::scalar(0,M_PI);
        particle.pos[5]=Rand::scalar(0,2*M_PI);

    }
}

/*******************************************************************************/
void ScalingSeries::init()
{
    t0=Time::now();
    double Np, Nn;
    GeometryCGAL::init();
    ParametersSSprune &params=get_parameters();
    //counter for iterations
    iter=0;
    // init delta
    numTotPart=params.numParticles;
    delta_p=params.radius0[0];
    delta_n=M_PI;
    radius_p=delta_p;
    radius_n=delta_n;
    n=params.n;

    //compute number of iterations
    Np=log2(volumeSphere(params.radius0[0],n)/volumeSphere(params.delta_desired_p, n));
    Nn=log2(volumeSphere(M_PI,n)/volumeSphere(params.delta_desired_n,n));
    N=round(max(Np,Nn));
    number.resize(N,0.0);
    
    cout<<"N"<<N<<"\n";
	
    // compute zoom
    zoom=1.0/pow(2,1.0/6);
}

double ScalingSeries::volumeSphere(const double &radius,const int &n)
{ 
    double volume=0;
    volume=pow(M_PI,n/2)/tgamma(n/2+1)*pow(radius,n);
    return volume;
}

/*******************************************************************************/
bool ScalingSeries::step()
{   
    double tau_p;
    iter++;
    cout<<"iter "<<iter<<"\n";
    ParametersSSprune &params=get_parameters();

    //reduce delta
    delta_p=zoom*delta_p;
    delta_n=zoom*delta_n;
	
    // compute tau
    tau_p=pow(delta_p/params.delta_desired_p,2.0);

    //eveDensityCover
    if (iter==1)
    {
        x.assign(params.numParticles,Particle());
        initialRandomize();

    }
    else
    {
        if( iter>N)
        {
            return true;
        }
	
        evenDensityCover(numTotPart);
    }
	
    cout<<"numTotPart "<<numTotPart<<endl;
    number[iter-1]=numTotPart;

    //compute weights
    computeWeights(tau_p, numTotPart);

    //pruning
    pruning(numTotPart);
    cout<<"numTotPart "<<numTotPart<<endl;

    //unionDeltaSpheres
    radius_p=delta_p;
    radius_n=delta_n;
	  
    return false;    
}


/*******************************************************************************/
Vector ScalingSeries::finalize()
{  
//  cout<<"we are in finalize"<<endl;


    MsParticle ms_particle;
    Vector result;

    result.resize(8,0.0);

    double larger_weights=0.0;
    int index_most=0;
    evenDensityCover(numTotPart);
    computeWeights(1, numTotPart);

    //find most significant particle
    for(size_t i=0; i< numTotPart; i++)
    { 	
        if(larger_weights<=x[i].weights|| i==0)
        {
            larger_weights=x[i].weights;
            index_most=i;
        }
   }
    
    ms_particle.pos[0]=x[index_most].pos[0];
    ms_particle.pos[1]=x[index_most].pos[1];
    ms_particle.pos[2]=x[index_most].pos[2];
    ms_particle.pos[3]=x[index_most].pos[3];
    ms_particle.pos[4]=x[index_most].pos[4];
    ms_particle.pos[5]=x[index_most].pos[5];
        
    performanceIndex(ms_particle);
   
    Matrix H=rpr(ms_particle.pos.subVector(3,5));
    Affine affine(H(0,0),H(0,1),H(0,2),ms_particle.pos[0],
                  H(1,0),H(1,1),H(1,2),ms_particle.pos[1],
                  H(2,0),H(2,1),H(2,2),ms_particle.pos[2]);
                           
    std::transform(model.points_begin(),model.points_end(),
                   model.points_begin(),affine);
       
    result[0]=ms_particle.pos[0];
    result[1]=ms_particle.pos[1];
    result[2]=ms_particle.pos[2];
    result[3]=ms_particle.pos[3];
    result[4]=ms_particle.pos[4];
    result[5]=ms_particle.pos[5];
    result[6]=ms_particle.error_index;
    dt=Time::now()-t0;
    result[7]=dt;
    cout<<"time "<< result[7]<<endl;
    cout<<"number of particles"<< number.subVector(0,N-1).toString(3,3)<<endl;
  
    return result;
}

/*******************************************************************************/ 
void ScalingSeries::evenDensityCover(int &numTotPart)
{	
    std::deque<Particle> new_x;
    ParametersSSprune &params=get_parameters();
    int numSpheres=x.size();
    int count_tentative;

    numTotPart=params.numParticles*numSpheres;

    new_x.assign(numSpheres*params.numParticles,Particle());
	
    for (size_t i=0; i<x.size(); i++)
    {
        for (size_t j=0; j<params.numParticles; j++)
        {
            count_tentative=0;
            Particle &particle=new_x[i*params.numParticles+j];

            particle.pos[0]=Rand::scalar(x[i].pos[0]-radius_p,x[i].pos[0]+radius_p);
            particle.pos[1]=Rand::scalar(x[i].pos[1]-radius_p,x[i].pos[1]+radius_p);
            particle.pos[2]=Rand::scalar(x[i].pos[2]-radius_p,x[i].pos[2]+radius_p);
            particle.pos[3]=fmod(Rand::scalar(x[i].pos[3]-radius_n,x[i].pos[3]+radius_n),2*M_PI);
            particle.pos[4]=fmod(Rand::scalar(x[i].pos[4]-radius_n/2,x[i].pos[4]+radius_n/2),2*M_PI);
            particle.pos[5]=fmod(Rand::scalar(x[i].pos[5]-radius_n,x[i].pos[5]+radius_n),2*M_PI);

            while (belongToOtherSpheres(i, particle)==true && count_tentative<5)
            {
                particle.pos[0]=Rand::scalar(x[i].pos[0]-radius_p,x[i].pos[0]+radius_p);
                particle.pos[1]=Rand::scalar(x[i].pos[1]-radius_p,x[i].pos[1]+radius_p);
                particle.pos[2]=Rand::scalar(x[i].pos[2]-radius_p,x[i].pos[2]+radius_p);
                particle.pos[3]=fmod(Rand::scalar(x[i].pos[3]-radius_n,x[i].pos[3]+radius_n),2*M_PI);
                particle.pos[4]=fmod(Rand::scalar(x[i].pos[4]-radius_n/2,x[i].pos[4]+radius_n/2),2*M_PI);
                particle.pos[5]=fmod(Rand::scalar(x[i].pos[5]-radius_n,x[i].pos[5]+radius_n),2*M_PI);
                count_tentative++;
            }
        }
    }
       	
    x.assign(numSpheres*params.numParticles,Particle());
    x=new_x;
}

/*******************************************************************************/
bool ScalingSeries::belongToOtherSpheres(const int &i, const Particle &new_x)
{	
    int count=0;
    bool check=false;
    int check_on_dimensions []={0, 0, 0, 0, 0, 0};

    if (i==0)
    {
        check=false;
    }
    else
    {
        for(size_t k=0; k< i-1; k++)
        {
            int sum=0;

            if(((x[k].pos[0]<=new_x.pos[0]) && (new_x.pos[0]<=x[k].pos[0]+radius_p)) || ((x[k].pos[0]>=new_x.pos[0]) && (new_x.pos[0]>=x[k].pos[0]-radius_p)))
            {
                check_on_dimensions[0]=1;
            }

            if(((x[k].pos[1]<=new_x.pos[1]) && (new_x.pos[1]<=x[k].pos[1]+radius_p)) || ((x[k].pos[1]>=new_x.pos[1]) && (new_x.pos[1]>=x[k].pos[1]-radius_p)))
            {
                check_on_dimensions[1]=1;
            }

            if(((x[k].pos[2]<=new_x.pos[2]) && ( new_x.pos[2]<=x[k].pos[2]+radius_p)) || ((x[k].pos[2]>=new_x.pos[0]) && (new_x.pos[2]>=x[k].pos[2]-radius_p)))
            {
                check_on_dimensions[2]=1;
            }

            if(((x[k].pos[3]<=new_x.pos[3]) && (new_x.pos[3]<=(x[k].pos[3]+radius_n))) || ((x[k].pos[3]=new_x.pos[3]) && new_x.pos[3]>=(x[k].pos[3]-radius_n)))
            {
                check_on_dimensions[3]=1;
            }

            if(((x[k].pos[4]<=new_x.pos[4]) && (new_x.pos[4]<=(x[k].pos[4]+radius_n/2)))|| ((x[k].pos[4]>=new_x.pos[4]) && (new_x.pos[4]>=(x[k].pos[4]-radius_n/2))))
            {
                check_on_dimensions[4]=1;
            }

            if(((x[k].pos[5]<=new_x.pos[5]) && (new_x.pos[5]<=(x[k].pos[5]+radius_n)))|| ((x[k].pos[5]>=new_x.pos[5]) && (new_x.pos[5]>=(x[k].pos[5]-radius_n))))
            {
                 check_on_dimensions[5]=1;
            }

            sum=check_on_dimensions[0]+check_on_dimensions[1]+check_on_dimensions[2]+check_on_dimensions[3]+check_on_dimensions[4]+check_on_dimensions[5];

            if(sum==6)
                count++;

        }
        if (count>=1 )
            check=true;
        else
            check=false;
    }

    return check;
}

/*******************************************************************************/
void ScalingSeries::computeWeights(const double &tau_p, const int &numTotPart)
{
    double sum=0.0;
    ParametersSSprune &params=get_parameters();
	
    for(size_t k=0; k<numTotPart; k++)
    {
        x[k].weights=1.0;
        Particle &particle=x[k];

        Matrix H=rpr(particle.pos.subVector(3,5));
        H.transposed();

        H(0,3)=particle.pos[0];
        H(1,3)=particle.pos[1];
        H(2,3)=particle.pos[2];

        H=SE3inv(H);

        for (size_t i=0; i<measurements.size(); i++)
        {
                Point &m=measurements[i];


                double x=H(0,0)*m[0]+H(0,1)*m[1]+H(0,2)*m[2]+H(0,3);
                double y=H(1,0)*m[0]+H(1,1)*m[1]+H(1,2)*m[2]+H(1,3);
                double z=H(2,0)*m[0]+H(2,1)*m[1]+H(2,2)*m[2]+H(2,3);


                particle.weights=particle.weights*exp(-0.5*tree.squared_distance(Point(x,y,z))/(pow(params.errp,2.0)*tau_p));

        }

    sum+=particle.weights;
    }
    
    for (size_t i=0; i<x.size(); i++)
    {
        x[i].weights=x[i].weights/sum;
    }
}

/*******************************************************************************/
bool ScalingSeries::isEqual(const double &x, const double &y)
{
    return fabs(x-y)<=std::numeric_limits<double>::epsilon();
}

/*******************************************************************************/
void ScalingSeries::pruning( int &numTotPart)
{	
    std::deque<Particle> new_x, pruned_x;
    Vector c,u, new_index;

    //Initialization
    c.resize(numTotPart, 0.0);
    u.resize(numTotPart, 0.0);
    new_index.resize(numTotPart, 0);
    new_x.assign(numTotPart,Particle());
    c[0]=x[0].weights;

    for(size_t i=1; i<numTotPart; i++)
    {
        c[i]=c[i-1]+x[i].weights;
    }

    int i=0;
    u[0]=Rand::scalar(0,1)/numTotPart;

    for(size_t j=0; j<numTotPart; j++)
    {
        u[j]=u[0]+1.0/numTotPart*j;

        while(u[j]>c[i])
        {
            i++;
        }

        new_x[j].pos=x[i].pos;
        new_x[j].weights=1/numTotPart;

    }
    bool check1,check2,check3,check4,check5,check6;
    //cut off repeated particles

    for( size_t k=0; k<numTotPart; k++)
    {
        if(k>0)
        {
            check1=isEqual(new_x[k].pos[0],new_x[k-1].pos[0]);
            check2=isEqual(new_x[k].pos[1],new_x[k-1].pos[1]);
            check3=isEqual(new_x[k].pos[2],new_x[k-1].pos[2]);
            check4=isEqual(new_x[k].pos[3],new_x[k-1].pos[3]);
            check5= isEqual(new_x[k].pos[4],new_x[k-1].pos[4]);
            check6=isEqual(new_x[k].pos[5],new_x[k-1].pos[5]);
            if(!check1 || !check2 || !check3  || !check4  || !check5 || !check6 )
            {
                pruned_x.push_back(new_x[k]);
            }
        }
        else
        {
            pruned_x.push_back(new_x[k]);
        }
    }

    numTotPart=pruned_x.size();

    x.assign(numTotPart, Particle());
    x=pruned_x;
}

/*******************************************************************************/
void ScalingSeries::performanceIndex( MsParticle &ms_particle)
{  
    ms_particle.error_index=0;

    Matrix H=rpr(ms_particle.pos.subVector(3,5));
    H.transposed();
    H(0,3)=ms_particle.pos[0];
    H(1,3)=ms_particle.pos[1];
    H(2,3)=ms_particle.pos[2];
    H=SE3inv(H);
    for (size_t i=0; i<measurements.size(); i++)
    {
        Point &m=measurements[i];

        double x=H(0,0)*m[0]+H(0,1)*m[1]+H(0,2)*m[2]+H(0,3);
        double y=H(1,0)*m[0]+H(1,1)*m[1]+H(1,2)*m[2]+H(1,3);
        double z=H(2,0)*m[0]+H(2,1)*m[1]+H(2,2)*m[2]+H(2,3);

        ms_particle.error_index+=sqrt(tree.squared_distance(Point(x,y,z)));
    }
    ms_particle.error_index=ms_particle.error_index/measurements.size();
}

/*******************************************************************************/
Matrix ScalingSeries::rpr(const Vector &particle)
{
    Matrix H(4,4);
    double phi=particle[0];
    double theta=particle[1];
    double psi=particle[2];
    H(0,0)=cos(phi)*cos(theta)*cos(psi)-sin(phi)*sin(psi);
    H(0,1)=-cos(phi)*cos(theta)*sin(psi)-sin(phi)*cos(psi);
    H(0,2)=cos(phi)*sin(theta);
    H(1,0)=sin(phi)*cos(theta)*cos(psi)+cos(phi)*sin(psi);
    H(1,1)=-sin(phi)*cos(theta)*sin(psi)+cos(phi)*cos(psi);
    H(1,2)=sin(phi)*sin(theta) ;
    H(2,0)= -sin(theta)*cos(psi);
    H(2,1)=sin(theta)*sin(psi) ;
    H(2,2)=cos(theta) ;
       
    return H;   
}

/*******************************************************************************/
void ScalingSeries::solve()
{
    bool finished=false;
    while(finished==false)
    {
        finished=ScalingSeries::step();
    }
}

/*******************************************************************************/
bool ScalingSeries::readMeasurements(ifstream &fin, const int &downsampling)
{
    ParametersSSprune &params=get_parameters();
    int state=0;
    int nPoints;
    char line[255];
    params.numMeas=0;

    while (!fin.eof())
    {
        fin.getline(line,sizeof(line),'\n');
        Bottle b(line);
        Value firstItem=b.get(0);
        bool isNumber=firstItem.isInt() || firstItem.isDouble();

        if (state==0)
        {
            string tmp=firstItem.asString().c_str();
            std::transform(tmp.begin(),tmp.end(),tmp.begin(),::toupper);
            if (tmp=="OFF")
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
               get_measurements().push_back(Point(b.get(0).asDouble(),
                                                   b.get(1).asDouble(),
                                                   b.get(2).asDouble()));
                params.numMeas++;

                nPoints-=downsampling;
                if (nPoints<=0)
                    return true;
            }
        }
    }

    return false;
}
    
/*******************************************************************************/    
bool ScalingSeries::configure(ResourceFinder &rf, const int &n_obj)
{
    stringstream ss2;
    ss2 << n_obj;
    str_obj = ss2.str();

    this->rf=&rf;
    if (!this->rf->check("modelFile"+str_obj))
    {
        yError()<<"model file not provided!";
        return false;
    }

    if (!this->rf->check("measurementsFile"))
    {
        yError()<<"measurements file not provided!";
        return false;
    }

    downsampling=rf.check("downsampling", Value(1)).asInt();

    string modelFileName=this->rf->find("modelFile"+str_obj).asString().c_str();
    string measurementsFileName=this->rf->find("measurementsFile").
                                asString().c_str();
     if(this->rf->find("measurementsFile").isNull())
            string measurementsFileName="../measurements.off";
   cout<<"debug2"<<endl;

    ParametersSSprune &parameters=get_parameters();

    parameters.numParticles=this->rf->find("M").asInt();
    if (this->rf->find("M").isNull())
        parameters.numParticles=this->rf->check("M",Value(3)).asInt();

        cout<<"debug3"<<endl;

    parameters.n=this->rf->find("n").asInt();
    if (this->rf->find("n").isNull())
        parameters.n=this->rf->check("n",Value(6)).asInt();

     parameters.p=this->rf->find("p").asInt();
    if (this->rf->find("p").isNull())
         parameters.p=this->rf->check("p",Value(3)).asInt();

    parameters.center0.resize(3,0.0);
    bool check=readCenter("center0",parameters.center0);
    cout<<"debug4"<<endl;

    if(!check)
    {
        parameters.center0[0]=this->rf->check("center0",Value(0.2)).asDouble();
        parameters.center0[1]=this->rf->check("center0",Value(0.2)).asDouble();
        parameters.center0[2]=this->rf->check("center0",Value(0.3)).asDouble();
    }
    cout<<"center0"<<parameters.center0.toString().c_str()<<endl;
            parameters.radius0.resize(3,0.0);
    check=readRadius("radius0",parameters.radius0);
    cout<<"debug5"<<endl;

    if(!check)
    {
        parameters.radius0.resize(3,0.0);
        parameters.radius0[0]=this->rf->check("radius0",Value(0.2)).asDouble();
        parameters.radius0[1]=this->rf->check("radius0",Value(0.2)).asDouble();
        parameters.radius0[2]=this->rf->check("radius0",Value(0.2)).asDouble();
    }
    cout<<"radius0"<<parameters.radius0.toString().c_str()<<endl;
    parameters.errp=this->rf->find("errp").asDouble();
    if (this->rf->find("errp").isNull())
        parameters.errp=this->rf->check("errp",Value(0.005)).asDouble();

    parameters.errn=this->rf->find("errn").asDouble();
    if (this->rf->find("errn").isNull())
        parameters.errn=this->rf->check("errn",Value(M_PI/36)).asDouble();

    cout<<"debug6"<<endl;
    // read the polyhedron from a .OFF file
    ifstream modelFile(modelFileName.c_str());
    if (!modelFile.is_open())
    {
        yError()<<"problem opening model file!";
        return false;
    }

    modelFile>>get_model();
    cout<<"debug7"<<endl;

    if (modelFile.bad())
    {
        yError()<<"problem reading model file!";
        modelFile.close();
        return false;
    }
    modelFile.close();

    cout<<"debug8"<<endl;

    // read the measurements file
    ifstream measurementsFile(measurementsFileName.c_str());
    if (!measurementsFile.is_open())
    {
        yError()<<"problem opening measurements file!";
        measurementsFile.close();
        return false;
    }
    if (!readMeasurements(measurementsFile, downsampling))
    {
        yError()<<"problem reading measurements file!";
        modelFile.close();
        measurementsFile.close();
        return false;
    }
    measurementsFile.close();

    cout<<"debug9"<<endl;

    parameters.delta_desired_p=parameters.errp*sqrt(exp(1)/parameters.numMeas);

    parameters.delta_desired_n=parameters.errn*sqrt(exp(1)/parameters.numMeas);
}

/*******************************************************************************/
void ScalingSeries::saveData( const yarp::sig::Vector &ms_particle, const int &i)
{      
    stringstream ss2;
    ss2 << i;
    string str_i = ss2.str();

    string outputFileName=this->rf->check("outputFileSS",Value("../../outputs/outputSS_trial"+str_i+"_object"+str_obj+".off")).
                       asString().c_str();
    string   outputFileName2=this->rf->check("outputFileDataSS",Value("../../outputs/output_dataSS_trial"+str_i+"_object"+str_obj+".off")).
                       asString().c_str();                     
	
    ofstream fout(outputFileName.c_str());
    if(fout.is_open())
    {
        fout<<get_model();
        
    }
    else
        cout<< "problem opening output_data file!";
	       
    fout.close();
	        
    ofstream fout2(outputFileName2.c_str());                                           

    if(fout2.is_open())
    {
             fout2 << "solution: "<<ms_particle.subVector(0,5).toString(3,3).c_str()<<endl;
         fout2 << "found in "<<ms_particle[7]<<" [s]"<<endl;
         fout2<< "error_index "<<ms_particle(6)<<endl;
         fout2<< "number of particles "<< number.toString().c_str()<<endl;
     }
     else
         cout<< "problem opening output_data file!";

     fout2.close();  
}

/******************************************************************************/
void ScalingSeries::saveStatisticsData(const yarp::sig::Matrix &solutions, const int &i)
{

    stringstream ss2;
    ss2 << i;
    string str_obj2 = ss2.str();


    string outputFileName2=this->rf->check("outputFileSS",Value("../../outputs/outputStatisticsSS_object"+str_obj2+".off")).
                   asString().c_str();
                       
    double average1, average_time;   
    average1=0;
    average_time=0;
    ofstream fout2(outputFileName2.c_str());                                           
 
    if(fout2.is_open())
    {
        for(int j=0; j<solutions.rows(); j++)
        {

            fout2<<"trial "<<j<<": "<<solutions.getRow(j).toString().c_str()<<endl;
            average1=average1+solutions(j,0);
            average_time=average_time+solutions(j,1);
        }

        fout2<<"average "<< average1/solutions.rows()<<endl;
        fout2<<"average time "<<average_time/solutions.rows()<<endl;

    }
}

/*******************************************************************************/    
yarp::sig::Vector ScalingSeries::localization()
{
    yarp::sig::Vector result;

    init();
    solve();
    return result=finalize();
};

    
    

 
