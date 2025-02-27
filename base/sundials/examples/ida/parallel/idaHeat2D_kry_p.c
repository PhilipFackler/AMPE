/*
 * -----------------------------------------------------------------
 * $Revision: 1.1 $
 * $Date: 2007/10/25 20:03:35 $
 * -----------------------------------------------------------------
 * Programmer(s): Allan Taylor, Alan Hindmarsh and
 *                Radu Serban @ LLNL
 * -----------------------------------------------------------------
 * Example problem for IDA: 2D heat equation, parallel, GMRES.
 *
 * This example solves a discretized 2D heat equation problem.
 * This version uses the Krylov solver IDASpgmr.
 *
 * The DAE system solved is a spatial discretization of the PDE
 *          du/dt = d^2u/dx^2 + d^2u/dy^2
 * on the unit square. The boundary condition is u = 0 on all edges.
 * Initial conditions are given by u = 16 x (1 - x) y (1 - y).
 * The PDE is treated with central differences on a uniform MX x MY
 * grid. The values of u at the interior points satisfy ODEs, and
 * equations u = 0 at the boundaries are appended, to form a DAE
 * system of size N = MX * MY. Here MX = MY = 10.
 *
 * The system is actually implemented on submeshes, processor by
 * processor, with an MXSUB by MYSUB mesh on each of NPEX * NPEY
 * processors.
 *
 * The system is solved with IDA using the Krylov linear solver
 * IDASPGMR. The preconditioner uses the diagonal elements of the
 * Jacobian only. Routines for preconditioning, required by
 * IDASPGMR, are supplied here. The constraints u >= 0 are posed
 * for all components. Local error testing on the boundary values
 * is suppressed. Output is taken at t = 0, .01, .02, .04,
 * ..., 10.24.
 * -----------------------------------------------------------------
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <ida/ida.h>
#include <ida/ida_spgmr.h>
#include <nvector/nvector_parallel.h>
#include <sundials/sundials_types.h>
#include <sundials/sundials_math.h>

#include <mpi.h>

#define ZERO  RCONST(0.0)
#define ONE   RCONST(1.0)
#define TWO   RCONST(2.0)

#define NOUT         11             /* Number of output times */

#define NPEX         2              /* No. PEs in x direction of PE array */
#define NPEY         2              /* No. PEs in y direction of PE array */
                                    /* Total no. PEs = NPEX*NPEY */
#define MXSUB        5              /* No. x points per subgrid */
#define MYSUB        5              /* No. y points per subgrid */

#define MX           (NPEX*MXSUB)   /* MX = number of x mesh points */
#define MY           (NPEY*MYSUB)   /* MY = number of y mesh points */
                                    /* Spatial mesh is MX by MY */

typedef struct {  
  long int thispe, mx, my, ixsub, jysub, npex, npey, mxsub, mysub;
  realtype    dx, dy, coeffx, coeffy, coeffxy;
  realtype    uext[(MXSUB+2)*(MYSUB+2)];
  N_Vector    pp;    /* vector of diagonal preconditioner elements */
  MPI_Comm    comm;
} *UserData;

/* User-supplied residual function and supporting routines */

int resHeat(realtype tt, 
            N_Vector uu, N_Vector up, N_Vector rr, 
            void *user_data);

static int rescomm(N_Vector uu, N_Vector up, void *user_data);

static int reslocal(realtype tt, N_Vector uu, N_Vector up, 
                    N_Vector res,  void *user_data);

static int BSend(MPI_Comm comm, long int thispe, long int ixsub, long int jysub,
                 long int dsizex, long int dsizey, realtype uarray[]);

static int BRecvPost(MPI_Comm comm, MPI_Request request[], long int thispe,
                     long int ixsub, long int jysub,
                     long int dsizex, long int dsizey,
                     realtype uext[], realtype buffer[]);

static int BRecvWait(MPI_Request request[], long int ixsub, long int jysub,
                     long int dsizex, realtype uext[], realtype buffer[]);

/* User-supplied preconditioner routines */

int PsolveHeat(realtype tt, 
               N_Vector uu, N_Vector up, N_Vector rr, 
               N_Vector rvec, N_Vector zvec,
               realtype c_j, realtype delta, void *user_data, 
               N_Vector tmp);

