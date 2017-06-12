static char help[] =
"Structured-grid Poisson problem in 2D using DMDA+SNES.  Option prefix fsh_.\n"
"Solves  - nabla^2 u = f  by putting it in form  F(u) = - nabla^2 u - f.\n"
"Dirichlet boundary conditions on unit square.  Three different problems\n"
"where exact solution is known.  Multigrid-capable because call-backs\n"
"fully-rediscretize for the supplied grid.\n\n";

/*
note -fsh_problem manuexp is problem re-used many times in Smith et al 1996

add options to any run:
    -{ksp,snes}_monitor -{ksp,snes}_converged_reason

since these are linear problems, consider adding:
    -snes_type ksponly -ksp_rtol 1.0e-12

see study/mgstudy.sh for multigrid parameter study

this makes sense and shows V-cycles:
$ ./fish -da_refine 3 -pc_type mg -snes_type ksponly -ksp_converged_reason -mg_levels_ksp_monitor

in parallel with -snes_fd_color (exploits full rediscretization)
$ mpiexec -n 2 ./fish -da_refine 4 -pc_type mg -snes_fd_color

compare with rediscretization at every level or use Galerkin coarse grid operator
$ ./fish -da_refine 4 -pc_type mg -snes_monitor
$ ./fish -da_refine 4 -pc_type mg -snes_monitor -pc_mg_galerkin

choose linear solver for coarse grid (default is preonly+lu):
$ ./fish -da_refine 4 -pc_type mg -mg_coarse_ksp_type cg -mg_coarse_pc_type jacobi -ksp_view

to make truly random init, with time as seed, add
    #include <time.h>
    ...
        ierr = PetscRandomSetSeed(rctx,time(NULL)); CHKERRQ(ierr);
        ierr = PetscRandomSeed(rctx); CHKERRQ(ierr);

to generate classical jacobi/gauss-seidel results, put f in a Vec and
add viewer for RHS:
   PetscViewer viewer;
   PetscViewerASCIIOpen(COMM,"rhs.m",&viewer);
   PetscViewerPushFormat(viewer,PETSC_VIEWER_ASCII_MATLAB);
   VecView(f,viewer);
then do:
$ ./fish -da_refine 1 -snes_monitor -ksp_monitor -snes_max_it 1 -ksp_type richardson -pc_type jacobi|sor
with e.g. -ksp_monitor_solution :foo.m:ascii_matlab
*/

#include <petsc.h>
#include "poissonfunctions.h"


// exact solutions  u(x,y),  for boundary condition and error calculation

double u_exact_manupoly(double x, double y, double z) {
    return (x - x*x) * (y*y - y);
}

double u_exact_manuexp(double x, double y, double z) {
    return - x * exp(y);
}

double u_exact_zero(double x, double y, double z) {
    return 0.0;
}

// right-hand-side functions  f(x,y) = - laplacian u

double f_rhs_manupoly(double x, double y, double z) {
    double uxx, uyy;
    uxx  = - 2.0 * (y*y - y);
    uyy  = (x - x*x) * 2.0;
    return - uxx - uyy;
}

double f_rhs_manuexp(double x, double y, double z) {
    return x * exp(y);  // indeed   - (u_xx + u_yy) = -u  !
}

double f_rhs_zero(double x, double y, double z) {
    return 0.0;
}

PetscErrorCode formExact(DMDALocalInfo *info, Vec u, PoissonCtx* user) {
    PetscErrorCode ierr;
    int     i, j;
    double  xymin[2], xymax[2], hx, hy, x, y, **au;
    ierr = DMDAGetBoundingBox(info->da,xymin,xymax); CHKERRQ(ierr);
    hx = (xymax[0] - xymin[0]) / (info->mx - 1);
    hy = (xymax[1] - xymin[1]) / (info->my - 1);
    ierr = DMDAVecGetArray(info->da, u, &au);CHKERRQ(ierr);
    for (j=info->ys; j<info->ys+info->ym; j++) {
        y = xymin[1] + j * hy;
        for (i=info->xs; i<info->xs+info->xm; i++) {
            x = xymin[0] + i * hx;
            au[j][i] = user->u_exact(x,y,0.0);
        }
    }
    ierr = DMDAVecRestoreArray(info->da, u, &au);CHKERRQ(ierr);
    return 0;
}

static void* u_exact_ptr[] = {&u_exact_manupoly, &u_exact_manuexp, &u_exact_zero};

static void* f_rhs_ptr[]   = {&f_rhs_manupoly,   &f_rhs_manuexp,   &f_rhs_zero};

