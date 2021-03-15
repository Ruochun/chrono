// =============================================================================
// PROJECT CHRONO - http://projectchrono.org
//
// Copyright (c) 2021 projectchrono.org
// All right reserved.
//
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file at the top level of the distribution and at
// http://projectchrono.org/license-chrono.txt.
//
// =============================================================================
// Authors: Jason Zhou
// =============================================================================
//
// Demo to show the steering capability of the Curiosity Rover on SCM terrain
// Demo includes slight turning and spinning of the rover
//
// =============================================================================

#include "chrono_models/robot/curiosity/Curiosity.h"

#include "chrono/geometry/ChTriangleMeshConnected.h"
#include "chrono/physics/ChLinkMotorRotationAngle.h"
#include "chrono/physics/ChLoadContainer.h"
#include "chrono/physics/ChSystemSMC.h"
#include "chrono/utils/ChUtilsInputOutput.h"

#include "chrono/physics/ChSystemNSC.h"
#include "chrono/physics/ChBodyEasy.h"
#include "chrono/physics/ChInertiaUtils.h"
#include "chrono/assets/ChTexture.h"
#include "chrono/assets/ChTriangleMeshShape.h"
#include "chrono/geometry/ChTriangleMeshConnected.h"

#include "chrono/utils/ChUtilsCreators.h"
#include "chrono/utils/ChUtilsGenerators.h"
#include "chrono/utils/ChUtilsGeometry.h"
#include "chrono/utils/ChUtilsInputOutput.h"
#include "chrono/assets/ChBoxShape.h"
#include "chrono/physics/ChParticlesClones.h"
#include "chrono/physics/ChLinkMotorRotationSpeed.h"
#include "chrono/physics/ChLinkMotorRotationTorque.h"
#include "chrono/physics/ChLinkDistance.h"

#include "chrono_irrlicht/ChIrrApp.h"

#include "chrono_vehicle/ChVehicleModelData.h"
#include "chrono_vehicle/terrain/SCMDeformableTerrain.h"

#include "chrono_thirdparty/filesystem/path.h"

#include <chrono>

using namespace chrono;
using namespace chrono::irrlicht;
using namespace chrono::geometry;
using namespace chrono::curiosity;

using namespace irr;

bool output = false;
const std::string out_dir = GetChronoOutputPath() + "SCM_DEF_SOIL";

// SCM grid spacing
double mesh_resolution = 0.02;

// Enable/disable bulldozing effects
bool enable_bulldozing = true;

// Enable/disable moving patch feature
bool enable_moving_patch = true;

// If true, use provided callback to change soil properties based on location
bool var_params = true;

// Custom callback for setting location-dependent soil properties.
// Note that the (x,y) location is given in the terrain's reference plane.
// Here, the vehicle moves in the terrain's negative y direction!
class MySoilParams : public vehicle::SCMDeformableTerrain::SoilParametersCallback {
  public:
    virtual void Set(double x, double y) override {
        m_Bekker_Kphi = 0.82e6;
        m_Bekker_Kc = 0.14e4;
        m_Bekker_n = 1.0;
        m_Mohr_cohesion = 0.017e4;
        m_Mohr_friction = 35.0;
        m_Janosi_shear = 1.78e-2;
        m_elastic_K = 2e8;
        m_damping_R = 3e4;
    }
};

// Use custom material for the Viper Wheel
bool use_custom_mat = true;

// Return customized wheel material parameters
std::shared_ptr<ChMaterialSurface> CustomWheelMaterial(ChContactMethod contact_method) {
    float mu = 0.65f;   // coefficient of friction
    float cr = 0.1f;   // coefficient of restitution
    float Y = 2e7f;    // Young's modulus
    float nu = 0.3f;   // Poisson ratio
    float kn = 2e5f;   // normal stiffness
    float gn = 40.0f;  // normal viscous damping
    float kt = 2e5f;   // tangential stiffness
    float gt = 20.0f;  // tangential viscous damping

    switch (contact_method) {
        case ChContactMethod::NSC: {
            auto matNSC = chrono_types::make_shared<ChMaterialSurfaceNSC>();
            matNSC->SetFriction(mu);
            matNSC->SetRestitution(cr);
            return matNSC;
        }
        case ChContactMethod::SMC: {
            auto matSMC = chrono_types::make_shared<ChMaterialSurfaceSMC>();
            matSMC->SetFriction(mu);
            matSMC->SetRestitution(cr);
            matSMC->SetYoungModulus(Y);
            matSMC->SetPoissonRatio(nu);
            matSMC->SetKn(kn);
            matSMC->SetGn(gn);
            matSMC->SetKt(kt);
            matSMC->SetGt(gt);
            return matSMC;
        }
        default:
            return std::shared_ptr<ChMaterialSurface>();
    }
}

