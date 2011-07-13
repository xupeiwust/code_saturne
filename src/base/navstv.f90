!-------------------------------------------------------------------------------

!     This file is part of the Code_Saturne Kernel, element of the
!     Code_Saturne CFD tool.

!     Copyright (C) 1998-2010 EDF S.A., France

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

subroutine navstv &
!================

 ( nvar   , nscal  , iterns , icvrge ,                            &
   isostd ,                                                       &
   dt     , tpucou , rtp    , rtpa   , propce , propfa , propfb , &
   tslagr , coefa  , coefb  , frcxt  ,                            &
   trava  , ximpa  , uvwk   )

!===============================================================================
! FONCTION :
! ----------

! Solving of NS equations for incompressible or slightly compressible flows for
! one time step. Both convection-diffusion and continuity steps are performed.
! The velocity components are solved together in once.

!-------------------------------------------------------------------------------
!ARGU                             ARGUMENTS
!__________________.____._____.________________________________________________.
! name             !type!mode ! role                                           !
!__________________!____!_____!________________________________________________!
! nvar             ! i  ! <-- ! total number of variables                      !
! nscal            ! i  ! <-- ! total number of scalars                        !
! iterns           ! e  ! <-- ! numero d'iteration sur navsto                  !
! icvrge           ! e  ! <-- ! indicateur de convergence du pnt fix           !
! isostd           ! te ! <-- ! indicateur de sortie standard                  !
!    (nfabor+1)    !    !     !  +numero de la face de reference               !
! dt(ncelet)       ! ra ! <-- ! time step (per cell)                           !
! tpucou(ncelet,3) ! ra ! <-- ! velocity-pressure coupling (interleaved)       !
! rtp, rtpa        ! ra ! <-- ! calculated variables at cell centers           !
!  (ncelet, *)     !    !     !  (at current and previous time steps)          !
! propce(ncelet, *)! ra ! <-- ! physical properties at cell centers            !
! propfa(nfac, *)  ! ra ! <-- ! physical properties at interior face centers   !
! propfb(nfabor, *)! ra ! <-- ! physical properties at boundary face centers   !
! coefa, coefb     ! ra ! <-- ! boundary conditions                            !
!  (nfabor, *)     !    !     !                                                !
! frcxt(ncelet,3)  ! tr ! <-- ! force exterieure generant la pression          !
!                  !    !     !  hydrostatique                                 !
! tslagr           ! tr ! <-- ! terme de couplage retour du                    !
!(ncelet,*)        !    !     !     lagrangien                                 !
! trava            ! tr ! <-- ! tableau de travail pour couplage               !
!(3,ncelet)        !    !     ! vitesse pression par point fixe                !
! ximpa            ! tr ! <-- ! tableau de travail pour couplage               !
!(3,3,ncelet)      !    !     ! vitesse pression par point fixe                !
! uvwk             ! tr ! <-- ! tableau de travail pour couplage u/p           !
!(3,ncelet)        !    !     ! sert a stocker la vitesse de                   !
!                  !    !     ! l'iteration precedente                         !
!__________________!____!_____!________________________________________________!

!     TYPE : E (ENTIER), R (REEL), A (ALPHANUMERIQUE), T (TABLEAU)
!            L (LOGIQUE)   .. ET TYPES COMPOSES (EX : TR TABLEAU REEL)
!     MODE : <-- donnee, --> resultat, <-> Donnee modifiee
!            --- tableau de travail

!===============================================================================

!===============================================================================
! Module files
!===============================================================================

use paramx
use dimens, only: ndimfb
use numvar
use entsor
use cstphy
use cstnum
use optcal
use pointe
use albase
use parall
use period
use ppppar
use ppthch
use ppincl
use cplsat
use mesh

!===============================================================================

implicit none

! Arguments

integer          nvar   , nscal  , iterns , icvrge

integer          isostd(nfabor+1)

double precision dt(ncelet), tpucou(ncelet,3), rtp(ncelet,*), rtpa(ncelet,*)
double precision propce(ncelet,*)
double precision propfa(nfac,*), propfb(ndimfb,*)
double precision tslagr(ncelet,*)
double precision coefa(ndimfb,*), coefb(ndimfb,*)
double precision frcxt(ncelet,3)
double precision trava(ndim,ncelet),ximpa(ndim,ndim,ncelet)
double precision uvwk(ndim,ncelet)

! Local variables