int main(int argc,char **argv) {
    PetscErrorCode    ierr;
    DM             da;
    SNES           snes;
    KSP            ksp;
    Vec            u, uexact;
    PoissonCtx     user;
    double         Lx = 1.0, Ly = 1.0;
    PetscBool      init_random = PETSC_FALSE;
    DMDALocalInfo  info;
    double         errinf,err2h;

    PetscInitialize(&argc,&argv,NULL,help);
    user.problem = MANUEXP;
    ierr = PetscOptionsBegin(PETSC_COMM_WORLD,"fsh_", "options for fish.c", ""); CHKERRQ(ierr);
    ierr = PetscOptionsReal("-Lx",
         "set Lx in domain [0,Lx] x [0,Ly]","fish.c",Lx,&Lx,NULL);CHKERRQ(ierr);
    ierr = PetscOptionsReal("-Ly",
         "set Ly in domain [0,Lx] x [0,Ly]","fish.c",Ly,&Ly,NULL);CHKERRQ(ierr);
    ierr = PetscOptionsBool("-init_random",
         "initial state is random (default is zero)",
         "fish.c",init_random,&init_random,NULL);CHKERRQ(ierr);
    ierr = PetscOptionsEnum("-problem",
         "problem type (determines exact solution and RHS)",
         "fish.c",PoissonProblemTypes,
         (PetscEnum)user.problem,(PetscEnum*)&user.problem,NULL); CHKERRQ(ierr);
    user.u_exact = u_exact_ptr[user.problem];
    user.f_rhs = f_rhs_ptr[user.problem];
    ierr = PetscOptionsEnd(); CHKERRQ(ierr);

    ierr = DMDACreate2d(PETSC_COMM_WORLD,
               DM_BOUNDARY_NONE, DM_BOUNDARY_NONE,
               DMDA_STENCIL_STAR,
               3,3,PETSC_DECIDE,PETSC_DECIDE,
               1,1,NULL,NULL,&da); CHKERRQ(ierr);
    ierr = DMSetFromOptions(da); CHKERRQ(ierr);
    ierr = DMSetUp(da); CHKERRQ(ierr);  // this must be called BEFORE SetUniformCoordinates
    ierr = DMSetApplicationContext(da,&user);CHKERRQ(ierr);
    ierr = DMDASetUniformCoordinates(da,0.0,Lx,0.0,Ly,0.0,1.0);CHKERRQ(ierr);
    ierr = DMCreateGlobalVector(da,&u);CHKERRQ(ierr);
    ierr = PetscObjectSetName((PetscObject)u,"u");CHKERRQ(ierr);

    ierr = SNESCreate(PETSC_COMM_WORLD,&snes); CHKERRQ(ierr);
    ierr = SNESSetDM(snes,da); CHKERRQ(ierr);
    ierr = DMDASNESSetFunctionLocal(da,INSERT_VALUES,
             (DMDASNESFunction)Form2DFunctionLocal,&user); CHKERRQ(ierr);
    ierr = DMDASNESSetJacobianLocal(da,
             (DMDASNESJacobian)Form2DJacobianLocal,&user); CHKERRQ(ierr);
    ierr = SNESGetKSP(snes,&ksp); CHKERRQ(ierr);
    ierr = KSPSetType(ksp,KSPCG); CHKERRQ(ierr);
    ierr = SNESSetFromOptions(snes); CHKERRQ(ierr);

    if (init_random) {
        PetscRandom   rctx;
        ierr = PetscRandomCreate(PETSC_COMM_WORLD,&rctx); CHKERRQ(ierr);
        ierr = VecSetRandom(u,rctx); CHKERRQ(ierr);
        ierr = PetscRandomDestroy(&rctx); CHKERRQ(ierr);
    } else {
        ierr = VecSet(u,0.0); CHKERRQ(ierr);
    }

    ierr = SNESSolve(snes,NULL,u); CHKERRQ(ierr);

    ierr = DMDAGetLocalInfo(da,&info); CHKERRQ(ierr);
    ierr = VecDuplicate(u,&uexact);CHKERRQ(ierr);
    ierr = formExact(&info,uexact,&user); CHKERRQ(ierr);
    ierr = VecAXPY(u,-1.0,uexact); CHKERRQ(ierr);    // u <- u + (-1.0) uexact
    ierr = VecNorm(u,NORM_INFINITY,&errinf); CHKERRQ(ierr);
    ierr = VecNorm(u,NORM_2,&err2h); CHKERRQ(ierr);
    err2h /= PetscSqrtReal((double)(info.mx-1)*(info.my-1)); // like continuous L2
    ierr = PetscPrintf(PETSC_COMM_WORLD,
           "on %d x %d grid:  error |u-uexact|_inf = %g, |...|_h = %.2e\n",
           info.mx,info.my,errinf,err2h); CHKERRQ(ierr);

    VecDestroy(&u);  VecDestroy(&uexact);
    SNESDestroy(&snes);  DMDestroy(&da);
    PetscFinalize();
    return 0;
}

