# --enable-contributing-plugin=yes/no
AC_ARG_ENABLE([contributing-plugin],
              [AS_HELP_STRING([--enable-contributing-plugin=@<:@yes/no@:>@],
                              [Build with tools to simplify contributing to existing project.])],
              [enable_contributing_plugin=$enableval],
              [enable_contributing_plugin=yes])

# for if ENABLE_CONTRIBUTING_PLUGIN in Makefile.am
AM_CONDITIONAL(ENABLE_CONTRIBUTING_PLUGIN, test x$enable_contributing_plugin != xno)

# Ensure our makefile is generated by autoconf
AC_CONFIG_FILES([plugins/contributing/Makefile])
