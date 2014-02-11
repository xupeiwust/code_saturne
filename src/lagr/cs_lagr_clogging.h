#ifndef __CS_LAGR_CLOGGING_H__
#define __CS_LAGR_CLOGGING_H__

/*============================================================================
 * Clogging modeling.
 *============================================================================*/

/*
  This file is part of Code_Saturne, a general-purpose CFD tool.

  Copyright (C) 1998-2014 EDF S.A.

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

/*----------------------------------------------------------------------------
 *  Local headers
 *----------------------------------------------------------------------------*/

#include "cs_defs.h"
#include "cs_lagr_tracking.h"

/*----------------------------------------------------------------------------*/

BEGIN_C_DECLS

/*============================================================================
 * Type definitions
 *============================================================================*/

typedef struct {

  cs_real_t   faraday_cst;
  cs_real_t   free_space_permit;
  cs_real_t   water_permit;
  cs_real_t   ionic_strength;
  cs_real_t   jamming_limit;
  cs_real_t   min_porosity;
  cs_real_t   phi1;
  cs_real_t   phi2;

  cs_real_t*  temperature;
  cs_real_t*  debye_length;
  cs_real_t   cstham;
  cs_real_t   dcutof;
  cs_real_t   lambwl;
  cs_real_t   kboltz;

} cs_lagr_clogging_param_t;

/*============================================================================
 * Public function definitions for Fortran API
 *============================================================================*/

/*----------------------------------------------------------------------------
 * Clogging initialization.
 *
 * - Retrieve various parameters for storing in global structure.
 * - Compute and store the Debye screening length
 *----------------------------------------------------------------------------*/

void
CS_PROCF (cloginit, CLOGINIT)(const cs_real_t   *faraday_cst,
                              const cs_real_t   *free_space_permit,
                              const cs_real_t   *water_permit,
                              const cs_real_t   *ionic_strength,
                              const cs_real_t   *jamming_limit,
                              const cs_real_t   *min_porosity,
                              const cs_real_t    temperature[],
                              const cs_real_t   *phi1,
                              const cs_real_t   *phi2,
                              const cs_real_t   *cstham,
                              const cs_real_t   *dcutof,
                              const cs_real_t   *lambwl,
                              const cs_real_t   *kboltz
);

/*============================================================================
 * Public function definitions
 *============================================================================*/

/*----------------------------------------------------------------------------
 * Clogging finalization.
 *
 * Deallocate the arrays storing temperature and Debye length.
 *----------------------------------------------------------------------------*/

void
cs_lagr_clogging_finalize(void);

/*----------------------------------------------------------------------------
 * Clogging barrier
 *
 * - Compute the number of deposited particles in contact with the depositing
 *   particle
 * - Re-compute the energy barrier if this number is greater than zero
 *----------------------------------------------------------------------------*/

int
cs_lagr_clogging_barrier(cs_lagr_particle_t    particle,
                         cs_lnum_t             face_id,
                         cs_real_t             face_area,
                         cs_real_t            *energy_barrier,
                         cs_real_t            *surface_coverage,
                         cs_real_t            *limit,
                         cs_real_t            *mporos);

/*----------------------------------------------------------------------------*/

END_C_DECLS

#endif /* __CS_LAGR_CLOGGING_H__ */