int PsetupHeat(realtype tt, 
               N_Vector yy, N_Vector yp, N_Vector rr, 
               realtype c_j, void *user_data,
               N_Vector tmp1, N_Vector tmp2, N_Vector tmp3);

/* Private function to check function return values */

static int InitUserData(int thispe, MPI_Comm comm, UserData data);

static int SetInitialProfile(N_Vector uu, N_Vector up, N_Vector id,
                             N_Vector res, UserData data);

static void PrintHeader(long int Neq, realtype rtol, realtype atol);

static void PrintOutput(int id, void *mem, realtype t, N_Vector uu);

static void PrintFinalStats(void *mem);

static int check_flag(void *flagvalue, char *funcname, int opt, int id);

/*
 *--------------------------------------------------------------------
 * MAIN PROGRAM
 *--------------------------------------------------------------------
 */

int main(int argc, char *argv[])
{
  MPI_Comm comm;
  void *mem;
  UserData data;
  int iout, thispe, ier, npes;
  long int Neq, local_N;
  realtype rtol, atol, t0, t1, tout, tret;
  N_Vector uu, up, constraints, id, res;

  mem = NULL;
  data = NULL;
  uu = up = constraints = id = res = NULL;

  /* Get processor number and total number of pe's. */

  MPI_Init(&argc, &argv);
  comm = MPI_COMM_WORLD;
  MPI_Comm_size(comm, &npes);
  MPI_Comm_rank(comm, &thispe);
  
  if (npes != NPEX*NPEY) {
    if (thispe == 0)
      fprintf(stderr, 
              "\nMPI_ERROR(0): npes = %d is not equal to NPEX*NPEY = %d\n", 
              npes,NPEX*NPEY);
    MPI_Finalize();
    return(1);
  }
  
  /* Set local length local_N and global length Neq. */

  local_N = MXSUB*MYSUB;
  Neq     = MX * MY;
  
  /* Allocate and initialize the data structure and N-vectors. */

  data = (UserData) malloc(sizeof *data);
  data->pp = NULL;
  if(check_flag((void *)data, "malloc", 2, thispe)) 
    MPI_Abort(comm, 1);

  uu = N_VNew_Parallel(comm, local_N, Neq);
  if(check_flag((void *)uu, "N_VNew_Parallel", 0, thispe)) 
    MPI_Abort(comm, 1);

  up = N_VNew_Parallel(comm, local_N, Neq);
  if(check_flag((void *)up, "N_VNew_Parallel", 0, thispe)) 
    MPI_Abort(comm, 1);

  res = N_VNew_Parallel(comm, local_N, Neq);
  if(check_flag((void *)res, "N_VNew_Parallel", 0, thispe)) 
    MPI_Abort(comm, 1);

  constraints = N_VNew_Parallel(comm, local_N, Neq);
  if(check_flag((void *)constraints, "N_VNew_Parallel", 0, thispe)) 
    MPI_Abort(comm, 1);

  id = N_VNew_Parallel(comm, local_N, Neq);
  if(check_flag((void *)id, "N_VNew_Parallel", 0, thispe)) 
    MPI_Abort(comm, 1);

  /* An N-vector to hold preconditioner. */
  data->pp = N_VNew_Parallel(comm, local_N, Neq);
  if(check_flag((void *)data->pp, "N_VNew_Parallel", 0, thispe)) 
    MPI_Abort(comm, 1);

  InitUserData(thispe, comm, data);
  
  /* Initialize the uu, up, id, and res profiles. */

  SetInitialProfile(uu, up, id, res, data);
  
  /* Set constraints to all 1's for nonnegative solution values. */

  N_VConst(ONE, constraints);
  
  t0 = ZERO; t1 = RCONST(0.01);
  
  /* Scalar relative and absolute tolerance. */

  rtol = ZERO;
  atol = RCONST(1.0e-3);

  /* Call IDACreate and IDAMalloc to initialize solution. */

  mem = IDACreate();
  if(check_flag((void *)mem, "IDACreate", 0, thispe)) MPI_Abort(comm, 1);

  ier = IDASetUserData(mem, data);
  if(check_flag(&ier, "IDASetUserData", 1, thispe)) MPI_Abort(comm, 1);

  ier = IDASetSuppressAlg(mem, TRUE);
  if(check_flag(&ier, "IDASetSuppressAlg", 1, thispe)) MPI_Abort(comm, 1);

  ier = IDASetId(mem, id);
  if(check_flag(&ier, "IDASetId", 1, thispe)) MPI_Abort(comm, 1);

  ier = IDASetConstraints(mem, constraints);
  if(check_flag(&ier, "IDASetConstraints", 1, thispe)) MPI_Abort(comm, 1);
  N_VDestroy_Parallel(constraints);  

  ier = IDAInit(mem, resHeat, t0, uu, up);
  if(check_flag(&ier, "IDAInit", 1, thispe)) MPI_Abort(comm, 1);
  
  ier = IDASStolerances(mem, rtol, atol);
  if(check_flag(&ier, "IDASStolerances", 1, thispe)) MPI_Abort(comm, 1);

  /* Call IDASpgmr to specify the linear solver. */

  ier = IDASpgmr(mem, 0);
  if(check_flag(&ier, "IDASpgmr", 1, thispe)) MPI_Abort(comm, 1);

  ier = IDASpilsSetPreconditioner(mem, PsetupHeat, PsolveHeat);
  if(check_flag(&ier, "IDASpilsSetPreconditioner", 1, thispe)) MPI_Abort(comm, 1);

  /* Print output heading (on processor 0 only) and intial solution  */
  
  if (thispe == 0) PrintHeader(Neq, rtol, atol);
  PrintOutput(thispe, mem, t0, uu); 
  
  /* Loop over tout, call IDASolve, print output. */

  for (tout = t1, iout = 1; iout <= NOUT; iout++, tout *= TWO) {

    ier = IDASolve(mem, tout, &tret, uu, up, IDA_NORMAL);
    if(check_flag(&ier, "IDASolve", 1, thispe)) MPI_Abort(comm, 1);

    PrintOutput(thispe, mem, tret, uu);

  }
  
  /* Print remaining counters. */

  if (thispe == 0) PrintFinalStats(mem);

  /* Free memory */

  IDAFree(&mem);

  N_VDestroy_Parallel(id);
  N_VDestroy_Parallel(res);
  N_VDestroy_Parallel(up);
  N_VDestroy_Parallel(uu);

  N_VDestroy_Parallel(data->pp);
  free(data);

  MPI_Finalize();

  return(0);

}