integer          iccocg, inc, iel, iel1, iel2, ifac, imax
integer          ii    , inod
integer          isou, ivar, iitsm
integer          iclipr, iclipf
integer          icliup, iclivp, icliwp, init
integer          icluma, iclvma, iclwma
integer          iflmas, iflmab, ipcrom, ipbrom
integer          iflms1, iflmb1, iflmb0
integer          nswrgp, imligp, iwarnp
integer          nbrval, iappel, iescop, idtsca
integer          ndircp, icpt  , iecrw
integer          numcpl
double precision rnorm , rnorma, rnormi, vitnor
double precision dtsrom, unsrom, surf  , rhom, rovolsdt
double precision epsrgp, climgp, extrap, xyzmax(3)
double precision thetap, xdu, xdv, xdw
double precision xxp0 , xyp0 , xzp0
double precision rhofac, dtfac, ddepx , ddepy, ddepz
double precision xnrdis
double precision vitbox, vitboy, vitboz

double precision rvoid(1)

double precision, allocatable, dimension(:), target :: viscf, viscb
double precision, allocatable, dimension(:), target :: wvisfi, wvisbi
double precision, allocatable, dimension(:) :: drtp, smbr, rovsdt
double precision, allocatable, dimension(:,:) :: trav
double precision, allocatable, dimension(:) :: w1, w2, w3
double precision, allocatable, dimension(:) :: w4, w5, w6
double precision, allocatable, dimension(:) :: w7, w8, w9
double precision, allocatable, dimension(:) :: w10
double precision, allocatable, dimension(:,:) :: dfrcxt
double precision, allocatable, dimension(:,:) :: frchy, dfrchy
double precision, allocatable, dimension(:) :: esflum, esflub

double precision, pointer, dimension(:) :: viscfi => null(), viscbi => null()

double precision, dimension(:,:), allocatable :: gradp
double precision, dimension(:,:), allocatable :: vel
double precision, dimension(:,:), allocatable :: vela
double precision, dimension(:,:), allocatable :: tpucov

!===============================================================================

!===============================================================================
! 1.  INITIALISATION
!===============================================================================

! Allocate temporary arrays for the velocity-pressure resolution
allocate(viscf(nfac), viscb(nfabor))
allocate(drtp(ncelet), smbr(ncelet), rovsdt(ncelet))
allocate(trav(3,ncelet))
allocate(vela(3,ncelet))
allocate(vel(3,ncelet))
if (ipucou.eq.1 .or. ncpdct.gt.0) then 
  allocate(tpucov(3,ncelet))
endif

! Allocate other arrays, depending on user options
!if (iphydr.eq.1) allocate(dfrcxt(ncelet,3))
allocate(dfrcxt(ncelet,3))
if (icalhy.eq.1) allocate(frchy(ncelet,ndim), dfrchy(ncelet,ndim))
if (iescal(iestot).gt.0) allocate(esflum(nfac), esflub(nfabor))
if (itytur.eq.3.and.irijnu.eq.1) then
  allocate(wvisfi(nfac), wvisbi(nfabor))
  viscfi => wvisfi(1:nfac)
  viscbi => wvisbi(1:nfabor)
else
  viscfi => viscf(1:nfac)
  viscbi => viscb(1:nfabor)
endif

! Allocate work arrays
allocate(w1(ncelet), w2(ncelet), w3(ncelet))
allocate(w4(ncelet), w5(ncelet), w6(ncelet))
allocate(w7(ncelet), w8(ncelet), w9(ncelet))
if (irnpnw.eq.1) allocate(w10(ncelet))

! Interleaved value of vel and vela and tpucou
do iel = 1, ncelet
  vel (1,iel) = rtp (iel,iu)
  vel (2,iel) = rtp (iel,iv)
  vel (3,iel) = rtp (iel,iw)
  vela(1,iel) = rtpa(iel,iu)
  vela(2,iel) = rtpa(iel,iv)
  vela(3,iel) = rtpa(iel,iw)
enddo

if(iwarni(iu).ge.1) then
  write(nfecra,1000)
endif

! Initialize variables to avoid compiler warnings

ivar = 0
iflmas = 0
ipcrom = 0
imax = 0

! Memoire

