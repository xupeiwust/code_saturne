/*============================================================================
 * Compute the wall distance using the CDO framework
 *============================================================================*/

/* VERS */

/*
  This file is part of Code_Saturne, a general-purpose CFD tool.

  Copyright (C) 1998-2015 EDF S.A.

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

#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <float.h>

/*----------------------------------------------------------------------------
 *  Local headers
 *----------------------------------------------------------------------------*/

#include <bft_mem.h>
#include <bft_printf.h>

#include "cs_mesh.h"
#include "cs_post.h"
#include "cs_mesh_location.h"
#include "cs_field.h"
#include "cs_cdo.h"
#include "cs_param.h"
#include "cs_reco.h"
#include "cs_cdo_toolbox.h"

/*----------------------------------------------------------------------------
 * Header for the current file
 *----------------------------------------------------------------------------*/

#include "cs_walldistance.h"

/*----------------------------------------------------------------------------*/

BEGIN_C_DECLS

/*============================================================================
 * Private function prototypes
 *============================================================================*/

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Compute the wall distance for a face-based scheme
 *
 * \param[in]      connect   pointer to a cs_cdo_connect_t structure
 * \param[in]      cdoq      pointer to a cs_cdo_quantities_t structure
 * \param[in]      eq        pointer to the associated cs_equation_t structure
 * \param[in]      field     pointer to a cs_field_t structure
 * \param[in, out] dist      array storing the wall distance to compute
 */
/*----------------------------------------------------------------------------*/

