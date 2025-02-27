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
#ifndef included_KKSdiluteBinary
#define included_KKSdiluteBinary

#include "KKSFreeEnergyFunctionDiluteBinary.h"
#include "FreeEnergyStrategy.h"
#include "FuncFort.h"
#include "InterpolationType.h"

#include "SAMRAI/pdat/CellData.h"
#include "SAMRAI/pdat/SideData.h"
#include "SAMRAI/tbox/Database.h"
#include "SAMRAI/hier/Box.h"
class MolarVolumeStrategy;

#include <string>
#include <vector>

class KKSdiluteBinary:
   public FreeEnergyStrategy
{
public:
   KKSdiluteBinary(
      boost::shared_ptr<tbox::Database> conc_db,
      const EnergyInterpolationType energy_interp_func_type,
      const ConcInterpolationType conc_interp_func_type,
      MolarVolumeStrategy* mvstrategy,
      const int conc_l_id,
      const int conc_a_id);

   ~KKSdiluteBinary()
   {
   };

   virtual void setup(boost::shared_ptr<tbox::Database> calphad_db);

   void computeFreeEnergyLiquid(
      const boost::shared_ptr<hier::PatchHierarchy > hierarchy,
      const int temperature_id,
      const int fl_id,
      const bool gp );

   void computeDerivFreeEnergyLiquid(
      const boost::shared_ptr<hier::PatchHierarchy > hierarchy,
      const int temperature_id,
      const int fl_id );

   void computeFreeEnergySolidA(
      const boost::shared_ptr<hier::PatchHierarchy > hierarchy,
      const int temperature_id,
      const int fs_id,
      const bool gp );

   void computeDerivFreeEnergySolidA(
      const boost::shared_ptr<hier::PatchHierarchy > hierarchy,
      const int temperature_id,
      const int fs_id );

   void computeFreeEnergySolidB(
      const boost::shared_ptr<hier::PatchHierarchy > hierarchy,
      const int temperature_id,
      const int fs_id,
      const bool gp );

   void computeDerivFreeEnergySolidB(
      const boost::shared_ptr<hier::PatchHierarchy > hierarchy,
      const int temperature_id,
      const int fs_id );

   void computeFreeEnergyLiquid(
      hier::Patch& patch,
      const int temperature_id,
      const int fl_id,
      const bool gp );

   void computeFreeEnergySolidA(
      hier::Patch& patch,
      const int temperature_id,
      const int fs_id,
      const bool gp );

   void computeFreeEnergySolidB(
      hier::Patch& patch,
      const int temperature_id,
      const int fs_id,
      const bool gp );

   virtual void addDrivingForce(
      const double time,
      hier::Patch& patch,
      const int temperature_id,
      const int phase_id,
      const int eta_id,
      const int conc_id, 
      const int f_l_id,
      const int f_a_id,
      const int f_b_id,
      const int rhs_id);

   void computeDrivingForce(
      const double time,
      hier::Patch& patch,
      const int temperature_id,
      const int phase_id,
      const int eta_id,
      const int conc_id,
      const int f_l_id,
      const int f_a_id,
      const int f_b_id,
      const int rhs_id);

   void addDrivingForceEta(
      const double time,
      hier::Patch& patch,
      const int temperature_id,
      const int phase_id,
      const int eta_id,
      const int conc_id, 
      const int f_l_id,
      const int f_a_id,
      const int f_b_id,
      const int rhs_id );

   virtual void computeSecondDerivativeEnergyPhaseL(
      const double temperature,
      const std::vector<double>& c,
      std::vector<double>& d2fdc2,
      const bool use_internal_units=true)
   {
      defaultComputeSecondDerivativeEnergyPhaseL(temperature,c,d2fdc2,use_internal_units);
   }
   virtual void computeSecondDerivativeEnergyPhaseA(
      const double temperature,
      const std::vector<double>& c,
      std::vector<double>& d2fdc2,
      const bool use_internal_units=true)
   {
      defaultComputeSecondDerivativeEnergyPhaseA(temperature,c,d2fdc2,use_internal_units);
   }
   virtual void computeSecondDerivativeEnergyPhaseB(
      const double temperature,
      const std::vector<double>& c,
      std::vector<double>& d2fdc2,
      const bool use_internal_units=true)
   {
      defaultComputeSecondDerivativeEnergyPhaseB(temperature,c,d2fdc2,use_internal_units);
   }
   
   void computeSecondDerivativeEnergyPhase(
      const char phase,
      const double temp,
      const std::vector<double>& c,
      std::vector<double>& d2fdc2,
      const bool use_internal_units)
   {
      switch( phase ){
         case 'l':
            computeSecondDerivativeEnergyPhaseL(temp,c,d2fdc2,use_internal_units);
            break;
            
         case 'a':
            computeSecondDerivativeEnergyPhaseA(temp,c,d2fdc2,use_internal_units);
            break;
            
         case 'b':
            computeSecondDerivativeEnergyPhaseB(temp,c,d2fdc2,use_internal_units);
            break;
            
         default:
            tbox::pout<<"undefined phase="<<phase<<"!!!"<<std::endl;
            tbox::SAMRAI_MPI::abort();
      }
   }
   
   void preRunDiagnostics()
   {
      d_kksdilute_fenergy->preRunDiagnostics();
   }

private:

   EnergyInterpolationType d_energy_interp_func_type;
   ConcInterpolationType d_conc_interp_func_type;

   int d_conc_l_id;
   int d_conc_a_id;

   double computeMuA(
      const double t,
      const double c );

   double computeMuL(
      const double t,
      const double c );

private:

   void defaultComputeSecondDerivativeEnergyPhaseL(
      const double temperature,
      const std::vector<double>& c,
      std::vector<double>& d2fdc2,
      const bool use_internal_units);

   void defaultComputeSecondDerivativeEnergyPhaseA(
      const double temperature,
      const std::vector<double>& c,
      std::vector<double>& d2fdc2,
      const bool use_internal_units);

   void defaultComputeSecondDerivativeEnergyPhaseB(
      const double temperature,
      const std::vector<double>& c,
      std::vector<double>& d2fdc2,
      const bool use_internal_units);

   MolarVolumeStrategy* d_mv_strategy;

   KKSFreeEnergyFunctionDiluteBinary* d_kksdilute_fenergy;
   
   double hprime(const double phi)
   {
      const char interp = energyInterpChar( d_energy_interp_func_type );
      return FORT_DERIV_INTERP_FUNC(phi, &interp);
   }

   void addDrivingForceOnPatch(
      boost::shared_ptr< pdat::CellData<double> > cd_rhs,
      boost::shared_ptr< pdat::CellData<double> > cd_temperature,
      boost::shared_ptr< pdat::CellData<double> > cd_phi,
      boost::shared_ptr< pdat::CellData<double> > cd_f_l,
      boost::shared_ptr< pdat::CellData<double> > cd_f_a,
      boost::shared_ptr< pdat::CellData<double> > cd_c_l,
      boost::shared_ptr< pdat::CellData<double> > cd_c_a,
      const hier::Box& pbox );

   void computeFreeEnergyPrivate(
      const boost::shared_ptr<hier::PatchHierarchy > hierarchy,
      const int temperature_id,
      const int f_id,
      const int c_i_id,
      const PHASE_INDEX pi,
      const bool gp );
 
   void computeDerivFreeEnergyPrivate(
      const boost::shared_ptr<hier::PatchHierarchy > hierarchy,
      const int temperature_id,
      const int f_id,
      const int c_i_id,
      const PHASE_INDEX pi );
 
   void computeFreeEnergyPrivate(
      hier::Patch& patch,
      const int temperature_id,
      const int f_id,
      const int c_i_id,
      const PHASE_INDEX pi,
      const bool gp );
 
   void computeDerivFreeEnergyPrivate(
      hier::Patch& patch,
      const int temperature_id,
      const int f_id,
      const int c_i_id,
      const PHASE_INDEX pi );
 
   void computeFreeEnergyPrivatePatch(
      const hier::Box& pbox,
      boost::shared_ptr< pdat::CellData<double> > cd_temp,
      boost::shared_ptr< pdat::CellData<double> > cd_free_energy,
      boost::shared_ptr< pdat::CellData<double> > cd_conc_i,
      const PHASE_INDEX pi,
      const bool gp );

   void computeDerivFreeEnergyPrivatePatch(
      const hier::Box& pbox,
      boost::shared_ptr< pdat::CellData<double> > cd_temp,
      boost::shared_ptr< pdat::CellData<double> > cd_free_energy,
      boost::shared_ptr< pdat::CellData<double> > cd_conc_i,
      const PHASE_INDEX pi );

   void addDrivingForceEtaOnPatchPrivate(
      boost::shared_ptr< pdat::CellData<double> > cd_rhs,
      boost::shared_ptr< pdat::CellData<double> > cd_temperature,
      boost::shared_ptr< pdat::CellData<double> > cd_phi,
      boost::shared_ptr< pdat::CellData<double> > cd_f_l,
      boost::shared_ptr< pdat::CellData<double> > cd_f_a,
      boost::shared_ptr< pdat::CellData<double> > cd_c_l,
      boost::shared_ptr< pdat::CellData<double> > cd_c_a,
      const hier::Box& pbox );
   
};

#endif
