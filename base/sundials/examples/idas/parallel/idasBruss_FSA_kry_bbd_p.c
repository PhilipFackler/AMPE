/* * -----------------------------------------------------------------
 * $Revision:
 * $Date:
 * -----------------------------------------------------------------
 * Programmer(s): Cosmin Petra and Radu Serban @ LLNL
 * -----------------------------------------------------------------
 * Example program for IDA: Brusselator, parallel, GMRES, IDABBD
 * preconditioner, FSA.
 *
 * This example program for IDAS uses IDASPGMR as the linear solver.
 * It is written for a parallel computer system and uses the
 * IDABBDPRE band-block-diagonal preconditioner module for the
 * IDASPGMR package.
 *
 * The mathematical problem solved in this example is a DAE system
 * that arises from a system of partial differential equations after
 * spatial discretization.
 * 
 * The PDE system is a two-species time-dependent PDE known as
 * Brusselator PDE and models a chemically reacting system.
 *
 *                  
 *  du/dt = eps1(u  + u) + u^2 v -(B+1)u + A  
 *               xx   yy
 *                                              domain [0,L]X[0,L]
 *  dv/dt = eps2(v  + v) - u^2 v + Bu
 *               xx   yy
 *
 *  B.C. Neumann
 *  I.C  u(x,y,t0) = u0(x,y) =  1  - 0.5*cos(pi*y/L) 
 *       v(x,y,t0) = v0(x,y) = 3.5 - 2.5*cos(pi*x/L) 
 *
 * The PDEs are discretized by central differencing on a MX by MY
 * mesh, and so the system size Neq is the product MX*MY*NUM_SPECIES. 
 * Dirichlet B.C. gives algebraic equations on the boundary and 
 * differential equations in the interior of the domain mesh.
 * The system is actually implemented on submeshes, processor by 
 * processor, with an MXSUB by MYSUB mesh on each of NPEX * NPEY 
 * processors.
 *
 * The average of the solution u at final time is also computed.
 *            / /
 *        g = | | u(x,y,tf) dx dy
 *            / /
 * Also the sensitivities of g with respect to parameters eps1 and 
 * eps2 are computed.
 *                  / /
 *       dg/d eps = | | u  (x,y,tf)  dx dy  
 *                  / /  eps
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <idas/idas.h>
#include <idas/idas_spgmr.h>
#include <idas/idas_bbdpre.h>
#include <nvector/nvector_parallel.h>
#include <sundials/sundials_dense.h>
#include <sundials/sundials_types.h>
#include <sundials/sundials_math.h>

#include <mpi.h>

/* Problem Constants */
#define NUM_SPECIES 2 
#define ctL         RCONST(1.0)    /* Domain =[0,L]^2 */
#define ctA         RCONST(1.0)
#define ctB         RCONST(3.4)
#define ctEps       RCONST(2.0e-3)

#define NS          2

#define PI          RCONST(3.1415926535898) /* pi */ 

#define MXSUB       41    /* Number of x mesh points per processor subgrid */
#define MYSUB       41    /* Number of y mesh points per processor subgrid */
#define NPEX        2     /* Number of subgrids in the x direction */
#define NPEY        2     /* Number of subgrids in the y direction */
#define MX          (MXSUB*NPEX)      /* MX = number of x mesh points */
#define MY          (MYSUB*NPEY)      /* MY = number of y mesh points */
#define NSMXSUB     (NUM_SPECIES * MXSUB)
#define NEQ         (NUM_SPECIES*MX*MY) /* Number of equations in system */


#define RTOL        RCONST(1.e-5)  /*  rtol tolerance */
#define ATOL        RCONST(1.e-5)  /*  atol tolerance */
#define NOUT        6  
#define TMULT       RCONST(10.0)   /* Multiplier for tout values */
#define TADD        RCONST(0.3)    /* Increment for tout values */

#define ZERO        RCONST(0.0)
#define HALF        RCONST(0.5)
#define ONE         RCONST(1.0)


/* User-defined vector accessor macro IJ_Vptr. */

/*
 * IJ_Vptr is defined in order to express the underlying 3-d structure of the 
 * dependent variable vector from its underlying 1-d storage (an N_Vector).
 * IJ_Vptr(vv,i,j) returns a pointer to the location in vv corresponding to 
 * species index is = 0, x-index ix = i, and y-index jy = j.                
 */

#define IJ_Vptr(vv,i,j) (&NV_Ith_P(vv, (i)*NUM_SPECIES + (j)*NSMXSUB ))

/* Type: UserData.  Contains problem constants, preconditioner data, etc. */
typedef struct {
  int ns, thispe, npes, ixsub, jysub, npex, npey;
  int mxsub, mysub, nsmxsub, nsmxsub2;
  realtype A, B, L, eps[NUM_SPECIES];
  realtype dx, dy;
  realtype cox[NUM_SPECIES], coy[NUM_SPECIES];
  realtype gridext[(MXSUB+2)*(MYSUB+2)*NUM_SPECIES];
  realtype rhs[NUM_SPECIES];
  MPI_Comm comm;
  realtype rates[2];
  int n_local;
} *UserData;

/* Prototypes for functions called by the IDA Solver. */
static int res(realtype tt, 
               N_Vector uv, N_Vector uvp, N_Vector rr, 
               void *user_data);

static int reslocal(int Nlocal, realtype tt, 
                    N_Vector uv, N_Vector uvp, N_Vector res, 
                    void *user_data);

static int rescomm(int Nlocal, realtype tt,
                   N_Vector uv, N_Vector uvp, 
                   void *user_data);

/* Integrate over spatial domain. */
static int integr(MPI_Comm comm, N_Vector uv, void *user_data, realtype *intval);