if(nterup.gt.1) then

  do iel = 1,ncelet
    do isou = 1, 3
    !     La boucle sur NCELET est une securite au cas
    !       ou on utiliserait UVWK par erreur a ITERNS = 1
      uvwk(isou,iel) = vel(isou,iel)
    enddo
  enddo

  ! Calcul de la norme L2 de la vitesse
  if(iterns.eq.1) then
    xnrmu0 = 0.d0
    do iel = 1, ncel
      xnrmu0 = xnrmu0 +(vela(1,iel)**2        &
                      + vela(2,iel)**2        &
                      + vela(3,iel)**2)       &
                      * volume(iel)
    enddo
    if(irangp.ge.0) then
      call parsom (xnrmu0)
      !==========
    endif
    ! En cas de couplage entre deux instances de Code_Saturne, on calcule
    ! la norme totale de la vitesse
    ! Necessaire pour que l'une des instances ne stoppe pas plus tot que les autres
    ! (il faudrait quand meme verifier les options numeriques, ...)
    do numcpl = 1, nbrcpl
      call tbrcpl ( numcpl, 1, 1, xnrmu0, xnrdis )
      !==========
      xnrmu0 = xnrmu0 + xnrdis
    enddo
    xnrmu0 = sqrt(xnrmu0)
  endif

  ! On assure la periodicite ou le parallelisme de UVWK et la pression
  ! (cette derniere vaut la pression a l'iteration precedente)
  if(iterns.gt.1) then
    if (irangp.ge.0.or.iperio.eq.1) then
      call synvin(uvwk(1,1))
      !==========
      call synsca(rtpa(1,ipr))
      !==========
    endif
  endif

endif


!===============================================================================
! 2.  ETAPE DE PREDICTION DES VITESSES
!===============================================================================

iappel = 1
iflmas = ipprof(ifluma(iu))
iflmab = ipprob(ifluma(iu))

call predvv &
!==========
( iappel ,                                                       &
  nvar   , nscal  , iterns ,                                     &
  ncepdc , ncetsm ,                                              &
  icepdc , icetsm , itypsm ,                                     &
  dt     , rtp    , rtpa   , vel    , vela   ,                   &
  propce , propfa , propfb ,                                     &
  propfa(1,iflmas), propfb(1,iflmab),                            &
  tslagr , coefa  , coefb  , coefau , coefbu , cofafu , cofbfu , &
  ckupdc , smacel , frcxt ,                                      &
  trava  , ximpa  , uvwk   , dfrcxt , tpucov ,  trav  ,          &
  viscf  , viscb  , viscfi , viscbi ,                            &
  w1     , w7     , w8     , w9     , w10    )

! --- Sortie si pas de pression continuite
!       on met a jour les flux de masse, et on sort

if( iprco.le.0 ) then

  icliup = iclrtp(iu,icoef)
  iclivp = iclrtp(iv,icoef)
  icliwp = iclrtp(iw,icoef)

  iflmas = ipprof(ifluma(iu))
  iflmab = ipprob(ifluma(iu))
  ipcrom = ipproc(irom)
  ipbrom = ipprob(irom)

  init   = 1
  inc    = 1
  iccocg = 1
  iflmb0 = 1
  if (iale.eq.1) iflmb0 = 0
  nswrgp = nswrgr(iu)
  imligp = imligr(iu)
  iwarnp = iwarni(iu)
  epsrgp = epsrgr(iu)
  climgp = climgr(iu)
  extrap = extrag(iu)

  call inimav                                                     &
  !==========
 ( nvar   , nscal  ,                                              &
   iu     ,                                                       &
   iflmb0 , init   , inc    , imrgra ,iccocg , nswrgp , imligp ,  &
   iwarnp , nfecra ,                                              &
   epsrgp , climgp , extrap ,                                     &
   propce(1,ipcrom), propfb(1,ipbrom),                            &
   vel    ,                                                       &
   coefau , coefbu ,                                              &
   propfa(1,iflmas), propfb(1,iflmab)  )


  ! Ajout de la vitesse du solide dans le flux convectif,
  ! si le maillage est mobile (solide rigide)
  ! En turbomachine, on conna�t exactement la vitesse de maillage � ajouter
  if (imobil.eq.1) then

    iflmas = ipprof(ifluma(iu))
    iflmab = ipprob(ifluma(iu))
    ipcrom = ipproc(irom)
    ipbrom = ipprob(irom)

    do ifac = 1, nfac
      iel1 = ifacel(1,ifac)
      iel2 = ifacel(2,ifac)
      dtfac  = 0.5d0*(dt(iel1) + dt(iel2))
      rhofac = 0.5d0*(propce(iel1,ipcrom) + propce(iel2,ipcrom))
      vitbox = omegay*cdgfac(3,ifac) - omegaz*cdgfac(2,ifac)
      vitboy = omegaz*cdgfac(1,ifac) - omegax*cdgfac(3,ifac)
      vitboz = omegax*cdgfac(2,ifac) - omegay*cdgfac(1,ifac)
      propfa(ifac,iflmas) = propfa(ifac,iflmas) - rhofac*(        &
           vitbox*surfac(1,ifac) + vitboy*surfac(2,ifac) + vitboz*surfac(3,ifac) )
    enddo
    do ifac = 1, nfabor
      iel = ifabor(ifac)
      dtfac  = dt(iel)
      rhofac = propfb(ifac,ipbrom)
      vitbox = omegay*cdgfbo(3,ifac) - omegaz*cdgfbo(2,ifac)
      vitboy = omegaz*cdgfbo(1,ifac) - omegax*cdgfbo(3,ifac)
      vitboz = omegax*cdgfbo(2,ifac) - omegay*cdgfbo(1,ifac)
      propfb(ifac,iflmab) = propfb(ifac,iflmab) - rhofac*(        &
           vitbox*surfbo(1,ifac) + vitboy*surfbo(2,ifac) + vitboz*surfbo(3,ifac) )
    enddo

  endif

  return

endif

!===============================================================================
! 3.  ETAPE DE PRESSION/CONTINUITE ( VITESSE/PRESSION )
!===============================================================================

if(iwarni(iu).ge.1) then
  write(nfecra,1200)
endif

! --- Pas de temps scalaire ou pas
idtsca = 0
if ((ipucou.eq.1).or.(ncpdct.gt.0)) idtsca = 1

call resopv &
!==========
 ( nvar   , nscal  ,                                              &
   ncepdc , ncetsm ,                                              &
   icepdc , icetsm , itypsm ,                                     &
   isostd , idtsca ,                                              &
   dt     , rtp    , rtpa   , vel    , vela   ,                   &
   propce , propfa , propfb ,                                     &
   coefa  , coefb  , coefau , coefbu ,                            &
   ckupdc , smacel ,                                              &
   frcxt  , dfrcxt , tpucov , trav   ,                            &
   viscf  , viscb  , viscfi , viscbi ,                            &
   drtp   , smbr   , rovsdt , tslagr ,                            &
   frchy  , dfrchy , trava  )


!===============================================================================
! 4.  REACTUALISATION DU CHAMP DE VITESSE
!===============================================================================

iclipr = iclrtp(ipr,icoef)
iclipf = iclrtp(ipr,icoeff)
icliup = iclrtp(iu ,icoef)
iclivp = iclrtp(iv ,icoef)
icliwp = iclrtp(iw ,icoef)

iflmas = ipprof(ifluma(iu))
iflmab = ipprob(ifluma(iu))
ipcrom = ipproc(irom  )
ipbrom = ipprob(irom  )


!       IREVMC = 0 : Only the standard method is available for the coupled
!                    version of navstv.

!     The predicted velocity is corrected by the cell gradient of the
!     pressure increment.

!     GRADIENT DE L'INCREMENT TOTAL DE PRESSION

if (idtvar.lt.0) then
  do iel = 1, ncel
    drtp(iel) = (rtp(iel,ipr) -rtpa(iel,ipr)) / relaxv(ipr)
  enddo
else
  do iel = 1, ncel
    drtp(iel) = rtp(iel,ipr) -rtpa(iel,ipr)
  enddo
endif

! --->    TRAITEMENT DU PARALLELISME ET DE LA PERIODICITE

if (irangp.ge.0.or.iperio.eq.1) then
  call synsca(drtp)
  !==========
endif


iccocg = 1
inc = 0
if (iphydr.eq.1) inc = 1
nswrgp = nswrgr(ipr)
imligp = imligr(ipr)
iwarnp = iwarni(ipr)
epsrgp = epsrgr(ipr)
climgp = climgr(ipr)
extrap = extrag(ipr)

!Allocation
allocate(gradp (ncelet,3))

call grdpot &
!==========
( ipr , imrgra , inc    , iccocg , nswrgp , imligp , iphydr ,    &
 iwarnp , nfecra , epsrgp , climgp , extrap ,                   &
 rvoid  ,                                                       &
 dfrcxt(1,1),dfrcxt(1,2),dfrcxt(1,3),                           &
 drtp   , coefa(1,iclipf) , coefb(1,iclipr)  ,                  &
 gradp  )

!     REACTUALISATION DU CHAMP DE VITESSES

thetap = thetav(ipr)
do iel = 1, ncelet
  do isou = 1, 3
    trav(isou,iel) = gradp(iel,isou)
  enddo
enddo

!Free memory
deallocate(gradp)


!     REACTUALISATION DU CHAMP DE VITESSES

thetap = thetav(ipr)
if (iphydr.eq.0) then
  if (idtsca.eq.0) then
    do iel = 1, ncel
      dtsrom = -thetap*dt(iel)/propce(iel,ipcrom)
      do isou = 1, 3
        vel(isou,iel) = vel(isou,iel)+dtsrom*trav(isou,iel)
      enddo
    enddo
  else
    do iel = 1, ncel
      unsrom = -thetap/propce(iel,ipcrom)
      ! tpucov is an interleaved array
      do isou = 1, 3
        vel(isou,iel) = vel(isou,iel) + unsrom*tpucov(isou,iel)*trav(isou,iel)
      enddo
    enddo
  endif
else
  if (idtsca.eq.0) then
    do iel = 1, ncel
      dtsrom = thetap*dt(iel)/propce(iel,ipcrom)
      do isou = 1, 3
        vel(isou,iel) = vel(isou,iel)                           &
           +dtsrom*(dfrcxt(iel,isou)-trav(isou,iel) )
      enddo
    enddo
  else
    do iel = 1, ncel
      unsrom = thetap/propce(iel,ipcrom)
      ! tpucov is an interleaved array
      do isou = 1, 3
        vel(isou,iel) = vel(isou,iel)                         &
             +unsrom*tpucov(isou,iel)                         &
             *(dfrcxt(iel,isou)-trav(isou,iel))
      enddo
    enddo
  endif
!     mise a jour des forces exterieures pour le calcul des gradients
  do iel=1,ncel
    frcxt(iel,1) = frcxt(iel,1) + dfrcxt(iel,1)
    frcxt(iel,2) = frcxt(iel,2) + dfrcxt(iel,2)
    frcxt(iel,3) = frcxt(iel,3) + dfrcxt(iel,3)
  enddo
  if (irangp.ge.0.or.iperio.eq.1) then
    call synvec(frcxt(1,1), frcxt(1,2), frcxt(1,3))
    !==========
  endif
  !     mise a jour des Dirichlets de pression en sortie dans COEFA
  iclipr = iclrtp(ipr,icoef)
  iclipf = iclrtp(ipr,icoeff)
  do ifac = 1,nfabor
    if (isostd(ifac).eq.1)                              &
         coefa(ifac,iclipr) = coefa(ifac,iclipr)        &
         + coefa(ifac,iclipf)
  enddo
endif


!===============================================================================
! 5.  CALCUL D'UN ESTIMATEUR D'ERREUR DE L'ETAPE DE CORRECTION ET TOTAL
!===============================================================================

if(iescal(iescor).gt.0.or.iescal(iestot).gt.0) then

  ! ---> REPERAGE DES VARIABLES

  icliup = iclrtp(iu,icoef)
  iclivp = iclrtp(iv,icoef)
  icliwp = iclrtp(iw,icoef)

  ipcrom = ipproc(irom)
  ipbrom = ipprob(irom)


  ! ---> ECHANGE DES VITESSES ET PRESSION EN PERIODICITE ET PARALLELISME

  !    Pour les estimateurs IESCOR et IESTOT, la vitesse doit etre echangee.

  !    Pour l'estimateur IESTOT, la pression doit etre echangee aussi.

  !    Cela ne remplace pas l'echange du debut de pas de temps
  !     a cause de usproj qui vient plus tard et des calculs suite)


  ! --- Vitesse

  if (irangp.ge.0.or.iperio.eq.1) then
    call synvin(vel)
    !==========
  endif

  !  -- Pression

  if(iescal(iestot).gt.0) then

    if (irangp.ge.0.or.iperio.eq.1) then
      call synsca(rtp(1,ipr))
      !==========
    endif

  endif


  ! ---> CALCUL DU FLUX DE MASSE DEDUIT DE LA VITESSE REACTUALISEE

  init   = 1
  inc    = 1
  iccocg = 1
  iflmb0 = 1
  if (iale.eq.1) iflmb0 = 0
  nswrgp = nswrgr(iu)
  imligp = imligr(iu)
  iwarnp = iwarni(iu)
  epsrgp = epsrgr(iu)
  climgp = climgr(iu)
  extrap = extrag(iu)

  call inimav                                                     &
  !==========
 ( nvar   , nscal  ,                                              &
   iu     ,                                                       &
   iflmb0 , init   , inc    , imrgra , iccocg , nswrgp , imligp , &
   iwarnp , nfecra ,                                              &
   epsrgp , climgp , extrap ,                                     &
   propce(1,ipcrom), propfb(1,ipbrom),                            &
   vel    ,                                                       &
   coefau , coefbu ,                                              &
   esflum , esflub )


  ! ---> CALCUL DE L'ESTIMATEUR CORRECTION : DIVERGENCE DE ROM * U (N + 1)
  !                                          - GAMMA

  if(iescal(iescor).gt.0) then
    init = 1
    call divmas(ncelet,ncel,nfac,nfabor,init,nfecra,            &
         ifacel,ifabor,esflum,esflub,w1)

    if (ncetsm.gt.0) then
      do iitsm = 1, ncetsm
        iel = icetsm(iitsm)
        w1(iel) = w1(iel)-volume(iel)*smacel(iitsm,ipr)
      enddo
    endif

    if(iescal(iescor).eq.2) then
      iescop = ipproc(iestim(iescor))
      do iel = 1, ncel
        propce(iel,iescop) =  abs(w1(iel))
      enddo
    elseif(iescal(iescor).eq.1) then
      iescop = ipproc(iestim(iescor))
      do iel = 1, ncel
        propce(iel,iescop) =  abs(w1(iel)) / volume(iel)
      enddo
    endif
  endif


  ! ---> CALCUL DE L'ESTIMATEUR TOTAL

  if(iescal(iestot).gt.0) then

    !   INITIALISATION DE TRAV AVEC LE TERME INSTATIONNAIRE

    do iel = 1, ncel
      rovolsdt = propce(iel,ipcrom)*volume(iel)/dt(iel)
      do isou = 1, 3
        trav(isou,iel) = rovolsdt *                               &
                 ( vela(isou,iel)- vel(isou,iel) )
      enddo
    enddo

    !   APPEL A PREDUV AVEC RTP ET RTP AU LIEU DE RTP ET RTPA
    !                  AVEC LE FLUX DE MASSE RECALCULE
    iappel = 2
    call predvv &
    !==========
 ( iappel ,                                                       &
   nvar   , nscal  , iterns ,                                     &
   ncepdc , ncetsm ,                                              &
   icepdc , icetsm , itypsm ,                                     &
   dt     , rtp    , rtp    , vel    , vel    ,                   &
   propce , propfa , propfb ,                                     &
   esflum , esflub ,                                              &
   tslagr , coefa  , coefb  , coefau , coefbu , cofafu , cofbfu , &
   ckupdc , smacel , frcxt  ,                                     &
   trava  , ximpa  , uvwk   , dfrcxt , tpucov , trav   ,          &
   viscf  , viscb  , viscfi , viscbi ,                            &
   w1     , w7     , w8     , w9     , w10    )

  endif

