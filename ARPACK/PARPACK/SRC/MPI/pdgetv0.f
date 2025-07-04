c-----------------------------------------------------------------------
c\BeginDoc
c
c\Name: pdgetv0
c
c Message Passing Layer: MPI
c
c\Description: 
c  Generate a random initial residual vector for the Arnoldi process.
c  Force the residual vector to be in the range of the operator OP.  
c
c\Usage:
c  call pdgetv0
c     ( COMM, IDO, BMAT, ITRY, INITV, N, J, V, LDV, RESID, RNORM, 
c       IPNTR, WORKD, WORKL, IERR )
c
c\Arguments
c  COMM    MPI Communicator for the processor grid.  (INPUT)
c
c  IDO     Integer.  (INPUT/OUTPUT)
c          Reverse communication flag.  IDO must be zero on the first
c          call to pdgetv0.
c          -------------------------------------------------------------
c          IDO =  0: first call to the reverse communication interface
c          IDO = -1: compute  Y = OP * X  where
c                    IPNTR(1) is the pointer into WORKD for X,
c                    IPNTR(2) is the pointer into WORKD for Y.
c                    This is for the initialization phase to force the
c                    starting vector into the range of OP.
c          IDO =  2: compute  Y = B * X  where
c                    IPNTR(1) is the pointer into WORKD for X,
c                    IPNTR(2) is the pointer into WORKD for Y.
c          IDO = 99: done
c          -------------------------------------------------------------
c
c  BMAT    Character*1.  (INPUT)
c          BMAT specifies the type of the matrix B in the (generalized)
c          eigenvalue problem A*x = lambda*B*x.
c          B = 'I' -> standard eigenvalue problem A*x = lambda*x
c          B = 'G' -> generalized eigenvalue problem A*x = lambda*B*x
c
c  ITRY    Integer.  (INPUT)
c          ITRY counts the number of times that pdgetv0 is called.  
c          It should be set to 1 on the initial call to pdgetv0.
c
c  INITV   Logical variable.  (INPUT)
c          .TRUE.  => the initial residual vector is given in RESID.
c          .FALSE. => generate a random initial residual vector.
c
c  N       Integer.  (INPUT)
c          Dimension of the problem.
c
c  J       Integer.  (INPUT)
c          Index of the residual vector to be generated, with respect to
c          the Arnoldi process.  J > 1 in case of a "restart".
c
c  V       Double precision N by J array.  (INPUT)
c          The first J-1 columns of V contain the current Arnoldi basis
c          if this is a "restart".
c
c  LDV     Integer.  (INPUT)
c          Leading dimension of V exactly as declared in the calling 
c          program.
c
c  RESID   Double precision array of length N.  (INPUT/OUTPUT)
c          Initial residual vector to be generated.  If RESID is 
c          provided, force RESID into the range of the operator OP.
c
c  RNORM   Double precision scalar.  (OUTPUT)
c          B-norm of the generated residual.
c
c  IPNTR   Integer array of length 3.  (OUTPUT)
c
c  WORKD   Double precision work array of length 2*N.  (REVERSE COMMUNICATION).
c          On exit, WORK(1:N) = B*RESID to be used in SSAITR.
c
c  WORKL   Double precision work space used for Gram Schmidt orthogonalization
c
c  IERR    Integer.  (OUTPUT)
c          =  0: Normal exit.
c          = -1: Cannot generate a nontrivial restarted residual vector
c                in the range of the operator OP.
c
c\EndDoc
c
c-----------------------------------------------------------------------
c
c\BeginLib
c
c\Local variables:
c     xxxxxx  real
c
c\References:
c  1. D.C. Sorensen, "Implicit Application of Polynomial Filters in
c     a k-Step Arnoldi Method", SIAM J. Matr. Anal. Apps., 13 (1992),
c     pp 357-385.
c  2. R.B. Lehoucq, "Analysis and Implementation of an Implicitly 
c     Restarted Arnoldi Iteration", Rice University Technical Report
c     TR95-13, Department of Computational and Applied Mathematics.
c
c\Routines called:
c     second   ARPACK utility routine for timing.
c     pdvout   Parallel ARPACK utility routine for vector output.
c     pdlarnv  Parallel wrapper for LAPACK routine dlarnv (generates a random vector).
c     dgemv    Level 2 BLAS routine for matrix vector multiplication.
c     dcopy    Level 1 BLAS that copies one vector to another.
c     ddot     Level 1 BLAS that computes the scalar product of two vectors.
c     pdnorm2  Parallel version of  Level 1 BLAS that computes the norm of a vector.
c
c\Author
c     Danny Sorensen               Phuong Vu
c     Richard Lehoucq              Cray Research, Inc. &
c     Dept. of Computational &     CRPC / Rice University
c     Applied Mathematics          Houston, Texas
c     Rice University           
c     Houston, Texas            
c
c\Parallel Modifications
c     Kristi Maschhoff
c
c\Revision history:
c     Starting Point: Serial Code FILE: getv0.F   SID: 2.3
c
c\SCCS Information: 
c FILE: getv0.F   SID: 1.6   DATE OF SID: 04/17/99   
c
c\EndLib
c
c-----------------------------------------------------------------------
c
      subroutine pdgetv0 
     &   ( comm, ido, bmat, itry, initv, n, j, v, ldv, resid, rnorm, 
     &     ipntr, workd, workl, ierr )
