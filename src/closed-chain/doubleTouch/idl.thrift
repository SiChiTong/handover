# Copyright: (C) 2016 iCub Facility - Istituto Italiano di Tecnologia
# Authors: Giulia Vezzani
# CopyPolicy: Released under the terms of the GNU GPL v2.0.
#
# idl.thrift
/**
* Property
*
* IDL structure to set/show advanced parameters.
*/
struct Bottle
{
} (
   yarp.name = "yarp::os::Bottle"
   yarp.includefile="yarp/os/Bottle.h"
  )

/**
* closedChain_IDL
*
* IDL Interface to \ref closedChain services.
*/

service closedChain_IDL
{
    /**
    * Compute manipulability for all the poses
    * @entry is a Bottle containing all the poses
    * @return a Bottle with all the manipulabilities
    */
    Bottle compute_manipulability(1: Bottle entry);

/**
    * Get computed joints
    * @return a Bottle with all joints solutions
    */
    Bottle get_solutions();
  
}