endif

!===============================================================================
! 6.  TRAITEMENT DU POINT FIXE SUR LE SYSTEME VITESSE/PRESSION
!===============================================================================

if(nterup.gt.1) then
! TEST DE CONVERGENCE DE L'ALGORITHME ITERATIF
! On initialise ICVRGE a 1 et on le met a 0 si une des phases n'est
! pas convergee

  icvrge = 1

  do iel = 1,ncel
    xdu = vel(1,iel) - uvwk(1,iel)
    xdv = vel(2,iel) - uvwk(2,iel)
    xdw = vel(3,iel) - uvwk(3,iel)
    xnrmu = xnrmu +(xdu**2 + xdv**2 + xdw**2)     &
                                * volume(iel)
  enddo
  ! --->    TRAITEMENT DU PARALLELISME

  if(irangp.ge.0) call parsom (xnrmu)
                              !==========
  ! -- >    TRAITEMENT DU COUPLAGE ENTRE DEUX INSTANCES DE CODE_SATURNE
  do numcpl = 1, nbrcpl
    call tbrcpl ( numcpl, 1, 1, xnrmu, xnrdis )
    !==========
    xnrmu = xnrmu + xnrdis
  enddo
  xnrmu = sqrt(xnrmu)

  ! Indicateur de convergence du point fixe
  if(xnrmu.ge.epsup*xnrmu0) icvrge = 0