/* Prototypes for supporting functions */
static void BSend(MPI_Comm comm, int thispe, int ixsub, int jysub,
                  int dsizex, int dsizey, realtype carray[]);

static void BRecvPost(MPI_Comm comm, MPI_Request request[], int thispe,
                      int ixsub, int jysub,
                      int dsizex, int dsizey,
                      realtype cext[], realtype buffer[]);

static void BRecvWait(MPI_Request request[], int ixsub, int jysub,
                      int dsizex, realtype cext[], realtype buffer[]);

static void ReactRates(realtype xx, realtype yy, realtype *cxy, realtype *ratesxy, 
                     UserData data);

/* Prototypes for private functions */
static void InitUserData(UserData data, int thispe, int npes, 
                         MPI_Comm comm);

static void SetInitialProfiles(N_Vector uv, N_Vector uvp, N_Vector id,
                               N_Vector resid, UserData data);

static void PrintHeader(int SystemSize, int maxl, 
                        int mudq, int mldq, 
                        int mukeep, int mlkeep,
                        realtype rtol, realtype atol);

static void PrintOutput(void *mem, N_Vector uv, realtype time,
                        UserData data, MPI_Comm comm);

static void PrintSol(void* mem, N_Vector uv, N_Vector uvp, UserData data,
                     MPI_Comm comm);

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
  int SystemSize, local_N, mudq, mldq, mukeep, mlkeep;
  realtype rtol, atol, t0, tout, tret;
  N_Vector uv, uvp, resid, id, *uvS, *uvpS;
  int thispe, npes, maxl, iout, retval;
  realtype pbar[NS];
  int is;
  realtype intval;

  uv = uvp = resid = id = NULL;
  uvS = uvpS = NULL;
  data = NULL;
  mem = NULL;

  /* Set communicator, and get processor number and total number of PE's. */
  MPI_Init(&argc, &argv);
  comm = MPI_COMM_WORLD;
  MPI_Comm_rank(comm, &thispe);
  MPI_Comm_size(comm, &npes);

  if (npes != NPEX*NPEY) {
    if (thispe == 0)
      fprintf(stderr, 
              "\nMPI_ERROR(0): npes = %d not equal to NPEX*NPEY = %d\n", 
              npes, NPEX*NPEY);
    MPI_Finalize();
    return(1); 
  }
  
  /* Set local length (local_N) and global length (SystemSize). */
  local_N = MXSUB*MYSUB*NUM_SPECIES;
  SystemSize = NEQ;

  /* Set up user data block data. */
  data = (UserData) malloc(sizeof *data);

  InitUserData(data, thispe, npes, comm);
  
  /* Create needed vectors, and load initial values.
     The vector resid is used temporarily only.        */
  
  uv  = N_VNew_Parallel(comm, local_N, SystemSize);
  if(check_flag((void *)uv, "N_VNew_Parallel", 0, thispe)) MPI_Abort(comm, 1);

  uvp  = N_VNew_Parallel(comm, local_N, SystemSize);
  if(check_flag((void *)uvp, "N_VNew_Parallel", 0, thispe)) MPI_Abort(comm, 1);

  resid = N_VNew_Parallel(comm, local_N, SystemSize);
  if(check_flag((void *)resid, "N_VNew_Parallel", 0, thispe)) MPI_Abort(comm, 1);

  id  = N_VNew_Parallel(comm, local_N, SystemSize);
  if(check_flag((void *)id, "N_VNew_Parallel", 0, thispe)) MPI_Abort(comm, 1);

  uvS = N_VCloneVectorArray_Parallel(NS, uv);
  if (check_flag((void *)uvS, "N_VCloneVectorArray_Parallel", 0, thispe)) MPI_Abort(comm, 1);
  for (is=0;is<NS;is++) N_VConst(ZERO, uvS[is]);
    
  uvpS = N_VCloneVectorArray_Parallel(NS, uv);
  if (check_flag((void *)uvpS, "N_VCloneVectorArray_Parallel", 0, thispe))  MPI_Abort(comm, 1);
  for (is=0;is<NS;is++) N_VConst(ZERO, uvpS[is]);

  SetInitialProfiles(uv, uvp, id, resid, data);

  /* Set remaining inputs to IDAS. */
  t0 = ZERO;
  rtol = RTOL; 
  atol = ATOL;
  
  /* Call IDACreate and IDAInit to initialize solution */
  mem = IDACreate();
  if(check_flag((void *)mem, "IDACreate", 0, thispe)) MPI_Abort(comm, 1);

  retval = IDASetUserData(mem, data);
  if(check_flag(&retval, "IDASetUserData", 1, thispe)) MPI_Abort(comm, 1);

  retval = IDASetId(mem, id);
  if(check_flag(&retval, "IDASetId", 1, thispe)) MPI_Abort(comm, 1);

  retval = IDAInit(mem, res, t0, uv, uvp);
  if(check_flag(&retval, "IDAInit", 1, thispe)) MPI_Abort(comm, 1);
  
  retval = IDASStolerances(mem, rtol, atol);
  if(check_flag(&retval, "IDASStolerances", 1, thispe)) MPI_Abort(comm, 1);


  /* Enable forward sensitivity analysis. */
  retval = IDASensInit(mem, NS, IDA_SIMULTANEOUS, NULL, uvS, uvpS);
  if(check_flag(&retval, "IDASensInit", 1, thispe)) MPI_Abort(comm, 1);

  retval = IDASensEEtolerances(mem);
  if(check_flag(&retval, "IDASensEEtolerances", 1, thispe)) MPI_Abort(comm, 1);

  retval = IDASetSensErrCon(mem, TRUE);
  if (check_flag(&retval, "IDASetSensErrCon", 1, thispe)) MPI_Abort(comm, 1);

  pbar[0] = data->eps[0];
  pbar[1] = data->eps[1];
  retval = IDASetSensParams(mem, data->eps, pbar, NULL);
  if (check_flag(&retval, "IDASetSensParams", 1, thispe)) MPI_Abort(comm, 1);

  /* Call IDASpgmr to specify the IDAS LINEAR SOLVER IDASPGMR */
  maxl = 16;
  retval = IDASpgmr(mem, maxl);
  if(check_flag(&retval, "IDASpgmr", 1, thispe)) MPI_Abort(comm, 1);

  /* Call IDABBDPrecInit to initialize the band-block-diagonal preconditioner.
     The half-bandwidths for the difference quotient evaluation are exact
     for the system Jacobian, but only a 5-diagonal band matrix is retained. */
  mudq = mldq = NSMXSUB;
  mukeep = mlkeep = 2;
  retval = IDABBDPrecInit(mem, local_N, mudq, mldq, mukeep, mlkeep, 
                          ZERO, reslocal, NULL);
  if(check_flag(&retval, "IDABBDPrecInit", 1, thispe)) MPI_Abort(comm, 1);

  /* Call IDACalcIC (with default options) to correct the initial values. */
  tout = RCONST(0.001);
  retval = IDACalcIC(mem, IDA_YA_YDP_INIT, tout);
  if(check_flag(&retval, "IDACalcIC", 1, thispe)) MPI_Abort(comm, 1);
  
  /* On PE 0, print heading, basic parameters, initial values. */
  if (thispe == 0) PrintHeader(SystemSize, maxl, 
                               mudq, mldq, mukeep, mlkeep,
                               rtol, atol);
  PrintOutput(mem, uv, t0, data, comm);


  /* Call IDAS in tout loop, normal mode, and print selected output. */
  for (iout = 1; iout <= NOUT; iout++) {
    
    retval = IDASolve(mem, tout, &tret, uv, uvp, IDA_NORMAL);
    if(check_flag(&retval, "IDASolve", 1, thispe)) MPI_Abort(comm, 1);

    PrintOutput(mem, uv, tret, data, comm);

    if (iout < 3) tout *= TMULT;
    else          tout += TADD;

  }
  /* Print each PE's portion of the solution in a separate file. */
  /* PrintSol(mem, uv, uvp, data, comm); */


  /* On PE 0, print final set of statistics. */  
  if (thispe == 0) {
    PrintFinalStats(mem);
  }

  /* calculate integral of u over domain. */
  integr(comm, uv, data, &intval);
  if (thispe == 0) {
    printf("\n\nThe average of u on the domain:\ng = %g\n", intval);
  }

  /* integrate the sensitivities of u over domain. */
  IDAGetSens(mem, &tret, uvS);
  if (thispe == 0)
    printf("\nSensitivities of g:\n");

  for (is=0; is<NS; is++) {
    integr(comm, uvS[is], data, &intval);
    if (thispe == 0) {
      printf("w.r.t. eps%d = %14.10f\n", is, intval);
    }
  }

  /* Free memory. */
  N_VDestroy_Parallel(uv);
  N_VDestroy_Parallel(uvp);
  N_VDestroy_Parallel(id);
  N_VDestroy_Parallel(resid);
  N_VDestroyVectorArray_Parallel(uvS, NS);
  N_VDestroyVectorArray_Parallel(uvpS, NS);
  IDAFree(&mem);

  free(data);

  MPI_Finalize();

  return(0);
}

