/*============================================================================
 * Routines to handle cs_equation_t structure and its related structures
 *============================================================================*/

/*
  This file is part of Code_Saturne, a general-purpose CFD tool.

  Copyright (C) 1998-2016 EDF S.A.

  This program is free software; you can redistribute it and/or modify it under
  the terms of the GNU General Public License as published by the Free Software
  Foundation; either version 2 of the License, or (at your option) any later
  version.

  This program is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
  details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc., 51 Franklin
  Street, Fifth Floor, Boston, MA 02110-1301, USA.
*/

/*----------------------------------------------------------------------------*/

#include "cs_defs.h"

/*----------------------------------------------------------------------------
 * Standard C library headers
 *----------------------------------------------------------------------------*/

#include <assert.h>
#include <string.h>
#include <float.h>
#include <stdlib.h>
#include <math.h>

#if defined(HAVE_MPI)
#include <mpi.h>
#endif

#if defined(HAVE_PETSC)
#include <petscdraw.h>
#include <petscviewer.h>
#include <petscksp.h>
#endif

/*----------------------------------------------------------------------------
 *  Local headers
 *----------------------------------------------------------------------------*/

#include <bft_mem.h>
#include <bft_printf.h>

#include "cs_base.h"
#include "cs_cdo.h"
#include "cs_evaluate.h"
#include "cs_mesh_location.h"
#include "cs_multigrid.h"
#include "cs_timer_stats.h"
#include "cs_cdovb_scaleq.h"
#include "cs_cdofb_scaleq.h"

#if defined(HAVE_PETSC)
#include "cs_sles_petsc.h"
#endif

/*----------------------------------------------------------------------------
 * Header for the current file
 *----------------------------------------------------------------------------*/

#include "cs_equation.h"

/*----------------------------------------------------------------------------*/

BEGIN_C_DECLS

/*============================================================================
 * Type definitions
 *============================================================================*/

/*----------------------------------------------------------------------------
 * Function pointer types
 *----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Initialize a builder structure
 *
 * \param[in] eq         pointer to a cs_equation_param_t structure
 * \param[in] mesh       pointer to a cs_mesh_t structure
 * \param[in] connect    pointer to a cs_cdo_connect_t structure
 * \param[in] quant      pointer to a cs_cdo_quantities_t structure
 * \param[in] time_step  time_step structure
 *
 * \return a pointer to a new allocated builder structure
 */
/*----------------------------------------------------------------------------*/

typedef void *
(cs_equation_init_builder_t)(const cs_equation_param_t  *eqp,
                             const cs_mesh_t            *mesh,
                             const cs_cdo_connect_t     *connect,
                             const cs_cdo_quantities_t  *cdoq,
                             const cs_time_step_t       *time_step);

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Compute the contribution of source terms for the current time
 *
 * \param[in, out] builder    pointer to builder structure
 */
/*----------------------------------------------------------------------------*/

typedef void
(cs_equation_compute_source_t)(void          *builder);

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Build a linear system within the CDO framework
 *
 * \param[in]      m          pointer to a cs_mesh_t structure
 * \param[in]      field_val  pointer to the current value of the field
 * \param[in]      dt_cur     current value of the time step
 * \param[in, out] builder    pointer to builder structure
 * \param[in, out] rhs        pointer to a right-hand side array pointer
 * \param[in, out] sla_mat    pointer to cs_sla_matrix_t structure pointer
 */
/*----------------------------------------------------------------------------*/

typedef void
(cs_equation_build_system_t)(const cs_mesh_t            *mesh,
                             const cs_real_t            *field_val,
                             double                      dt_cur,
                             void                       *builder,
                             cs_real_t                 **rhs,
                             cs_sla_matrix_t           **sla_mat);

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Store solution(s) of the linear system into a field structure
 *
 * \param[in]      solu       solution array
 * \param[in, out] builder    pointer to builder structure
 * \param[in, out] field_val  pointer to the current value of the field
 */
/*----------------------------------------------------------------------------*/

typedef void
(cs_equation_update_field_t)(const cs_real_t            *solu,
                             void                       *builder,
                             cs_real_t                  *field_val);


/*----------------------------------------------------------------------------*/
/*!
 * \brief  Extra-operation related to this equation
 *
 * \param[in]       eqname     name of the equation
 * \param[in]       field      pointer to a field strufcture
 * \param[in, out]  builder    pointer to builder structure
 */
/*----------------------------------------------------------------------------*/

typedef void
(cs_equation_extra_op_t)(const char                 *eqname,
                         const cs_field_t           *field,
                         void                       *builder);

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Get the computed values at each face
 *
 * \param[in]  builder    pointer to a builder structure
 * \param[in]  field      pointer to a cs_field_t structure
 *
 * \return  a pointer to an array of double (size n_faces)
 */
/*----------------------------------------------------------------------------*/

typedef const double *
(cs_equation_get_f_values_t)(const void          *builder,
                             const cs_field_t    *field);

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Retrieve a pointer to a buffer of size at least the number of unknows
 *
 * \param[in, out]  builder    pointer to a builder structure
 *
 * \return  a pointer to an array of double
 */
/*----------------------------------------------------------------------------*/

typedef cs_real_t *
(cs_equation_get_tmpbuf_t)(void);

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Destroy a builder structure
 *
 * \param[in, out]  builder   pointer to a builder structure
 *
 * \return a NULL pointer
 */
/*----------------------------------------------------------------------------*/

typedef void *
(cs_equation_free_builder_t)(void  *builder);

/*============================================================================
 * Local variables
 *============================================================================*/

/* Default initialization */
static cs_equation_algo_t _algo_info_by_default = {
#if defined(HAVE_PETSC)
  CS_EQUATION_ALGO_PETSC_ITSOL, // Family of iterative solvers
#else
  CS_EQUATION_ALGO_CS_ITSOL,    // Family of iterative solvers
#endif
  0,                            // n_iters
  50,                           // max. number of iterations
  0,                            // n_cumulated_iters
  10000,                        // max. number of cumulated iterations
  1e-6                          // stopping criterion
};

static cs_param_itsol_t _itsol_info_by_default = {
#if defined(HAVE_PETSC)
  CS_PARAM_PRECOND_ILU0,  // preconditioner
  CS_PARAM_ITSOL_BICG,    // iterative solver
#else
  CS_PARAM_PRECOND_DIAG,  // preconditioner
  CS_PARAM_ITSOL_CG,      // iterative solver
#endif
  2500,                   // max. number of iterations
  1e-12,                  // stopping criterion on the accuracy
  150,                    // output frequency
  false                   // normalization of the residual (true or false)
};

/* List of available keys for setting an equation */
typedef enum {

  EQKEY_HODGE_DIFF_ALGO,
  EQKEY_HODGE_DIFF_COEF,
  EQKEY_HODGE_TIME_ALGO,
  EQKEY_HODGE_TIME_COEF,
  EQKEY_ITSOL,
  EQKEY_ITSOL_EPS,
  EQKEY_ITSOL_MAX_ITER,
  EQKEY_ITSOL_RESNORM,
  EQKEY_PRECOND,
  EQKEY_SOLVER_FAMILY,
  EQKEY_SPACE_SCHEME,
  EQKEY_VERBOSITY,
  EQKEY_SLES_VERBOSITY,
  EQKEY_BC_ENFORCEMENT,
  EQKEY_BC_QUADRATURE,
  EQKEY_EXTRA_OP,
  EQKEY_ADV_OP_TYPE,
  EQKEY_ADV_WEIGHT_ALGO,
  EQKEY_ADV_WEIGHT_CRIT,
  EQKEY_ADV_FLUX_QUADRA,
  EQKEY_TIME_SCHEME,
  EQKEY_TIME_THETA,
  EQKEY_ERROR

} eqkey_t;

/* List of keys for setting a reaction term */
typedef enum {

  REAKEY_LUMPING,
  REAKEY_HODGE_ALGO,
  REAKEY_HODGE_COEF,
  REAKEY_INV_PTY,
  REAKEY_ERROR

} reakey_t;

/*=============================================================================
 * Local Macro definitions and structure definitions
 *============================================================================*/

struct _cs_equation_t {

  char *restrict         name;    /* Short description */

  cs_equation_param_t   *param;   /* Set of parameters related to an equation */

  /* Variable atached to this equation is also attached to a cs_field_t
     structure */
  char *restrict         varname;
  int                    field_id;

  /* Timer statistic for a "light" profiling */
  int     main_ts_id;     /* Id of the main timer states structure related
                             to this equation */
  int     pre_ts_id;      /* Id of the timer stats structure gathering all
                             steps before the resolution of the linear
                             systems */
  int     solve_ts_id;    /* Id of the timer stats structure related
                             to the inversion of the linear system */
  int     extra_op_ts_id; /* Id of the timer stats structure gathering all
                             steps afterthe resolution of the linear systems
                             (post, balance...) */

  bool    do_build;       /* false => keep the system as it is */

  /* Algebraic system */
  cs_matrix_structure_t    *ms;      /* matrix structure (how are stored
                                        coefficients of the matrix a) */
  cs_matrix_t              *matrix;  // matrix to inverse with cs_sles_solve()
  cs_real_t                *rhs;     // right-hand side

  /* System builder depending on the numerical scheme*/
  void                     *builder;

  /* Pointer to functions */
  cs_equation_init_builder_t    *init_builder;
  cs_equation_free_builder_t    *free_builder;
  cs_equation_build_system_t    *build_system;
  cs_equation_compute_source_t  *compute_source;
  cs_equation_update_field_t    *update_field;
  cs_equation_extra_op_t        *postprocess;
  cs_equation_get_f_values_t    *get_f_values;
  cs_equation_get_tmpbuf_t      *get_tmpbuf;

};

/*============================================================================
 * Private variables
 *============================================================================*/

static const char _err_empty_eq[] =
  N_(" Stop setting an empty cs_equation_t structure.\n"
     " Please check your settings.\n");

/*============================================================================
 * Private function prototypes
 *============================================================================*/

#if defined(HAVE_PETSC)

/*----------------------------------------------------------------------------
 * \brief Add visualization of the matrix graph
 *
 * \param[in]  ksp     Krylov SubSpace structure
 *----------------------------------------------------------------------------*/

static void
_add_view(KSP          ksp)
{
  const char *p = getenv("CS_USER_PETSC_MAT_VIEW");

  if (p != NULL) {

    /* Get system and preconditioner matrixes */

    Mat a, pa;
    KSPGetOperators(ksp, &a, &pa);

    /* Output matrix in several ways depending on
       CS_USER_PETSC_MAT_VIEW environment variable */

    if (strcmp(p, "DEFAULT") == 0)
      MatView(a, PETSC_VIEWER_DEFAULT);

    else if (strcmp(p, "DRAW_WORLD") == 0)
      MatView(a, PETSC_VIEWER_DRAW_WORLD);

    else if (strcmp(p, "DRAW") == 0) {

      PetscViewer viewer;
      PetscDraw draw;
      PetscViewerDrawOpen(PETSC_COMM_WORLD, NULL, "PETSc View",
                          0, 0, 600, 600, &viewer);
      PetscViewerDrawGetDraw(viewer, 0, &draw);
      PetscViewerDrawSetPause(viewer, -1);
      MatView(a, viewer);
      PetscDrawPause(draw);

      PetscViewerDestroy(&viewer);

    }

  }

}

/*----------------------------------------------------------------------------
 * \brief PETSc solver using CG with Jacobi preconditioner
 *
 * \param[in, out] context  pointer to optional (untyped) value or structure
 * \param[in, out] ksp      pointer to PETSc KSP context
 *----------------------------------------------------------------------------*/

static void
_cg_diag_setup_hook(void   *context,
                    KSP     ksp)
{
  PC pc;

  KSPSetType(ksp, KSPCG);   /* Preconditioned Conjugate Gradient */

  KSPSetNormType(ksp, KSP_NORM_UNPRECONDITIONED); /* Try to have "true" norm */

  KSPGetPC(ksp, &pc);
  PCSetType(pc, PCJACOBI);  /* Jacobi (diagonal) preconditioning */

  _add_view(ksp);
}

/*----------------------------------------------------------------------------
 * \brief PETSc solver using CG with SSOR preconditioner
 *        Warning: this PETSc implementation is only available in serial mode
 *
 * \param[in, out] context  pointer to optional (untyped) value or structure
 * \param[in, out] ksp      pointer to PETSc KSP context
 *----------------------------------------------------------------------------*/

static void
_cg_ssor_setup_hook(void   *context,
                    KSP     ksp)
{
  PC pc;

  KSPSetType(ksp, KSPCG);   /* Preconditioned Conjugate Gradient */

  KSPSetNormType(ksp, KSP_NORM_UNPRECONDITIONED); /* Try to have "true" norm */

  KSPGetPC(ksp, &pc);
  PCSetType(pc, PCSOR);  /* SSOR preconditioning */
  PCSORSetSymmetric(pc, SOR_SYMMETRIC_SWEEP);

  _add_view(ksp);
}

/*----------------------------------------------------------------------------
 * \brief PETSc solver using CG with Additive Schwarz preconditioner
 *
 * \param[in, out] context  pointer to optional (untyped) value or structure
 * \param[in, out] ksp      pointer to PETSc KSP context
 *----------------------------------------------------------------------------*/

static void
_cg_as_setup_hook(void   *context,
                  KSP     ksp)
{
  PC pc;

  KSPSetType(ksp, KSPCG);   /* Preconditioned Conjugate Gradient */

  KSPSetNormType(ksp, KSP_NORM_UNPRECONDITIONED); /* Try to have "true" norm */

  KSPGetPC(ksp, &pc);
  PCSetType(pc, PCASM);

  _add_view(ksp);
}

/*----------------------------------------------------------------------------
 * \brief PETSc solver using CG with ICC preconditioner
 *        Warning: this PETSc implementation is only available in serial mode
 *
 * \param[in, out] context  pointer to optional (untyped) value or structure
 * \param[in, out] ksp      pointer to PETSc KSP context
 *----------------------------------------------------------------------------*/

static void
_cg_icc_setup_hook(void    *context,
                   KSP      ksp)
{
  PC pc;

  KSPSetType(ksp, KSPCG);   /* Preconditioned Conjugate Gradient */

  KSPSetNormType(ksp, KSP_NORM_UNPRECONDITIONED); /* Try to have "true" norm */

  KSPGetPC(ksp, &pc);
  PCSetType(pc, PCICC);
  PCFactorSetLevels(pc, 0);

  _add_view(ksp);
}

/*----------------------------------------------------------------------------
 * \brief PETSc solver using CG with GAMG preconditioner
 *
 * \param[in, out] context  pointer to optional (untyped) value or structure
 * \param[in, out] ksp      pointer to PETSc KSP context
 *----------------------------------------------------------------------------*/