c
      include   'mpif.h'
c
c     %---------------%
c     | MPI Variables |
c     %---------------%
c
      integer    comm
c
c     %----------------------------------------------------%
c     | Include files for debugging and timing information |
c     %----------------------------------------------------%
c
      include   'debug.h'
      include   'stat.h'
c
c     %------------------%
c     | Scalar Arguments |
c     %------------------%
c
      character  bmat*1
      logical    initv
      integer    ido, ierr, itry, j, ldv, n
      Double precision
     &           rnorm
c
c     %-----------------%
c     | Array Arguments |
c     %-----------------%
c
      integer    ipntr(3)
      Double precision
     &           resid(n), v(ldv,j), workd(2*n), workl(2*j)
c
c     %------------%
c     | Parameters |
c     %------------%
c
      Double precision
     &           one, zero
      parameter (one = 1.0, zero = 0.0)
c
c     %------------------------%
c     | Local Scalars & Arrays |
c     %------------------------%
c
      logical    first, inits, orth
      integer    idist, iseed(4), iter, msglvl, jj, myid, igen
      Double precision
     &           rnorm0
      save       first, iseed, inits, iter, msglvl, orth, rnorm0
c
      Double precision     
     &           rnorm_buf
c
c     %----------------------%
c     | External Subroutines |
c     %----------------------%
c
      external   pdlarnv, pdvout, dcopy, dgemv, second
c
c     %--------------------%
c     | External Functions |
c     %--------------------%
c
      Double precision
     &           ddot, pdnorm2
      external   ddot, pdnorm2
c
c     %---------------------%
c     | Intrinsic Functions |
c     %---------------------%
c
      intrinsic    abs, sqrt
c
c     %-----------------%
c     | Data Statements |
c     %-----------------%
c
      data       inits /.true./
c
c     %-----------------------%
c     | Executable Statements |
c     %-----------------------%
c
c
c     %-----------------------------------%
c     | Initialize the seed of the LAPACK |
c     | random number generator           |
c     %-----------------------------------%
c
      if (inits) then
c
c        %-----------------------------------%
c        | Generate a seed on each processor |
c        | using process id (myid).          |
c        | Note: the seed must be between 1  |
c        | and 4095.  iseed(4) must be odd.  |
c        %-----------------------------------%
c
         call MPI_COMM_RANK(comm, myid, ierr)
         igen = 1000 + 2*myid + 1
         if (igen .gt. 4095) then
            write(0,*) 'Error in p_getv0: seed exceeds 4095!'
         end if
c
         iseed(1) = igen/1000
         igen     = mod(igen,1000)
         iseed(2) = igen/100
         igen     = mod(igen,100)
         iseed(3) = igen/10
         iseed(4) = mod(igen,10)
c
         inits    = .false.
      end if
c
      if (ido .eq.  0) then
c 
c        %-------------------------------%
c        | Initialize timing statistics  |
c        | & message level for debugging |
c        %-------------------------------%
c
         call second (t0)
         msglvl = mgetv0
c 
         ierr   = 0
         iter   = 0
         first  = .FALSE.
         orth   = .FALSE.
c
c        %-----------------------------------------------------%
c        | Possibly generate a random starting vector in RESID |
c        | Use a LAPACK random number generator used by the    |
c        | matrix generation routines.                         |
c        |    idist = 1: uniform (0,1)  distribution;          |
c        |    idist = 2: uniform (-1,1) distribution;          |
c        |    idist = 3: normal  (0,1)  distribution;          |
c        %-----------------------------------------------------%
c
         if (.not.initv) then
            idist = 2
            call pdlarnv (comm, idist, iseed, n, resid)
         end if
c 
c        %----------------------------------------------------------%
c        | Force the starting vector into the range of OP to handle |
c        | the generalized problem when B is possibly (singular).   |
c        %----------------------------------------------------------%
c
         call second (t2)
         if (bmat .eq. 'G') then
            nopx = nopx + 1
            ipntr(1) = 1
            ipntr(2) = n + 1
            call dcopy (n, resid, 1, workd, 1)
            ido = -1
            go to 9000
         end if
      end if
c 
c     %-----------------------------------------%
c     | Back from computing OP*(initial-vector) |
c     %-----------------------------------------%
c
      if (first) go to 20
c
c     %-----------------------------------------------%
c     | Back from computing B*(orthogonalized-vector) |
c     %-----------------------------------------------%
c
      if (orth)  go to 40
c 
      call second (t3)
      tmvopx = tmvopx + (t3 - t2)
