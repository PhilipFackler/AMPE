// Copyright (c) 2018, Lawrence Livermore National Security, LLC and
// UT-Battelle, LLC.
// Produced at the Lawrence Livermore National Laboratory and
// the Oak Ridge National Laboratory
// Written by M.R. Dorr, J.-L. Fattebert and M.E. Wickett
// LLNL-CODE-747500
// All rights reserved.
// This file is part of AMPE. 
// For details, see https://github.com/LLNL/AMPE
// Please also read AMPE/LICENSE.
// Redistribution and use in source and binary forms, with or without 
// modification, are permitted provided that the following conditions are met:
// - Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the disclaimer below.
// - Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the disclaimer (as noted below) in the
//   documentation and/or other materials provided with the distribution.
// - Neither the name of the LLNS/LLNL nor the names of its contributors may be
//   used to endorse or promote products derived from this software without
//   specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL LAWRENCE LIVERMORE NATIONAL SECURITY,
// LLC, UT BATTELLE, LLC, 
// THE U.S. DEPARTMENT OF ENERGY OR CONTRIBUTORS BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
// OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
// IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
// 
#include "CALPHADFreeEnergyFunctionsTernary.h"

#include "SAMRAI/SAMRAI_config.h"

#include "SAMRAI/tbox/SAMRAI_MPI.h"
#include "SAMRAI/tbox/SAMRAIManager.h"
#include "SAMRAI/tbox/PIO.h"
#include "SAMRAI/tbox/InputManager.h"
#include "SAMRAI/tbox/TimerManager.h"
#include "SAMRAI/tbox/Database.h"

#include <boost/make_shared.hpp>

#include <string>
#include <fstream>

using namespace SAMRAI;
using namespace std;

//compute mobility parameter according to
//S.G. Kim, Acta mat. 2007
int main( int argc, char *argv[] )
{
   // Initialize MPI, SAMRAI, and enable logging.

   tbox::SAMRAI_MPI::init(&argc, &argv);
   tbox::SAMRAIManager::initialize();
   tbox::SAMRAIManager::startup();

   {
   std::string input_filename = argv[1];

   //-----------------------------------------------------------------------
   // Create input database and parse all data in input file.

   boost::shared_ptr<tbox::MemoryDatabase> input_db(
      new tbox::MemoryDatabase("input_db"));
   tbox::InputManager::getManager()->parseInputFile(input_filename, input_db);

   std::string run_name =
      input_filename.substr( 0, input_filename.rfind( "." ) );

   std::string log_file_name = run_name + ".log";
   tbox::PIO::logOnlyNodeZero( log_file_name );

#ifdef GITVERSION
#define xstr(x) #x
#define LOG(x) tbox::plog<<" AMPE: git version "<<xstr(x)<<endl;
    LOG(GITVERSION);
    tbox::plog<<endl;
#endif

   tbox::plog << "input_filename = " << input_filename << endl;

   boost::shared_ptr<tbox::Database> model_db =
      input_db->getDatabase("ModelParameters");

   double phase_well_scale = model_db->getDouble( "phi_well_scale" );
   double epsilon = model_db->getDouble( "epsilon_phi" );

   string energy_interp_func_type = "pbg";
   string conc_interp_func_type   = "lin";
 
   boost::shared_ptr<tbox::Database> temperature_db =
      model_db->getDatabase( "Temperature" );
   double temperature = temperature_db->getDouble( "temperature" );

   boost::shared_ptr<tbox::Database> conc_db(
      model_db->getDatabase( "ConcentrationModel" ));
   string conc_avg_func_type =
      conc_db->getStringWithDefault( "avg_func_type", "a" );

   double mv = conc_db->getDouble("molar_volume");

   double DL = conc_db->getDouble("D_liquid");

   boost::shared_ptr<tbox::Database> dcalphad_db=
      conc_db->getDatabase( "Calphad" );
   std::string calphad_filename = dcalphad_db->getString( "filename" );
   boost::shared_ptr<tbox::MemoryDatabase> calphad_db (
      new tbox::MemoryDatabase( "calphad_db" ) );
   tbox::InputManager::getManager()->parseInputFile(
      calphad_filename, calphad_db );
   
   boost::shared_ptr<tbox::Database> newton_db;
   int maxits=20;
   if ( conc_db->isDatabase( "NewtonSolver" ) ){
      newton_db = conc_db->getDatabase( "NewtonSolver" );
      maxits= newton_db->getIntegerWithDefault("max_its",20);
   }

   CALPHADFreeEnergyFunctionsTernary
      cafe(calphad_db, newton_db,
           energy_interp_func_type,
           conc_interp_func_type);

   // initial guesses
   double init_guess[5];
   model_db->getDoubleArray("initial_guess", &init_guess[0], 5);

   double nominalc[2];
   model_db->getDoubleArray("concentration",&nominalc[0],2);
   double lceq[5]={init_guess[0], init_guess[1], // liquid
                   init_guess[2], init_guess[3], // solid
                   init_guess[4]};

   model_db->printClassData(tbox::plog);

   // choose pair of phases: phaseL, phaseA
   const PHASE_INDEX pi0=phaseL;
   const PHASE_INDEX pi1=phaseA;

   bool found_ceq =
      cafe.computeCeqT(temperature,pi0,pi1,nominalc[0],
                       nominalc[1],&lceq[0], maxits);
   if( lceq[0]>1. )found_ceq = false;
   if( lceq[0]<0. )found_ceq = false;
   if( lceq[1]>1. )found_ceq = false;
   if( lceq[1]<0. )found_ceq = false;

   if( found_ceq ){
      cout<<"For nominal composition "<<nominalc[0]<<","<<nominalc[1]
          <<", found equilibrium concentrations: "<<endl;
      cout<<"Liquid: "<<lceq[0]<<","<<lceq[1]<<endl;
      cout<<"Solid:  "<<lceq[2]<<","<<lceq[3]<<endl;
      cout<<"Solid fraction: "<<lceq[4]<<endl;

      cout<<"Interfacial energy: "
          <<epsilon*sqrt(16.*phase_well_scale)/(3.*sqrt(2.))<<" (J/m^2)"<<endl;
      cout<<"Delta: "<<epsilon/sqrt(32.*phase_well_scale)<<" (um)"<<endl;

      const PHASE_INDEX pi0=phaseL;
      std::vector<double> d2fdc2(4);
      cafe.computeSecondDerivativeFreeEnergy(
         temperature,&lceq[0],pi0,d2fdc2);

      double zeta=0.;
      for(int i=0;i<2;i++)
      for(int j=0;j<2;j++)
         zeta+=(lceq[i]-lceq[i+2])*d2fdc2[2*i+j]*(lceq[j]-lceq[j+2]);
      zeta/=DL;
      zeta*=(1.e-6/mv); // convert from J/mol to pJ/um^3

      cout<<"zeta = "<<zeta<<endl;

      double xi = epsilon/sqrt(32.*phase_well_scale);
      double a2 = 47./60.;
      double mobility=1./(3.*(2.*xi*xi)*a2*zeta);

      cout<<"mobility = "<<mobility<<endl;

   }else{
      cout<<"ERROR: Equilibrium concentrations not found... "<<endl;
      cout<<"Cannot compute mobility"<<endl;
   }

   input_db.reset();

   }

   tbox::SAMRAIManager::shutdown();
   tbox::SAMRAIManager::finalize();
   tbox::SAMRAI_MPI::finalize();

   return(0);
}
