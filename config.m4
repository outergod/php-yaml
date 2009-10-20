dnl $Id$
dnl config.m4 for extension yaml

PHP_ARG_WITH(yaml, [whether to enable LibYAML support],
[  --with-yaml[[=DIR]]       Include LibYAML support], yes, yes)

if test "$PHP_YAML" != "no"; then
  if test -r "$PHP_YAML/include/yaml.h"; then
    YAML_DIR="$PHP_YAML"
  else
    AC_MSG_CHECKING([for yaml in default path])
    for i in /usr /usr/local; do
      if test -r "$i/include/yaml.h"; then
        YAML_DIR=$i
        AC_MSG_RESULT([found in $i])
        break
      fi
    done
  fi

  if test -z "$YAML_DIR"; then
    AC_MSG_RESULT([not found])
    AC_MSG_ERROR([Please install LibYAML (http://pyyaml.org/wiki/LibYAML)])
  fi

  PHP_ADD_INCLUDE($YAML_DIR/include)

  LIBNAME=yaml
  LIBSYMBOL=yaml_parser_initialize

  PHP_CHECK_LIBRARY($LIBNAME, $LIBSYMBOL,
  [
    PHP_ADD_LIBRARY_WITH_PATH($LIBNAME, $YAML_DIR/lib, YAML_SHARED_LIBADD)
    AC_DEFINE(HAVE_YAMLLIB, 1, [ ])
  ],[
    AC_MSG_ERROR([wrong yaml lib version or lib not found])
  ],[
    -L$YAML_DIR/lib
  ])

  PHP_NEW_EXTENSION(yaml, yaml.c emitter.c parser.c, $ext_shared)
  PHP_SUBST(YAML_SHARED_LIBADD)
fi