/*
 *--------------------------------------------------------------------
 * FUNCTIONS CALLED BY IDA
 *--------------------------------------------------------------------
 */

/*
 * resHeat: heat equation system residual function                       
 * This uses 5-point central differencing on the interior points, and    
 * includes algebraic equations for the boundary values.                 
 * So for each interior point, the residual component has the form       
 *    res_i = u'_i - (central difference)_i                              
 * while for each boundary point, it is res_i = u_i. 
 *                    
 * This parallel implementation uses several supporting routines. 
 * First a call is made to rescomm to do communication of subgrid boundary
 * data into array uext.  Then reslocal is called to compute the residual
 * on individual processors and their corresponding domains.  The routines
 * BSend, BRecvPost, and BREcvWait handle interprocessor communication
 * of uu required to calculate the residual. 
 */

int resHeat(realtype tt, 
            N_Vector uu, N_Vector up, N_Vector rr, 
            void *user_data)
{
  int retval;
  
  /* Call rescomm to do inter-processor communication. */
  retval = rescomm(uu, up, user_data);

  /* Call reslocal to calculate res. */
  retval = reslocal(tt, uu, up, rr, user_data);
  
  return(0);

}

/*
 * PsetupHeat: setup for diagonal preconditioner for heatsk.    
 *                                                                 
 * The optional user-supplied functions PsetupHeat and          
 * PsolveHeat together must define the left preconditoner        
 * matrix P approximating the system Jacobian matrix               
 *                   J = dF/du + cj*dF/du'                         
 * (where the DAE system is F(t,u,u') = 0), and solve the linear   
 * systems P z = r.   This is done in this case by keeping only    
 * the diagonal elements of the J matrix above, storing them as    
 * inverses in a vector pp, when computed in PsetupHeat, for    
 * subsequent use in PsolveHeat.                                 
 *                                                                 
 * In this instance, only cj and data (user data structure, with    
 * pp etc.) are used from the PsetupHeat argument list.         
 *
 */

