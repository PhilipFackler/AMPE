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
#include "SteadyStateTemperatureStrategy.h"

#include "TemperatureFACSolver.h"
#include "QuatFort.h"
#include "TemperatureFACOps.h"

#include "SAMRAI/tbox/Database.h"

SteadyStateTemperatureStrategy::SteadyStateTemperatureStrategy(
   const int temperature_scratch_id,
   const int rhs_id,
   const int weight_id,
   boost::shared_ptr<tbox::Database > temperature_sys_solver_database,
   solv::LocationIndexRobinBcCoefs* bc_coefs )
{
   assert( temperature_scratch_id>=0 );
   assert( rhs_id>=0 );
   assert( weight_id>=0 );
  
   d_temperature_scratch_id = temperature_scratch_id;
   d_rhs_id         = rhs_id;
   d_weight_id      = weight_id;

   boost::shared_ptr<TemperatureFACOps> fac_ops (
      new TemperatureFACOps(
         "SteadyStateTemperatureStrategyFACOps",
         temperature_sys_solver_database ) );
   
   d_temperature_sys_solver =
      new TemperatureFACSolver(
         "SteadyStateTemperatureStrategySysSolver",
         fac_ops,
         temperature_sys_solver_database );
   
   if( bc_coefs!=0 ){
      d_temperature_sys_solver->setBcObject(bc_coefs);
   }else{
      d_temperature_sys_solver->setBoundaries( "Dirichlet" );
   }
}

//-----------------------------------------------------------------------

SteadyStateTemperatureStrategy::~SteadyStateTemperatureStrategy()
{
   delete d_temperature_sys_solver;
}

//-----------------------------------------------------------------------

void SteadyStateTemperatureStrategy::initialize(
   const boost::shared_ptr<hier::PatchHierarchy >& patch_hierarchy )
{
   int finest = patch_hierarchy->getFinestLevelNumber();

   d_temperature_sys_solver->initializeSolverState(
      d_temperature_scratch_id, d_rhs_id,
      patch_hierarchy, 0, finest );
}

//-----------------------------------------------------------------------

void SteadyStateTemperatureStrategy::resetSolversState(
   const boost::shared_ptr<hier::PatchHierarchy > hierarchy)
{
   assert ( d_temperature_sys_solver );
   d_temperature_sys_solver->
      resetSolverState(
         d_temperature_scratch_id,
         d_rhs_id,
         hierarchy);
}