/*
 *--------------------------------------------------------------------
 * PRIVATE FUNCTIONS
 *--------------------------------------------------------------------
 */

/*
 * InitUserData: Load problem constants in data (of type UserData).   
 */

static void InitUserData(UserData data, int thispe, int npes, 
                         MPI_Comm comm)
{
  data->jysub = thispe / NPEX;
  data->ixsub = thispe - (data->jysub)*NPEX;
  data->mxsub = MXSUB;
  data->mysub = MYSUB;
  data->npex = NPEX;
  data->npey = NPEY;
  data->ns = NUM_SPECIES;
  data->dx = ctL/(MX-1);
  data->dy = ctL/(MY-1);
  data->thispe = thispe;
  data->npes   = npes;
  data->nsmxsub = MXSUB * NUM_SPECIES;
  data->nsmxsub2 = (MXSUB+2)*NUM_SPECIES;
  data->comm = comm;
  data->n_local = MXSUB*MYSUB*NUM_SPECIES;

  data->A = ctA;
  data->B = ctB;
  data->L = ctL;
  data->eps[0] = data->eps[1] = ctEps;
}

/*
 * SetInitialProfiles: Set initial conditions in uv, uvp, and id.
 */

static void SetInitialProfiles(N_Vector uv, N_Vector uvp, N_Vector id,
                               N_Vector resid, UserData data)
{
  int ixsub, jysub, mxsub, mysub, nsmxsub, ix, jy;
  realtype *idxy, dx, dy, x, y, *uvxy, *uvxy1, L, npex, npey;

  ixsub = data->ixsub;
  jysub = data->jysub;
  mxsub = data->mxsub;
  mysub = data->mysub;
  nsmxsub = data->nsmxsub;
  npex = data->npex;
  npey = data->npey;
  dx = data->dx;
  dy = data->dy;
  L = data->L;

  /* Loop over grid, load uv values and id values. */
  for (jy = 0; jy < mysub; jy++) {
    y = (jy + jysub*mysub) * dy;
    for (ix = 0; ix < mxsub; ix++) {

      x = (ix + ixsub*mxsub) * dx;
      uvxy = IJ_Vptr(uv,ix,jy); 

      uvxy[0] = RCONST(1.0) - HALF*cos(PI*y/L); 
      uvxy[1] = RCONST(3.5) - RCONST(2.5)*cos(PI*x/L);
    }
  }
 
  N_VConst(ONE, id);

  if (jysub == 0) {
    for (ix=0; ix<mxsub; ix++) {
      idxy = IJ_Vptr(id,ix,0);
      idxy[0] = idxy[1] = ZERO;

      uvxy  = IJ_Vptr(uv,ix,0);
      uvxy1 = IJ_Vptr(uv,ix,1);
      uvxy[0] = uvxy1[0];
      uvxy[1] = uvxy1[1];
    }
  }

  if (ixsub == npex-1) {
    for (jy = 0; jy < mysub; jy++) {
      idxy = IJ_Vptr(id,mxsub-1,jy);
      idxy[0] = idxy[1] = ZERO;

      uvxy  = IJ_Vptr(uv,mxsub-1,jy);
      uvxy1 = IJ_Vptr(uv,mxsub-2,jy);
      uvxy[0] = uvxy1[0];
      uvxy[1] = uvxy1[1];

    }
  }


  if (ixsub == 0) {
    for (jy = 0; jy < mysub; jy++) {
      idxy = IJ_Vptr(id,0,jy);
      idxy[0] = idxy[1] = ZERO;

      uvxy  = IJ_Vptr(uv,0,jy);
      uvxy1 = IJ_Vptr(uv,1,jy);
      uvxy[0] = uvxy1[0];
      uvxy[1] = uvxy1[1];

    }    
  }

  if (jysub == npey-1) {
    for (ix=0; ix<mxsub; ix++) {
      idxy = IJ_Vptr(id,ix,jysub);
      idxy[0] = idxy[1] = ZERO;

      uvxy  = IJ_Vptr(uv,ix,mysub-1);
      uvxy1 = IJ_Vptr(uv,ix,mysub-2);
      uvxy[0] = uvxy1[0];
      uvxy[1] = uvxy1[1];

    }
  }

  /* Derivative found by calling the residual function with uvp = 0. */
  N_VConst(ZERO, uvp);
  res(ZERO, uv, uvp, resid, data);
  N_VScale(-ONE, resid, uvp);


}

