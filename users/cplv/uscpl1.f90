!-------------------------------------------------------------------------------

!VERS


!     This file is part of the Code_Saturne Kernel, element of the
!     Code_Saturne CFD tool.

!     Copyright (C) 1998-2008 EDF S.A., France

!     contact: saturne-support@edf.fr

!     The Code_Saturne Kernel is free software; you can redistribute it
!     and/or modify it under the terms of the GNU General Public License
!     as published by the Free Software Foundation; either version 2 of
!     the License, or (at your option) any later version.

!     The Code_Saturne Kernel is distributed in the hope that it will be
!     useful, but WITHOUT ANY WARRANTY; without even the implied warranty
!     of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
!     GNU General Public License for more details.

!     You should have received a copy of the GNU General Public License
!     along with the Code_Saturne Kernel; if not, write to the
!     Free Software Foundation, Inc.,
!     51 Franklin St, Fifth Floor,
!     Boston, MA  02110-1301  USA

!-------------------------------------------------------------------------------

                  subroutine uscpl1
!================


!===============================================================================
!  FONCTION  :
!  ---------


!   SOUS-PROGRAMME DU MODULE LAGRANGIEN COUPLE CHARBON PULVERISE :
!   --------------------------------------------------------------

!    ROUTINE UTILISATEUR POUR PHYSIQUE PARTICULIERE

!      COMBUSTION EULERIENNE DE CHARBON PULVERISE ET
!      TRANSPORT LAGRANGIEN DES PARTICULES DE CHARBON

!    ROUTINE UTILISATEUR POUR ENTREE DES PARAMETRES DE CALCUL
!      (COMMONS)

!-------------------------------------------------------------------------------
! Arguments
!__________________.____._____.________________________________________________.
!    nom           !type!mode !                   role                         !
!__________________!____!_____!________________________________________________!
!__________________!____!_____!________________________________________________!

!     TYPE : E (ENTIER), R (REEL), A (ALPHANUMERIQUE), T (TABLEAU)
!            L (LOGIQUE)   .. ET TYPES COMPOSES (EX : TR TABLEAU REEL)
!     MODE : <-- donnee, --> resultat, <-> Donnee modifiee
!            --- tableau de travail
!===============================================================================

implicit none

!===============================================================================
!     DONNEES EN COMMON
!===============================================================================

include "paramx.h"
include "dimens.h"
include "numvar.h"
include "optcal.h"
include "cstphy.h"
include "entsor.h"
include "cstnum.h"
include "parall.h"
include "period.h"
include "ppppar.h"
include "ppthch.h"
include "coincl.h"
include "cpincl.h"
include "ppincl.h"

!===============================================================================

integer          ipp , icha

!===============================================================================

! TEST_A_ENLEVER_POUR_UTILISER_LE_SOUS_PROGRAMME_DEBUT
!===============================================================================
! 0.  CE TEST PERMET A L'UTILISATEUR D'ETRE CERTAIN QUE C'EST
!       SA VERSION DU SOUS PROGRAMME QUI EST UTILISEE
!       ET NON CELLE DE LA BIBLIOTHEQUE
!===============================================================================

if(1.eq.1) then
  write(nfecra,9000)
  call csexit (1)
endif

 9000 format(                                                           &
'@                                                            ',/,&
'@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@',/,&
'@                                                            ',/,&
'@ @@ ATTENTION : ARRET A L''ENTREE DES DONNEES               ',/,&
'@    =========                                               ',/,&
'@     COMBUSTION CHARBON PULVERISE COUPLE AU                 ',/,&
'@     TRANSPORT LAGRANGIEN DES PARTICULES DE CHARBON :       ',/,&
'@     LE SOUS-PROGRAMME UTILISATEUR uscpl1 DOIT ETRE COMPLETE',/,&
'@                                                            ',/,&
'@  Le calcul ne sera pas execute.                            ',/,&
'@                                                            ',/,&
'@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@',/,&
'@                                                            ',/)

!===============================================================================
! TEST_A_ENLEVER_POUR_UTILISER_LE_SOUS_PROGRAMME_FIN


!===============================================================================
! 1. VARIABLES TRANSPORTEES
!===============================================================================

!  Sortie chrono, suivi listing, sortie histo
!     Si l'on n'affecte pas les tableaux suivants,
!     les valeurs par defaut seront utilisees

!       ICHRVR( ) = sortie chono (oui 1/non 0)
!       ILISVR( ) = suivi listing (oui 1/non 0)
!       IHISVR( ) = sortie historique (nombre de sondes et numeros)
!       si IHISVR(.,1)  = -1 sortie sur toutes les sondes definies
!                            dans usini1