c 
c     %------------------------------------------------------%
c     | Starting vector is now in the range of OP; r = OP*r; |
c     | Compute B-norm of starting vector.                   |
c     %------------------------------------------------------%
c
      call second (t2)
      first = .TRUE.
      if (bmat .eq. 'G') then
         nbx = nbx + 1
         call dcopy (n, workd(n+1), 1, resid, 1)
         ipntr(1) = n + 1
         ipntr(2) = 1
         ido = 2
         go to 9000
      else if (bmat .eq. 'I') then
         call dcopy (n, resid, 1, workd, 1)
      end if
c 
   20 continue
c
      if (bmat .eq. 'G') then
         call second (t3)
         tmvbx = tmvbx + (t3 - t2)
      endif
c 
      first = .FALSE.
      if (bmat .eq. 'G') then
          rnorm_buf = ddot (n, resid, 1, workd, 1)
          call MPI_ALLREDUCE( rnorm_buf, rnorm0, 1,
     &          MPI_DOUBLE_PRECISION, MPI_SUM, comm, ierr )
          rnorm0 = sqrt(abs(rnorm0))
      else if (bmat .eq. 'I') then
          rnorm0 = pdnorm2( comm, n, resid, 1 )
      end if
      rnorm  = rnorm0
c
c     %---------------------------------------------%
c     | Exit if this is the very first Arnoldi step |
c     %---------------------------------------------%
c
      if (j .eq. 1) go to 50
c 
c     %----------------------------------------------------------------
c     | Otherwise need to B-orthogonalize the starting vector against |
c     | the current Arnoldi basis using Gram-Schmidt with iter. ref.  |
c     | This is the case where an invariant subspace is encountered   |
c     | in the middle of the Arnoldi factorization.                   |
c     |                                                               |
c     |       s = V^{T}*B*r;   r = r - V*s;                           |
c     |                                                               |
c     | Stopping criteria used for iter. ref. is discussed in         |
c     | Parlett's book, page 107 and in Gragg & Reichel TOMS paper.   |
c     %---------------------------------------------------------------%
c
      orth = .TRUE.
   30 continue
c
      call dgemv ('T', n, j-1, one, v, ldv, workd, 1,
     &            zero, workl(j+1), 1)
      call MPI_ALLREDUCE( workl(j+1), workl, j-1,
     &                    MPI_DOUBLE_PRECISION, MPI_SUM, comm, ierr)
      call dgemv ('N', n, j-1, -one, v, ldv, workl, 1,
     &            one, resid, 1)
c 
c     %----------------------------------------------------------%
c     | Compute the B-norm of the orthogonalized starting vector |
c     %----------------------------------------------------------%
c
      call second (t2)
      if (bmat .eq. 'G') then
         nbx = nbx + 1
         call dcopy (n, resid, 1, workd(n+1), 1)
         ipntr(1) = n + 1
         ipntr(2) = 1
         ido = 2
         go to 9000
      else if (bmat .eq. 'I') then
         call dcopy (n, resid, 1, workd, 1)
      end if
c 
   40 continue
c
      if (bmat .eq. 'G') then
         call second (t3)
         tmvbx = tmvbx + (t3 - t2)
      endif
c
      if (bmat .eq. 'G') then
         rnorm_buf = ddot (n, resid, 1, workd, 1)
         call MPI_ALLREDUCE( rnorm_buf, rnorm, 1,
     &            MPI_DOUBLE_PRECISION, MPI_SUM, comm, ierr )
         rnorm = sqrt(abs(rnorm))
      else if (bmat .eq. 'I') then
         rnorm = pdnorm2( comm, n, resid, 1 )
      end if
c
c     %--------------------------------------%
c     | Check for further orthogonalization. |
c     %--------------------------------------%
c
      if (msglvl .gt. 2) then
          call pdvout (comm, logfil, 1, rnorm0, ndigit, 
     &                '_getv0: re-orthonalization ; rnorm0 is')
          call pdvout (comm, logfil, 1, rnorm, ndigit, 
     &                '_getv0: re-orthonalization ; rnorm is')
      end if
c
      if (rnorm .gt. 0.717*rnorm0) go to 50
c 
      iter = iter + 1
      if (iter .le. 5) then
c
c        %-----------------------------------%
c        | Perform iterative refinement step |
c        %-----------------------------------%
c
         rnorm0 = rnorm
         go to 30
      else
c
c        %------------------------------------%
c        | Iterative refinement step "failed" |
c        %------------------------------------%
c
         do 45 jj = 1, n
            resid(jj) = zero
   45    continue
         rnorm = zero
         ierr = -1
      end if
c 
   50 continue
c
      if (msglvl .gt. 0) then
         call pdvout (comm, logfil, 1, rnorm, ndigit,
     &        '_getv0: B-norm of initial / restarted starting vector')
      end if
      if (msglvl .gt. 2) then
         call pdvout (comm, logfil, n, resid, ndigit,
     &        '_getv0: initial / restarted starting vector')
      end if
      ido = 99
c 
      call second (t1)
      tgetv0 = tgetv0 + (t1 - t0)
c 
 9000 continue
      return
c
c     %----------------%
c     | End of pdgetv0 |
c     %----------------%
c
      end