/*
 * Print first lines of output (problem description)
 * and table headerr
 */

static void PrintHeader(int SystemSize, int maxl, 
                        int mudq, int mldq, 
                        int mukeep, int mlkeep,
                        realtype rtol, realtype atol)
{
  printf("\n Brusselator PDE -  DAE parallel example problem for IDA \n\n");
  printf("Number of species ns: %d", NUM_SPECIES);
  printf("     Mesh dimensions: %d x %d", MX, MY);
  printf("     Total system size: %d\n",SystemSize);
  printf("Subgrid dimensions: %d x %d", MXSUB, MYSUB);
  printf("     Processor array: %d x %d\n", NPEX, NPEY);
#if defined(SUNDIALS_EXTENDED_PRECISION)
  printf("Tolerance parameters:  rtol = %Lg   atol = %Lg\n", rtol, atol);
#elif defined(SUNDIALS_DOUBLE_PRECISION)
  printf("Tolerance parameters:  rtol = %lg   atol = %lg\n", rtol, atol);
#else
  printf("Tolerance parameters:  rtol = %g   atol = %g\n", rtol, atol);
#endif
  printf("Linear solver: IDASPGMR     Max. Krylov dimension maxl: %d\n", maxl);
  printf("Preconditioner: band-block-diagonal (IDABBDPRE), with parameters\n");
  printf("     mudq = %d,  mldq = %d,  mukeep = %d,  mlkeep = %d\n",
         mudq, mldq, mukeep, mlkeep);
  printf("CalcIC called to correct initial concentrations \n\n");
  printf("-----------------------------------------------------------\n");
  printf("  t        bottom-left  top-right");
  printf("    | nst  k      h\n");
  printf("-----------------------------------------------------------\n\n");
}


/*
 * PrintOutput: Print output values at output time t = tt.
 * Selected run statistics are printed.  Then values of c1 and c2
 * are printed for the bottom left and top right grid points only.
 */

static void PrintOutput(void *mem, N_Vector uv, realtype tt,
                        UserData data, MPI_Comm comm)
{
  MPI_Status status;
  realtype *cdata, clast[2], hused;
  long int nst;
  int i, kused, flag, thispe, npelast, ilast;;

  thispe = data->thispe; 
  npelast = data->npes - 1;
  cdata = NV_DATA_P(uv);
  
  /* Send conc. at top right mesh point from PE npes-1 to PE 0. */
  if (thispe == npelast) {
    ilast = NUM_SPECIES*MXSUB*MYSUB - 2;
    if (npelast != 0)
      MPI_Send(&cdata[ilast], 2, PVEC_REAL_MPI_TYPE, 0, 0, comm);
    else { clast[0] = cdata[ilast]; clast[1] = cdata[ilast+1]; }
  }
  
  /* On PE 0, receive conc. at top right from PE npes - 1.
     Then print performance data and sampled solution values. */
  if (thispe == 0) {
    
    if (npelast != 0)
      MPI_Recv(&clast[0], 2, PVEC_REAL_MPI_TYPE, npelast, 0, comm, &status);
    
    flag = IDAGetLastOrder(mem, &kused);
    check_flag(&flag, "IDAGetLastOrder", 1, thispe);
    flag = IDAGetNumSteps(mem, &nst);
    check_flag(&flag, "IDAGetNumSteps", 1, thispe);
    flag = IDAGetLastStep(mem, &hused);
    check_flag(&flag, "IDAGetLastStep", 1, thispe);

#if defined(SUNDIALS_EXTENDED_PRECISION)
    printf("%8.2Le %12.4Le %12.4Le   | %3ld  %1d %12.4Le\n", 
         tt, cdata[0], clast[0], nst, kused, hused);
    for (i=1;i<NUM_SPECIES;i++)
      printf("         %12.4Le %12.4Le   |\n",cdata[i],clast[i]);
#elif defined(SUNDIALS_DOUBLE_PRECISION)
    printf("%8.2le %12.4le %12.4le   | %3ld  %1d %12.4le\n", 
         tt, cdata[0], clast[0], nst, kused, hused);
    for (i=1;i<NUM_SPECIES;i++)
      printf("         %12.4le %12.4le   |\n",cdata[i],clast[i]);
#else
    printf("%8.2e %12.4e %12.4e   | %3ld  %1d %12.4e\n", 
         tt, cdata[0], clast[0], nst, kused, hused);
    for (i=1;i<NUM_SPECIES;i++)
      printf("         %12.4e %12.4e   |\n",cdata[i],clast[i]);
#endif
    printf("\n");

  }

}