endif

! ---> RECALAGE DE LA PRESSION SUR UNE PRESSION A MOYENNE NULLE
!  On recale si on n'a pas de Dirichlet. Or le nombre de Dirichlets
!  calcule dans typecl.F est NDIRCL si IDIRCL=1 et NDIRCL-1 si IDIRCL=0
!  (ISTAT vaut toujours 0 pour la pression)

if (idircl(ipr).eq.1) then
  ndircp = ndircl(ipr)
else
  ndircp = ndircl(ipr)-1
endif
if(ndircp.le.0) then
  call prmoy0 &
  !==========
( ncelet , ncel   , nfac   , nfabor ,                         &
  volume , rtp(1,ipr) )
endif

! Calcul de la pression totale IPRTOT : (definie comme propriete )
! En compressible, la pression resolue est deja la pression totale

if (ippmod(icompf).lt.0) then
  xxp0   = xyzp0(1)
  xyp0   = xyzp0(2)
  xzp0   = xyzp0(3)
  do iel=1,ncel
    propce(iel,ipproc(iprtot))= rtp(iel,ipr)           &
         + ro0*( gx*(xyzcen(1,iel)-xxp0)               &
         + gy*(xyzcen(2,iel)-xyp0)                     &
         + gz*(xyzcen(3,iel)-xzp0) )                   &
         + p0 - pred0
  enddo
