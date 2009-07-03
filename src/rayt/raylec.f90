!-------------------------------------------------------------------------------

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

subroutine raylec &
!================

 ( idbia0 , idbra0 ,                                              &
   ndim   , ncelet , ncel   , nfac   , nfabor ,                   &
   nideve , nrdeve , nituse , nrtuse ,                            &
   idevel , ituser , ia     ,                                     &
   propce , propfb ,                                              &
   rdevel , rtuser , ra     )

!===============================================================================
! FONCTION :
! --------

!   SOUS-PROGRAMME DU MODULE RAYONNEMENT :
!   --------------------------------------

!         Lecture du fichier suite au 1er passage


!-------------------------------------------------------------------------------
! Arguments
!__________________.____._____.________________________________________________.
!    nom           !type!mode !                   role                         !
!__________________!____!_____!________________________________________________!
! idbia0           ! e  ! <-- ! numero de la 1ere case libre dans ia           !
! idbra0           ! e  ! <-- ! numero de la 1ere case libre dans ra           !
! ndim             ! e  ! <-- ! dimension de l'espace                          !
! ncelet           ! e  ! <-- ! nombre d'elements halo compris                 !
! ncel             ! e  ! <-- ! nombre d'elements actifs                       !
! nfac             ! e  ! <-- ! nombre de faces internes                       !
! nfabor           ! e  ! <-- ! nombre de faces de bord                        !
! nideve nrdeve    ! e  ! <-- ! longueur de idevel rdevel                      !
! nituse nrtuse    ! e  ! <-- ! longueur de ituser rtuser                      !
! idevel(nideve    ! te ! <-- ! tab entier complementaire developemt           !
! ituser(nituse    ! te ! <-- ! tab entier complementaire utilisateur          !
! ia(*)            ! tr ! --- ! macro tableau entier                           !
! propce           ! tr ! <-- ! proprietes physiques au centre des             !
! (ncelet,*)       !    !     !    cellules                                    !
! propfb           ! tr ! <-- ! proprietes physiques au centre des             !
!  (nfabor,*)      !    !     !    faces de bord                               !
! rdevel(nrdeve    ! tr ! <-- ! tab reel complementaire developemt             !
! rtuser(nrtuse    ! tr ! <-- ! tab reel complementaire utilisateur            !
! ra(*)            ! tr ! --- ! macro tableau reel                             !
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
include "numvar.h"
include "optcal.h"
include "pointe.h"
include "entsor.h"
include "cstphy.h"
include "ppppar.h"
include "ppthch.h"
include "cpincl.h"
include "ppincl.h"
include "radiat.h"

!===============================================================================

! Arguments

integer          idbia0 , idbra0
integer          ndim   , ncelet , ncel   , nfac   , nfabor
integer          nideve , nrdeve , nituse , nrtuse

integer          idevel(nideve), ituser(nituse), ia(*)


double precision rdevel(nrdeve), rtuser(nrtuse), ra(*)

double precision propce(ncelet,*)
double precision propfb(nfabor,*)
!
! VARIABLES LOCALES

character        rubriq*64
character        cphase(nphsmx)*2
integer          idebia, idebra
integer          iok

integer          jphast
integer          ncelok , nfaiok , nfabok , nsomok
integer          ierror , irtyp  , itysup , nbval
integer          ilecec , nberro , ivers
integer          impamr

!===============================================================================
!===============================================================================
! 0 - GESTION MEMOIRE
!===============================================================================

idebia = idbia0
idebra = idbra0

!===============================================================================
! 1. LECTURE DU FICHIER SUITE
!===============================================================================

if (isuird.eq.1) then

!  ---> Ouverture

    write(nfecra,6000)

!     (ILECEC=1:lecture)
    ilecec = 1
    call opnsui(ficamr,len(ficamr),ilecec,impamr,ierror)
    !==========
    if (ierror.ne.0) then
      write(nfecra,9011) ficamr
      call csexit (1)
    endif

    write(nfecra,6010)


!  ---> Type de fichier suite
!        Pourrait porter le numero de version si besoin.
!        On ne se sert pas de IVERS pour le moment

    itysup = 0
    nbval  = 1
    irtyp  = 1
    RUBRIQ = 'version_fichier_suite_rayonnement'
    call lecsui(impamr,rubriq,len(rubriq),itysup,nbval,irtyp,     &
                ivers,ierror)

    if (ierror.ne.0) then
      write(nfecra,9200)ficamr
      call csexit (1)
    endif


!  ---> Tests

    iok = 0

!     Dimensions des supports

    call tstsui(impamr,ncelok,nfaiok,nfabok,nsomok)
    !==========
    if (ncelok.eq.0) then
      write(nfecra,9210)
      iok = iok + 1
    endif
    if (nfabok.eq.0) then
      write(nfecra,9211)
      iok = iok + 1
    endif


!  ---> Pour test ulterieur si pb : arret

    nberro = 0

!  ---> Lecture des donnees

!     Aux faces de bord

      itysup = 3
      nbval  = 1
      irtyp  = 2

      RUBRIQ = 'tparoi_fb'
      call lecsui(impamr,rubriq,len(rubriq),itysup,nbval,irtyp,   &
           propfb(1,ipprob(itparo)),ierror)
      nberro=nberro+ierror

      RUBRIQ = 'qincid_fb'
      call lecsui(impamr,rubriq,len(rubriq),itysup,nbval,irtyp,   &
           propfb(1,ipprob(iqinci)),ierror)
      nberro=nberro+ierror

      RUBRIQ = 'hfconv_fb'
      call lecsui(impamr,rubriq,len(rubriq),itysup,nbval,irtyp,   &
           propfb(1,ipprob(ihconv)),ierror)
      nberro=nberro+ierror

     RUBRIQ = 'flconv_fb'
      call lecsui(impamr,rubriq,len(rubriq),itysup,nbval,irtyp,   &
           propfb(1,ipprob(ifconv)),ierror)
      nberro=nberro+ierror


!     Aux cellules

     itysup = 1
      nbval  = 1
      irtyp  = 2

      RUBRIQ = 'rayimp_ce'
      call lecsui(impamr,rubriq,len(rubriq),itysup,nbval,irtyp,   &
           propce(1,ipproc(itsri(1))),ierror)
      nberro=nberro+ierror

      RUBRIQ = 'rayexp_ce'
      call lecsui(impamr,rubriq,len(rubriq),itysup,nbval,irtyp,   &
           propce(1,ipproc(itsre(1))),ierror)
      nberro=nberro+ierror


!  ---> Si pb : arret

    if(nberro.ne.0) then
      write(nfecra,9100)
      call csexit (1)
    endif

    write(nfecra,6011)

!  ---> Fermeture du fichier suite

    call clssui(impamr,ierror)

    if (ierror.ne.0) then
      write(nfecra,8011) ficamr
    endif

    write(nfecra,6099)

! Fin détection suite rayonnement
endif

!--------
! FORMATS
!--------

 1110 format(                                                           &
'@                                                            ',/,&
'@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@',/,&
'@                                                            ',/,&
'@ @@ ATTENTION : LECTURE DU FICHIER SUITE RAYONNEMENT        ',/,&
'@    =========                                               ',/,&
'@      DONNEES AMONT ET ACTUELLES DIFFERENTES                ',/,&
'@                                                            ',/,&
'@    Le de phases qui rayonnent a ete modifie                ',/,&
'@                                                            ',/,&
'@    Le calcul ne peut etre execute.                         ',/,&
'@                                                            ',/,&
'@    Verifier le fichier suite rayonnement.                  ',/,&
'@                                                            ',/,&
'@               NPHAS                                        ',/,&
'@  AMONT : ',I10                                              ,/,&
'@  ACTUEL: ',I10                                              ,/,&
'@                                                            ',/,&
'@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@',/,&
'@                                                            ',/)

 6000 FORMAT (   3X,'** INFORMATIONS SUR LE MODULE DE RAYONNEMENT   ',/,&
           3X,'   ------------------------------------------  ',/,&
           3X,' Lecture d''un fichier suite                   '  )
 6010 FORMAT (   3X,'   Debut de la lecture                         '  )
 6011 FORMAT (   3X,'   Fin   de la lecture                         '  )
 6099 FORMAT (   3X,' Fin de la lecture du fichier suite            ',/,&
'                                                             ',/,&
'-------------------------------------------------------------',/)

 8011 format(                                                           &
'@                                                            ',/,&
'@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@',/,&
'@                                                            ',/,&
'@ @@ ATTENTION : ERREUR A LA FERMETURE DU FICHIER SUITE      ',/,&
'@    =========   RAYONNEMENT                                 ',/,&
'@                                                            ',/,&
'@    Probleme sur le fichier de nom (',A13,')                ',/,&
'@                                                            ',/,&
'@    Le calcul se poursuit...                                ',/,&
'@                                                            ',/,&
'@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@',/,&
'@                                                            ',/)
 9011 format(                                                           &
'@                                                            ',/,&
'@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@',/,&
'@                                                            ',/,&
'@ @@ ATTENTION : ARRET A LA LECTURE DU FICHIER SUITE         ',/,&
'@    =========   RAYONNEMENT                                 ',/,&
'@      ERREUR A L''OUVERTURE DU FICHIER SUITE                ',/,&
'@                                                            ',/,&
'@    Le calcul ne peut pas etre execute.                     ',/,&
'@                                                            ',/,&
'@    Verifier l''existence et le nom (',A13,') du            ',/,&
'@        fichier suite dans le repertoire de travail.        ',/,&
'@                                                            ',/,&
'@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@',/)
 9200 format(                                                           &
'@                                                            ',/,&
'@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@',/,&
'@                                                            ',/,&
'@ @@ ATTENTION : ARRET A LA LECTURE DU FICHIER SUITE         ',/,&
'@    =========                                    RAYONNEMENT',/,&
'@      TYPE DE FICHIER INCORRECT                             ',/,&
'@                                                            ',/,&
'@    Le fichier ',A13      ,' ne semble pas etre un fichier  ',/,&
'@      suite rayonnement.                                    ',/,&
'@                                                            ',/,&
'@    Le calcul ne peut etre execute.                         ',/,&
'@                                                            ',/,&
'@    Verifier que le fichier suite utilise correspond bien   ',/,&
'@        a un fichier suite rayonnement.                     ',/,&
'@                                                            ',/,&
'@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@',/,&
'@                                                            ',/)
 9210 format(                                                           &
'@                                                            ',/,&
'@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@',/,&
'@                                                            ',/,&
'@ @@ ATTENTION : ARRET A LA LECTURE DU FICHIER SUITE         ',/,&
'@    =========   RAYONNEMENT                                 ',/,&
'@      DONNEES AMONT ET ACTUELLES INCOHERENTES               ',/,&
'@                                                            ',/,&
'@    Le nombre de cellules a ete modifie                     ',/,&
'@                                                            ',/,&
'@    Le calcul ne peut etre execute.                         ',/,&
'@                                                            ',/,&
'@                                                            ',/,&
'@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@',/,&
'@                                                            ',/)
 9211 format(                                                           &
'@                                                            ',/,&
'@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@',/,&
'@                                                            ',/,&
'@ @@ ATTENTION : ARRET A LA LECTURE DU FICHIER SUITE         ',/,&
'@    =========   RAYONNEMENT                                 ',/,&
'@      DONNEES AMONT ET ACTUELLES INCOHERENTES               ',/,&
'@                                                            ',/,&
'@    Le nombre de faces de bord a ete modifie                ',/,&
'@                                                            ',/,&
'@    Le calcul ne peut etre execute.                         ',/,&
'@                                                            ',/,&
'@                                                            ',/,&
'@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@',/,&
'@                                                            ',/)
 9100 format(                                                           &
'@                                                            ',/,&
'@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@',/,&
'@                                                            ',/,&
'@ @@ ATTENTION: ARRET A LA LECTURE DU FICHIER SUITE          ',/,&
'@    =========   RAYONNEMENT                                 ',/,&
'@      ERREUR LORS DE LA LECTURE DES DONNEES                 ',/,&
'@                                                            ',/,&
'@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@',/,&
'@                                                            ',/)

!----
! FIN
!----

return

end