static void
_compute_cdofb(const cs_cdo_connect_t     *connect,
               const cs_cdo_quantities_t  *cdoq,
               const cs_equation_t        *eq,
               const cs_field_t           *field,
               cs_real_t                   dist[])
{
  cs_lnum_t  i, k;

  const cs_real_t  *c_var = field->val;
  const cs_real_t  *f_var = cs_equation_get_face_values(eq);

  /* Loop on cells */
  for (cs_lnum_t c_id = 0; c_id < cdoq->n_cells; c_id++) {

    cs_real_3_t  cell_gradient = {0., 0., 0.};
    cs_real_t  inv_cell_vol = 1/cdoq->cell_vol[c_id];

    for (i = connect->c2f->idx[c_id]; i < connect->c2f->idx[c_id+1]; i++) {

      cs_lnum_t  f_id = connect->c2f->col_id[i];
      cs_quant_t  fq = cdoq->face[f_id];
      int  sgn = connect->c2f->sgn[i];
      cs_real_t  dualedge_contrib = fq.meas*sgn*(f_var[f_id] - c_var[c_id]);

      for (k = 0; k < 3; k++)
        cell_gradient[k] += dualedge_contrib*fq.unitv[k];

    } // Loop on cell faces

    for (k = 0; k < 3; k++)
      cell_gradient[k] *= inv_cell_vol;

    /* Compute the distance from the wall at this cell center */
    cs_real_t  tmp = _dp3(cell_gradient, cell_gradient) + 2*c_var[c_id];
    assert(tmp >= 0.); // Sanity check

    dist[c_id] = sqrt(tmp) - _n3(cell_gradient);

  } // Loop on cells

  cs_post_write_var(-1,              // id du maillage de post
                    field->name,
                    1,               // dim
                    false,           // interlace
                    true,            // true = original mesh
                    CS_POST_TYPE_cs_real_t,
                    dist,            // values on cells
                    NULL,            // values at internal faces
                    NULL,            // values at border faces
                    NULL);           // time step management structure

  cs_data_info_t  dinfo = cs_analysis_data(cdoq->n_cells, // n_elts
                                           1,             // stride
                                           CS_DOUBLE,     // cs_datatype_t
                                           dist,          // data
                                           false);        // absolute values ?

  bft_printf("\n -bnd- WallDistance.Max   % 10.6e\n", dinfo.max.value);
  bft_printf(" -bnd- WallDistance.Mean  % 10.6e\n", dinfo.mean);
  bft_printf(" -bnd- WallDistance.Sigma % 10.6e\n", dinfo.sigma);
  bft_printf(msepline);
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Compute the wall distance for a vertex-based scheme
 *
 * \param[in]      connect   pointer to a cs_cdo_connect_t structure
 * \param[in]      cdoq      pointer to a cs_cdo_quantities_t structure
 * \param[in]      field     pointer to a cs_field_t structure
 * \param[in, out] dist      array storing the wall distance to compute
 */
/*----------------------------------------------------------------------------*/

static void
_compute_cdovb(const cs_cdo_connect_t     *connect,
               const cs_cdo_quantities_t  *cdoq,
               const cs_field_t           *field,
               cs_real_t                   dist[])
{
  cs_lnum_t  i, k;
  cs_real_t  *gdi = NULL, *dualcell_vol = NULL, *cell_gradient = NULL;
  cs_real_3_t  *vtx_gradient = NULL;

  const cs_real_t  *var = field->val;

  /* Compute a discrete gradient of var along each edge */
  cs_sla_matvec(connect->e2v, var, &gdi, true);

  /* Reconstruct a vector field at each cell center associated to gdi */
  cs_reco_ccen_edge_dofs(connect, cdoq, gdi, &cell_gradient);

  /* Reconstruct gradient at vertices from gradient at cells */
  BFT_MALLOC(vtx_gradient, cdoq->n_vertices, cs_real_3_t);
  BFT_MALLOC(dualcell_vol, cdoq->n_vertices, cs_real_t);
  for (i = 0; i < cdoq->n_vertices; i++) {
    vtx_gradient[i][0] = vtx_gradient[i][1] = vtx_gradient[i][2] = 0.;
    dualcell_vol[i] = 0.;
  }

  for (cs_lnum_t  c_id = 0; c_id < cdoq->n_cells; c_id++) {

    cs_lnum_t  cshift = 3*c_id;

    for (i = connect->c2v->idx[c_id]; i < connect->c2v->idx[c_id+1]; i++) {

      cs_lnum_t  v_id = connect->c2v->ids[i];

      dualcell_vol[v_id] += cdoq->dcell_vol[i];
      for (k = 0; k < 3; k++)
        vtx_gradient[v_id][k] += cdoq->dcell_vol[i]*cell_gradient[cshift+k];

    } // Loop on cell vertices

  } // Loop on cells

  for (i = 0; i < cdoq->n_vertices; i++) {
    cs_real_t  inv_dualcell_vol = 1/dualcell_vol[i];
    for (k = 0; k < 3; k++)
      vtx_gradient[i][k] *= inv_dualcell_vol;
  }

  /* Compute now wall distance at each vertex */
  for (i = 0; i < cdoq->n_vertices; i++) {
    cs_real_t  tmp = _dp3(vtx_gradient[i], vtx_gradient[i]) + 2*var[i];
    assert(tmp >= 0); // Sanity check
    dist[i] = sqrt(tmp) - _n3(vtx_gradient[i]);
  }

  /* Post-processing */
  cs_post_write_vertex_var(-1,              // id du maillage de post
                           field->name,
                           1,               // dim
                           false,           // interlace
                           true,            // true = original mesh
                           CS_POST_TYPE_cs_real_t,
                           dist,            // values on vertices
                           NULL);           // time step management structure

  cs_data_info_t  dinfo = cs_analysis_data(cdoq->n_vertices, // n_elts
                                           1,                // stride
                                           CS_DOUBLE,        // cs_datatype_t
                                           dist,             // data
                                           false);           // abs. values ?

  bft_printf("\n -bnd- WallDistance.Max   % 10.6e\n", dinfo.max.value);
  bft_printf(" -bnd- WallDistance.Mean  % 10.6e\n", dinfo.mean);
  bft_printf(" -bnd- WallDistance.Sigma % 10.6e\n", dinfo.sigma);

  /* Free memory */
  BFT_FREE(gdi);
  BFT_FREE(dualcell_vol);
  BFT_FREE(cell_gradient);
  BFT_FREE(vtx_gradient);
}

/*============================================================================
 * Public function prototypes
 *============================================================================*/

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Compute the wall distance
 *
 * \param[in]   connect   pointer to a cs_cdo_connect_t structure
 * \param[in]   cdoq      pointer to a cs_cdo_quantities_t structure
 * \param[in]   eq        pointer to the associated cs_equation_t structure
 */
/*----------------------------------------------------------------------------*/

void
cs_walldistance_compute(const cs_cdo_connect_t      *connect,
                        const cs_cdo_quantities_t   *cdoq,
                        const cs_equation_t         *eq)
{
  cs_space_scheme_t  space_scheme = cs_equation_get_space_scheme(eq);
  cs_field_t  *field = cs_equation_get_field(eq);
  cs_real_t  *dist = NULL;
  const cs_lnum_t *n_elts = cs_mesh_location_get_n_elts(field->location_id);

  /* Sanity checks */
  assert(field->is_owner);
  assert(field->dim == 1);

  /* Initialize dist array */
  BFT_MALLOC(dist, n_elts[0], cs_real_t);
  for (int i = 0; i < n_elts[0]; i++)
    dist[i] = 0;

  switch (space_scheme) {

  case CS_SPACE_SCHEME_CDOVB:
    assert(n_elts[0] == cdoq->n_vertices);
    _compute_cdovb(connect, cdoq, field, dist);
    break;

  case CS_SPACE_SCHEME_CDOFB:
    assert(n_elts[0] == cdoq->n_cells);
    _compute_cdofb(connect, cdoq, eq, field, dist);
    break;

  default:
    assert(0);
    break;
  }

  /* Replace field values by dist */
  for (int i = 0; i < n_elts[0]; i++)
    field->val[i] = dist[i];

  /* Free memory */
  BFT_FREE(dist);
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Setup an new equation related to the wall distance
 *
 * \param[in]   eq          pointer to the associated cs_equation_t structure
 * \param[in]   wall_ml_id  id of the mesh location related to wall boundaries
 */
/*----------------------------------------------------------------------------*/

void
cs_walldistance_setup(cs_equation_t   *eq,
                      int              wall_ml_id)
{
  /* Sanity check */
  assert(!strcmp(cs_equation_get_name(eq), "WallDistance"));

  /* By default: vertex-based CDO schemes and a unitary material property
     are set */

  /* Unity is a material property defined by default */
  cs_equation_link(eq, "diffusion", "unity");

  /* Add boundary conditions */
  cs_equation_add_bc(eq,
                     cs_mesh_location_get_name(wall_ml_id),
                     "dirichlet",  // type of boundary condition
                     "value",      // type of definition
                     "0.0");       // value to set

  /* Add source term */
  cs_equation_add_source_term(eq,
                              "WallDist.st",   // label
                              "cells",         // mesh location name
                              "explicit",      // type of source term
                              "value",         // type of definition
                              "1.0");          // value to set


  /* Post-processing of the computed unknown only at the beginning */
  cs_equation_set(eq, "post_freq", "0");

#if defined(HAVE_PETSC)  /* Modify the default settings */
  cs_equation_set(eq, "solver_family", "petsc");
#endif

}

/*----------------------------------------------------------------------------*/

END_C_DECLS