endif

!===============================================================================
! 7.  IMPRESSIONS
!===============================================================================

iflmas = ipprof(ifluma(iu))
iflmab = ipprob(ifluma(iu))
ipcrom = ipproc(irom)
ipbrom = ipprob(irom)

if (iwarni(iu).ge.1) then

  write(nfecra,2000)

  rnorm = -1.d0
  do iel = 1, ncel
    rnorm  = max(rnorm,abs(rtp(iel,ipr)))
  enddo
  if (irangp.ge.0) call parmax (rnorm)
                   !==========
  write(nfecra,2100)rnorm

  rnorm = -1.d0
  do iel = 1, ncel
    vitnor =                                                    &
     sqrt(vel(1,iel)**2+vel(2,iel)**2+vel(3,iel)**2)
    if(vitnor.ge.rnorm) then
      rnorm = vitnor
      imax  = iel
    endif
  enddo

  xyzmax(1) = xyzcen(1,imax)
  xyzmax(2) = xyzcen(2,imax)
  xyzmax(3) = xyzcen(3,imax)

  if (irangp.ge.0) then
    nbrval = 3
    call parmxl (nbrval, rnorm, xyzmax)
    !==========
  endif

  write(nfecra,2200) rnorm,xyzmax(1),xyzmax(2),xyzmax(3)


  ! Pour la periodicite et le parallelisme, rom est echange dans phyvar


  rnorma = -grand
  rnormi =  grand
  do ifac = 1, nfac
    iel1 = ifacel(1,ifac)
    iel2 = ifacel(2,ifac)
    surf = surfan(ifac)
    rhom = (propce(iel1,ipcrom)+propce(iel2,ipcrom))*0.5d0
    rnorm = propfa(ifac,iflmas)/(surf*rhom)
    rnorma = max(rnorma,rnorm)
    rnormi = min(rnormi,rnorm)
  enddo
  if (irangp.ge.0) then
    call parmax (rnorma)
    !==========
    call parmin (rnormi)
    !==========
  endif
  write(nfecra,2300)rnorma, rnormi

  rnorma = -grand
  rnormi =  grand
  do ifac = 1, nfabor
    rnorm = propfb(ifac,iflmab)/(surfbn(ifac)*propfb(ifac,ipbrom))
    rnorma = max(rnorma,rnorm)
    rnormi = min(rnormi,rnorm)
  enddo
  if (irangp.ge.0) then
    call parmax (rnorma)
    !==========
    call parmin (rnormi)
    !==========
  endif
  write(nfecra,2400)rnorma, rnormi

  rnorm = 0.d0
  do ifac = 1, nfabor
    rnorm = rnorm + propfb(ifac,iflmab)
  enddo

  if (irangp.ge.0) call parsom (rnorm)
                   !==========

  write(nfecra,2500)rnorm

  write(nfecra,2001)

  if(nterup.gt.1) then
    if(icvrge.eq.0) then
      write(nfecra,2600) iterns
      write(nfecra,2601) xnrmu, xnrmu0, epsup
      write(nfecra,2001)
      if(iterns.eq.nterup) then
        write(nfecra,2603)
        write(nfecra,2001)
      endif
    else
      write(nfecra,2602) iterns
      write(nfecra,2601) xnrmu, xnrmu0, epsup
      write(nfecra,2001)
    endif
  endif

