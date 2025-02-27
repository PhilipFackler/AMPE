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
#ifndef included_CompositionStrategyMobilities
#define included_CompositionStrategyMobilities 

#include "CALPHADMobility.h"
#include "FreeEnergyStrategy.h"

#include "SAMRAI/tbox/Database.h"

#include <string>

class CompositionStrategyMobilities
{
public:
   CompositionStrategyMobilities(
      boost::shared_ptr<tbox::Database> input_db,
      const bool,
      const unsigned short ncompositions,
      FreeEnergyStrategy* free_energy_strategy
   );
   
   virtual ~CompositionStrategyMobilities(){};

   void printDiagnostics(const boost::shared_ptr<hier::PatchHierarchy > hierarchy,
                         const int temperature_scratch_id);
   void printDiagnostics(const double Tmin, const double Tmax);

   void computeDiffusionMobilityPhaseL(
      const std::vector<double>& c,
      const double temp,
      std::vector<double>& mobility);
   void computeDiffusionMobilityPhaseA(
      const std::vector<double>& c,
      const double temp,
      std::vector<double>& mobility);
   void computeDiffusionMobilityPhaseB(
      const std::vector<double>& c,
      const double temp,
      std::vector<double>& mobility);

   double computeDiffusionMobilityBinaryPhaseL(const double, const double);
   double computeDiffusionMobilityBinaryPhaseA(const double, const double);
   double computeDiffusionMobilityBinaryPhaseB(const double, const double);

protected:

   // free energy needed to compute diffusion in each phase
   FreeEnergyStrategy* d_free_energy_strategy;

   void printMobilitiesVsComposition( const double temperature, std::ostream &os );
   void printDiffusionVsComposition( const double temperature, std::ostream &os );

private:
   unsigned short d_ncompositions;

   bool d_with_third_phase;
   
   std::vector<CALPHADMobility> d_calphad_mobilities_phaseL;
   std::vector<CALPHADMobility> d_calphad_mobilities_phaseA;
   std::vector<CALPHADMobility> d_calphad_mobilities_phaseB;

   void computeDiffusionMobilityTernaryPhaseL(
      const double c0,
      const double c1,
      const double temp,
      std::vector<double>& mobility);

   void computeDiffusionMobilityTernaryPhaseA(
      const double c0,
      const double c1,
      const double temp,
      std::vector<double>& mobility);

   void computeDiffusionMobilityTernaryPhaseB(
      const double c0,
      const double c1,
      const double temp,
      std::vector<double>& mobility);

   void computeDiffusionMobilityPhase(
      const char phase,
      const std::vector<double>& c,
      const double temp,
      std::vector<double>& mobility)
   {
      switch( phase ){
         case 'l':
            computeDiffusionMobilityPhaseL(c,temp,mobility);
            break;
            
         case 'a':
            computeDiffusionMobilityPhaseA(c,temp,mobility);
            break;
            
         case 'b':
            computeDiffusionMobilityPhaseB(c,temp,mobility);
            break;
            
         default:
            tbox::pout<<"CompositionStrategyMobilities::computeDiffusionMobilityPhase(), Error: phase="<<phase<<"!!!"<<std::endl;
            tbox::SAMRAI_MPI::abort();
      }
   }
};

#endif

