c Copyright (c) 2018, Lawrence Livermore National Security, LLC.
c Produced at the Lawrence Livermore National Laboratory
c Written by M.R. Dorr, J.-L. Fattebert and M.E. Wickett
c LLNL-CODE-747500
c All rights reserved.
c This file is part of AMPE. 
c For details, see https://github.com/LLNL/AMPE
c Please also read AMPE/LICENSE.
c Redistribution and use in source and binary forms, with or without 
c modification, are permitted provided that the following conditions are met:
c - Redistributions of source code must retain the above copyright notice,
c   this list of conditions and the disclaimer below.
c - Redistributions in binary form must reproduce the above copyright notice,
c   this list of conditions and the disclaimer (as noted below) in the
c   documentation and/or other materials provided with the distribution.
c - Neither the name of the LLNS/LLNL nor the names of its contributors may be
c   used to endorse or promote products derived from this software without
c   specific prior written permission.
c
c THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
c AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
c IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
c ARE DISCLAIMED. IN NO EVENT SHALL LAWRENCE LIVERMORE NATIONAL SECURITY,
c LLC, THE U.S. DEPARTMENT OF ENERGY OR CONTRIBUTORS BE LIABLE FOR ANY
c DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
c DAMAGES  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
c OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
c HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
c STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
c IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
c POSSIBILITY OF SUCH DAMAGE.
c 
define(NDIM,2)dnl
include(SAMRAI_FORTDIR/pdat_m4arrdim2d.i)dnl
c        
c        Correct r.h.s. for quaternion implicit time evolution equation
c        Generalization of method proposed by Warren, Kobayashi et al., 
c        Act. Mater.51 (2003)
c        
      subroutine correctrhsquatforsymmetry(
     &   lo0, hi0, lo1, hi1,
     &   depth, dx,
     &   nonsymm_diff_x, nonsymm_diff_y,
     &   symm_diff_x, symm_diff_y,
     &   ngdiff,
     &   rhs, ngrhs,
     &   quat, ngq,
     &   facecoeffx, facecoeffy, ngfacecoeff,
     &   mobility, ngmob,
     &   iqrot_x, iqrot_y, ngiq
     &   )

      implicit none

      integer
     &   lo0, lo1, hi0, hi1,
     &   depth, ngdiff, ngrhs, ngq, ngfacecoeff, ngmob, ngiq

      double precision dx(NDIM)

      double precision nonsymm_diff_x(SIDE2d0(lo,hi,ngdiff),depth)
      double precision nonsymm_diff_y(SIDE2d1(lo,hi,ngdiff),depth)

      double precision symm_diff_x(SIDE2d0(lo,hi,ngdiff),depth)
      double precision symm_diff_y(SIDE2d1(lo,hi,ngdiff),depth)

      double precision rhs(CELL2d(lo,hi,ngrhs),depth)

      double precision quat(CELL2d(lo,hi,ngq),depth)

      double precision facecoeffx(SIDE2d0(lo,hi,ngfacecoeff),depth)
      double precision facecoeffy(SIDE2d1(lo,hi,ngfacecoeff),depth)
      
      double precision mobility(CELL2d(lo,hi,ngmob))
      
      integer iqrot_x(SIDE2d0(lo,hi,ngiq))
      integer iqrot_y(SIDE2d1(lo,hi,ngiq))

      integer i, j, m, iq
      double precision beta, lambda
      double precision invdx2(NDIM)
      double precision tmp(depth)
      double precision dtmp_x(depth), dtmp_y(depth)
      double precision dprime_x(depth), dprime_y(depth)
      
      invdx2(1) = 1.d0 / ( dx(1) * dx(1) )
      invdx2(2) = 1.d0 / ( dx(2) * dx(2) )

      do j = lo1, hi1
         do i = lo0, hi0         
               
            if ( depth > 1 ) then

c                 Diffs on right side need to have the conjugate
c                 of the symmetry rotation applied before using, but
c                 only for the vector/quaternion orientation.

               do m = 1, depth
                  dtmp_x(m) = symm_diff_x(i+1,j,m)
                  dtmp_y(m) = symm_diff_y(i,j+1,m)
               enddo
               
               iq = -1 * iqrot_x(i+1,j)
               call quatsymmrotate( dtmp_x, iq, dprime_x, depth )

               iq = -1 * iqrot_y(i,j+1)
               call quatsymmrotate( dtmp_y, iq, dprime_y, depth )

            else

               dprime_x(1) = symm_diff_x(i+1,j,1)
               dprime_y(1) = symm_diff_y(i,j+1,1)

            endif

            do m = 1, depth

               tmp(m) = 
     &            invdx2(1) * (
     &              facecoeffx(i+1,j,m) *
     &                ( nonsymm_diff_x(i+1,j,m) - dprime_x(m) )
     &              -
     &              facecoeffx(i,j,m) * 
     &                ( nonsymm_diff_x(i,j,m) - symm_diff_x(i,j,m) )
     &            )
     &            +
     &            invdx2(2) * (
     &              facecoeffy(i,j+1,m) *
     &                ( nonsymm_diff_y(i,j+1,m) - dprime_y(m) )
     &              -
     &              facecoeffy(i,j,m) *
     &                ( nonsymm_diff_y(i,j,m) - symm_diff_y(i,j,m) )
     &            )

            enddo

c subtract projection of tmp along quat
            if ( depth > 1 ) then

               beta = 0.d0
               lambda = 0.d0

               do m = 1, depth
                  beta   = beta   + quat(i,j,m) * quat(i,j,m)
                  lambda = lambda + quat(i,j,m) * tmp(m)
               enddo

               lambda = lambda / beta

               do m = 1, depth
                  rhs(i,j,m) = rhs(i,j,m) +
     &               mobility(i,j) * ( tmp(m) - lambda * quat(i,j,m) )
               enddo

            else

               rhs(i,j,1) = rhs(i,j,1) + mobility(i,j) * tmp(1)

            endif

         enddo
      enddo

      return
      end