int PsetupHeat(realtype tt, 
               N_Vector yy, N_Vector yp, N_Vector rr, 
               realtype c_j, void *user_data,
               N_Vector tmp1, N_Vector tmp2, N_Vector tmp3)
{
  realtype *ppv, pelinv;
  long int lx, ly, ixbegin, ixend, jybegin, jyend, locu, mxsub, mysub;
  long int ixsub, jysub, npex, npey;
  UserData data;

  data = (UserData) user_data;

  ppv = NV_DATA_P(data->pp);
  ixsub = data->ixsub;
  jysub = data->jysub;
  mxsub = data->mxsub;
  mysub = data->mysub;
  npex  = data->npex;
  npey  = data->npey;
  
  /* Initially set all pp elements to one. */
  N_VConst(ONE, data->pp);
  
  /* Prepare to loop over subgrid. */
  ixbegin = 0;
  ixend   = mxsub-1;
  jybegin = 0;
  jyend   = mysub-1;
  if (ixsub == 0) ixbegin++; if (ixsub == npex-1) ixend--;
  if (jysub == 0) jybegin++; if (jysub == npey-1) jyend--;
  pelinv = ONE/(c_j + data->coeffxy); 
  
  /* Load the inverse of the preconditioner diagonal elements
     in loop over all the local subgrid. */
  
  for (ly = jybegin; ly <=jyend; ly++) {
    for (lx = ixbegin; lx <= ixend; lx++) {
      locu  = lx + ly*mxsub;
      ppv[locu] = pelinv;
    }
  }

  return(0);

}

/*
 * PsolveHeat: solve preconditioner linear system.              
 * This routine multiplies the input vector rvec by the vector pp 
 * containing the inverse diagonal Jacobian elements (previously  
 * computed in PsetupHeat), returning the result in zvec.      
 */

int PsolveHeat(realtype tt, 
               N_Vector uu, N_Vector up, N_Vector rr, 
               N_Vector rvec, N_Vector zvec,
               realtype c_j, realtype delta, void *user_data, 
               N_Vector tmp)
{
  UserData data;

  data = (UserData) user_data;
  
  N_VProd(data->pp, rvec, zvec);

  return(0);

}

/*
 *--------------------------------------------------------------------
 * SUPPORTING FUNCTIONS
 *--------------------------------------------------------------------
 */


/* 
 * rescomm routine.  This routine performs all inter-processor
 * communication of data in u needed to calculate G.                 
 */

static int rescomm(N_Vector uu, N_Vector up, void *user_data)
{
  UserData data;
  realtype *uarray, *uext, buffer[2*MYSUB];
  MPI_Comm comm;
  long int thispe, ixsub, jysub, mxsub, mysub;
  MPI_Request request[4];
  
  data = (UserData) user_data;
  uarray = NV_DATA_P(uu);
  
  /* Get comm, thispe, subgrid indices, data sizes, extended array uext. */
  comm = data->comm;  thispe = data->thispe;
  ixsub = data->ixsub;   jysub = data->jysub;
  mxsub = data->mxsub;   mysub = data->mysub;
  uext = data->uext;
  
  /* Start receiving boundary data from neighboring PEs. */
  BRecvPost(comm, request, thispe, ixsub, jysub, mxsub, mysub, uext, buffer);
  
  /* Send data from boundary of local grid to neighboring PEs. */
  BSend(comm, thispe, ixsub, jysub, mxsub, mysub, uarray);
  
  /* Finish receiving boundary data from neighboring PEs. */
  BRecvWait(request, ixsub, jysub, mxsub, uext, buffer);

  return(0);
  
}

/*
 * reslocal routine.  Compute res = F(t, uu, up).  This routine assumes
 * that all inter-processor communication of data needed to calculate F
 * has already been done, and that this data is in the work array uext.  
 */