endif

! Free memory
deallocate(viscf, viscb)
deallocate(drtp, smbr, rovsdt)
deallocate(trav)
if (allocated(dfrcxt)) deallocate(dfrcxt)
if (allocated(frchy))  deallocate(frchy, dfrchy)
if (allocated(esflum)) deallocate(esflum, esflub)
if (allocated(wvisfi)) deallocate(wvisfi, wvisbi)
deallocate(w1, w2, w3)
deallocate(w4, w5, w6)
deallocate(w7, w8, w9)
if (allocated(w10)) deallocate(w10)

! Interleaved values of vel and vela

do iel = 1, ncelet
  rtp (iel,iu) = vel (1,iel)
  rtp (iel,iv) = vel (2,iel)
  rtp (iel,iw) = vel (3,iel)
  rtpa(iel,iu) = vela(1,iel)
  rtpa(iel,iv) = vela(2,iel)
  rtpa(iel,iw) = vela(3,iel)
  if (ipucou.eq.1 .or. ncpdct.gt.0) then 
    tpucou(iel,1) = tpucov(1,iel)
    tpucou(iel,2) = tpucov(2,iel)
    tpucou(iel,3) = tpucov(3,iel)
  endif
enddo

! Free memory
!--------------
deallocate(vel)
deallocate(vela)
if(ipucou.eq.1 .or. ncpdct.gt.0) deallocate(tpucov)