/* 
 * PrintSol the PE's portion of the solution to a file.
 */
static void PrintSol(void* mem, N_Vector uv, N_Vector uvp, 
                     UserData data, MPI_Comm comm)
{
  FILE* fout;
  realtype *uvxy;
  int ix, jy, mxsub, mysub, thispe;
  char szFilename[128];

  thispe = data->thispe;
  sprintf(szFilename, "ysol%d.txt", thispe);

  fout = fopen(szFilename, "w+");
  if (fout==NULL) {
    printf("PE[% 2d] is unable to write solution to disk!\n", thispe);
    return;
  }

  mxsub = data->mxsub;
  mysub = data->mysub;

  for (jy=0; jy<mysub; jy++) {
    for (ix=0; ix<mxsub; ix++) {
    
      uvxy  = IJ_Vptr(uv, ix, jy);
      fprintf(fout, "%g\n%g\n", uvxy[0], uvxy[1]);
    }
  }    
  fclose(fout);
}



/*
 * PrintFinalStats: Print final run data contained in iopt.              
 */

static void PrintFinalStats(void *mem)
{
  long int nst, nre, nreLS, netf, ncfn, nni, ncfl, nli, npe, nps, nge;
  int flag;

  flag = IDAGetNumSteps(mem, &nst);
  check_flag(&flag, "IDAGetNumSteps", 1, 0);
  flag = IDAGetNumResEvals(mem, &nre);
  check_flag(&flag, "IDAGetNumResEvals", 1, 0);
  flag = IDAGetNumErrTestFails(mem, &netf);
  check_flag(&flag, "IDAGetNumErrTestFails", 1, 0);
  flag = IDAGetNumNonlinSolvConvFails(mem, &ncfn);
  check_flag(&flag, "IDAGetNumNonlinSolvConvFails", 1, 0);
  flag = IDAGetNumNonlinSolvIters(mem, &nni);
  check_flag(&flag, "IDAGetNumNonlinSolvIters", 1, 0);

  flag = IDASpilsGetNumConvFails(mem, &ncfl);
  check_flag(&flag, "IDASpilsGetNumConvFails", 1, 0);
  flag = IDASpilsGetNumLinIters(mem, &nli);
  check_flag(&flag, "IDASpilsGetNumLinIters", 1, 0);
  flag = IDASpilsGetNumPrecEvals(mem, &npe);
  check_flag(&flag, "IDASpilsGetNumPrecEvals", 1, 0);
  flag = IDASpilsGetNumPrecSolves(mem, &nps);
  check_flag(&flag, "IDASpilsGetNumPrecSolves", 1, 0);
  flag = IDASpilsGetNumResEvals(mem, &nreLS);
  check_flag(&flag, "IDASpilsGetNumResEvals", 1, 0);

  flag = IDABBDPrecGetNumGfnEvals(mem, &nge);
  check_flag(&flag, "IDABBDPrecGetNumGfnEvals", 1, 0);

  printf("-----------------------------------------------------------\n");
  printf("\nFinal statistics: \n\n");

  printf("Number of steps                    = %ld\n", nst);
  printf("Number of residual evaluations     = %ld\n", nre+nreLS);
  printf("Number of nonlinear iterations     = %ld\n", nni);
  printf("Number of error test failures      = %ld\n", netf);
  printf("Number of nonlinear conv. failures = %ld\n\n", ncfn);

  printf("Number of linear iterations        = %ld\n", nli);
  printf("Number of linear conv. failures    = %ld\n\n", ncfl);

  printf("Number of preconditioner setups    = %ld\n", npe);
  printf("Number of preconditioner solves    = %ld\n", nps);
  printf("Number of local residual evals.    = %ld\n", nge);

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

/*
 *--------------------------------------------------------------------
 * FUNCTIONS CALLED BY IDA & SUPPORTING FUNCTIONS
 *--------------------------------------------------------------------
 */

/*
 * res: System residual function 
 *
 * To compute the residual function F, this routine calls:
 * rescomm, for needed communication, and then
 * reslocal, for computation of the residuals on this processor.      
 */

static int res(realtype tt, 
               N_Vector uv, N_Vector uvp, N_Vector rr, 
               void *user_data)
{
  int retval;
  UserData data;
  int Nlocal;
  
  data = (UserData) user_data;
  
  Nlocal = data->n_local;

  /* Call rescomm to do inter-processor communication. */
  retval = rescomm(Nlocal, tt, uv, uvp, user_data);
  
  /* Call reslocal to calculate the local portion of residual vector. */
  retval = reslocal(Nlocal, tt, uv, uvp, rr, user_data);
  
  return(0);
}

/*
 * rescomm: Communication routine in support of resweb.
 * This routine performs all inter-processor communication of components
 * of the uv vector needed to calculate F, namely the components at all
 * interior subgrid boundaries (ghost cell data).  It loads this data
 * into a work array cext (the local portion of c, extended).
 * The message-passing uses blocking sends, non-blocking receives,
 * and receive-waiting, in routines BRecvPost, BSend, BRecvWait.         
 */
static int rescomm(int Nlocal, realtype tt, 
                   N_Vector uv, N_Vector uvp,
                   void *user_data)
{

  UserData data;
  realtype *cdata, *gridext, buffer[2*NUM_SPECIES*MYSUB];
  int thispe, ixsub, jysub, nsmxsub, nsmysub;
  MPI_Comm comm;
  MPI_Request request[4];
  
  data = (UserData) user_data;
  cdata = NV_DATA_P(uv);

  /* Get comm, thispe, subgrid indices, data sizes, extended array cext. */  
  comm = data->comm;     
  thispe = data->thispe;

  ixsub = data->ixsub;   
  jysub = data->jysub;
  gridext = data->gridext;
  nsmxsub = data->nsmxsub; 
  nsmysub = (data->ns)*(data->mysub);
  
  /* Start receiving boundary data from neighboring PEs. */
  BRecvPost(comm, request, thispe, ixsub, jysub, nsmxsub, nsmysub, 
            gridext, buffer);
  
  /* Send data from boundary of local grid to neighboring PEs. */
  BSend(comm, thispe, ixsub, jysub, nsmxsub, nsmysub, cdata);
  
  /* Finish receiving boundary data from neighboring PEs. */
  BRecvWait(request, ixsub, jysub, nsmxsub, gridext, buffer);
  
  return(0);
}

/*
 * BRecvPost: Start receiving boundary data from neighboring PEs.
 * (1) buffer should be able to hold 2*NUM_SPECIES*MYSUB realtype entries,
 *     should be passed to both the BRecvPost and BRecvWait functions, and
 *     should not be manipulated between the two calls.
 * (2) request should have 4 entries, and is also passed in both calls. 
 */

static void BRecvPost(MPI_Comm comm, MPI_Request request[], int my_pe,
                      int ixsub, int jysub,
                      int dsizex, int dsizey,
                      realtype cext[], realtype buffer[])
{
  int offsetce;
  /* Have bufleft and bufright use the same buffer. */
  realtype *bufleft = buffer, *bufright = buffer+NUM_SPECIES*MYSUB;

  /* If jysub > 0, receive data for bottom x-line of cext. */
  if (jysub != 0)
    MPI_Irecv(&cext[NUM_SPECIES], dsizex, PVEC_REAL_MPI_TYPE,
              my_pe-NPEX, 0, comm, &request[0]);
  
  /* If jysub < NPEY-1, receive data for top x-line of cext. */
  if (jysub != NPEY-1) {
    offsetce = NUM_SPECIES*(1 + (MYSUB+1)*(MXSUB+2));
    MPI_Irecv(&cext[offsetce], dsizex, PVEC_REAL_MPI_TYPE,
              my_pe+NPEX, 0, comm, &request[1]);
  }
  
  /* If ixsub > 0, receive data for left y-line of cext (via bufleft). */
  if (ixsub != 0) {
    MPI_Irecv(&bufleft[0], dsizey, PVEC_REAL_MPI_TYPE,
              my_pe-1, 0, comm, &request[2]);
  }
  
  /* If ixsub < NPEX-1, receive data for right y-line of cext (via bufright). */
  if (ixsub != NPEX-1) {
    MPI_Irecv(&bufright[0], dsizey, PVEC_REAL_MPI_TYPE,
              my_pe+1, 0, comm, &request[3]);
  }
}

/*
 * BRecvWait: Finish receiving boundary data from neighboring PEs.
 * (1) buffer should be able to hold 2*NUM_SPECIES*MYSUB realtype entries,
 *     should be passed to both the BRecvPost and BRecvWait functions, and
 *     should not be manipulated between the two calls.
 * (2) request should have 4 entries, and is also passed in both calls.  
 */

static void BRecvWait(MPI_Request request[], int ixsub, int jysub,
                      int dsizex, realtype cext[], realtype buffer[])
{
  int i;
  int ly, dsizex2, offsetce, offsetbuf;
  realtype *bufleft = buffer, *bufright = buffer+NUM_SPECIES*MYSUB;
  MPI_Status status;
  
  dsizex2 = dsizex + 2*NUM_SPECIES;
  
  /* If jysub > 0, receive data for bottom x-line of cext. */
  if (jysub != 0)
    MPI_Wait(&request[0],&status);
  
  /* If jysub < NPEY-1, receive data for top x-line of cext. */
  if (jysub != NPEY-1)
    MPI_Wait(&request[1],&status);

  /* If ixsub > 0, receive data for left y-line of cext (via bufleft). */
  if (ixsub != 0) {
    MPI_Wait(&request[2],&status);

    /* Copy the buffer to cext */
    for (ly = 0; ly < MYSUB; ly++) {
      offsetbuf = ly*NUM_SPECIES;
      offsetce = (ly+1)*dsizex2;
      for (i = 0; i < NUM_SPECIES; i++)
        cext[offsetce+i] = bufleft[offsetbuf+i];
    }
  }
  
  /* If ixsub < NPEX-1, receive data for right y-line of cext (via bufright). */
  if (ixsub != NPEX-1) {
    MPI_Wait(&request[3],&status);

    /* Copy the buffer to cext */
    for (ly = 0; ly < MYSUB; ly++) {
      offsetbuf = ly*NUM_SPECIES;
      offsetce = (ly+2)*dsizex2 - NUM_SPECIES;
      for (i = 0; i < NUM_SPECIES; i++)
        cext[offsetce+i] = bufright[offsetbuf+i];
    }
  }
}

/*
 * BSend: Send boundary data to neighboring PEs.
 * This routine sends components of uv from internal subgrid boundaries
 * to the appropriate neighbor PEs.                                      
 */
 
static void BSend(MPI_Comm comm, int my_pe, int ixsub, int jysub,
                  int dsizex, int dsizey, realtype cdata[])
{
  int i;
  int ly, offsetc, offsetbuf;
  realtype bufleft[NUM_SPECIES*MYSUB], bufright[NUM_SPECIES*MYSUB];

  /* If jysub > 0, send data from bottom x-line of uv. */

  if (jysub != 0)
    MPI_Send(&cdata[0], dsizex, PVEC_REAL_MPI_TYPE, my_pe-NPEX, 0, comm);

  /* If jysub < NPEY-1, send data from top x-line of uv. */

  if (jysub != NPEY-1) {
    offsetc = (MYSUB-1)*dsizex;
    MPI_Send(&cdata[offsetc], dsizex, PVEC_REAL_MPI_TYPE, my_pe+NPEX, 0, comm);
  }

  /* If ixsub > 0, send data from left y-line of uv (via bufleft). */

  if (ixsub != 0) {
    for (ly = 0; ly < MYSUB; ly++) {
      offsetbuf = ly*NUM_SPECIES;
      offsetc = ly*dsizex;
      for (i = 0; i < NUM_SPECIES; i++)
        bufleft[offsetbuf+i] = cdata[offsetc+i];
    }
    MPI_Send(&bufleft[0], dsizey, PVEC_REAL_MPI_TYPE, my_pe-1, 0, comm);   
  }
  
  /* If ixsub < NPEX-1, send data from right y-line of uv (via bufright). */
  
  if (ixsub != NPEX-1) {
    for (ly = 0; ly < MYSUB; ly++) {
      offsetbuf = ly*NUM_SPECIES;
      offsetc = offsetbuf*MXSUB + (MXSUB-1)*NUM_SPECIES;
      for (i = 0; i < NUM_SPECIES; i++)
        bufright[offsetbuf+i] = cdata[offsetc+i];
    }
    MPI_Send(&bufright[0], dsizey, PVEC_REAL_MPI_TYPE, my_pe+1, 0, comm);   
  }
}
 
/* Define lines are for ease of readability in the following functions. */

#define mxsub      (data->mxsub)
#define mysub      (data->mysub)
#define npex       (data->npex)
#define npey       (data->npey)
#define ixsub      (data->ixsub)
#define jysub      (data->jysub)
#define nsmxsub    (data->nsmxsub)
#define nsmxsub2   (data->nsmxsub2)
#define dx         (data->dx)
#define dy         (data->dy)
#define cox        (data->cox)
#define coy        (data->coy)
#define gridext    (data->gridext)
#define eps        (data->eps)
#define ns         (data->ns)

/*
 * reslocal: Compute res = F(t,uv,uvp).
 * This routine assumes that all inter-processor communication of data
 * needed to calculate F has already been done.  Components at interior
 * subgrid boundaries are assumed to be in the work array cext.
 * The local portion of the uv vector is first copied into cext.
 * The exterior Neumann boundary conditions are explicitly handled here
 * by copying data from the first interior mesh line to the ghost cell
 * locations in cext.  Then the reaction and diffusion terms are
 * evaluated in terms of the cext array, and the residuals are formed.
 * The reaction terms are saved separately in the vector data->rates
 * for use by the preconditioner setup routine.                          
 */

static int reslocal(int Nlocal, realtype tt, 
                    N_Vector uv, N_Vector uvp, N_Vector rr,
                    void *user_data)
{
  realtype *uvdata, *uvpxy, *resxy, xx, yy, dcyli, dcyui, dcxli, dcxui, dx2, dy2;
  realtype ixend, ixstart, jystart, jyend;
  int ix, jy, is, i, locc, ylocce, locce;
  realtype rates[2];
  UserData data;
  
  data = (UserData) user_data;

  /* Get data pointers, subgrid data, array sizes, work array cext. */
  uvdata = NV_DATA_P(uv);

  dx2 = dx * dx;
  dy2 = dy * dy;

  /* Copy local segment of uv vector into the working extended array gridext. */
  locc = 0;
  locce = nsmxsub2 + NUM_SPECIES;
  for (jy = 0; jy < mysub; jy++) {
    for (i = 0; i < nsmxsub; i++) gridext[locce+i] = uvdata[locc+i];
    locc = locc + nsmxsub;
    locce = locce + nsmxsub2;
  }

  /* To facilitate homogeneous Neumann boundary conditions, when this is
     a boundary PE, copy data from the first interior mesh line of uv to gridext. */
  
  /* If jysub = 0, copy x-line 2 of uv to gridext. */
  if (jysub == 0)
    { for (i = 0; i < nsmxsub; i++) gridext[NUM_SPECIES+i] = uvdata[nsmxsub+i]; }
  

  /* If jysub = npey-1, copy x-line mysub-1 of uv to gridext. */
  if (jysub == npey-1) {
    locc = (mysub-2)*nsmxsub;
    locce = (mysub+1)*nsmxsub2 + NUM_SPECIES;
    for (i = 0; i < nsmxsub; i++) gridext[locce+i] = uvdata[locc+i];
  }
  

  /* If ixsub = 0, copy y-line 2 of uv to gridext. */
  if (ixsub == 0) {
    for (jy = 0; jy < mysub; jy++) {
      locc = jy*nsmxsub + NUM_SPECIES;
      locce = (jy+1)*nsmxsub2;
      for (i = 0; i < NUM_SPECIES; i++) gridext[locce+i] = uvdata[locc+i];
    }
  }
  

  /* If ixsub = npex-1, copy y-line mxsub-1 of uv to gridext. */
  if (ixsub == npex-1) {
    for (jy = 0; jy < mysub; jy++) {
      locc  = (jy+1)*nsmxsub - 2*NUM_SPECIES;
      locce = (jy+2)*nsmxsub2 - NUM_SPECIES;
      for (i = 0; i < NUM_SPECIES; i++) gridext[locce+i] = uvdata[locc+i];
    }
  }
  
  /* Loop over all grid points, setting local array rates to right-hand sides.
     Then set rr values appropriately (ODE in the interior and DAE on the boundary)*/
  ixend = ixstart = jystart = jyend = 0;  

  if (jysub==0)      jystart = 1;
  if (jysub==npey-1) jyend   = 1;
  if (ixsub==0)      ixstart = 1;
  if (ixsub==npex-1) ixend   = 1;

  for (jy = jystart; jy < mysub-jyend; jy++) { 
    ylocce = (jy+1)*nsmxsub2;
    yy     = (jy+jysub*mysub)*dy;

    for (ix = ixstart; ix < mxsub-ixend; ix++) {
      locce = ylocce + (ix+1)*NUM_SPECIES;
      xx = (ix + ixsub*mxsub)*dx;

      ReactRates(xx, yy, &(gridext[locce]), rates, data);      
      
      resxy = IJ_Vptr(rr,ix,jy); 
      uvpxy = IJ_Vptr(uvp,ix,jy); 
      
      for (is = 0; is < NUM_SPECIES; is++) {
        dcyli = gridext[locce+is]          - gridext[locce+is-nsmxsub2];
        dcyui = gridext[locce+is+nsmxsub2] - gridext[locce+is];
        
        dcxli = gridext[locce+is]             - gridext[locce+is-NUM_SPECIES];
        dcxui = gridext[locce+is+NUM_SPECIES] - gridext[locce+is];
        
        resxy[is] = uvpxy[is]- eps[is]*( (dcxui-dcxli)/dx2 + (dcyui-dcyli)/dy2 ) - rates[is];
      }
    }
  }

  /* Algebraic equation correspoding to boundary mesh point. */
  if (jysub==0) {
    for (ix=0; ix<mxsub; ix++) {
      
      locce = nsmxsub2 + NUM_SPECIES * (ix+1);
      resxy = IJ_Vptr(rr,ix,0); 

      for (is=0; is<NUM_SPECIES; is++)
        resxy[is] = gridext[locce+is+nsmxsub2] - gridext[locce+is];
    }
  }

  if (ixsub==npex-1) {
    for(jy=0; jy<mysub; jy++) {
      locce = (jy+1)*nsmxsub2 + nsmxsub2-NUM_SPECIES;
      resxy = IJ_Vptr(rr,mxsub-1,jy); 

      for (is=0; is<NUM_SPECIES; is++)
        resxy[is] = gridext[locce+is-NUM_SPECIES] - gridext[locce+is];
    }
  }
  
  if (ixsub==0) {
    for (jy=0; jy<mysub; jy++) {
      locce = (jy+1)*nsmxsub2 + NUM_SPECIES;
      resxy = IJ_Vptr(rr,0,jy); 

      for (is=0; is<NUM_SPECIES; is++)
        resxy[is] = gridext[locce+is-NUM_SPECIES] - gridext[locce+is];
    }    
  }

  if (jysub==npey-1) {
    for(ix=0; ix<mxsub; ix++) {
      locce = nsmxsub2*mysub + (ix+1)*NUM_SPECIES;
      resxy = IJ_Vptr(rr,ix, mysub-1);

      for (is=0; is<NUM_SPECIES; is++)
        resxy[is] = gridext[locce+is-nsmxsub2] - gridext[locce+is];
    }
  }
  return(0);
}

/* 
 * ReactRates: Evaluate reaction rates at a given spatial point.   
 * At a given (x,y), evaluate the array of ns reaction terms R.          
 */

static void ReactRates(realtype xx, realtype yy, realtype *uvval, realtype *rates,
                       UserData data)
{
  realtype A, B;

  A = data->A; B = data->B;

  rates[0] = uvval[0]*uvval[0]*uvval[1];
  rates[1] = - rates[0];

  rates[0] += A-(B+1)*uvval[0];
  rates[1] += B*uvval[0];
}

/* Integrate over the spatial domain. Each process computes the integral on his
   grid. After that processes call MPI_REDUCE to compute the sum of the local values.*/
static int integr(MPI_Comm comm, N_Vector uv, void *user_data, realtype *intval)
{
  int ix, jy;
  int retval;
  realtype *uvdata;
  UserData data;
  
  realtype buf[2];
  
  data = (UserData) user_data;

  /* compute the integral on the (local) grid */
  uvdata = NV_DATA_P(uv);

  *intval = 0;

  for (jy=1; jy<mysub; jy++)
    for (ix=1; ix<mxsub; ix++) 
      //consider only u
      *intval += uvdata[ix*NUM_SPECIES + jy*NSMXSUB];
  *intval *= (dx*dy);

  buf[0] = *intval;

  /* Sum local values and get the result on all processors. */
  retval = MPI_Allreduce(buf, buf+1, 1, 
                      MPI_DOUBLE,
                      MPI_SUM, 
                      comm);

  *intval = buf[1];
  return(0);
}