static int reslocal(realtype tt, 
                    N_Vector uu, N_Vector up, N_Vector rr,
                    void *user_data)
{
  realtype *uext, *uuv, *upv, *resv;
  realtype termx, termy, termctr;
  long int lx, ly, offsetu, offsetue, locu, locue;
  long int ixsub, jysub, mxsub, mxsub2, mysub, npex, npey;
  long int ixbegin, ixend, jybegin, jyend;
  UserData data;
  
  /* Get subgrid indices, array sizes, extended work array uext. */
  
  data = (UserData) user_data;
  uext = data->uext;
  uuv = NV_DATA_P(uu);
  upv = NV_DATA_P(up);
  resv = NV_DATA_P(rr);
  ixsub = data->ixsub; jysub = data->jysub;
  mxsub = data->mxsub; mxsub2 = data->mxsub + 2;
  mysub = data->mysub; npex = data->npex; npey = data->npey;
  
  /* Initialize all elements of rr to uu. This sets the boundary
     elements simply without indexing hassles. */
  
  N_VScale(ONE, uu, rr);
  
  /* Copy local segment of u vector into the working extended array uext.
     This completes uext prior to the computation of the rr vector.     */
  
  offsetu = 0;
  offsetue = mxsub2 + 1;
  for (ly = 0; ly < mysub; ly++) {
    for (lx = 0; lx < mxsub; lx++) uext[offsetue+lx] = uuv[offsetu+lx];
    offsetu = offsetu + mxsub;
    offsetue = offsetue + mxsub2;
  }
  
  /* Set loop limits for the interior of the local subgrid. */
  
  ixbegin = 0;
  ixend   = mxsub-1;
  jybegin = 0;
  jyend   = mysub-1;
  if (ixsub == 0) ixbegin++; if (ixsub == npex-1) ixend--;
  if (jysub == 0) jybegin++; if (jysub == npey-1) jyend--;
  
  /* Loop over all grid points in local subgrid. */

  for (ly = jybegin; ly <=jyend; ly++) {
    for (lx = ixbegin; lx <= ixend; lx++) {
      locu  = lx + ly*mxsub;
      locue = (lx+1) + (ly+1)*mxsub2;
      termx = data->coeffx *(uext[locue-1]      + uext[locue+1]);
      termy = data->coeffy *(uext[locue-mxsub2] + uext[locue+mxsub2]);
      termctr = data->coeffxy*uext[locue];
      resv[locu] = upv[locu] - (termx + termy - termctr);
   }
  }
  return(0);

}

/*
 * Routine to send boundary data to neighboring PEs.                     
 */

static int BSend(MPI_Comm comm, long int thispe, long int ixsub, long int jysub,
                 long int dsizex, long int dsizey, realtype uarray[])
{
  long int ly, offsetu;
  realtype bufleft[MYSUB], bufright[MYSUB];

  /* If jysub > 0, send data from bottom x-line of u. */
  
  if (jysub != 0)
    MPI_Send(&uarray[0], dsizex, PVEC_REAL_MPI_TYPE, thispe-NPEX, 0, comm);
  
  /* If jysub < NPEY-1, send data from top x-line of u. */
  
  if (jysub != NPEY-1) {
    offsetu = (MYSUB-1)*dsizex;
    MPI_Send(&uarray[offsetu], dsizex, PVEC_REAL_MPI_TYPE, 
             thispe+NPEX, 0, comm);
  }
  
  /* If ixsub > 0, send data from left y-line of u (via bufleft). */
  
  if (ixsub != 0) {
    for (ly = 0; ly < MYSUB; ly++) {
      offsetu = ly*dsizex;
      bufleft[ly] = uarray[offsetu];
    }
    MPI_Send(&bufleft[0], dsizey, PVEC_REAL_MPI_TYPE, thispe-1, 0, comm);   
  }
  
  /* If ixsub < NPEX-1, send data from right y-line of u (via bufright). */
  
  if (ixsub != NPEX-1) {
    for (ly = 0; ly < MYSUB; ly++) {
      offsetu = ly*MXSUB + (MXSUB-1);
      bufright[ly] = uarray[offsetu];
    }
    MPI_Send(&bufright[0], dsizey, PVEC_REAL_MPI_TYPE, thispe+1, 0, comm);   
  }

  return(0);

}

/*
 * Routine to start receiving boundary data from neighboring PEs.
 * Notes:
 *   1) buffer should be able to hold 2*MYSUB realtype entries, should be
 *      passed to both the BRecvPost and BRecvWait functions, and should not
 *      be manipulated between the two calls.
 *   2) request should have 4 entries, and should be passed in 
 *      both calls also. 
 */