!--------
! FORMATS
!--------
#if defined(_CS_LANG_FR)

 1000 format(/,                                                   &
'   ** RESOLUTION POUR LA VITESSE                             ',/,&
'      --------------------------                             ',/)
 1200 format(/,                                                   &
'   ** RESOLUTION POUR LA PRESSION CONTINUITE                 ',/,&
'      --------------------------------------                 ',/)
 2000 format(/,' APRES PRESSION CONTINUITE',/,                    &
'-------------------------------------------------------------'  )
 2100 format(                                                           &
' Pression max.',E12.4   ,' (max. de la valeur absolue)       ',/)
 2200 format(                                                           &
' Vitesse  max.',E12.4   ,' en',3E11.3                         ,/)
 2300 format(                                                           &
' Vitesse  en face interne max.',E12.4   ,' ; min.',E12.4        )
 2400 format(                                                           &
' Vitesse  en face de bord max.',E12.4   ,' ; min.',E12.4        )
 2500 format(                                                           &
' Bilan de masse   au bord   ',E14.6                             )
 2600 format(                                                           &
' Informations Point fixe a l''iteration :',I10                ,/)
 2601 format('norme = ',E12.4,' norme 0 = ',E12.4,' toler  = ',E12.4 ,/)
 2602 format(                                                           &
' Convergence du point fixe a l''iteration ',I10               ,/)
 2603 format(                                                           &
' Non convergence du couplage vitesse pression par point fixe  ' )
 2001 format(                                                           &
'-------------------------------------------------------------',/)

#else

 1000 format(/,                                                   &
'   ** SOLVING VELOCITY'                                       ,/,&
'      ----------------'                                       ,/)
 1200 format(/,                                                   &
'   ** SOLVING CONTINUITY PRESSURE'                            ,/,&
'      ---------------------------'                            ,/)
 2000 format(/,' AFTER CONTINUITY PRESSURE',/,                    &
'-------------------------------------------------------------'  )
 2100 format(                                                           &
' Max. pressure',E12.4   ,' (max. absolute value)'             ,/)
 2200 format(                                                           &
' Max. velocity',E12.4   ,' en',3E11.3                         ,/)
 2300 format(                                                           &
' Max. velocity at interior face',E12.4   ,' ; min.',E12.4       )
 2400 format(                                                           &
' Max. velocity at boundary face',E12.4   ,' ; min.',E12.4       )
 2500 format(                                                           &
' Mass balance  at boundary  ',E14.6                             )
 2600 format(                                                           &
' Fixed point informations at iteration:',I10                  ,/)
 2601 format('norm = ',E12.4,' norm 0 = ',E12.4,' toler  = ',E12.4   ,/)
 2602 format(                                                           &
' Fixed point convergence at iteration ',I10                   ,/)
 2603 format(                                                           &
' Non convergence of fixed point for velocity pressure coupling' )
 2001 format(                                                           &
'-------------------------------------------------------------',/)

#endif

!----
! FIN
!----

return

end subroutine