int main(int argc, char* argv[]) {
    GetLog() << "Copyright (c) 2017 projectchrono.org\nChrono version: " << CHRONO_VERSION << "\n\n";

    // Global parameter for moving patch size:
    double wheel_range = 0.5;
    double body_range = 1.2;

    // Create a Chrono::Engine physical system
    ChSystemSMC my_system;

    // Create the Irrlicht visualization (open the Irrlicht device,
    // bind a simple user interface, etc. etc.)
    ChIrrApp application(&my_system, L"Curiosity Steering Maneuver", core::dimension2d<u32>(1800, 1000), VerticalDir::Y, false, true);
    application.AddTypicalSky();
    application.AddTypicalLights();
    application.AddTypicalCamera(core::vector3df(2.0f, 1.4f, 0.0f), core::vector3df(0, (f32)wheel_range, 0));
    application.AddLightWithShadow(core::vector3df(-5.0f, 8.0f, -0.5f), core::vector3df(-1.0, 0, 0), 100, 1, 35, 100, 512,
                                   video::SColorf(0.8f, 0.8f, 1.0f));

    // Initialize output
    if (output) {
        if (!filesystem::create_directory(filesystem::path(out_dir))) {
            std::cout << "Error creating directory " << out_dir << std::endl;
            return 1;
        }
    }
    utils::CSV_writer csv(" ");

    // Viper rover initial position and orientation
    ChVector<double> body_pos(-5, -0.2, 0);
    ChQuaternion<> body_rot = Q_from_AngX(-CH_C_PI / 2);

    std::shared_ptr<CuriosityRover> rover;

    if (use_custom_mat == true) {
        // if customize wheel material
        rover = chrono_types::make_shared<CuriosityRover>(&my_system, body_pos, body_rot, CustomWheelMaterial(ChContactMethod::SMC));

        // the user can choose to enable DC motor option
        // if the DC motor option has been enabled, the rotational speed will be switched to no-load-speed of the DC motor
        // defaut linear relationship is set to stall torque 1000 N-m, and no load speed 3.1415 rad/s
        rover->SetDCControl(true);
        rover->Initialize();

        // Default value is w = 3.1415 rad/s
        // User can define using SetMotorSpeed
        // curiosity->SetMotorSpeed(CH_C_PI,WheelID::LF);
        // curiosity->SetMotorSpeed(CH_C_PI,WheelID::RF);
        // curiosity->SetMotorSpeed(CH_C_PI,WheelID::LM);
        // curiosity->SetMotorSpeed(CH_C_PI,WheelID::RM);
        // curiosity->SetMotorSpeed(CH_C_PI,WheelID::LB);
        // curiosity->SetMotorSpeed(CH_C_PI,WheelID::RB);

    } else {
        // if use default material
        rover = chrono_types::make_shared<CuriosityRover>(&my_system, body_pos, body_rot);

        // the user can choose to enable DC motor option
        // if the DC motor option has been enabled, the rotational speed will be switched to no-load-speed of the DC motor
        // defaut linear relationship is set to stall torque 1000 N-m, and no load speed 3.1415 rad/s
        rover->SetDCControl(true);
        rover->Initialize();

        // Default value is w = 3.1415 rad/s
        // User can define using SetMotorSpeed
        // curiosity->SetMotorSpeed(CH_C_PI,WheelID::LF);
        // curiosity->SetMotorSpeed(CH_C_PI,WheelID::RF);
        // curiosity->SetMotorSpeed(CH_C_PI,WheelID::LM);
        // curiosity->SetMotorSpeed(CH_C_PI,WheelID::RM);
        // curiosity->SetMotorSpeed(CH_C_PI,WheelID::LB);
        // curiosity->SetMotorSpeed(CH_C_PI,WheelID::RB);
    }

    //
    // THE DEFORMABLE TERRAIN
    //

    // Create the 'deformable terrain' object
    vehicle::SCMDeformableTerrain mterrain(&my_system);

    // Displace/rotate the terrain reference plane.
    // Note that SCMDeformableTerrain uses a default ISO reference frame (Z up). Since the mechanism is modeled here in
    // a Y-up global frame, we rotate the terrain plane by -90 degrees about the X axis.
    // Note: Irrlicht uses a Y-up frame
    mterrain.SetPlane(ChCoordsys<>(ChVector<>(0, -0.5, 0), Q_from_AngX(-CH_C_PI_2)));

    // Use a regular grid:
    double length = 15;
    double width = 15;
    mterrain.Initialize(length, width, mesh_resolution);

    // Set the soil terramechanical parameters
    if (var_params) {
        // Here we use the soil callback defined at the beginning of the code
        auto my_params = chrono_types::make_shared<MySoilParams>();
        mterrain.RegisterSoilParametersCallback(my_params);
    } else {
        // If var_params is set to be false, these parameters will be used
        mterrain.SetSoilParameters(0.2e6,  // Bekker Kphi
                                   0,      // Bekker Kc
                                   1.1,    // Bekker n exponent
                                   0,      // Mohr cohesive limit (Pa)
                                   30,     // Mohr friction limit (degrees)
                                   0.01,   // Janosi shear coefficient (m)
                                   4e7,    // Elastic stiffness (Pa/m), before plastic yield, must be > Kphi
                                   3e4     // Damping (Pa s/m), proportional to negative vertical speed (optional)
        );
    }

    // Set up bulldozing factors
    if (enable_bulldozing) {
        mterrain.EnableBulldozing(true);  // inflate soil at the border of the rut
        mterrain.SetBulldozingParameters(
            55,  // angle of friction for erosion of displaced material at the border of the rut
            1,   // displaced material vs downward pressed material.
            5,   // number of erosion refinements per timestep
            6);  // number of concentric vertex selections subject to erosion
    }

    // We need to add a moving patch under every wheel
    // Or we can define a large moving patch at the pos of the rover body
    if (enable_moving_patch) {
        // add moving patch for the SCM terrain
        // the bodies were retrieved from the rover instance
        mterrain.AddMovingPatch(rover->GetWheelBody(WheelID::LF), ChVector<>(0, 0, 0), ChVector<>(0.5, 2 * wheel_range, 2 * wheel_range));
        mterrain.AddMovingPatch(rover->GetWheelBody(WheelID::RF), ChVector<>(0, 0, 0), ChVector<>(0.5, 2 * wheel_range, 2 * wheel_range));
        mterrain.AddMovingPatch(rover->GetWheelBody(WheelID::LM), ChVector<>(0, 0, 0), ChVector<>(0.5, 2 * wheel_range, 2 * wheel_range));
        mterrain.AddMovingPatch(rover->GetWheelBody(WheelID::RM), ChVector<>(0, 0, 0), ChVector<>(0.5, 2 * wheel_range, 2 * wheel_range));
        mterrain.AddMovingPatch(rover->GetWheelBody(WheelID::LB), ChVector<>(0, 0, 0), ChVector<>(0.5, 2 * wheel_range, 2 * wheel_range));
        mterrain.AddMovingPatch(rover->GetWheelBody(WheelID::RB), ChVector<>(0, 0, 0), ChVector<>(0.5, 2 * wheel_range, 2 * wheel_range));
    }

    // Set some visualization parameters: either with a texture, or with falsecolor plot, etc.
    mterrain.SetPlotType(vehicle::SCMDeformableTerrain::PLOT_PRESSURE, 0, 20000);

    mterrain.GetMesh()->SetWireframe(true);

    // ==IMPORTANT!== Use this function for adding a ChIrrNodeAsset to all items
    application.AssetBindAll();

    // ==IMPORTANT!== Use this function for 'converting' into Irrlicht meshes the assets
    application.AssetUpdateAll();

    // Use shadows in realtime view
    application.AddShadowAll();

    application.SetTimestep(0.001);

    int step = 0;

    // this state number helps to identify the process of the demo
    // state 0 : initial state, after step 1000, start the one-side turning procedure, steering motors start rotating
    // state 1 : one-side turning lock stage, after step 1800 steering motors are locked, rover will keep the one-side turning procedure
    // state 2 : after step 6000, stop main motors, steers return to orginal position
    // state 3 : stop steer rotation
    // state 4 : start preparing for rover spin turning
    // state 5 : lock steers when rover is in position
    // state 6 : start spin turning
    int state = 0;

    while (application.GetDevice()->run()) {
        if (output) {
            // vehicle::TerrainForce frc = mterrain.GetContactForce(mrigidbody);
            // csv << my_system.GetChTime() << frc.force << frc.moment << frc.point << std::endl;
        }

        if(step == 1000){
            rover->SetSteerSpeed(CH_C_PI/8,WheelID::LF);
            rover->SetSteerSpeed(CH_C_PI/8,WheelID::RF);
            rover->SetSteerSpeed(-CH_C_PI/8,WheelID::LB);
            rover->SetSteerSpeed(-CH_C_PI/8,WheelID::RB);
            state = 1;
            std::cout<<"Entering state 1"<<std::endl;
        }

        if(step == 1800){
            rover->SetSteerSpeed(0,WheelID::LF);
            rover->SetSteerSpeed(0,WheelID::RF);
            rover->SetSteerSpeed(0,WheelID::LB);
            rover->SetSteerSpeed(0,WheelID::RB);
            state = 2;
            std::cout<<"Entering state 2"<<std::endl;
        }

        if(step == 6000){
            rover->SetMotorSpeed(0,WheelID::LF);
            rover->SetMotorSpeed(0,WheelID::RF);
            rover->SetMotorSpeed(0,WheelID::LM);
            rover->SetMotorSpeed(0,WheelID::RM);
            rover->SetMotorSpeed(0,WheelID::LB);
            rover->SetMotorSpeed(0,WheelID::RB);
            rover->SetSteerSpeed(-CH_C_PI/8,WheelID::LF);
            rover->SetSteerSpeed(-CH_C_PI/8,WheelID::RF);
            rover->SetSteerSpeed(CH_C_PI/8,WheelID::LB);
            rover->SetSteerSpeed(CH_C_PI/8,WheelID::RB);
            state = 3;
            std::cout<<"Entering state 3"<<std::endl;
        }

        if(state == 3 && (abs(rover->GetSteerAngle(WheelID::LF) - 0) < 1e-3)){
            rover->SetSteerSpeed(0,WheelID::LF);
            rover->SetSteerSpeed(0,WheelID::RF);
            rover->SetSteerSpeed(0,WheelID::LB);
            rover->SetSteerSpeed(0,WheelID::RB);
            state = 4;
            std::cout<<"Entering state 4"<<std::endl;
        }

        if(state == 4){
            rover->SetSteerSpeed(-CH_C_PI/8,WheelID::LF);
            rover->SetSteerSpeed(CH_C_PI/8,WheelID::RF);
            rover->SetSteerSpeed(CH_C_PI/8,WheelID::LB);
            rover->SetSteerSpeed(-CH_C_PI/8,WheelID::RB);
            state = 5;
            std::cout<<"Entering state 5"<<std::endl;
        }

        if(state == 5 && (abs(rover->GetSteerAngle(WheelID::LF) - (-CH_C_PI/9)) < 1e-3)){
            rover->SetSteerSpeed(0,WheelID::LF);
            rover->SetSteerSpeed(0,WheelID::RF);
            rover->SetSteerSpeed(0,WheelID::LB);
            rover->SetSteerSpeed(0,WheelID::RB);
            state = 6;
            std::cout<<"Entering state 6"<<std::endl;
        }

        if(state == 6){
            rover->SetMotorSpeed(-CH_C_PI,WheelID::LF);
            rover->SetMotorSpeed(-CH_C_PI,WheelID::LM);
            rover->SetMotorSpeed(-CH_C_PI,WheelID::LB);
            rover->SetMotorSpeed(CH_C_PI,WheelID::RF);
            rover->SetMotorSpeed(CH_C_PI,WheelID::RM);
            rover->SetMotorSpeed(CH_C_PI,WheelID::RB);
        }

        rover->Update();

        application.BeginScene();

        application.GetSceneManager()->getActiveCamera()->setTarget(core::vector3dfCH(rover->GetChassisBody()->GetPos()));
        application.DrawAll();

        application.DoStep();
        tools::drawColorbar(0, 20000, "Pressure yield [Pa]", application.GetDevice(), 1600);
        application.EndScene();

        ////mterrain.PrintStepStatistics(std::cout);
        step = step + 1;
    }

    if (output) {
        csv.write_to_file(out_dir + "/output.dat");
    }

    return 0;
}