/*
 * The loader's version, the one place to change it: raise it for every release. MAJOR.MINOR are
 * the plugin API's (RON_API_MAJOR / RON_API_MINOR in include/ron_plugin.h; pkg_version_resource
 * checks they match), PATCH counts the releases of one API version. The version resource
 * (src/bootstrap/version.rc), the first line of loader.log and the release zip's name
 * (build.ps1 -Package) all read it from here. Preprocessor lines only: the resource compiler
 * includes this file too.
 */
#ifndef RON_LOADER_VERSION_H
#define RON_LOADER_VERSION_H

#define LOADER_VERSION_MAJOR 1
#define LOADER_VERSION_MINOR 1
#define LOADER_VERSION_PATCH 0

#define LOADER_STR_(x) #x
#define LOADER_STR(x) LOADER_STR_(x)
/* "1.1.0": one string literal, since the resource compiler does not join adjacent ones */
#define LOADER_VERSION_STRING LOADER_STR(LOADER_VERSION_MAJOR.LOADER_VERSION_MINOR.LOADER_VERSION_PATCH)

#endif
