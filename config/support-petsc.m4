dnl $Id: support-petsc.m4,v 1.1 2006/01/14 00:29:08 jafh Exp $

AC_DEFUN([BTNG_SUPPORT_PETSC],[
# Begin macro SUPPORT_PETSC
dnl Support PETSC by setting PETSC_DIR, PETSC_ARCH,
dnl petsc_INCLUDE and petsc_LIBS.
dnl Support --with-petsc-optimize to use optimized PETSC library.
dnl Support --with-petsc-mpiuni to use PETSC uniprocessor MPI library.
dnl
dnl Arg1: non-empty if you want the default to be on.
dnl
dnl This version supports PETSc-2.1.0 and later.

# Set PETSC_DIR to the PETSC root directory.
BTNG_ARG_WITH_PREFIX(petsc,PETSC_DIR,
ifelse($1,,
[[  --with-petsc=PATH	Support PETSc, and specify PETSC top-level directory.
			Setting PETSC_DIR is equivalent to this.]],
[  --without-petsc	Do not support PETSc.])
,
# User was not specific about specifying PETSc.
ifelse($1,,
# PETSc should be off by default.
# So if user specified --with-petsc, it should include a path or it is an error.
if test "${with_petsc+set}" = set ; then
  # User specified --with-petsc ambiguously.
  AC_MSG_ERROR([You must specify a path with --with-petsc=...])
else
  # User did not specify --with-petsc, so turn it off.
  unset PETSC_DIR;
fi
,
# PETSc should be on by default.
# So require that user say where it is or turn it off.
AC_MSG_ERROR([You must specify either --with-petsc=... or --without-petsc.])
))

# Set version numbers (PETSC_VERSION_...) for our own use.
# Users can directly access these from the PETSc header file.
if test "${PETSC_DIR+set}" = set; then
[
PETSC_VERSION_MAJOR=`sed -e '/^[ \t]\{0,\}#define PETSC_VERSION_MAJOR/!d' -e 's/.\{0,\}[ \t]\{1,\}//' $PETSC_DIR/include/petscversion.h`
PETSC_VERSION_MINOR=`sed -e '/^[ \t]\{0,\}#define PETSC_VERSION_MINOR/!d' -e 's/.\{0,\}[ \t]\{1,\}//' $PETSC_DIR/include/petscversion.h`
PETSC_VERSION_SUBMINOR=`sed -e '/^[ \t]\{0,\}#define PETSC_VERSION_SUBMINOR/!d' -e 's/.\{0,\}[ \t]\{1,\}//' $PETSC_DIR/include/petscversion.h`
]
fi
BTNG_AC_LOG_VAR(PETSC_VERSION_MAJOR PETSC_VERSION_MINOR PETSC_VERSION_SUBMINOR)

# Set PETSC_ARCH.
BTNG_ARG_WITH_PREFIX(petsc-arch,PETSC_ARCH,
[  --with-petsc-arch=PETSC_ARCH
			Specify the PETSC architecture.
			If omitted, the output of the petscarch script
			in the PETSc directory is used.])

# Set PETSC_OPTIMIZE.
BTNG_ARG_WITH_ENV_WRAPPER(petsc-optimize,PETSC_OPTIMIZE,
[  --with-petsc-optimize
			Use the optimized PETSC libraries],
# By default, use the debug PETSC library.
PETSC_OPTIMIZE=g
BTNG_AC_LOG_VAR(with_petsc_optimize)
test "$with_petsc_optimize" = yes && PETSC_OPTIMIZE=O
)

# Set PETSC_MPIUNI.
BTNG_ARG_WITH_ENV_WRAPPER(petsc-mpiuni,PETSC_MPIUNI,
[  --with-petsc-mpiuni	Use the PETSC uniprocessor MPI library],
# By default, do not use the PETSC uniprocessor MPI library.
unset PETSC_MPIUNI
)

# Set PETSC_LIBFILES
unset PETSC_LIBFILES
AC_ARG_WITH(petsc-libfiles,
[  --with-petsc-libfiles
			Specify explit PETSc library files instead
			of -L and -l flags (may help some debuggers)],
test "$with_petsc_libfiles" = yes && PETSC_LIBFILES=yes)


if test "${PETSC_DIR+set}" = set; then
  # Set up PETSC only if PETSC_DIR is defined.

  if test ! -d "$PETSC_DIR"; then
    AC_MSG_WARN([PETSC directory ($PETSC_DIR) does not look right])
  fi
  if test -z "$PETSC_ARCH"; then
    PETSC_ARCH=`$PETSC_DIR/bin/petscarch`
  fi
  BTNG_AC_LOG_VAR(PETSC_ARCH)
  if test ! -d "$PETSC_DIR/bmake/$PETSC_ARCH"; then
    AC_MSG_WARN([PETSC architecture ($PETSC_ARCH) does not look right])
  fi
  if test ! "$PETSC_OPTIMIZE" = g && test ! "$PETSC_OPTIMIZE" = O; then
    AC_MSG_ERROR([PETSC optimize should be either g or O])
  fi

  petsc_INCLUDES="-I$PETSC_DIR/include -I$PETSC_DIR/bmake/$PETSC_ARCH"
  petsc_INCLUDES="$petsc_INCLUDES -I$PETSC_DIR/src/vec"
  # Currently, I'm not entirely sure why we have to explicitly specify
  # the src/vec directory in the include path.  But there is at least
  # one required file there that cannot be found in the include directory.

  petsc_LIBDIR="${PETSC_DIR}/lib/lib${PETSC_OPTIMIZE}/${PETSC_ARCH}"

  # Issue the -L flag if not specifying PETSc libraries by file names.
  test ! "${PETSC_LIBFILES+set}" = set && petsc_LIBS="-L${petsc_LIBDIR}"

  # Build up a list of PETSC library files.
  petsc_libs_ls1=`cd ${PETSC_DIR}/lib/lib${PETSC_OPTIMIZE}/${PETSC_ARCH} && echo lib*.*`
  if test -n "$petsc_libs_ls1"; then
    unset petsc_libs_ls
    for i in $petsc_libs_ls1; do
      j=`echo $i | sed -e 's/lib//' -e 's/\.a$//' -e 's/\.so$//'`
      if echo "$petsc_libs_ls" | grep -v " $j " > /dev/null; then # Note padding!
        petsc_libs_ls="$petsc_libs_ls $j ";	# Note space padding!
      fi
    done
  fi
  # Remove mpiuni from the list of PETSC libraries unless user asked for it.
  if test ! "${PETSC_MPIUNI}" = yes; then
    petsc_libs_ls=`echo "$petsc_libs_ls" | sed 's/ mpiuni //g'`
  fi
  # Move some low-level libraries to the end to ensure resolution
  # for linkers that only make one pass.
  for i in petscmat petscvec petsc; do
    petsc_libs_ls=`echo "$petsc_libs_ls" | sed 's/\(.*\)\( \{0,1\}'"$i"'\{0,1\} \)\(.*\)/\1 \3 \2/g'`
  done
  # Build up petsc_LIBS string using library names.
  BTNG_AC_LOG_VAR(petsc_libs_ls1 petsc_libs_ls)
  if test -n "$petsc_libs_ls"; then
    if test "${PETSC_LIBFILES+set}" = set; then
      for i in $petsc_libs_ls; do
        petsc_LIBS="$petsc_LIBS ${PETSC_DIR}/lib/lib${PETSC_OPTIMIZE}/${PETSC_ARCH}/lib${i}.a"
      done
    else
      for i in $petsc_libs_ls; do
        petsc_LIBS="$petsc_LIBS -l$i"
      done
    fi
  fi

  BTNG_AC_LOG_VAR(PETSC_DIR petsc_INCLUDES petsc_LIBS PETSC_OPTIMIZE PETSC_MPIUNI)

fi
# End macro SUPPORT_PETSC
])