static void
_cg_gamg_setup_hook(void    *context,
                    KSP      ksp)
{
  PC pc;

  KSPSetType(ksp, KSPCG);   /* Preconditioned Conjugate Gradient */

  KSPSetNormType(ksp, KSP_NORM_UNPRECONDITIONED); /* Try to have "true" norm */

  KSPGetPC(ksp, &pc);
  PCSetType(pc, PCGAMG);

  _add_view(ksp);
}

/*----------------------------------------------------------------------------
 * \brief PETSc solver using CG with Boomer AMG preconditioner (Hypre library)
 *
 * \param[in, out] context  pointer to optional (untyped) value or structure
 * \param[in, out] ksp      pointer to PETSc KSP context
 *----------------------------------------------------------------------------*/

static void
_cg_bamg_setup_hook(void    *context,
                    KSP      ksp)
{
  PC pc;

  KSPSetType(ksp, KSPCG);   /* Preconditioned Conjugate Gradient */

  KSPSetNormType(ksp, KSP_NORM_UNPRECONDITIONED); /* Try to have "true" norm */

  KSPGetPC(ksp, &pc);
  PCSetType(pc, PCHYPRE);

  _add_view(ksp);
}

/*----------------------------------------------------------------------------
 * \brief PETSc solver using GMRES with ILU0 preconditioner
 *        Warning: this PETSc implementation is only available in serial mode
 *
 * \param[in, out] context  pointer to optional (untyped) value or structure
 * \param[in, out] ksp      pointer to PETSc KSP context
 *----------------------------------------------------------------------------*/

static void
_gmres_ilu_setup_hook(void    *context,
                      KSP      ksp)
{
  PC pc;

  const int  n_max_restart = 30;

  KSPSetType(ksp, KSPGMRES);   /* Preconditioned GMRES */

  KSPGMRESSetRestart(ksp, n_max_restart);
  KSPSetNormType(ksp, KSP_NORM_UNPRECONDITIONED); /* Try to have "true" norm */

  KSPGetPC(ksp, &pc);
  PCSetType(pc, PCILU);
  PCFactorSetLevels(pc, 0);

  _add_view(ksp);
}

/*----------------------------------------------------------------------------
 * \brief PETSc solver using GMRES with block Jacobi preconditioner
 *
 * \param[in, out] context  pointer to optional (untyped) value or structure
 * \param[in, out] ksp      pointer to PETSc KSP context
 *----------------------------------------------------------------------------*/

static void
_gmres_bjacobi_setup_hook(void    *context,
                          KSP      ksp)
{
  PC pc;

  const int  n_max_restart = 30;

  KSPSetType(ksp, KSPGMRES);   /* Preconditioned GMRES */

  KSPGMRESSetRestart(ksp, n_max_restart);
  KSPSetNormType(ksp, KSP_NORM_UNPRECONDITIONED); /* Try to have "true" norm */

  KSPGetPC(ksp, &pc);
  PCSetType(pc, PCBJACOBI);

  _add_view(ksp);
}

/*----------------------------------------------------------------------------
 * \brief PETSc solver using BiCGStab with ILU0 preconditioner
 *        Warning: this PETSc implementation is only available in serial mode
 *
 * \param[in, out] context  pointer to optional (untyped) value or structure
 * \param[in, out] ksp      pointer to PETSc KSP context
 *----------------------------------------------------------------------------*/

static void
_bicg_ilu_setup_hook(void    *context,
                     KSP      ksp)
{
  PC pc;

  KSPSetType(ksp, KSPBCGS);   /* Preconditioned BiCGStab */
  KSPSetNormType(ksp, KSP_NORM_UNPRECONDITIONED); /* Try to have "true" norm */

  KSPGetPC(ksp, &pc);
  PCSetType(pc, PCILU);
  PCFactorSetLevels(pc, 0);

  _add_view(ksp);
}

/*----------------------------------------------------------------------------
 * \brief PETSc solver using BiCGStab with block Jacobi preconditioner
 *
 * \param[in, out] context  pointer to optional (untyped) value or structure
 * \param[in, out] ksp      pointer to PETSc KSP context
 *----------------------------------------------------------------------------*/

static void
_bicg_bjacobi_setup_hook(void    *context,
                         KSP      ksp)
{
  PC pc;

  KSPSetType(ksp, KSPBCGS);   /* Preconditioned BICGStab */
  KSPSetNormType(ksp, KSP_NORM_UNPRECONDITIONED); /* Try to have "true" norm */

  KSPGetPC(ksp, &pc);
  PCSetType(pc, PCBJACOBI);

  _add_view(ksp);
}

#endif /* defined(HAVE_PETSC) */

/*----------------------------------------------------------------------------
 * \brief Initialize SLES strcuture for the resolution of the linear system
 *
 * \param[in] eq  pointer to an cs_equation_t structure
 *----------------------------------------------------------------------------*/

static void
_sles_initialization(const cs_equation_t  *eq)
{
  const cs_equation_param_t  *eqp = eq->param;
  const cs_equation_algo_t  algo = eqp->algo_info;
  const cs_param_itsol_t  itsol = eqp->itsol_info;

  switch (algo.type) {
  case CS_EQUATION_ALGO_CS_ITSOL:
    {
      int  poly_degree = 0; // by default: Jacobi preconditioner

      if (itsol.precond == CS_PARAM_PRECOND_POLY1)
        poly_degree = 1;

      if (itsol.precond != CS_PARAM_PRECOND_POLY1 &&
          itsol.precond != CS_PARAM_PRECOND_DIAG)
        bft_error(__FILE__, __LINE__, 0,
                  " Incompatible preconditioner with Code_Saturne solvers.\n"
                  " Please change your settings (try PETSc ?)");

      switch (itsol.solver) { // Type of iterative solver
      case CS_PARAM_ITSOL_CG:
        cs_sles_it_define(eq->field_id,  // give the field id (future: eq_id ?)
                          NULL,
                          CS_SLES_PCG,
                          poly_degree,
                          itsol.n_max_iter);
        break;
      case CS_PARAM_ITSOL_BICG:
        cs_sles_it_define(eq->field_id,  // give the field id (future: eq_id ?)
                          NULL,
                          CS_SLES_BICGSTAB,
                          poly_degree,
                          itsol.n_max_iter);
        break;
      case CS_PARAM_ITSOL_GMRES:
        cs_sles_it_define(eq->field_id,  // give the field id (future: eq_id ?)
                          NULL,
                          CS_SLES_GMRES,
                          poly_degree,
                          itsol.n_max_iter);
        break;
      case CS_PARAM_ITSOL_AMG:
        {
          cs_multigrid_t  *mg = cs_multigrid_define(eq->field_id,
                                                    NULL);

          /* Advanced setup (default is specified inside the brackets) */
          cs_multigrid_set_solver_options
            (mg,
             CS_SLES_JACOBI,   // descent smoother type (CS_SLES_PCG)
             CS_SLES_JACOBI,   // ascent smoother type (CS_SLES_PCG)
             CS_SLES_PCG,      // coarse solver type (CS_SLES_PCG)
             itsol.n_max_iter, // n max cycles (100)
             5,                // n max iter for descent (10)
             5,                // n max iter for asscent (10)
             1000,             // n max iter coarse solver (10000)
             0,                // polynomial precond. degree descent (0)
             0,                // polynomial precond. degree ascent (0)
             0,                // polynomial precond. degree coarse (0)
             1.0,    // precision multiplier descent (< 0 forces max iters)
             1.0,    // precision multiplier ascent (< 0 forces max iters)
             1);     // requested precision multiplier coarse (default 1)

        }
      default:
        bft_error(__FILE__, __LINE__, 0,
                  _(" Undefined iterative solver for solving %s equation.\n"
                    " Please modify your settings."), eq->name);
        break;
      } // end of switch

      /* Define the level of verbosity for SLES structure */
      int  sles_verbosity = eq->param->sles_verbosity;
      if (sles_verbosity > 1) {

        cs_sles_t  *sles = cs_sles_find_or_add(eq->field_id, NULL);
        cs_sles_it_t  *sles_it = (cs_sles_it_t *)cs_sles_get_context(sles);

        /* Set verbosity */
        cs_sles_set_verbosity(sles, sles_verbosity);

        if (sles_verbosity > 2) /* Add plot */
          cs_sles_it_set_plot_options(sles_it,
                                      eq->name,
                                      true);    /* use_iteration instead of
                                                   wall clock time */

      }

    } // Solver provided by Code_Saturne
    break;

  case CS_EQUATION_ALGO_PETSC_ITSOL:
    {
#if defined(HAVE_PETSC)

      /* Initialization must be called before setting options;
         it does not need to be called before calling
         cs_sles_petsc_define(), as this is handled automatically. */

      PetscBool is_initialized;
      PetscInitialized(&is_initialized);
      if (is_initialized == PETSC_FALSE) {
#if defined(HAVE_MPI)
        PETSC_COMM_WORLD = cs_glob_mpi_comm;
#endif
        PetscInitializeNoArguments();
      }

      switch (eqp->itsol_info.solver) {

      case CS_PARAM_ITSOL_CG:
        switch (eqp->itsol_info.precond) {

        case CS_PARAM_PRECOND_DIAG:
          cs_sles_petsc_define(eq->field_id,
                               NULL,
                               MATMPIAIJ,
                               _cg_diag_setup_hook,
                               NULL);
          break;
        case CS_PARAM_PRECOND_SSOR:
          cs_sles_petsc_define(eq->field_id,
                               NULL,
                               MATSEQAIJ, // Warning SEQ not MPI
                               _cg_ssor_setup_hook,
                               NULL);
          break;
        case CS_PARAM_PRECOND_ICC0:
          cs_sles_petsc_define(eq->field_id,
                               NULL,
                               MATSEQAIJ, // Warning SEQ not MPI
                               _cg_icc_setup_hook,
                               NULL);
          break;

        case CS_PARAM_PRECOND_AMG:
          {
            int  amg_type = 1;

            if (amg_type == 0) { // GAMG

              PetscOptionsSetValue("-pc_gamg_agg_nsmooths", "1");
              PetscOptionsSetValue("-mg_levels_ksp_type", "richardson");
              PetscOptionsSetValue("-mg_levels_pc_type", "sor");
              PetscOptionsSetValue("-mg_levels_ksp_max_it", "1");
              PetscOptionsSetValue("-pc_gamg_threshold", "0.02");
              PetscOptionsSetValue("-pc_gamg_reuse_interpolation", "TRUE");
              PetscOptionsSetValue("-pc_gamg_square_graph", "4");

              cs_sles_petsc_define(eq->field_id,
                                   NULL,
                                   MATMPIAIJ,
                                   _cg_gamg_setup_hook,
                                   NULL);

            }
            else if (amg_type == 1) { // Boomer AMG (hypre)

              PetscOptionsSetValue("-pc_type", "hypre");
              PetscOptionsSetValue("-pc_hypre_type","boomeramg");
              PetscOptionsSetValue("-pc_hypre_boomeramg_coarsen_type","HMIS");
              PetscOptionsSetValue("-pc_hypre_boomeramg_interp_type","ext+i-cc");
              PetscOptionsSetValue("-pc_hypre_boomeramg_agg_nl","2");
              PetscOptionsSetValue("-pc_hypre_boomeramg_P_max","4");
              PetscOptionsSetValue("-pc_hypre_boomeramg_strong_threshold","0.5");
              PetscOptionsSetValue("-pc_hypre_boomeramg_no_CF","");

              cs_sles_petsc_define(eq->field_id,
                                   NULL,
                                   MATMPIAIJ,
                                   _cg_bamg_setup_hook,
                                   NULL);

            }

          }
          break;
        case CS_PARAM_PRECOND_AS:
          cs_sles_petsc_define(eq->field_id,
                               NULL,
                               MATMPIAIJ,
                               _cg_as_setup_hook,
                               NULL);
          break;
        default:
          bft_error(__FILE__, __LINE__, 0,
                    " Couple (solver, preconditioner) not handled with PETSc.");
          break;

        } // switch on PETSc preconditionner
        break;

      case CS_PARAM_ITSOL_GMRES:

        switch (eqp->itsol_info.precond) {
        case CS_PARAM_PRECOND_ILU0:
          cs_sles_petsc_define(eq->field_id,
                               NULL,
                               MATSEQAIJ, // Warning SEQ not MPI
                               _gmres_ilu_setup_hook,
                               NULL);
          break;
        case CS_PARAM_PRECOND_DIAG:
          cs_sles_petsc_define(eq->field_id,
                               NULL,
                               MATMPIAIJ,
                               _gmres_bjacobi_setup_hook,
                               NULL);
          break;

        default:
          bft_error(__FILE__, __LINE__, 0,
                    " Couple (solver, preconditioner) not handled with PETSc.");
          break;

        } // switch on PETSc preconditionner
        break;

      case CS_PARAM_ITSOL_BICG:

        switch (eqp->itsol_info.precond) {
        case CS_PARAM_PRECOND_ILU0:
          cs_sles_petsc_define(eq->field_id,
                               NULL,
                               MATSEQAIJ, // Warning SEQ not MPI
                               _bicg_ilu_setup_hook,
                               NULL);
          break;
        case CS_PARAM_PRECOND_DIAG:
          cs_sles_petsc_define(eq->field_id,
                               NULL,
                               MATMPIAIJ,
                               _bicg_bjacobi_setup_hook,
                               NULL);
          break;

        default:
          bft_error(__FILE__, __LINE__, 0,
                    " Couple (solver, preconditioner) not handled with PETSc.");
          break;

        } // switch on PETSc preconditionner
        break;

      default:
        bft_error(__FILE__, __LINE__, 0, " Solver not handled.");
        break;

      } // switch on PETSc solver
#else
      bft_error(__FILE__, __LINE__, 0,
                _(" PETSC algorithms used to solve %s are not linked.\n"
                  " Please install Code_Saturne with PETSc."), eq->name);

#endif // HAVE_PETSC
    } // Solver provided by PETSc
    break;

  default:
    bft_error(__FILE__, __LINE__, 0,
              _(" Algorithm requested to solve %s is not implemented yet.\n"
                " Please modify your settings."), eq->name);
    break;

  } // end switch on algorithms

}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Given its name, get the id related to a cs_mesh_location_t structure
 *
 * \param[in]      ml_name    name of the location
 * \param[in, out] p_ml_id    pointer on the id of the related mesh location
 */
/*----------------------------------------------------------------------------*/