! --> Variables propres a la phase gaz continue

!      - Enthalpie de la phase gaz continue
ipp = ipprtp(isca(ihm))
ichrvr(ipp)  = 1
ilisvr(ipp)  = 1
ihisvr(ipp,1)= -1

! --> Variables propres a la phase continue

do icha = 1, ncharb

!       - Moyenne du traceur 1
!         (representatif des MV legeres du charbon ICHA)
  ipp = ipprtp(isca(if1m(icha)))
  ichrvr(ipp)  = 1
  ilisvr(ipp)  = 1
  ihisvr(ipp,1)= -1

!       - Moyenne du traceur 2
!         (representatif des MV lourdes du charbon ICHA)
  ipp = ipprtp(isca(if2m(icha)))
  ichrvr(ipp)  = 1
  ilisvr(ipp)  = 1
  ihisvr(ipp,1)= -1

enddo

!     - Moyenne du traceur 3 (representatif du C libere sous forme de CO
!       lors de la combustion heterogene)
ipp = ipprtp(isca(if3m))
ichrvr(ipp)  = 1
ilisvr(ipp)  = 1
ihisvr(ipp,1)= -1

!     - Variance associe au traceur 4 (representatif de l'air)
ipp = ipprtp(isca(if4p2m))
ichrvr(ipp)  = 1
ilisvr(ipp)  = 1
ihisvr(ipp,1)= -1

!===============================================================================
! 2. VARIABLES ALGEBRIQUES OU D'ETAT
!===============================================================================

!  Sortie chrono, suivi listing, sortie histo
!     Si l'on n'affecte pas les tableaux suivants,
!     les valeurs par defaut seront utilisees

!       ICHRVR( ) = sortie chono (oui 1/non 0)
!       ILISVR( ) = suivi listing (oui 1/non 0)
!       IHISVR( ) = sortie historique (nombre de sondes et numeros)
!       si IHISVR(.,1)  = -1 sortie sur toutes les sondes definies
!                            dans usini1

! --> Variables algebriques propres a la suspension gaz - particules

!     - Masse molaire du melange gazeux
ipp = ipppro(ipproc(immel))
ichrvr(ipp)   = 0
ilisvr(ipp)   = 0
ihisvr(ipp,1) = -1

!     - Temperature du melange gazeux
ipp = ipppro(ipproc(itemp1))
ichrvr(ipp)   = 1
ilisvr(ipp)   = 1
ihisvr(ipp,1) = -1

!     - Fraction massique (dans le melange gazeux) du CHx1m
ipp = ipppro(ipproc(iym1(1)))
ichrvr(ipp)   = 1
ilisvr(ipp)   = 1
ihisvr(ipp,1) = -1

!     - Fraction massique (dans le melange gazeux) du CHx2m
ipp = ipppro(ipproc(iym1(2)))
ichrvr(ipp)   = 1
ilisvr(ipp)   = 1
ihisvr(ipp,1) = -1

!     - Fraction massique (dans le melange gazeux) du CO
ipp = ipppro(ipproc(iym1(3)))
ichrvr(ipp)   = 1
ilisvr(ipp)   = 1
ihisvr(ipp,1) = -1

!     - Fraction massique (dans le melange gazeux) du O2
ipp = ipppro(ipproc(iym1(4)))
ichrvr(ipp)   = 1
ilisvr(ipp)   = 1
ihisvr(ipp,1) = -1

!     - Fraction massique (dans le melange gazeux) du CO2
ipp = ipppro(ipproc(iym1(5)))
ichrvr(ipp)   = 1
ilisvr(ipp)   = 1
ihisvr(ipp,1) = -1

!     - Fraction massique (dans le melange gazeux) du H2O
ipp = ipppro(ipproc(iym1(6)))
ichrvr(ipp)   = 1
ilisvr(ipp)   = 1
ihisvr(ipp,1) = -1

!     - Fraction massique (dans le melange gazeux) du N2
ipp = ipppro(ipproc(iym1(7)))
ichrvr(ipp)   = 1
ilisvr(ipp)   = 1
ihisvr(ipp,1) = -1


!===============================================================================
! 3. OPTIONS DE CALCUL
!===============================================================================

! --- Coefficient de relaxation de la masse volumique
!      RHO(n+1) = SRROM * RHO(n) + (1-SRROM) * RHO(n+1)

srrom = 0.8d0


!===============================================================================
! 4. CONSTANTES PHYSIQUES
!===============================================================================

! ---> Viscosite laminaire associee au scalaire enthalpie
!       DIFTL0 (diffusivite dynamique en kg/(m s))
diftl0 = 4.25d-5


!----
! FIN
!----

return

end