static int BRecvPost(MPI_Comm comm, MPI_Request request[], long int thispe,
                     long int ixsub, long int jysub,
                     long int dsizex, long int dsizey,
                     realtype uext[], realtype buffer[])
{
  long int offsetue;
  /* Have bufleft and bufright use the same buffer. */
  realtype *bufleft = buffer, *bufright = buffer+MYSUB;
  
  /* If jysub > 0, receive data for bottom x-line of uext. */
  if (jysub != 0)
    MPI_Irecv(&uext[1], dsizex, PVEC_REAL_MPI_TYPE,
              thispe-NPEX, 0, comm, &request[0]);
  
  /* If jysub < NPEY-1, receive data for top x-line of uext. */
  if (jysub != NPEY-1) {
    offsetue = (1 + (MYSUB+1)*(MXSUB+2));
    MPI_Irecv(&uext[offsetue], dsizex, PVEC_REAL_MPI_TYPE,
              thispe+NPEX, 0, comm, &request[1]);
  }
  
  /* If ixsub > 0, receive data for left y-line of uext (via bufleft). */
  if (ixsub != 0) {
    MPI_Irecv(&bufleft[0], dsizey, PVEC_REAL_MPI_TYPE,
              thispe-1, 0, comm, &request[2]);
  }
  
  /* If ixsub < NPEX-1, receive data for right y-line of uext (via bufright). */
  if (ixsub != NPEX-1) {
    MPI_Irecv(&bufright[0], dsizey, PVEC_REAL_MPI_TYPE,
              thispe+1, 0, comm, &request[3]);
  }

  return(0);
  
}

/*
 * Routine to finish receiving boundary data from neighboring PEs.
 * Notes:
 *   1) buffer should be able to hold 2*MYSUB realtype entries, should be
 *      passed to both the BRecvPost and BRecvWait functions, and should not
 *      be manipulated between the two calls.
 *   2) request should have four entries, and should be passed in both 
 *      calls also. 
 */

static int BRecvWait(MPI_Request request[], long int ixsub, long int jysub,
                     long int dsizex, realtype uext[], realtype buffer[])
{
  long int ly, dsizex2, offsetue;
  realtype *bufleft = buffer, *bufright = buffer+MYSUB;
  MPI_Status status;
  
  dsizex2 = dsizex + 2;
  
  /* If jysub > 0, receive data for bottom x-line of uext. */
  if (jysub != 0)
    MPI_Wait(&request[0],&status);
  
  /* If jysub < NPEY-1, receive data for top x-line of uext. */
  if (jysub != NPEY-1)
    MPI_Wait(&request[1],&status);
  
  /* If ixsub > 0, receive data for left y-line of uext (via bufleft). */
  if (ixsub != 0) {
    MPI_Wait(&request[2],&status);
    
    /* Copy the buffer to uext. */
    for (ly = 0; ly < MYSUB; ly++) {
      offsetue = (ly+1)*dsizex2;
      uext[offsetue] = bufleft[ly];
    }
  }
  
  /* If ixsub < NPEX-1, receive data for right y-line of uext (via bufright). */
  if (ixsub != NPEX-1) {
    MPI_Wait(&request[3],&status);
    
    /* Copy the buffer to uext */
    for (ly = 0; ly < MYSUB; ly++) {
      offsetue = (ly+2)*dsizex2 - 1;
      uext[offsetue] = bufright[ly];
    }
  }

  return(0);

}

/*
 *--------------------------------------------------------------------
 * PRIVATE FUNCTIONS
 *--------------------------------------------------------------------
 */

/* 
 * InitUserData initializes the user's data block data. 
 */

static int InitUserData(int thispe, MPI_Comm comm, UserData data)
{
  data->thispe = thispe;
  data->dx = ONE/(MX-ONE);       /* Assumes a [0,1] interval in x. */
  data->dy = ONE/(MY-ONE);       /* Assumes a [0,1] interval in y. */
  data->coeffx  = ONE/(data->dx * data->dx);
  data->coeffy  = ONE/(data->dy * data->dy);
  data->coeffxy = TWO/(data->dx * data->dx) + TWO/(data->dy * data->dy) ;
  data->jysub   = thispe/NPEX;
  data->ixsub   = thispe - data->jysub * NPEX;
  data->npex    = NPEX;
  data->npey    = NPEY;
  data->mx      = MX;
  data->my      = MY;
  data->mxsub = MXSUB;
  data->mysub = MYSUB;
  data->comm    = comm;
  return(0);

}

