// Copyright (c) 2018, Lawrence Livermore National Security, LLC.
// Produced at the Lawrence Livermore National Laboratory
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
// LLC, THE U.S. DEPARTMENT OF ENERGY OR CONTRIBUTORS BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
// OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
// IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
// 
#ifndef included_CALPHADSpeciesPhaseGibbsEnergy
#define included_CALPHADSpeciesPhaseGibbsEnergy 

#include <boost/make_shared.hpp>
#include "SAMRAI/tbox/Database.h"
using namespace SAMRAI;

#include <vector>

class CALPHADSpeciesPhaseGibbsEnergy
{
private :

   std::string         d_name;
   std::vector<double> d_tc; 
   std::vector<double> d_a;  
   std::vector<double> d_b;  
   std::vector<double> d_c;  
   std::vector<double> d_d2; 
   std::vector<double> d_d3; 
   std::vector<double> d_d4; 
   std::vector<double> d_d7; 
   std::vector<double> d_dm1;
   std::vector<double> d_dm9;
   
   bool d_test_fenergy_done;

public :

   CALPHADSpeciesPhaseGibbsEnergy(){};

   void initialize(
      const std::string& name,
      boost::shared_ptr<tbox::Database> db );

   double fenergy( const double T ); // expect T in Kelvin
   void plotFofT(std::ostream& os);
};

#endif