static void
_check_ml_name(const char   *ml_name,
               int          *p_ml_id)
{
  *p_ml_id = cs_mesh_location_get_id_by_name(ml_name);

  if (*p_ml_id == -1)
    bft_error(__FILE__, __LINE__, 0,
              _(" Invalid mesh location name %s.\n"
                " This mesh location is not already defined.\n"), ml_name);
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Print the name of the corresponding equation key
 *
 * \param[in] key        name of the key
 *
 * \return a string
 */
/*----------------------------------------------------------------------------*/

static const char *
_print_eqkey(eqkey_t  key)
{
  switch (key) {
  case EQKEY_HODGE_DIFF_ALGO:
    return "hodge_diff_algo";
  case EQKEY_HODGE_DIFF_COEF:
    return "hodge_diff_coef";
  case EQKEY_HODGE_TIME_ALGO:
    return "hodge_time_algo";
  case EQKEY_HODGE_TIME_COEF:
    return "hodge_time_coef";
  case EQKEY_ITSOL:
    return "itsol";
  case EQKEY_ITSOL_EPS:
    return "itsol_eps";
  case EQKEY_ITSOL_MAX_ITER:
    return "itsol_max_iter";
  case EQKEY_ITSOL_RESNORM:
    return "itsol_resnorm";
  case EQKEY_PRECOND:
    return "precond";
  case EQKEY_SOLVER_FAMILY:
    return "solver_family";
  case EQKEY_SPACE_SCHEME:
    return "space_scheme";
  case EQKEY_VERBOSITY:
    return "verbosity";
  case EQKEY_SLES_VERBOSITY:
    return "itsol_verbosity";
  case EQKEY_BC_ENFORCEMENT:
    return "bc_enforcement";
  case EQKEY_BC_QUADRATURE:
    return "bc_quadrature";
  case EQKEY_EXTRA_OP:
    return "extra_op";
  case EQKEY_ADV_OP_TYPE:
    return "adv_formulation";
  case EQKEY_ADV_WEIGHT_ALGO:
    return "adv_weight";
  case EQKEY_ADV_WEIGHT_CRIT:
    return "adv_weight_criterion";
  case EQKEY_ADV_FLUX_QUADRA:
    return "adv_flux_quad";
  case EQKEY_TIME_SCHEME:
    return "time_scheme";
  case EQKEY_TIME_THETA:
    return "time_theta";

  default:
    assert(0);
  }

  return NULL; // avoid a warning
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Print the name of the corresponding reaction term key
 *
 * \param[in] key        name of the key
 *
 * \return a string
 */
/*----------------------------------------------------------------------------*/

static const char *
_print_reakey(reakey_t  key)
{
  switch (key) {
  case REAKEY_LUMPING:
    return "lumping";
  case REAKEY_HODGE_ALGO:
    return "hodge_algo";
  case REAKEY_HODGE_COEF:
    return "hodge_coef";
  case REAKEY_INV_PTY:
    return "inv_pty";
  default:
    assert(0);
  }

  return NULL; // avoid a warning
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Get the corresponding enum from the name of an equation key.
 *         If not found, print an error message
 *
 * \param[in] keyname    name of the key
 *
 * \return a eqkey_t
 */
/*----------------------------------------------------------------------------*/

static eqkey_t
_get_eqkey(const char *keyname)
{
  eqkey_t  key = EQKEY_ERROR;

  if (strncmp(keyname, "hodge", 5) == 0) { /* key begins with hodge */
    if (strcmp(keyname, "hodge_diff_coef") == 0)
      key = EQKEY_HODGE_DIFF_COEF;
    else if (strcmp(keyname, "hodge_diff_algo") == 0)
      key = EQKEY_HODGE_DIFF_ALGO;
    else if (strcmp(keyname, "hodge_time_coef") == 0)
      key = EQKEY_HODGE_TIME_COEF;
    else if (strcmp(keyname, "hodge_time_algo") == 0)
      key = EQKEY_HODGE_TIME_ALGO;
  }

  else if (strncmp(keyname, "itsol", 5) == 0) { /* key begins with itsol */
    if (strcmp(keyname, "itsol") == 0)
      key = EQKEY_ITSOL;
    else if (strcmp(keyname, "itsol_eps") == 0)
      key = EQKEY_ITSOL_EPS;
    else if (strcmp(keyname, "itsol_max_iter") == 0)
      key = EQKEY_ITSOL_MAX_ITER;
    else if (strcmp(keyname, "itsol_resnorm") == 0)
      key = EQKEY_ITSOL_RESNORM;
    else if (strcmp(keyname, "itsol_verbosity") == 0)
      key = EQKEY_SLES_VERBOSITY;
  }

  else if (strcmp(keyname, "precond") == 0)
    key = EQKEY_PRECOND;

  else if (strcmp(keyname, "solver_family") == 0)
    key = EQKEY_SOLVER_FAMILY;

  else if (strcmp(keyname, "space_scheme") == 0)
    key = EQKEY_SPACE_SCHEME;

  else if (strcmp(keyname, "verbosity") == 0)
    key = EQKEY_VERBOSITY;

  else if (strncmp(keyname, "bc", 2) == 0) { /* key begins with bc */
    if (strcmp(keyname, "bc_enforcement") == 0)
      key = EQKEY_BC_ENFORCEMENT;
    else if (strcmp(keyname, "bc_quadrature") == 0)
      key = EQKEY_BC_QUADRATURE;
  }

  else if (strcmp(keyname, "extra_op") == 0)
    key = EQKEY_EXTRA_OP;

  else if (strncmp(keyname, "adv_", 4) == 0) {
    if (strcmp(keyname, "adv_formulation") == 0)
      key = EQKEY_ADV_OP_TYPE;
    else if (strcmp(keyname, "adv_weight_criterion") == 0)
      key = EQKEY_ADV_WEIGHT_CRIT;
    else if (strcmp(keyname, "adv_weight") == 0)
      key = EQKEY_ADV_WEIGHT_ALGO;
    else if (strcmp(keyname, "adv_flux_quad") == 0)
      key = EQKEY_ADV_FLUX_QUADRA;
  }

  else if (strncmp(keyname, "time_", 5) == 0) {
    if (strcmp(keyname, "time_scheme") == 0)
      key = EQKEY_TIME_SCHEME;
    else if (strcmp(keyname, "time_theta") == 0)
      key = EQKEY_TIME_THETA;
  }

  return key;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Get the corresponding enum from the name of a reaction term key.
 *         If not found, print an error message
 *
 * \param[in] keyname    name of the key
 *
 * \return a reakey_t
 */
/*----------------------------------------------------------------------------*/

static reakey_t
_get_reakey(const char *keyname)
{
  reakey_t  key = REAKEY_ERROR;

  if (strcmp(keyname, "lumping") == 0)
    key = REAKEY_LUMPING;
  else if (strcmp(keyname, "hodge_algo") == 0)
    key = REAKEY_HODGE_ALGO;
  else if (strcmp(keyname, "hodge_coef") == 0)
    key = REAKEY_HODGE_COEF;
  else if (strcmp(keyname, "inv_pty") == 0)
    key = REAKEY_INV_PTY;

  return key;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Create a cs_equation_param_t
 *
 * \param[in] type             type of equation
 * \param[in] var_type         type of variable (scalar, vector, tensor...)
 * \param[in] default_bc       type of boundary condition set by default
 *
 * \return a pointer to a new allocated cs_equation_param_t structure
 */
/*----------------------------------------------------------------------------*/

static cs_equation_param_t *
_create_equation_param(cs_equation_type_t     type,
                       cs_param_var_type_t    var_type,
                       cs_param_bc_type_t     default_bc)
{
  cs_equation_param_t  *eqp = NULL;

  BFT_MALLOC(eqp, 1, cs_equation_param_t);

  eqp->type = type;
  eqp->var_type = var_type;
  eqp->verbosity =  0;
  eqp->sles_verbosity =  0;
  eqp->process_flag =  0;

  /* Build the equation flag */
  eqp->flag = 0;
  eqp->space_scheme = CS_SPACE_SCHEME_CDOVB;

  /* Vertex-based schemes imply the two following discrete Hodge operators
     Default initialization is made in accordance with this choice */
  eqp->time_hodge.inv_pty = false; // inverse property ?
  eqp->time_hodge.type = CS_PARAM_HODGE_TYPE_VPCD;
  eqp->time_hodge.algo = CS_PARAM_HODGE_ALGO_VORONOI;
  eqp->time_property = NULL;

  /* Description of the time discretization (default values) */
  eqp->time_info.scheme = CS_TIME_SCHEME_IMPLICIT;
  eqp->time_info.theta = 1.0;
  eqp->time_info.do_lumping = false;

  /* Initial condition (zero value by default) */
  eqp->time_info.n_ic_definitions = 0;
  eqp->time_info.ic_definitions = NULL;

  /* Diffusion term */
  eqp->diffusion_property = NULL;
  eqp->diffusion_hodge.inv_pty = false; // inverse property ?
  eqp->diffusion_hodge.type = CS_PARAM_HODGE_TYPE_EPFD;
  eqp->diffusion_hodge.algo = CS_PARAM_HODGE_ALGO_COST;
  eqp->diffusion_hodge.coef = 1./3.;

  /* Advection term */
  eqp->advection_info.formulation = CS_PARAM_ADVECTION_FORM_CONSERV;
  eqp->advection_info.weight_algo = CS_PARAM_ADVECTION_WEIGHT_ALGO_UPWIND;
  eqp->advection_info.weight_criterion = CS_PARAM_ADVECTION_WEIGHT_XEXC;
  eqp->advection_info.quad_type = CS_QUADRATURE_BARY;
  eqp->advection_field = NULL;

  /* No reaction term by default */
  eqp->n_reaction_terms = 0;
  eqp->reaction_terms = NULL;
  eqp->reaction_properties = NULL;

  /* No source term by default (always in the right-hand side) */
  eqp->n_source_terms = 0;
  eqp->source_terms = NULL;

  /* Boundary conditions structure.
     One assigns a boundary condition by default */
  eqp->bc = cs_param_bc_create(default_bc);

  /* Settings for driving the linear algebra */
  eqp->algo_info = _algo_info_by_default;
  eqp->itsol_info = _itsol_info_by_default;

  return eqp;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Set the initial values for the variable related to an equation
 *
 * \param[in, out]  eq         pointer to a cs_equation_t structure
 * \param[in]       connect    pointer to a cs_cdo_connect_t struct.
 * \param[in]       cdoq       pointer to a cs_cdo_quantities_t struct.
 */
/*----------------------------------------------------------------------------*/

static void
_initialize_field_from_ic(cs_equation_t              *eq,
                          const cs_cdo_connect_t     *connect,
                          const cs_cdo_quantities_t  *cdoq)
{
  cs_flag_t  dof_flag = 0;
  cs_equation_param_t  *eqp = eq->param;
  
  switch (eqp->var_type) {
  case CS_PARAM_VAR_SCAL:
    dof_flag |= CS_FLAG_SCAL;
    break;
  case CS_PARAM_VAR_VECT:
    dof_flag |= CS_FLAG_VECT;
    break;
  case CS_PARAM_VAR_TENS:
    dof_flag |= CS_FLAG_TENS;
    break;
  default:
    bft_error(__FILE__, __LINE__, 0,
              _(" Incompatible type of variable for equation %s."), eq->name);
    break;
  }

  /* Retrieve the associated field */
  cs_get_t  get;
  cs_field_t  *field = cs_field_by_id(eq->field_id);
  cs_param_time_t  t_info = eqp->time_info;

  if (eqp->space_scheme == CS_SPACE_SCHEME_CDOVB) {
    dof_flag |= cs_cdo_primal_vtx;

    for (int def_id = 0; def_id < t_info.n_ic_definitions; def_id++) {

      /* Get and then set the definition of the initial condition */
      const cs_param_def_t  *ic = t_info.ic_definitions + def_id;

      int  ml_id;

      if (strlen(ic->ml_name) > 0)
        ml_id = cs_mesh_location_get_id_by_name(ic->ml_name);
      else
        ml_id = cs_mesh_location_get_id_by_name(N_("vertices"));

      if (ic->def_type == CS_PARAM_DEF_BY_VALUE)
        cs_evaluate_potential_from_value(dof_flag, ml_id, ic->def.get,
                                         field->val);
      
      else if (ic->def_type == CS_PARAM_DEF_BY_ANALYTIC_FUNCTION)
        cs_evaluate_potential_from_analytic(dof_flag, ml_id, ic->def.analytic,
                                            field->val);

    } // Loop on definitions

  }
  else { // Face-based schemes

    cs_real_t  *face_values = cs_equation_get_face_values(eq);
    assert(face_values != NULL);

    for (int def_id = 0; def_id < t_info.n_ic_definitions; def_id++) {

      /* Get and then set the definition of the initial condition */
      const cs_param_def_t  *ic = t_info.ic_definitions + def_id;

      int  ml_id;

      /* Initialize cell-based array */
      cs_flag_t  cell_flag = dof_flag | cs_cdo_primal_cell;

      if (strlen(ic->ml_name) > 0)
        ml_id = cs_mesh_location_get_id_by_name(ic->ml_name);
      else
        ml_id = cs_mesh_location_get_id_by_name(N_("cells"));
      
      if (ic->def_type == CS_PARAM_DEF_BY_VALUE)
        cs_evaluate_potential_from_value(cell_flag, ml_id, ic->def.get,
                                         field->val);

      else if (ic->def_type == CS_PARAM_DEF_BY_ANALYTIC_FUNCTION)
        cs_evaluate_potential_from_analytic(cell_flag, ml_id, ic->def.analytic,
                                            field->val);

      cs_flag_t  face_flag = dof_flag | cs_cdo_primal_face;

      if (ic->def_type == CS_PARAM_DEF_BY_VALUE)
        cs_evaluate_potential_from_value(face_flag, ml_id, ic->def.get,
                                         face_values);

      else if (ic->def_type == CS_PARAM_DEF_BY_ANALYTIC_FUNCTION)
        cs_evaluate_potential_from_analytic(face_flag, ml_id, ic->def.analytic,
                                            face_values);

    } // Loop on definitions

  } // Test on discretization scheme

}

/*============================================================================
 * Public function prototypes
 *============================================================================*/

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Define and initialize a new structure to store parameters related
 *         to an equation
 *
 * \param[in] eqname           name of the equation
 * \param[in] varname          name of the variable associated to this equation
 * \param[in] eqtype           type of equation (user, predefined...)
 * \param[in] vartype          type of variable (scalar, vector, tensor...)
 * \param[in] default_bc       type of boundary condition set by default
 *
 * \return  a pointer to the new allocated cs_equation_t structure
 */
/*----------------------------------------------------------------------------*/

cs_equation_t *
cs_equation_create(const char            *eqname,
                   const char            *varname,
                   cs_equation_type_t     eqtype,
                   cs_param_var_type_t    vartype,
                   cs_param_bc_type_t     default_bc)
{
  int  len = strlen(eqname)+1;

  cs_equation_t  *eq = NULL;

  BFT_MALLOC(eq, 1, cs_equation_t);

  /* Sanity checks */
  if (varname == NULL)
    bft_error(__FILE__, __LINE__, 0,
              _(" No variable name associated to an equation structure.\n"
                " Check your initialization."));

  if (eqname == NULL)
    bft_error(__FILE__, __LINE__, 0,
              _(" No equation name associated to an equation structure.\n"
                " Check your initialization."));

  /* Store eqname */
  BFT_MALLOC(eq->name, len, char);
  strncpy(eq->name, eqname, len);

  /* Store varname */
  len = strlen(varname)+1;
  BFT_MALLOC(eq->varname, len, char);
  strncpy(eq->varname, varname, len);

  eq->param = _create_equation_param(eqtype, vartype, default_bc);

  eq->field_id = -1;    // field is created in a second step
  eq->do_build = true;  // Force the construction of the algebraic system

  /* Set timer statistic structure to a default value */
  eq->main_ts_id = eq->pre_ts_id = eq->solve_ts_id = eq->extra_op_ts_id = -1;

  /* Algebraic system: allocated later */
  eq->ms = NULL;
  eq->matrix = NULL;
  eq->rhs = NULL;

  /* Builder structure for this equation */
  eq->builder = NULL;

  /* Pointer of function */
  eq->init_builder = NULL;
  eq->compute_source = NULL;
  eq->build_system = NULL;
  eq->update_field = NULL;
  eq->postprocess = NULL;
  eq->get_f_values = NULL;
  eq->get_tmpbuf = NULL;
  eq->free_builder = NULL;

  return  eq;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Destroy a cs_equation_t structure
 *
 * \param[in, out] eq    pointer to a cs_equation_t structure
 *
 * \return  a NULL pointer
 */
/*----------------------------------------------------------------------------*/

cs_equation_t *
cs_equation_free(cs_equation_t  *eq)
{
  if (eq == NULL)
    return eq;

  if (eq->main_ts_id > -1)
    cs_timer_stats_stop(eq->main_ts_id);

  BFT_FREE(eq->name);
  BFT_FREE(eq->varname);

  cs_equation_param_t  *eqp = eq->param;

  if (eqp->bc != NULL) { // Boundary conditions
    if (eqp->bc->n_defs > 0)
      BFT_FREE(eqp->bc->defs);
    BFT_FREE(eqp->bc);
    eqp->bc = NULL;
  }

  if (eqp->n_reaction_terms > 0) { // reaction terms

    for (int i = 0; i< eqp->n_reaction_terms; i++)
      BFT_FREE(eqp->reaction_terms[i].name);
    BFT_FREE(eqp->reaction_terms);

    /* Free only the array of pointers and not the pointers themselves
       since they are stored in a cs_domain_t structure */
    BFT_FREE(eqp->reaction_properties);

  }

  if (eqp->n_source_terms > 0) { // Source terms

    for (int i = 0; i< eqp->n_source_terms; i++)
      eqp->source_terms[i] = cs_source_term_free(eqp->source_terms[i]);
    BFT_FREE(eqp->source_terms);

  }

  BFT_FREE(eq->param);

  cs_matrix_structure_destroy(&(eq->ms));
  cs_matrix_destroy(&(eq->matrix));
  BFT_FREE(eq->rhs);

  eq->builder = eq->free_builder(eq->builder);

  if (eq->main_ts_id > -1)
    cs_timer_stats_stop(eq->main_ts_id);

  BFT_FREE(eq);

  return NULL;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Summary of a cs_equation_t structure
 *
 * \param[in]  eq       pointer to a cs_equation_t structure
 */
/*----------------------------------------------------------------------------*/

void
cs_equation_summary(const cs_equation_t  *eq)
{
  if (eq == NULL)
    return;

  const cs_equation_param_t  *eqp = eq->param;

  bft_printf("\n%s", lsepline);
  bft_printf("\tSummary of settings for %s eq. (variable %s)\n",
             eq->name, eq->varname);
  bft_printf("%s", lsepline);

  switch (eqp->type) {
  case CS_EQUATION_TYPE_USER:
    bft_printf("\t<%s/type> User-defined\n", eq->name);
    break;
  case CS_EQUATION_TYPE_PREDEFINED:
    bft_printf("\t<%s/type> Predefined\n", eq->name);
    break;
  case CS_EQUATION_TYPE_GROUNDWATER:
    bft_printf("\t<%s/type> Associated to groundwater flows\n", eq->name);
    break;
  default:
    bft_error(__FILE__, __LINE__, 0,
              " Eq. %s has no type.\n"
              " Please check your settings.", eq->name);
  }

  if (eqp->space_scheme == CS_SPACE_SCHEME_CDOVB)
    bft_printf("\t<%s/space scheme>  CDO vertex-based\n", eq->name);
  else if (eqp->space_scheme == CS_SPACE_SCHEME_CDOFB)
    bft_printf("\t<%s/space scheme>  CDO face-based\n", eq->name);

  bool  unsteady = (eqp->flag & CS_EQUATION_UNSTEADY) ? true : false;
  bool  convection = (eqp->flag & CS_EQUATION_CONVECTION) ? true : false;
  bool  diffusion = (eqp->flag & CS_EQUATION_DIFFUSION) ? true : false;
  bool  reaction = (eqp->flag & CS_EQUATION_REACTION) ? true : false;
  bool  source_term = (eqp->n_source_terms > 0) ? true : false;

  bft_printf("\t<%s/Terms>  unsteady:%s, convection:%s, diffusion:%s,"
             " reaction:%s, source term:%s\n",
             eq->name, cs_base_strtf(unsteady), cs_base_strtf(convection),
             cs_base_strtf(diffusion), cs_base_strtf(reaction),
             cs_base_strtf(source_term));

  /* Boundary conditions */
  if (eqp->verbosity > 0) {
    cs_param_bc_t  *bcp = eqp->bc;

    bft_printf("\t<%s/Boundary Conditions>\n", eq->name);
    bft_printf("\t\t<BC/Default> %s\n", cs_param_get_bc_name(bcp->default_bc));
    if (eqp->verbosity > 1)
      bft_printf("\t\t<BC/Enforcement> %s\n",
                 cs_param_get_bc_enforcement_name(bcp->enforcement));
    bft_printf("\t\t<BC/N_Definitions> %d\n", bcp->n_defs);
    if (eqp->verbosity > 1) {
      for (int id = 0; id < bcp->n_defs; id++)
        bft_printf("\t\t\t<BC> Location: %s; Type: %s; Definition type: %s\n",
                   cs_mesh_location_get_name(bcp->defs[id].loc_id),
                   cs_param_get_bc_name(bcp->defs[id].bc_type),
                   cs_param_get_def_type_name(bcp->defs[id].def_type));
    }
  }

  if (unsteady) {

    const cs_param_time_t  t_info = eqp->time_info;
    const cs_param_hodge_t  h_info = eqp->time_hodge;

    bft_printf("\n\t<%s/Unsteady term>\n", eq->name);
    bft_printf("\t<Time/Initial condition> number of definitions %d\n",
               t_info.n_ic_definitions);
    for (int i = 0; i < t_info.n_ic_definitions; i++) {
      const cs_param_def_t  *ic = t_info.ic_definitions + i;
      bft_printf("\t\t<Time/Initial condition> Location %s;"
                 " Definition type: %s\n",
                 ic->ml_name, cs_param_get_def_type_name(ic->def_type));
    }
    bft_printf("\t<Time/Scheme> ");
    switch (t_info.scheme) {
    case CS_TIME_SCHEME_IMPLICIT:
      bft_printf("implicit\n");
      break;
    case CS_TIME_SCHEME_EXPLICIT:
      bft_printf("explicit\n");
      break;
    case CS_TIME_SCHEME_CRANKNICO:
      bft_printf("Crank-Nicolson\n");
      break;
    case CS_TIME_SCHEME_THETA:
      bft_printf("theta scheme with value %f\n", t_info.theta);
      break;
    default:
      bft_error(__FILE__, __LINE__, 0, " Invalid time scheme.");
      break;
    }
    bft_printf("\t<Time/Mass lumping> %s\n", cs_base_strtf(t_info.do_lumping));
    bft_printf("\t<Time/Property> %s\n",
               cs_property_get_name(eqp->time_property));

    if (eqp->verbosity > 0) {
      bft_printf("\t<Time/Hodge> %s - %s\n",
                 cs_param_hodge_get_type_name(h_info),
                 cs_param_hodge_get_algo_name(h_info));
      bft_printf("\t\t<Time/Hodge> Inversion of property: %s\n",
                 cs_base_strtf(h_info.inv_pty));
      if (h_info.algo == CS_PARAM_HODGE_ALGO_COST)
        bft_printf("\t\t<Time/Hodge> Value of the coercivity coef.: %.3e\n",
                   h_info.coef);
    }

  } /* Unsteady term */

  if (diffusion) {

    const cs_param_hodge_t  h_info = eqp->diffusion_hodge;

    bft_printf("\n\t<%s/Diffusion term>\n", eq->name);
    bft_printf("\t<Diffusion> Property: %s\n",
               cs_property_get_name(eqp->diffusion_property));

    if (eqp->verbosity > 0) {
      bft_printf("\t<Diffusion/Hodge> %s - %s\n",
                 cs_param_hodge_get_type_name(h_info),
                 cs_param_hodge_get_algo_name(h_info));
      bft_printf("\t\t<Diffusion/Hodge> Inversion of property: %s\n",
                 cs_base_strtf(h_info.inv_pty));
      if (h_info.algo == CS_PARAM_HODGE_ALGO_COST)
        bft_printf("\t\t<Diffusion/Hodge> Value of the coercivity coef.: %.3e\n",
                   h_info.coef);
    }

  } /* Diffusion term */

  if (convection) {

    const cs_param_advection_t  a_info = eqp->advection_info;

    bft_printf("\n\t<%s/Advection term>\n", eq->name);
    bft_printf("\t<Advection field>  %s\n",
               cs_advection_field_get_name(eqp->advection_field));

    if (eqp->verbosity > 0) {
      bft_printf("\t<Advection/Formulation>");
      switch(a_info.formulation) {
      case CS_PARAM_ADVECTION_FORM_CONSERV:
        bft_printf(" Conservative\n");
        break;
      case CS_PARAM_ADVECTION_FORM_NONCONS:
        bft_printf(" Non-conservative\n");
        break;
      default:
        bft_error(__FILE__, __LINE__, 0,
                  " Invalid operator type for advection.");
      }

      bft_printf("\t<Advection/Operator> Weight_scheme");
      switch(a_info.weight_algo) {
      case CS_PARAM_ADVECTION_WEIGHT_ALGO_CENTERED:
        bft_printf(" centered\n");
        break;
      case CS_PARAM_ADVECTION_WEIGHT_ALGO_UPWIND:
        bft_printf(" upwind\n");
        break;
      case CS_PARAM_ADVECTION_WEIGHT_ALGO_SAMARSKII:
        bft_printf(" Samarskii\n");
        break;
      case CS_PARAM_ADVECTION_WEIGHT_ALGO_SG:
        bft_printf(" Scharfetter-Gummel\n");
        break;
      case CS_PARAM_ADVECTION_WEIGHT_ALGO_D10G5:
        bft_printf(" Specific with delta=10 and gamma=5\n");
        break;
      default:
        bft_error(__FILE__, __LINE__, 0,
                  " Invalid weight algorithm for advection.");
      }

    } // verbosity > 0

  } // Advection term

  if (reaction) {

    for (int r_id = 0; r_id < eqp->n_reaction_terms; r_id++) {

      const cs_param_reaction_t  r_info = eqp->reaction_terms[r_id];
      const cs_param_hodge_t  h_info = r_info.hodge;

      bft_printf("\n\t<%s/Reaction term> %s\n",
                 eq->name, cs_param_reaction_get_name(r_info));
      bft_printf("\t<Reaction> Property: %s\n",
                 cs_property_get_name(eqp->reaction_properties[r_id]));
      bft_printf("\t\t<Reaction/Operator> Type %s; Mass_lumping %s\n",
                 cs_param_reaction_get_type_name(r_info),
                 cs_base_strtf(r_info.do_lumping));

      if (eqp->verbosity > 0) {
        bft_printf("\t<Reaction/Hodge> %s - %s\n",
                   cs_param_hodge_get_type_name(h_info),
                   cs_param_hodge_get_algo_name(h_info));
        bft_printf("\t\t<Reaction/Hodge> Inversion of property: %s\n",
                   cs_base_strtf(h_info.inv_pty));
        if (h_info.algo == CS_PARAM_HODGE_ALGO_COST)
          bft_printf("\t\t<Reaction/Hodge> Value of the coercivity coef.: %.3e\n",
                     h_info.coef);
      }

    } // Loop on reaction terms

  } // Reaction terms

  if (source_term) {

    bft_printf("\n\t<%s/Source terms>\n", eq->name);
    for (int s_id = 0; s_id < eqp->n_source_terms; s_id++)
      cs_source_term_summary(eq->name, eqp->source_terms[s_id]);

  } // Source terms

  /* Iterative solver information */
  const cs_param_itsol_t   itsol = eqp->itsol_info;

  bft_printf("\n\t<%s/Sparse Linear Algebra>", eq->name);
  if (eqp->algo_info.type == CS_EQUATION_ALGO_CS_ITSOL)
    bft_printf(" Code_Saturne iterative solvers\n");
  else if (eqp->algo_info.type == CS_EQUATION_ALGO_PETSC_ITSOL)
    bft_printf(" PETSc iterative solvers\n");
  bft_printf("\t\t<sla> Solver.MaxIter     %d\n", itsol.n_max_iter);
  bft_printf("\t\t<sla> Solver.Name        %s\n",
             cs_param_get_solver_name(itsol.solver));
  bft_printf("\t\t<sla> Solver.Precond     %s\n",
             cs_param_get_precond_name(itsol.precond));
  bft_printf("\t\t<sla> Solver.Eps        % -10.6e\n", itsol.eps);
  bft_printf("\t\t<sla> Solver.Normalized  %s\n",
             cs_base_strtf(itsol.resid_normalized));

}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Assign a set of pointer functions for managing the cs_equation_t
 *         structure during the computation
 *
 * \param[in, out]  eq       pointer to a cs_equation_t structure
 */
/*----------------------------------------------------------------------------*/

void
cs_equation_last_setup(cs_equation_t  *eq)
{
  if (eq == NULL)
    return;

  cs_equation_param_t  *eqp = eq->param;

  /* Set timer statistics */
  if (eqp->verbosity > 0) {

    eq->main_ts_id = cs_timer_stats_create("stages", // parent name
                                           eq->name,
                                           eq->name);

    cs_timer_stats_start(eq->main_ts_id);
    cs_timer_stats_set_plot(eq->main_ts_id, 1);

    if (eqp->verbosity > 1) {

      char *label = NULL;

      int len = strlen("_extra_op") + strlen(eq->name) + 1;
      BFT_MALLOC(label, len, char);
      sprintf(label, "%s_pre", eq->name);
      eq->pre_ts_id = cs_timer_stats_create(eq->name, label, label);
      cs_timer_stats_set_plot(eq->pre_ts_id, 1);

      sprintf(label, "%s_solve", eq->name);
      eq->solve_ts_id = cs_timer_stats_create(eq->name, label, label);
      cs_timer_stats_set_plot(eq->solve_ts_id, 1);

      sprintf(label, "%s_extra_op", eq->name);
      eq->extra_op_ts_id = cs_timer_stats_create(eq->name, label, label);
      cs_timer_stats_set_plot(eq->extra_op_ts_id, 1);

      BFT_FREE(label);

    } // verbosity > 1

  } // verbosity > 0

  /* Set function pointers */
  switch(eqp->space_scheme) {

  case CS_SPACE_SCHEME_CDOVB:
    eq->init_builder = cs_cdovb_scaleq_init;
    eq->free_builder = cs_cdovb_scaleq_free;
    eq->build_system = cs_cdovb_scaleq_build_system;
    eq->compute_source = cs_cdovb_scaleq_compute_source;
    eq->update_field = cs_cdovb_scaleq_update_field;
    eq->postprocess = cs_cdovb_scaleq_extra_op;
    eq->get_tmpbuf = cs_cdovb_scaleq_get_tmpbuf;
    eq->get_f_values = NULL;
    break;

  case CS_SPACE_SCHEME_CDOFB:
    eq->init_builder = cs_cdofb_scaleq_init;
    eq->free_builder = cs_cdofb_scaleq_free;
    eq->build_system = cs_cdofb_scaleq_build_system;
    eq->compute_source = cs_cdofb_scaleq_compute_source;
    eq->update_field = cs_cdofb_scaleq_update_field;
    eq->postprocess = cs_cdofb_scaleq_extra_op;
    eq->get_tmpbuf = cs_cdofb_scaleq_get_tmpbuf;
    eq->get_f_values = cs_cdofb_scaleq_get_face_values;
    break;

  default:
    bft_error(__FILE__, __LINE__, 0,
              _(" Invalid scheme for the space discretization.\n"
                " Please check your settings."));
    break;
  }

  /* Advanced setup according to the type of discretization */
  if (eqp->space_scheme == CS_SPACE_SCHEME_CDOVB) {

    if (eqp->flag & CS_EQUATION_REACTION) {

      for (int r_id = 0; r_id < eqp->n_reaction_terms; r_id++) {

        const cs_param_reaction_t  r_info = eqp->reaction_terms[r_id];

        if (r_info.hodge.algo == CS_PARAM_HODGE_ALGO_WBS) {
          eqp->flag |= CS_EQUATION_HCONF_ST;
          break;
        }

      } // Loop on reaction terms

    } // There is at least one reaction term

  } // spatial scheme is vertex-based

  /* Initialize cs_sles_t structure */
  _sles_initialization(eq);

  /* Flag this equation such that parametrization is not modifiable anymore */
  eqp->flag |= CS_EQUATION_LOCKED;

  if (eq->main_ts_id > -1)
    cs_timer_stats_stop(eq->main_ts_id);

}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Set a parameter in a cs_equation_t structure attached to keyname
 *
 * \param[in, out]  eq        pointer to a cs_equation_t structure
 * \param[in]       keyname   name of key related to the member of eq to set
 * \param[in]       val       accessor to the value to set
 */
/*----------------------------------------------------------------------------*/

void
cs_equation_set_option(cs_equation_t       *eq,
                       const char          *keyname,
                       const void          *val)
{
  if (eq == NULL)
    bft_error(__FILE__, __LINE__, 0, _err_empty_eq);

  if (eq->main_ts_id > -1)
    cs_timer_stats_start(eq->main_ts_id);

  cs_equation_param_t  *eqp = eq->param;
  eqkey_t  key = _get_eqkey(keyname);

  if (key == EQKEY_ERROR) {
    bft_printf("\n\n Current key: %s\n", keyname);
    bft_printf(" Possible keys: ");
    for (int i = 0; i < EQKEY_ERROR; i++) {
      bft_printf("%s ", _print_eqkey(i));
      if (i > 0 && i % 3 == 0)
        bft_printf("\n\t");
    }
    bft_error(__FILE__, __LINE__, 0,
              _(" Invalid key %s for setting equation %s.\n"
                " Please read listing for more details and"
                " modify your settings."), keyname, eq->name);

  } /* Error message */

  if (eqp->flag & CS_EQUATION_LOCKED)
    bft_error(__FILE__, __LINE__, 0,
              _(" Equation %s is not modifiable anymore.\n"
                " Please check your settings."), eq->name);

  switch(key) {

  case EQKEY_SPACE_SCHEME:
    if (strcmp(val, "cdo_vb") == 0) {
      eqp->space_scheme = CS_SPACE_SCHEME_CDOVB;
      eqp->time_hodge.type = CS_PARAM_HODGE_TYPE_VPCD;
      eqp->diffusion_hodge.type = CS_PARAM_HODGE_TYPE_EPFD;
    }
    else if (strcmp(val, "cdo_fb") == 0) {
      eqp->space_scheme = CS_SPACE_SCHEME_CDOFB;
      eqp->time_hodge.type = CS_PARAM_HODGE_TYPE_CPVD;
      eqp->diffusion_hodge.type = CS_PARAM_HODGE_TYPE_EDFP;
    }
    else {
      const char *_val = val;
      bft_error(__FILE__, __LINE__, 0,
                _(" Invalid val %s related to key %s\n"
                  " Choice between cdo_vb or cdo_fb"), _val, keyname);
    }
    break;

  case EQKEY_HODGE_DIFF_ALGO:
    if (strcmp(val,"cost") == 0)
      eqp->diffusion_hodge.algo = CS_PARAM_HODGE_ALGO_COST;
    else if (strcmp(val, "voronoi") == 0)
      eqp->diffusion_hodge.algo = CS_PARAM_HODGE_ALGO_VORONOI;
    else if (strcmp(val, "wbs") == 0)
      eqp->diffusion_hodge.algo = CS_PARAM_HODGE_ALGO_WBS;
    else {
      const char *_val = val;
      bft_error(__FILE__, __LINE__, 0,
                _(" Invalid val %s related to key %s\n"
                  " Choice between cost, wbs or voronoi"), _val, keyname);
    }
    break;

  case EQKEY_HODGE_TIME_ALGO:
    if (strcmp(val,"cost") == 0)
      eqp->time_hodge.algo = CS_PARAM_HODGE_ALGO_COST;
    else if (strcmp(val, "voronoi") == 0)
      eqp->time_hodge.algo = CS_PARAM_HODGE_ALGO_VORONOI;
    else if (strcmp(val, "wbs") == 0)
      eqp->time_hodge.algo = CS_PARAM_HODGE_ALGO_WBS;
    else {
      const char *_val = val;
      bft_error(__FILE__, __LINE__, 0,
                _(" Invalid val %s related to key %s\n"
                  " Choice between cost, wbs or voronoi"), _val, keyname);
    }
    break;

  case EQKEY_HODGE_DIFF_COEF:
    if (strcmp(val, "dga") == 0)
      eqp->diffusion_hodge.coef = 1./3.;
    else if (strcmp(val, "sushi") == 0)
      eqp->diffusion_hodge.coef = 1./sqrt(3.);
    else if (strcmp(val, "gcr") == 0)
      eqp->diffusion_hodge.coef = 1.0;
    else
      eqp->diffusion_hodge.coef = atof(val);
    break;

  case EQKEY_HODGE_TIME_COEF:
    if (strcmp(val, "dga") == 0)
      eqp->time_hodge.coef = 1./3.;
    else if (strcmp(val, "sushi") == 0)
      eqp->time_hodge.coef = 1./sqrt(3.);
    else if (strcmp(val, "gcr") == 0)
      eqp->time_hodge.coef = 1.0;
    else
      eqp->time_hodge.coef = atof(val);
    break;

  case EQKEY_SOLVER_FAMILY:
    if (strcmp(val, "cs") == 0)
      eqp->algo_info.type = CS_EQUATION_ALGO_CS_ITSOL;
    else if (strcmp(val, "petsc") == 0)
      eqp->algo_info.type = CS_EQUATION_ALGO_PETSC_ITSOL;
    else {
      const char *_val = val;
      bft_error(__FILE__, __LINE__, 0,
                _(" Invalid val %s related to key %s\n"
                  " Choice between cs or petsc"), _val, keyname);
    }
    break;

  case EQKEY_ITSOL:
    if (strcmp(val, "cg") == 0)
      eqp->itsol_info.solver = CS_PARAM_ITSOL_CG;
    else if (strcmp(val, "bicg") == 0)
      eqp->itsol_info.solver = CS_PARAM_ITSOL_BICG;
    else if (strcmp(val, "gmres") == 0)
      eqp->itsol_info.solver = CS_PARAM_ITSOL_GMRES;
    else if (strcmp(val, "amg") == 0)
      eqp->itsol_info.solver = CS_PARAM_ITSOL_AMG;
    else {
      const char *_val = val;
      bft_error(__FILE__, __LINE__, 0,
                _(" Invalid val %s related to key %s\n"
                  " Choice between cg, bicg, gmres or amg"), _val, keyname);
    }
    break;

  case EQKEY_PRECOND:
    if (strcmp(val, "jacobi") == 0)
      eqp->itsol_info.precond = CS_PARAM_PRECOND_DIAG;
    else if (strcmp(val, "poly1") == 0)
      eqp->itsol_info.precond = CS_PARAM_PRECOND_POLY1;
    else if (strcmp(val, "ssor") == 0)
      eqp->itsol_info.precond = CS_PARAM_PRECOND_SSOR;
    else if (strcmp(val, "ilu0") == 0)
      eqp->itsol_info.precond = CS_PARAM_PRECOND_ILU0;
    else if (strcmp(val, "icc0") == 0)
      eqp->itsol_info.precond = CS_PARAM_PRECOND_ICC0;
    else if (strcmp(val, "amg") == 0)
      eqp->itsol_info.precond = CS_PARAM_PRECOND_AMG;
    else if (strcmp(val, "as") == 0)
      eqp->itsol_info.precond = CS_PARAM_PRECOND_AS;
    else {
      const char *_val = val;
      bft_error(__FILE__, __LINE__, 0,
                _(" Invalid val %s related to key %s\n"
                  " Choice between jacobi, poly1, ssor, ilu0,\n"
                  " icc0, amg or as"), _val, keyname);
    }
    break;

  case EQKEY_ITSOL_MAX_ITER:
    eqp->itsol_info.n_max_iter = atoi(val);
    break;

  case EQKEY_ITSOL_EPS:
    eqp->itsol_info.eps = atof(val);
    break;

  case EQKEY_ITSOL_RESNORM:
    if (strcmp(val, "true") == 0)
      eqp->itsol_info.resid_normalized = true;
    else if (strcmp(val, "false") == 0)
      eqp->itsol_info.resid_normalized = false;
    break;

  case EQKEY_VERBOSITY: // "verbosity"
    eqp->verbosity = atoi(val);
    break;

  case EQKEY_SLES_VERBOSITY: // "verbosity" for SLES structures
    eqp->sles_verbosity = atoi(val);
    break;

  case EQKEY_BC_ENFORCEMENT:
    if (strcmp(val, "strong") == 0)
      eqp->bc->enforcement = CS_PARAM_BC_ENFORCE_STRONG;
    else if (strcmp(val, "penalization") == 0)
      eqp->bc->enforcement = CS_PARAM_BC_ENFORCE_WEAK_PENA;
    else if (strcmp(val, "weak_sym") == 0)
      eqp->bc->enforcement = CS_PARAM_BC_ENFORCE_WEAK_SYM;
    else if (strcmp(val, "weak") == 0)
      eqp->bc->enforcement = CS_PARAM_BC_ENFORCE_WEAK_NITSCHE;
    else {
      const char *_val = val;
      bft_error(__FILE__, __LINE__, 0,
                _(" Invalid value %s related to key %s\n"
                  " Choice between strong, penalization, weak or\n"
                  " weak_sym."), _val, keyname);
    }
    break;

  case EQKEY_BC_QUADRATURE:
    if (strcmp(val, "subdiv") == 0)
      eqp->bc->use_subdiv = true;
    else if (strcmp(val, "bary") == 0)
      eqp->bc->quad_type = CS_QUADRATURE_BARY;
    else if (strcmp(val, "higher") == 0)
      eqp->bc->quad_type = CS_QUADRATURE_HIGHER;
    else if (strcmp(val, "highest") == 0)
      eqp->bc->quad_type = CS_QUADRATURE_HIGHEST;
    else {
      const char *_val = val;
      bft_error(__FILE__, __LINE__, 0,
                _(" Invalid key value %s for setting the quadrature behaviour"
                  " of boundary conditions.\n"
                  " Choices are among subdiv, bary, higher and highest."), _val);
    }
    break;

  case EQKEY_EXTRA_OP:
    if (strcmp(val, "peclet") == 0)
      eqp->process_flag |= CS_EQUATION_POST_PECLET;
    else if (strcmp(val, "none") == 0)
      eqp->process_flag |= CS_EQUATION_POST_NONE;
    else if (strcmp(val, "upwind_coef") == 0)
      eqp->process_flag |= CS_EQUATION_POST_UPWIND_COEF;
    break;

  case EQKEY_ADV_OP_TYPE:
    if (strcmp(val, "conservative") == 0)
      eqp->advection_info.formulation = CS_PARAM_ADVECTION_FORM_CONSERV;
    else if (strcmp(val, "non_conservative") == 0)
      eqp->advection_info.formulation = CS_PARAM_ADVECTION_FORM_NONCONS;
    else {
      const char *_val = val;
      bft_error(__FILE__, __LINE__, 0,
                _(" Invalid key value %s for setting the form of the convection"
                  " term.\n"
                  " Choices are among conservative and non_conservative."),
                _val);
    }
    break;

  case EQKEY_ADV_WEIGHT_ALGO:
    if (strcmp(val, "upwind") == 0)
      eqp->advection_info.weight_algo = CS_PARAM_ADVECTION_WEIGHT_ALGO_UPWIND;
    else if (strcmp(val, "samarskii") == 0)
      eqp->advection_info.weight_algo = CS_PARAM_ADVECTION_WEIGHT_ALGO_SAMARSKII;
    else if (strcmp(val, "sg") == 0)
      eqp->advection_info.weight_algo = CS_PARAM_ADVECTION_WEIGHT_ALGO_SG;
    else if (strcmp(val, "d10g5") == 0)
      eqp->advection_info.weight_algo = CS_PARAM_ADVECTION_WEIGHT_ALGO_D10G5;
    else if (strcmp(val, "centered") == 0)
      eqp->advection_info.weight_algo = CS_PARAM_ADVECTION_WEIGHT_ALGO_CENTERED;
    else {
      const char *_val = val;
      bft_error(__FILE__, __LINE__, 0,
                _(" Invalid key value %s for setting the algorithm for defining"
                  " the proportion of upwinding.\n"
                  " Choices are among upwind, samarskii, sg and centered."),
                _val);
    }
    break;

  case EQKEY_ADV_WEIGHT_CRIT:
    if (strcmp(val, "xexc") == 0)
      eqp->advection_info.weight_criterion = CS_PARAM_ADVECTION_WEIGHT_XEXC;
    else if (strcmp(val, "flux") == 0)
      eqp->advection_info.weight_criterion = CS_PARAM_ADVECTION_WEIGHT_FLUX;
    else {
      const char *_val = val;
      bft_error(__FILE__, __LINE__, 0,
                _(" Invalid key value %s for setting the algorithm for"
                  " computing the upwinding weight.\n"
                  " Choices are among flux and xexc."),
                _val);
    }
    break;

  case EQKEY_ADV_FLUX_QUADRA:
    if (strcmp(val, "bary") == 0)
      eqp->advection_info.quad_type = CS_QUADRATURE_BARY;
    else if (strcmp(val, "higher") == 0)
      eqp->advection_info.quad_type = CS_QUADRATURE_HIGHER;
    else if (strcmp(val, "highest") == 0)
      eqp->advection_info.quad_type = CS_QUADRATURE_HIGHEST;
    else {
      const char *_val = val;
      bft_error(__FILE__, __LINE__, 0,
                _(" Invalid key value %s for setting the quadrature behaviour"
                  " used for computing the advection flux.\n"
                  " Choices are among bary, higher and highest."), _val);
    }
    break;

  case EQKEY_TIME_SCHEME:
    if (strcmp(val, "implicit") == 0) {
      eqp->time_info.scheme = CS_TIME_SCHEME_IMPLICIT;
      eqp->time_info.theta = 1.;
    }
    else if (strcmp(val, "explicit") == 0) {
      eqp->time_info.scheme = CS_TIME_SCHEME_EXPLICIT;
      eqp->time_info.theta = 0.;
    }
    else if (strcmp(val, "crank_nicolson") == 0) {
      eqp->time_info.scheme = CS_TIME_SCHEME_CRANKNICO;
      eqp->time_info.theta = 0.5;
    }
    else if (strcmp(val, "theta_scheme") == 0)
      eqp->time_info.scheme = CS_TIME_SCHEME_THETA;
    else {
      const char *_val = val;
      bft_error(__FILE__, __LINE__, 0,
                _(" Invalid key value %s for setting the time scheme.\n"
                  " Choices are among implicit, explicit, crank_nicolson"
                  " and theta_scheme"), _val);
    }
    break;

  case EQKEY_TIME_THETA:
    eqp->time_info.theta = atof(val);
    break;

  default:
    bft_error(__FILE__, __LINE__, 0,
              _(" Key %s is not implemented yet."), keyname);

  } /* Switch on keys */

  if (eq->main_ts_id > -1)
    cs_timer_stats_stop(eq->main_ts_id);
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Associate a material property or an advection field with an equation
 *         for a given term (diffusion, time, convection)
 *
 * \param[in, out]  eq        pointer to a cs_equation_t structure
 * \param[in]       keyword   "time", "diffusion", "advection"
 * \param[in]       pointer   pointer to a given structure
 */
/*----------------------------------------------------------------------------*/

void
cs_equation_link(cs_equation_t       *eq,
                 const char          *keyword,
                 void                *pointer)
{
  if (eq == NULL)
    bft_error(__FILE__, __LINE__, 0, _err_empty_eq);

  cs_equation_param_t  *eqp = eq->param;

  if (strcmp("diffusion", keyword) == 0) {

    eqp->flag |= CS_EQUATION_DIFFUSION;
    eqp->diffusion_property = (cs_property_t *)pointer;

  }
  else if (strcmp("time", keyword) == 0) {

    eqp->flag |= CS_EQUATION_UNSTEADY;
    eqp->time_property = (cs_property_t *)pointer;

  }
  else if (strcmp("advection", keyword) == 0) {

    eqp->flag |= CS_EQUATION_CONVECTION;
    eqp->advection_field = (cs_adv_field_t *)pointer;

  }
  else
    bft_error(__FILE__, __LINE__, 0,
              _(" Invalid keyword for linking an equation.\n"
                " Current value: %s\n"
                " Possible choices: diffusion, time, advection\n"),
              keyword);

}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Define the initial condition of the unknown related to this equation
 *         This definition can be done by mesh location
 *         Available types of definition are: "value" and "analytic"
 *
 * \param[in, out]  eq        pointer to a cs_equation_t structure
 * \param[in]       ml_name   name of the associated mesh location (if NULL or
 *                            "" all entities are considered)
 * \param[in]       def_key   way of defining the value of the BC
 * \param[in]       val       pointer to the value
 */
/*----------------------------------------------------------------------------*/

void
cs_equation_set_ic(cs_equation_t    *eq,
                   const char       *ml_name,
                   const char       *def_key,
                   void             *val)
{
  if (eq == NULL)
    bft_error(__FILE__, __LINE__, 0, _err_empty_eq);

  cs_equation_param_t  *eqp = eq->param;
  cs_param_time_t  t_info = eqp->time_info;
  int  id = t_info.n_ic_definitions;

  BFT_REALLOC(t_info.ic_definitions, id+1, cs_param_def_t);

  cs_param_def_t  *ic = t_info.ic_definitions + id;

  /* Get the type of definition */
  if (strcmp(def_key, "value") == 0)
    ic->def_type = CS_PARAM_DEF_BY_VALUE;
  else if (strcmp(def_key, "analytic") == 0)
    ic->def_type = CS_PARAM_DEF_BY_ANALYTIC_FUNCTION;
  else
    bft_error(__FILE__, __LINE__, 0,
              _(" Invalid key for setting the initial condition.\n"
                " Given key: %s\n"
                " Available choices are 'value' and 'analytic'.\n"
                " Please modify your settings."), def_key);

  /* Handle the name of the mesh location */
  if (ml_name == NULL) {
    BFT_MALLOC(ic->ml_name, 1, char);
    strcpy(ic->ml_name, "");
  }
  else {
    BFT_MALLOC(ic->ml_name, strlen(ml_name) + 1, char);
    strcpy(ic->ml_name, ml_name);
  }

  /* Set the definition */
  cs_param_set_def(ic->def_type, eqp->var_type, val, &(ic->def));

  /* Update the number of definitions */
  t_info.n_ic_definitions += 1;
  eqp->time_info = t_info;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Define and initialize a new structure to store parameters related
 *         to an equation
 *         bc_key among "dirichlet", "neumann" or "robin"
 *         def_key among "value", "analytic", "user"
 *
 * \param[in, out]  eq        pointer to a cs_equation_t structure
 * \param[in]       ml_name   name of the related mesh location
 * \param[in]       bc_key    type of boundary condition to add
 * \param[in]       def_key   way of defining the value of the bc
 * \param[in]       val       pointer to the value
 */
/*----------------------------------------------------------------------------*/

void
cs_equation_add_bc(cs_equation_t    *eq,
                   const char       *ml_name,
                   const char       *bc_key,
                   const char       *def_key,
                   const void       *val)
{
  int  ml_id;

  if (eq == NULL)
    bft_error(__FILE__, __LINE__, 0, _err_empty_eq);

  cs_equation_param_t  *eqp = eq->param;
  cs_param_bc_t  *bc = eqp->bc;
  cs_param_bc_type_t  bc_type = CS_PARAM_N_BC_TYPES;
  cs_param_def_type_t  def_type = CS_PARAM_N_DEF_TYPES;

  /* Sanity checks */
  assert(bc != NULL);

  /* Add a new definition */
  int def_id = bc->n_defs;
  bc->n_defs += 1;
  BFT_REALLOC(bc->defs, bc->n_defs, cs_param_bc_def_t);

  /* Get the mesh location id from its name */
  _check_ml_name(ml_name, &ml_id);

  /* Get the type of definition */
  if (strcmp(def_key, "value") == 0)
    def_type = CS_PARAM_DEF_BY_VALUE;
  else if (strcmp(def_key, "array") == 0)
    def_type = CS_PARAM_DEF_BY_ARRAY;
  else if (strcmp(def_key, "analytic") == 0)
    def_type = CS_PARAM_DEF_BY_ANALYTIC_FUNCTION;
  else if (strcmp(def_key, "user") == 0)
    def_type = CS_PARAM_DEF_BY_USER_FUNCTION;
  else
    bft_error(__FILE__, __LINE__, 0,
              _(" Invalid key for setting the type of definition.\n"
                " Given key: %s\n"
                " Choice among value, field, evaluator, analytic, user, law"
                " or file\n"
                " Please modify your settings."), def_key);

  /* Get the type of boundary condition */
  if (strcmp(bc_key, "dirichlet") == 0)
    bc_type = CS_PARAM_BC_DIRICHLET;
  else if (strcmp(bc_key, "neumann") == 0)
    bc_type = CS_PARAM_BC_NEUMANN;
  else if (strcmp(bc_key, "robin") == 0)
    bc_type = CS_PARAM_BC_ROBIN;
  else
    bft_error(__FILE__, __LINE__, 0,
              _(" Invalid key for setting the type of boundary condition.\n"
                " Given key: %s\n"
                " Choice among dirichlet, neumann or robin.\n"
                " Please modify your settings."), bc_key);

  /* Check if this is a homogeneous boundary condition */
  if (def_type == CS_PARAM_DEF_BY_VALUE && eqp->var_type == CS_PARAM_VAR_SCAL) {
    cs_real_t  value = atof(val);
    if (fabs(value) < DBL_MIN) {
      if (bc_type == CS_PARAM_BC_DIRICHLET)
        bc_type = CS_PARAM_BC_HMG_DIRICHLET;
      if (bc_type == CS_PARAM_BC_NEUMANN)
        bc_type = CS_PARAM_BC_HMG_NEUMANN;
    }
  }

  cs_param_bc_def_set(bc->defs + def_id,
                      ml_id,
                      bc_type,
                      eqp->var_type,
                      def_type,
                      val, NULL); // coef2 is not used up to now
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Define and initialize a new structure to store parameters related
 *         to a reaction term
 *
 * \param[in, out] eq         pointer to a cs_equation_t structure
 * \param[in]      r_name     name of the reaction term or NULL
 * \param[in]      type_name  type of reaction term to add
 * \param[in]      property   pointer to a cs_property_t struct.
 */
/*----------------------------------------------------------------------------*/

void
cs_equation_add_reaction(cs_equation_t   *eq,
                         const char      *r_name,
                         const char      *type_name,
                         cs_property_t   *property)
{
  if (eq == NULL)
    bft_error(__FILE__, __LINE__, 0, _err_empty_eq);

  /* Only this kind of reaction term is available up to now */
  cs_param_reaction_type_t  r_type = CS_PARAM_N_REACTION_TYPES;
  cs_param_hodge_type_t  h_type = CS_PARAM_N_HODGE_TYPES;
  cs_param_hodge_algo_t  h_algo = CS_PARAM_N_HODGE_ALGOS;
  cs_equation_param_t  *eqp = eq->param;

  /* Add a new source term */
  int  r_id = eqp->n_reaction_terms;
  eqp->n_reaction_terms += 1;
  BFT_REALLOC(eqp->reaction_terms, eqp->n_reaction_terms, cs_param_reaction_t);

  /* Associate a property to this reaction term */
  BFT_REALLOC(eqp->reaction_properties, eqp->n_reaction_terms,
              cs_property_t *);
  eqp->reaction_properties[r_id] = property;

  /* Associate a name to this reaction term */
  const char  *name;
  char *_r_name = NULL;

  if (r_name == NULL) { /* Define a name by default */
    assert(r_id < 100);
    int len = strlen("reaction_00") + 1;
    BFT_MALLOC(_r_name, len, char);
    sprintf(_r_name, "reaction_%02d", r_id);
    name = _r_name;
  }
  else
    name = r_name;

  /* Set the type of reaction term */
  if (strcmp(type_name, "linear") == 0)
    r_type = CS_PARAM_REACTION_TYPE_LINEAR;
  else
    bft_error(__FILE__, __LINE__, 0,
              _(" Invalid type of reaction term for equation %s."), eq->name);

  /* Set options associated to the related discrete Hodge operator */
  switch (eqp->space_scheme) {

  case CS_SPACE_SCHEME_CDOVB:
    h_algo = CS_PARAM_HODGE_ALGO_WBS;
    h_type = CS_PARAM_HODGE_TYPE_VPCD;
    break;

  case CS_SPACE_SCHEME_CDOFB:
    bft_error(__FILE__, __LINE__, 0, "This case is not implemented yet.");
    break;

  default:
    bft_error(__FILE__, __LINE__, 0,
              _(" Invalid type of discretization scheme.\n"
                " Only CDO vertex-based and face-based scheme are handled.\n"
                " Please modify your settings for equation %s."), eq->name);
  }

  /* Get the type of source term */
  cs_param_reaction_add(eqp->reaction_terms + r_id,
                        name,
                        h_type,
                        h_algo,
                        r_type);

  /* Flag the equation with "reaction" */
  eqp->flag |= CS_EQUATION_REACTION;

  if (r_name == NULL)
    BFT_FREE(_r_name);
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Set advanced parameters related to a reaction term
 *         keyname among "lumping", "hodge_algo", "hodge_coef"...
 *         If r_name is NULL, all reaction terms of the given equation are set
 *         according to the couple (keyname, keyval)
 *
 * \param[in, out]  eq        pointer to a cs_equation_t structure
 * \param[in]       r_name    name of the reaction term
 * \param[in]       keyname   name of the key
 * \param[in]       keyval    pointer to the value to set to the key
 */
/*----------------------------------------------------------------------------*/

void
cs_equation_set_reaction_option(cs_equation_t    *eq,
                                const char       *r_name,
                                const char       *keyname,
                                const char       *keyval)
{
  int  i;

  if (eq == NULL)
    bft_error(__FILE__, __LINE__, 0, _err_empty_eq);

  if (eq->main_ts_id > -1)
    cs_timer_stats_start(eq->main_ts_id);

  cs_equation_param_t  *eqp = eq->param;

  /* Look for the requested reaction term */
  int  r_id = -1;
  if (r_name != NULL) { // Look for the related source term structure

    for (i = 0; i < eqp->n_reaction_terms; i++) {
      if (strcmp(eqp->reaction_terms[i].name, r_name) == 0) {
        r_id = i;
        break;
      }
    }

    if (r_id == -1) // Error
      bft_error(__FILE__, __LINE__, 0,
                _(" Cannot find the reaction term %s.\n"
                  " Please check your settings.\n"), r_name);

  } // r_name != NULL

  reakey_t  key = _get_reakey(keyname);

  if (key == REAKEY_ERROR) {
    bft_printf("\n\n Current key: %s\n", keyname);
    bft_printf(" Possible keys: ");
    for (i = 0; i < REAKEY_ERROR; i++) {
      bft_printf("%s ", _print_reakey(i));
      if (i > 0 && i % 3 == 0)
        bft_printf("\n\t");
    }
    bft_error(__FILE__, __LINE__, 0,
              _(" Invalid key for setting a reaction term %s.\n"
                " Please read listing for more details and"
                " modify your settings."), r_name);

  } /* Error message */

  switch(key) {

  case REAKEY_HODGE_ALGO:
    {
      cs_param_hodge_algo_t  h_algo = CS_PARAM_N_HODGE_ALGOS;

      if (strcmp(keyval,"cost") == 0)
        h_algo = CS_PARAM_HODGE_ALGO_COST;
      else if (strcmp(keyval, "voronoi") == 0)
        h_algo = CS_PARAM_HODGE_ALGO_VORONOI;
      else if (strcmp(keyval, "wbs") == 0)
        h_algo = CS_PARAM_HODGE_ALGO_WBS;
      else {
        const char *_val = keyval;
        bft_error(__FILE__, __LINE__, 0,
                  _(" Invalid val %s related to key %s\n"
                    " Choice between cost, wbs or voronoi"), _val, keyname);
      }

      if (r_id != -1)
        eqp->reaction_terms[r_id].hodge.algo = h_algo;
      else
        for (i = 0; i < eqp->n_reaction_terms; i++)
          eqp->reaction_terms[i].hodge.algo = h_algo;

    }
    break;

  case REAKEY_HODGE_COEF:
    {
      double  coef;

      if (strcmp(keyval, "dga") == 0)
        coef = 1./3.;
      else if (strcmp(keyval, "sushi") == 0)
        coef = 1./sqrt(3.);
      else if (strcmp(keyval, "gcr") == 0)
        coef = 1.0;
      else
        coef = atof(keyval);

      if (r_id != -1)
        eqp->reaction_terms[r_id].hodge.coef = coef;
      else
        for (i = 0; i < eqp->n_reaction_terms; i++)
          eqp->reaction_terms[i].hodge.coef = coef;

    }
    break;

  case REAKEY_INV_PTY:
    {
      bool  inv_pty = false;

      if (strcmp(keyval, "true") == 0)
        inv_pty = true;

      if (r_id != -1)
        eqp->reaction_terms[r_id].hodge.inv_pty = inv_pty;
      else
        for (i = 0; i < eqp->n_reaction_terms; i++)
          eqp->reaction_terms[i].hodge.inv_pty = inv_pty;

    }
    break;

  case REAKEY_LUMPING:
    {
      bool  do_lumping = false;

      if (strcmp(keyval, "true") == 0)
        do_lumping = true;

      if (r_id != -1)
        eqp->reaction_terms[r_id].do_lumping = do_lumping;
      else
        for (i = 0; i < eqp->n_reaction_terms; i++)
          eqp->reaction_terms[i].do_lumping = do_lumping;

    }
    break;

  default:
    bft_error(__FILE__, __LINE__, 0,
              _(" Key %s is not implemented yet."), keyname);

  } /* Switch on keys */

  if (eq->main_ts_id > -1)
    cs_timer_stats_stop(eq->main_ts_id);
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Define and initialize a new structure to store parameters related
 *         to a source term
 *         def_key among "value", "analytic", "user"...
 *
 * \param[in, out] eq            pointer to a cs_equation_t structure
 * \param[in]      ml_id         id related to a mesh location
 * \param[in]      array_desc    short description of this array (mask of bits)
 * \param[in]      array_values  pointer to the array values
 */
/*----------------------------------------------------------------------------*/

void
cs_equation_add_gravity_source_term(cs_equation_t   *eq,
                                    int              ml_id,
                                    cs_desc_t        array_desc,
                                    cs_real_t       *array_values)
{
  if (eq == NULL)
    bft_error(__FILE__, __LINE__, 0, _err_empty_eq);

  cs_source_term_type_t  st_type = CS_SOURCE_TERM_GRAVITY;
  cs_equation_param_t  *eqp = eq->param;

  /* Add a new source term */
  int  st_id = eqp->n_source_terms;

  eqp->n_source_terms += 1;
  BFT_REALLOC(eqp->source_terms, eqp->n_source_terms, cs_source_term_t *);

  /* Create and set new source term structure */
  eqp->source_terms[st_id] = cs_source_term_create("gravity_source",
                                                   ml_id,
                                                   st_type,
                                                   eqp->var_type);

  cs_source_term_def_by_array(eqp->source_terms[st_id],
                              array_desc,
                              array_values);

}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Define and initialize by value a new structure to store parameters
 *         related to a source term defined by a user
 *
 * \param[in, out]  eq        pointer to a cs_equation_t structure
 * \param[in]       st_name   name of the source term or NULL
 * \param[in]       ml_name   name of the related mesh location
 * \param[in]       val       pointer to the value
 */
/*----------------------------------------------------------------------------*/

void
cs_equation_add_source_term_by_val(cs_equation_t   *eq,
                                   const char      *st_name,
                                   const char      *ml_name,
                                   const void      *val)
{
  if (eq == NULL)
    bft_error(__FILE__, __LINE__, 0, _err_empty_eq);

  int  ml_id;
  char *_st_name = NULL;
  cs_source_term_type_t  st_type = CS_SOURCE_TERM_USER;
  cs_equation_param_t  *eqp = eq->param;

  const char  *name;

  /* Add a new source term */
  int  st_id = eqp->n_source_terms;

  eqp->n_source_terms += 1;
  BFT_REALLOC(eqp->source_terms, eqp->n_source_terms, cs_source_term_t *);

  if (st_name == NULL) { /* Define a name by default */
    assert(st_id < 100);
    int len = strlen("sourceterm_00") + 1;
    BFT_MALLOC(_st_name, len, char);
    sprintf(_st_name, "sourceterm_%2d", st_id);
    name = _st_name;
  }
  else
    name = st_name;

  /* Get the mesh location id from its name */
  _check_ml_name(ml_name, &ml_id);

  /* Create and set new source term structure */
  eqp->source_terms[st_id] = cs_source_term_create(name,
                                                   ml_id,
                                                   st_type,
                                                   eqp->var_type);

  cs_source_term_def_by_value(eqp->source_terms[st_id], val);

  if (st_name == NULL)
    BFT_FREE(_st_name);
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Define and initialize by an analytical function a new structure
 *         related to a source term defined by a user
 *
 * \param[in, out]  eq        pointer to a cs_equation_t structure
 * \param[in]       st_name   name of the source term or NULL
 * \param[in]       ml_name   name of the related mesh location
 * \param[in]       ana       pointer to an analytical function
 */
/*----------------------------------------------------------------------------*/

void
cs_equation_add_source_term_by_analytic(cs_equation_t        *eq,
                                        const char           *st_name,
                                        const char           *ml_name,
                                        cs_analytic_func_t   *ana)
{
  if (eq == NULL)
    bft_error(__FILE__, __LINE__, 0, _err_empty_eq);

  int  ml_id;
  char *_st_name = NULL;
  cs_source_term_type_t  st_type = CS_SOURCE_TERM_USER;
  cs_equation_param_t  *eqp = eq->param;

  const char  *name;

  /* Add a new source term */
  int  st_id = eqp->n_source_terms;

  eqp->n_source_terms += 1;
  BFT_REALLOC(eqp->source_terms, eqp->n_source_terms, cs_source_term_t *);

  if (st_name == NULL) { /* Define a name by default */
    assert(st_id < 100);
    int len = strlen("sourceterm_00") + 1;
    BFT_MALLOC(_st_name, len, char);
    sprintf(_st_name, "sourceterm_%2d", st_id);
    name = _st_name;
  }
  else
    name = st_name;

  /* Get the mesh location id from its name */
  _check_ml_name(ml_name, &ml_id);

  /* Create and set new source term structure */
  eqp->source_terms[st_id] = cs_source_term_create(name,
                                                  ml_id,
                                                  st_type,
                                                  eqp->var_type);

  cs_source_term_def_by_analytic(eqp->source_terms[st_id], ana);

  if (st_name == NULL)
    BFT_FREE(_st_name);
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Set advanced parameters which are members defined by default in a
 *         source term structure.
 *         keyname among "quadrature", "post"...
 *         If st_name is NULL, all source terms of the given equation are set
 *         according to keyname/keyval
 *
 * \param[in, out]  eq        pointer to a cs_equation_t structure
 * \param[in]       st_name   name of the source term
 * \param[in]       keyname   name of the key
 * \param[in]       keyval    pointer to the value to set to the key
 */
/*----------------------------------------------------------------------------*/

void
cs_equation_set_source_term_option(cs_equation_t    *eq,
                                   const char       *st_name,
                                   const char       *keyname,
                                   const char       *keyval)
{
  int  i;

  if (eq == NULL)
    bft_error(__FILE__, __LINE__, 0, _err_empty_eq);

  if (eq->main_ts_id > -1)
    cs_timer_stats_start(eq->main_ts_id);

  cs_equation_param_t  *eqp = eq->param;

  /* Look for the requested source term */
  int  st_id = -1;
  if (st_name != NULL) { // Look for the related source term structure

    for (i = 0; i < eqp->n_source_terms; i++) {
      if (strcmp(cs_source_term_get_name(eqp->source_terms[i]), st_name) == 0) {
        st_id = i;
        break;
      }
    }

    if (st_id == -1)
      bft_error(__FILE__, __LINE__, 0,
                _(" Cannot find source term %s among defined source terms.\n"
                  " Please check your settings for equation %s.\n"),
                st_name, eq->name);

  } // st_name != NULL

  if (st_id != -1)
    cs_source_term_set_option(eqp->source_terms[st_id], keyname, keyval);
  else
    for (i = 0; i < eqp->n_source_terms; i++)
      cs_source_term_set_option(eqp->source_terms[i], keyname, keyval);

  if (eq->main_ts_id > -1)
    cs_timer_stats_stop(eq->main_ts_id);
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Create a field structure related to this cs_equation_t structure
 *         to an equation
 *
 * \param[in, out]  eq       pointer to a cs_equation_t structure
 */
/*----------------------------------------------------------------------------*/

void
cs_equation_create_field(cs_equation_t     *eq)
{
  int  dim = 0, location_id = -1; // initialize values to avoid a warning

  int  field_mask = CS_FIELD_INTENSIVE | CS_FIELD_VARIABLE;

  /* Sanity check */
  assert(eq != NULL);

  const cs_equation_param_t  *eqp = eq->param;

  _Bool has_previous = (eqp->flag & CS_EQUATION_UNSTEADY) ? true : false;

  if (eq->main_ts_id > -1)
    cs_timer_stats_start(eq->main_ts_id);

  /* Define dim */
  switch (eqp->var_type) {
  case CS_PARAM_VAR_SCAL:
    dim = 1;
    break;
  case CS_PARAM_VAR_VECT:
    dim = 3;
    break;
  case CS_PARAM_VAR_TENS:
    dim = 9;
    break;
  default:
    bft_error(__FILE__, __LINE__, 0,
              _(" Type of equation for eq. %s is incompatible with the"
                " creation of a field structure.\n"), eq->name);
    break;
  }

  /* Associate a predefined mesh_location_id to this field */
  switch (eqp->space_scheme) {
  case CS_SPACE_SCHEME_CDOVB:
    location_id = cs_mesh_location_get_id_by_name(N_("vertices"));
    break;
  case CS_SPACE_SCHEME_CDOFB:
    location_id = cs_mesh_location_get_id_by_name(N_("cells"));
    break;
  default:
    bft_error(__FILE__, __LINE__, 0,
              _(" Space scheme for eq. %s is incompatible with a field.\n"
                " Stop adding a cs_field_t structure.\n"), eq->name);
    break;
  }

  if (location_id == -1)
    bft_error(__FILE__, __LINE__, 0,
              _(" Invalid mesh location id (= -1) for the current field\n"));

  cs_field_t  *fld = cs_field_create(eq->varname,
                                     field_mask,
                                     location_id,
                                     dim,
                                     true,          // interleave
                                     has_previous);

  /* Set default value for default keys */
  cs_field_set_key_int(fld, cs_field_key_id("log"), 1);
  cs_field_set_key_int(fld, cs_field_key_id("post_vis"), 1);

  /* Store the related field id */
  eq->field_id = cs_field_id_by_name(eq->varname);

  /* Allocate and initialize values */
  cs_field_allocate_values(fld);

  if (eq->main_ts_id > -1)
    cs_timer_stats_stop(eq->main_ts_id);
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Initialize the values of a field according to the initial condition
 *         related to its equation
 *
 * \param[in]       mesh       pointer to the mesh structure
 * \param[in]       connect    pointer to a cs_cdo_connect_t struct.
 * \param[in]       cdoq       pointer to a cs_cdo_quantities_t struct.
 * \param[in]       time_step  pointer to a time step structure
 * \param[in, out]  eq         pointer to a cs_equation_t structure
 */
/*----------------------------------------------------------------------------*/

void
cs_equation_init_system(const cs_mesh_t            *mesh,
                        const cs_cdo_connect_t     *connect,
                        const cs_cdo_quantities_t  *cdoq,
                        const cs_time_step_t       *time_step,
                        cs_equation_t              *eq)
{
  if (eq == NULL)
    return;

  if (eq->main_ts_id > -1)
    cs_timer_stats_start(eq->main_ts_id);

  const double  t_ini = 0;
  const cs_equation_param_t  *eqp = eq->param;

  /* Allocate and initialize a system builder */
  eq->builder = eq->init_builder(eqp, mesh, connect, cdoq, time_step);

  /* Compute the (initial) source term */
  eq->compute_source(eq->builder);

  /* Initialize the associated field to the initial condition if unsteady */
  if (!(eqp->flag & CS_EQUATION_UNSTEADY)) {
    if (eq->main_ts_id > -1)
      cs_timer_stats_stop(eq->main_ts_id);
    return;
  }

  cs_param_time_t  t_info = eqp->time_info;

  if (t_info.n_ic_definitions == 0) {
    if (eq->main_ts_id > -1)
      cs_timer_stats_stop(eq->main_ts_id);
    return; // By default, 0 is set
  }

  _initialize_field_from_ic(eq, connect, cdoq);

  if (eq->main_ts_id > -1)
    cs_timer_stats_stop(eq->main_ts_id);
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Check if one has to build the linear system
 *
 * \param[in]  eq        pointer to a cs_equation_t structure
 *
 * \return true or false
 */
/*----------------------------------------------------------------------------*/

bool
cs_equation_needs_build(const cs_equation_t    *eq)
{
  return eq->do_build;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Build the linear system for this equation
 *
 * \param[in]       m           pointer to a cs_mesh_t structure
 * \param[in]       time_step   pointer to a time step structure
 * \param[in]       dt_cur      value of the current time step
 * \param[in, out]  eq          pointer to a cs_equation_t structure
 */
/*----------------------------------------------------------------------------*/

void
cs_equation_build_system(const cs_mesh_t            *mesh,
                         const cs_time_step_t       *time_step,
                         double                      dt_cur,
                         cs_equation_t              *eq)
{
  cs_sla_matrix_t  *sla_mat = NULL;

  const char *eqn = eq->name;
  const cs_equation_param_t  *eqp = eq->param;
  const cs_field_t  *fld = cs_field_by_id(eq->field_id);

  if (eq->pre_ts_id > -1)
    cs_timer_stats_start(eq->pre_ts_id);

  eq->build_system(mesh, fld->val, dt_cur,
                   eq->builder,
                   &(eq->rhs),
                   &(sla_mat));

  /* Get information on the matrix related to this linear system */
  if (eqp->verbosity > 1 && time_step->nt_cur == 0) {

    cs_sla_matrix_set_info(sla_mat);

    cs_sla_matrix_info_t  minfo = sla_mat->info;

    bft_printf("\n Sparse Linear Algebra (SLA) sumup:\n");
    bft_printf("  <%s/sla> A.size         %d\n", eqn, sla_mat->n_rows);
    bft_printf("  <%s/sla> A.nnz          %lu\n", eqn, minfo.nnz);
    bft_printf("  <%s/sla> A.FillIn       %5.2e %%\n", eqn, minfo.fillin);
    bft_printf("  <%s/sla> A.StencilMin   %d\n", eqn, minfo.stencil_min);
    bft_printf("  <%s/sla> A.StencilMax   %d\n", eqn, minfo.stencil_max);
    bft_printf("  <%s/sla> A.StencilMean  %5.2e\n", eqn, minfo.stencil_mean);
  }

  /* Map a cs_sla_matrix_t structure into a cs_matrix_t structure */
  assert(sla_mat->type == CS_SLA_MAT_MSR);

  bool  do_transfer = true;
  if (eqp->space_scheme == CS_SPACE_SCHEME_CDOVB)
    do_transfer = false;

  /* First step: create a matrix structure */
  if (eq->ms == NULL)
    eq->ms = cs_matrix_structure_create_msr(CS_MATRIX_MSR,      // type
                                            do_transfer,        // transfer
                                            true,               // have_diag
                                            sla_mat->n_rows,    // n_rows
                                            sla_mat->n_cols,    // n_cols_ext
                                            &(sla_mat->idx),    // row_index
                                            &(sla_mat->col_id), // col_id
                                            NULL,               // halo
                                            NULL);              // numbering

  if (eq->matrix == NULL)
    eq->matrix = cs_matrix_create(eq->ms); // ms is also stored inside matrix

  const cs_lnum_t  *row_index, *col_id;
  cs_matrix_get_msr_arrays(eq->matrix, &row_index, &col_id, NULL, NULL);

  /* Second step: associate coefficients to a matrix structure */
  cs_matrix_transfer_coefficients_msr(eq->matrix,
                                      false,             // symmetric values ?
                                      NULL,              // diag. block
                                      NULL,              // extra-diag. block
                                      row_index,         // row_index
                                      col_id,            // col_id
                                      &(sla_mat->diag),  // diag. values
                                      &(sla_mat->val));  // extra-diag. values

  /* Free non-transferred parts of sla_mat */
  sla_mat = cs_sla_matrix_free(sla_mat);

  eq->do_build = false;

  if (eq->pre_ts_id > -1)
    cs_timer_stats_stop(eq->pre_ts_id);
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Solve the linear system for this equation
 *
 * \param[in, out]  eq          pointer to a cs_equation_t structure
 * \param[in]       do_logcvg   output information on convergence or not
 */
/*----------------------------------------------------------------------------*/

void
cs_equation_solve(cs_equation_t   *eq,
                  bool             do_logcvg)
{
  int  n_iters = 0;
  double  r_norm = DBL_MAX, residual = DBL_MAX;

  if (eq->solve_ts_id > -1)
    cs_timer_stats_start(eq->solve_ts_id);

  cs_real_t  *x = eq->get_tmpbuf();
  cs_sles_t  *sles = cs_sles_find_or_add(eq->field_id, NULL);
  cs_field_t  *fld = cs_field_by_id(eq->field_id);

  const cs_lnum_t  n_rows = cs_matrix_get_n_rows(eq->matrix);
  const cs_param_itsol_t  itsol_info = eq->param->itsol_info;

  if (eq->param->sles_verbosity > 0)
    printf("\n# %s >> Solve Ax = b with %s as solver and %s as precond.\n"
           "# System size: %8d ; eps: % -8.5e ;\n",
           eq->name, cs_param_get_solver_name(itsol_info.solver),
           cs_param_get_precond_name(itsol_info.precond),
           n_rows, itsol_info.eps);

  if (itsol_info.resid_normalized)
    r_norm = cs_euclidean_norm(n_rows, eq->rhs) / n_rows;
  else
    r_norm = 1.0;

  /* Sanity check (up to now, only scalar field are handled) */
  assert(fld->dim == 1);
  for (cs_lnum_t  i = 0; i < n_rows; i++)
    x[i] = fld->val[i];

  cs_sles_convergence_state_t code = cs_sles_solve(sles,
                                                   eq->matrix,
                                                   CS_HALO_ROTATION_IGNORE,
                                                   itsol_info.eps,
                                                   r_norm,
                                                   &n_iters,
                                                   &residual,
                                                   eq->rhs,
                                                   x,
                                                   0,      // aux. size
                                                   NULL);  // aux. buffers

  if (do_logcvg)
    bft_printf("  <%s/sles_cvg> code  %d n_iters  %d residual  % -8.4e\n",
               eq->name, code, n_iters, residual);

  if (eq->param->sles_verbosity > 0)
    printf("# %s >> n_iters = %d with a residual norm = %8.5e\n",
           eq->name, n_iters, residual);

  if (eq->solve_ts_id > -1)
    cs_timer_stats_stop(eq->solve_ts_id);

  /* Store the solution in the related field structure */
  if (eq->extra_op_ts_id > -1)
    cs_timer_stats_start(eq->extra_op_ts_id);

  /* Copy current field values to previous values */
  cs_field_current_to_previous(fld);

  /* Define the new field value for the current time */
  eq->update_field(x, eq->builder, fld->val);

  if (eq->extra_op_ts_id > -1)
    cs_timer_stats_stop(eq->extra_op_ts_id);

  if (eq->param->flag & CS_EQUATION_UNSTEADY)
    eq->do_build = true; /* Improvement: exhibit cases where a new build
                            is not needed */
  /* Free memory */
  cs_sles_free(sles);
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Predefined extra-operations related to this equation
 *
 * \param[in]  time_step  pointer to a time step structure
 * \param[in]  eq         pointer to a cs_equation_t structure
 */
/*----------------------------------------------------------------------------*/

void
cs_equation_extra_op(const cs_time_step_t       *time_step,
                     const cs_equation_t        *eq)
{
  if (eq == NULL)
    return;

  const cs_field_t  *field = cs_field_by_id(eq->field_id);
  const cs_equation_param_t  *eqp = eq->param;

  /* Cases where a post-processing is not required */
  if (eqp->process_flag & CS_EQUATION_POST_NONE)
    return;

  /* Perform the post-processing */
  if (eq->extra_op_ts_id > -1)
    cs_timer_stats_start(eq->extra_op_ts_id);

  eq->postprocess(eq->name, field, eq->builder);

  if (eq->extra_op_ts_id > -1)
    cs_timer_stats_stop(eq->extra_op_ts_id);
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Return true is the given equation is steady otherwise false
 *
 * \param[in]  eq       pointer to a cs_equation_t structure
 *
 * \return true or false
 */
/*----------------------------------------------------------------------------*/

bool
cs_equation_is_steady(const cs_equation_t    *eq)
{
  bool  is_steady = true;

  if (eq->param->flag & CS_EQUATION_UNSTEADY)
    is_steady = false;

  return is_steady;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Compute the values of the associated field at each face of the mesh
 *         If the pointer storing the values is NULL, it is alloacted inside the
 *         function
 *
 * \param[in]       eq       pointer to a cs_equation_t structure
 *
 * \return a pointer to the values
 */
/*----------------------------------------------------------------------------*/

cs_real_t *
cs_equation_get_face_values(const cs_equation_t    *eq)
{
  if (eq == NULL)
    return NULL;

  return eq->get_f_values(eq->builder, cs_field_by_id(eq->field_id));
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Return the name related to the given cs_equation_t structure
 *         to an equation
 *
 * \param[in]  eq       pointer to a cs_equation_t structure
 *
 * \return a name or NULL if not found
 */
/*----------------------------------------------------------------------------*/

const char *
cs_equation_get_name(const cs_equation_t    *eq)
{
  if (eq == NULL)
    return NULL;
  else
    return eq->name;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Return the field structure associated to a cs_equation_t structure
 *
 * \param[in]  eq       pointer to a cs_equation_t structure
 *
 * \return a cs_field_t structure or NULL if not found
 */
/*----------------------------------------------------------------------------*/

cs_field_t *
cs_equation_get_field(const cs_equation_t    *eq)
{
  if (eq == NULL)
    return NULL;
  else
    return cs_field_by_id(eq->field_id);
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Return the flag associated to an equation
 *
 * \param[in]  eq       pointer to a cs_equation_t structure
 *
 * \return a flag (cs_flag_t type)
 */
/*----------------------------------------------------------------------------*/

cs_flag_t
cs_equation_get_flag(const cs_equation_t    *eq)
{
  cs_flag_t  ret_flag = 0;

  if (eq == NULL)
    return ret_flag;

  ret_flag = eq->param->flag;

  return ret_flag;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Return the cs_equation_param_t structure associated to a
 *         cs_equation_t structure
 *
 * \param[in]  eq       pointer to a cs_equation_t structure
 *
 * \return a cs_equation_param_t structure or NULL if not found
 */
/*----------------------------------------------------------------------------*/

const cs_equation_param_t *
cs_equation_get_param(const cs_equation_t    *eq)
{
  if (eq == NULL)
    return NULL;
  else
    return eq->param;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Return a pointer to the cs_property_t structure associated to the
 *         diffusion term for this equation (NULL if not activated).
 *
 * \param[in]  eq       pointer to a cs_equation_t structure
 *
 * \return a pointer to a cs_property_t structure
 */
/*----------------------------------------------------------------------------*/

cs_property_t *
cs_equation_get_diffusion_property(const cs_equation_t    *eq)
{
  if (eq == NULL)
    return NULL;
  else
    return eq->param->diffusion_property;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Return a pointer to the cs_property_t structure associated to the
 *         unsteady term for this equation (NULL if not activated).
 *
 * \param[in]  eq       pointer to a cs_equation_t structure
 *
 * \return a pointer to a cs_property_t structure
 */
/*----------------------------------------------------------------------------*/

cs_property_t *
cs_equation_get_time_property(const cs_equation_t    *eq)
{
  if (eq == NULL)
    return NULL;
  else
    return eq->param->time_property;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Return a pointer to the cs_property_t structure associated to the
 *         reaction term called r_name and related to this equation
 *
 *
 * \param[in]  eq       pointer to a cs_equation_t structure
 *
 * \return a pointer to a cs_property_t structure or NULL if not found
 */
/*----------------------------------------------------------------------------*/

cs_property_t *
cs_equation_get_reaction_property(const cs_equation_t    *eq,
                                  const char             *r_name)
{
  if (eq == NULL)
    return NULL;

  if (r_name == NULL)
    return NULL;

  const cs_equation_param_t  *eqp = eq->param;

  /* Look for the requested reaction term */
  int  r_id = -1;
  for (int i = 0; i < eqp->n_reaction_terms; i++) {
    if (strcmp(eqp->reaction_terms[i].name, r_name) == 0) {
      r_id = i;
      break;
    }
  }

  if (r_id == -1)
    bft_error(__FILE__, __LINE__, 0,
              _(" Cannot find the reaction term %s in equation %s.\n"
                " Please check your settings.\n"), r_name, eq->name);

  return eqp->reaction_properties[r_id];
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Return the type of numerical scheme used for the discretization in
 *         space
 *
 * \param[in]  eq       pointer to a cs_equation_t structure
 *
 * \return  a cs_space_scheme_t variable
 */
/*----------------------------------------------------------------------------*/

cs_space_scheme_t
cs_equation_get_space_scheme(const cs_equation_t    *eq)
{
  if (eq == NULL)
    return CS_SPACE_N_SCHEMES;
  else
    return eq->param->space_scheme;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Return the type of variable solved by this equation
 *
 * \param[in]  eq       pointer to a cs_equation_t structure
 *
 * \return  the type of variable (sclar, vector...) associated to this equation
 */
/*----------------------------------------------------------------------------*/

cs_param_var_type_t
cs_equation_get_var_type(const cs_equation_t    *eq)
{
  if (eq == NULL)
    return CS_PARAM_N_VAR_TYPES;
  else
    return eq->param->var_type;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Return the type of equation for the given equation structure
 *
 * \param[in]  eq       pointer to a cs_equation_t structure
 *
 * \return  the type of the given equation
 */
/*----------------------------------------------------------------------------*/

cs_equation_type_t
cs_equation_get_type(const cs_equation_t    *eq)
{
  if (eq == NULL)
    return CS_EQUATION_N_TYPES;
  else
    return eq->param->type;
}

/*----------------------------------------------------------------------------*/

END_C_DECLS