/*
 * SetInitialProfile sets the initial values for the problem. 
 */

static int SetInitialProfile(N_Vector uu, N_Vector up,  N_Vector id, 
                             N_Vector res, UserData data)
{
  long int i, iloc, j, jloc, offset, loc, ixsub, jysub;
  long int ixbegin, ixend, jybegin, jyend;
  realtype xfact, yfact, *udata, *iddata, dx, dy;
  
  /* Initialize uu. */ 

  udata = NV_DATA_P(uu);
  iddata = NV_DATA_P(id);
  
  /* Set mesh spacings and subgrid indices for this PE. */
  dx = data->dx;
  dy = data->dy;
  ixsub = data->ixsub;
  jysub = data->jysub;
  
  /* Set beginning and ending locations in the global array corresponding 
     to the portion of that array assigned to this processor. */
  ixbegin = MXSUB*ixsub;
  ixend   = MXSUB*(ixsub+1) - 1;
  jybegin = MYSUB*jysub;
  jyend   = MYSUB*(jysub+1) - 1;
  
  /* Loop over the local array, computing the initial profile value.
     The global indices are (i,j) and the local indices are (iloc,jloc).
     Also set the id vector to zero for boundary points, one otherwise. */
  
  N_VConst(ONE,id);
  for (j = jybegin, jloc = 0; j <= jyend; j++, jloc++) {
    yfact = data->dy*j;
    offset= jloc*MXSUB;
    for (i = ixbegin, iloc = 0; i <= ixend; i++, iloc++) {
      xfact = data->dx * i;
      loc = offset + iloc;
      udata[loc] = RCONST(16.0) * xfact * (ONE - xfact) * yfact * (ONE - yfact);
      if (i == 0 || i == MX-1 || j == 0 || j == MY-1) iddata[loc] = ZERO;
    }
  }
  
  /* Initialize up. */
  
  N_VConst(ZERO, up);    /* Initially set up = 0. */
  
  /* resHeat sets res to negative of ODE RHS values at interior points. */
  resHeat(ZERO, uu, up, res, data);
  
  /* Copy -res into up to get correct initial up values. */
  N_VScale(-ONE, res, up);
  
  return(0);
}

/*
 * Print first lines of output and table heading
 */

static void PrintHeader(long int Neq, realtype rtol, realtype atol)
{ 
  printf("\nidakryx1_p: Heat equation, parallel example problem for IDA\n");
  printf("            Discretized heat equation on 2D unit square.\n");
  printf("            Zero boundary conditions,");
  printf(" polynomial initial conditions.\n");
  printf("            Mesh dimensions: %d x %d", MX, MY);
  printf("        Total system size: %ld\n\n", Neq);
  printf("Subgrid dimensions: %d x %d", MXSUB, MYSUB);
  printf("        Processor array: %d x %d\n", NPEX, NPEY);
#if defined(SUNDIALS_EXTENDED_PRECISION)
  printf("Tolerance parameters:  rtol = %Lg   atol = %Lg\n", rtol, atol);
#elif defined(SUNDIALS_DOUBLE_PRECISION)
  printf("Tolerance parameters:  rtol = %lg   atol = %lg\n", rtol, atol);
#else
  printf("Tolerance parameters:  rtol = %g   atol = %g\n", rtol, atol);
#endif
  printf("Constraints set to force all solution components >= 0. \n");
  printf("SUPPRESSALG = TRUE to suppress local error testing on ");
  printf("all boundary components. \n");
  printf("Linear solver: IDASPGMR  ");
  printf("Preconditioner: diagonal elements only.\n"); 
  
  /* Print output table heading and initial line of table. */
  printf("\n   Output Summary (umax = max-norm of solution) \n\n");
  printf("  time     umax       k  nst  nni  nli   nre   nreLS    h      npe nps\n");
  printf("----------------------------------------------------------------------\n");
}

/*
 * PrintOutput: print max norm of solution and current solver statistics
 */

static void PrintOutput(int id, void *mem, realtype t, N_Vector uu)
{
  realtype hused, umax;
  long int nst, nni, nje, nre, nreLS, nli, npe, nps;
  int kused, ier;

  umax = N_VMaxNorm(uu);

  if (id == 0) {

    ier = IDAGetLastOrder(mem, &kused);
    check_flag(&ier, "IDAGetLastOrder", 1, id);
    ier = IDAGetNumSteps(mem, &nst);
    check_flag(&ier, "IDAGetNumSteps", 1, id);
    ier = IDAGetNumNonlinSolvIters(mem, &nni);
    check_flag(&ier, "IDAGetNumNonlinSolvIters", 1, id);
    ier = IDAGetNumResEvals(mem, &nre);
    check_flag(&ier, "IDAGetNumResEvals", 1, id);
    ier = IDAGetLastStep(mem, &hused);
    check_flag(&ier, "IDAGetLastStep", 1, id);
    ier = IDASpilsGetNumJtimesEvals(mem, &nje);
    check_flag(&ier, "IDASpilsGetNumJtimesEvals", 1, id);
    ier = IDASpilsGetNumLinIters(mem, &nli);
    check_flag(&ier, "IDASpilsGetNumLinIters", 1, id);
    ier = IDASpilsGetNumResEvals(mem, &nreLS);
    check_flag(&ier, "IDASpilsGetNumResEvals", 1, id);
    ier = IDASpilsGetNumPrecEvals(mem, &npe);
    check_flag(&ier, "IDASpilsGetPrecEvals", 1, id);
    ier = IDASpilsGetNumPrecSolves(mem, &nps);
    check_flag(&ier, "IDASpilsGetNumPrecSolves", 1, id);

#if defined(SUNDIALS_EXTENDED_PRECISION)  
    printf(" %5.2Lf %13.5Le  %d  %3ld  %3ld  %3ld  %4ld  %4ld  %9.2Le  %3ld %3ld\n",
           t, umax, kused, nst, nni, nje, nre, nreLS, hused, npe, nps);
#elif defined(SUNDIALS_DOUBLE_PRECISION)  
    printf(" %5.2f %13.5le  %d  %3ld  %3ld  %3ld  %4ld  %4ld  %9.2le  %3ld %3ld\n",
           t, umax, kused, nst, nni, nje, nre, nreLS, hused, npe, nps);
#else
    printf(" %5.2f %13.5e  %d  %3ld  %3ld  %3ld  %4ld  %4ld  %9.2e  %3ld %3ld\n",
           t, umax, kused, nst, nni, nje, nre, nreLS, hused, npe, nps);
#endif

  }
}

/*
 * Print some final integrator statistics
 */

static void PrintFinalStats(void *mem)
{
  long int netf, ncfn, ncfl;

  IDAGetNumErrTestFails(mem, &netf);
  IDAGetNumNonlinSolvConvFails(mem, &ncfn);
  IDASpilsGetNumConvFails(mem, &ncfl);

  printf("\nError test failures            = %ld\n", netf);
  printf("Nonlinear convergence failures = %ld\n", ncfn);
  printf("Linear convergence failures    = %ld\n", ncfl);
}

/*
 * Check function return value...
 *   opt == 0 means SUNDIALS function allocates memory so check if
 *            returned NULL pointer
 *   opt == 1 means SUNDIALS function returns a flag so check if
 *            flag >= 0
 *   opt == 2 means function allocates memory so check if returned
 *            NULL pointer 
 */

static int check_flag(void *flagvalue, char *funcname, int opt, int id)
{
  int *errflag;

  if (opt == 0 && flagvalue == NULL) {
    /* Check if SUNDIALS function returned NULL pointer - no memory allocated */
    fprintf(stderr, 
            "\nSUNDIALS_ERROR(%d): %s() failed - returned NULL pointer\n\n", 
            id, funcname);
    return(1); 
  } else if (opt == 1) {
    /* Check if flag < 0 */
    errflag = (int *) flagvalue;
    if (*errflag < 0) {
      fprintf(stderr, 
              "\nSUNDIALS_ERROR(%d): %s() failed with flag = %d\n\n", 
              id, funcname, *errflag);
      return(1); 
    }
  } else if (opt == 2 && flagvalue == NULL) {
    /* Check if function returned NULL pointer - no memory allocated */
    fprintf(stderr, 
            "\nMEMORY_ERROR(%d): %s() failed - returned NULL pointer\n\n", 
            id, funcname);
    return(1); 
  }

  return(0);
}
