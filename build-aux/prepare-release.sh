#!/bin/bash

# This needs a bit more work, mostly on the "discplined engineering" front.
# IOW, instead of this UPSTREAM_BASE hack it would be better to have 3
# branches:
#   1) pristine upstream: for tracking upstream progress/retrogression
#   2) patched upstream: pristine upstream with our patches applied
#   3) working local: patches upstream + a set of scripts (like this) to
#      do everyday stuff like making new releases, exporting stuff to
#      OBS, etc...

# show help on usage and exit
usage() {
    echo "***************************************************"
    echo "*   Don not believe this help... needs updating.  *"
    echo "***************************************************"
    echo ""
    echo "usage: $0 [options], where the possible options are"
    echo "  -n <name>           name of your package"
    echo "  -v <version>        version to in rpm/SPEC file"
    echo "  -B <upstream-base>  name or SHA1 of baseline"
    echo "  -H <taget-head>     name or SHA1 of release HEAD"
    echo "  --obs               prepare OBS release"
    echo "  --gerrit            prepare gerrit release"
    echo ""
    echo "<name> is the name of the package, <version> is the version"
    echo "you want to export to OBS, and <upstream-base> is the name of"
    echo "the upstream git branch or the SHA1 you want to generate your"
    echo "release from and base your patches on top of. On OBS mode the"
    echo "output will be generated in a directory called obs-$VERSION."
    echo "Otherwise in gerrit mode, the output will be generated in a"
    echo "directory called packaging."
    echo ""
    echo "E.g.:"
    echo "  $0 -n pulseaudio -v 2.0 -B pulseaudio-2.0 -H tizen"
    echo ""
    echo "This will produce a gerrit export with version 2.0 against the"
    echo "SHA1 pulseaudio-2.0, producing patches up till tizen and"
    echo "place the result in a directory called packaging."
    exit 0
}

# emit an error message to stderr
error () {
    echo "error: $*" 1>&2
}

# emit an info message to stdout
info () {
    echo "$*"
}

# emit a warning message to stderr
warning () {
    echo "warning: $*" 1>&2
}

# generate a tarball for a given package and version from git to a directory
generate_tarball() {
    local _pkg="$1" _pkgversion="$2" _gitversion="$3" _dir="$4"
    local _tarball="$_dir/$_pkg-$_pkgversion.tar"

    echo "Generating tarball $_tarball.gz..."
    git archive --format=tar --prefix=$_pkg-$_pkgversion/ \
        $_gitversion > $_tarball && \
    gzip $_tarball
}

# generate patches to a directory against a baseline, print list of patches
generate_patches() {
    local _baseline="$1" _head="$2" _dir="$3" _patchlist

    echo "Generating patches against from $_baseline till $_head..." 1>&2
    pushd $_dir >& /dev/null && \
        _patchlist="`git format-patch -n $_baseline..$_head`" && \
        echo $_patchlist && \
    popd >& /dev/null

    if [ -n "$_patchlist" ]; then
        echo "Generated patches:" 1>&2
        echo "$_patchlist" | sed 's/^/    /g' 1>&2
    fi
}

# generate a spec file from a spec template filling in version and patches
generate_specfile() {
    local _specin="$1" _specout="$2" _version="$3" _patchlist

    shift 3
    _patchlist="$*"

    cat $_specin | sed "s/@VERSION@/$_version/g" > $_specout.tmp && \
    cat $_specout.tmp | while read -r line; do
        case $line in
        @DECLARE_PATCHES@)
            i=0
            for patch in $_patchlist; do
                echo "Patch$i: $patch"
                let i=$i+1
            done
            ;;
        @APPLY_PATCHES@)
            i=0
            for patch in $_patchlist; do
                echo "%patch$i -p1"
                let i=$i+1
            done
            ;;
        *)
            echo "$line"
            ;;
        esac
    done > $_specout && \
    rm -f $_specout.tmp
}

# generate linker scripts
generate_linker_scripts() {
    ./bootstrap.sh && make generate-linker-scripts
}

# parse the command line for configuration
parse_configuration() {
    local _bin="$0"

    while [ -n "${1#-}" ]; do
        case $1 in
            --name|-n)
                if [ -z "$PKG" ]; then
                    shift
                    PKG="$1"
                else
                    error "Multiple package names ($PKG, $1) specified."
                    usage
                fi
                ;;
            --version|-v)
                if [ -z "$VERSION" ]; then
                    shift
                    VERSION="$1"
                else
                    error "Multiple versions ($VERSION, $1) specified."
                    usage
                fi
                ;;
            --baseline|-B)
                if [ -z "$BASELINE" ]; then
                    shift
                    BASELINE="$1"
                else
                    error "Multiple git baselines ($BASELINE, $1) specified."
                    usage
                fi
                ;;
             --head|-H)
                if [ -z "$HEAD" ]; then
                    shift
                    HEAD="$1"
                else
                    error "Multiple git heads ($HEAD, $1) specified."
                    usage
                fi
                ;;
             --gerrit|-G)
                if [ -z "$GERRIT" ]; then
                    shift
                    GERRIT="$1"
                else
                    error "Multiple gerrit braches ($GERRIT, $1) specified."
                    usage
                fi
                ;;
             --obs|-O)
                if [ -z "$OBS" ]; then
                    OBS="yes"
                else
                    error "Multiple gerrit braches ($OBS, $1) specified."
                    usage
                fi
                ;;
             --gbs|-g)
                if [ -z "$GBS" ]; then
                    GBS="yes"
                else
                    error "Multiple gbs branches ($GBS, $1) specified."
                    usage
                fi
                ;;
             --author|-A)
                if [ -z "$AUTHOR" ]; then
                    shift
                    AUTHOR="$1"
                else
                    error "Multiple authors ($AUTHOR, $1) specified."
                    usage
                fi
                ;;
             --help|-h)
                usage 0
                ;;
             --debug|-d)
                set -x
                DEBUG=$((${DEBUG:-0} + 1))
                ;;
             *)
                error "Unknown option or unexpected argument '$1'."
                usage
                ;;
        esac
        shift
    done
}

# determine current git branch
git_current_branch() {
    local _br

    _br="`git branch -l | grep '^[^ ]' | cut -d ' ' -f 2`"

    echo "$_br"
}

# export to gerrit
gerrit_export() {
    local _specin _specout _no_patches
    local _stamp _branch _current _tag _chlog

    _specin=packaging.in/$PKG.spec.in
    _specout=packaging/$PKG.spec
    _no_patches=""

    _stamp=$(date -u +%Y%m%d.%H%M%S)
    _branch=gerrit-release-$_stamp
    _tag=submit/$GERRIT/$_stamp
    _chlog=packaging/$PKG.changes

    generate_specfile $_specin $_specout $VERSION $_no_patches

    echo "Preparing release branch $_branch (tag $_tag) from $HEAD..."

    _current="`git_current_branch`"
    git branch $_branch $HEAD && \
        git checkout $_branch && \
        git add packaging && \
        git commit -n -m "release: packaging for gerrit release." packaging && \
            echo "* $(date '+%a %b %d %H:%M:%S %Z %Y') $AUTHOR - $VERSION" \
                > $_chlog && \
            echo "- release: releasing $VERSION" >> $_chlog && \
               git add $_chlog &&
        git commit -m "release: updated changelog." $_chlog && \
        git tag -a -m "release tagged release." $_tag $HEAD && \
        echo "" && \
        echo "Branch $_branch is prepared for release." && \
        echo "To proceed with the release, please" && \
        echo "" && \
        echo "  1) git push --force tizen $HEAD^:refs/heads/master" && \
        echo "  2) git push tizen $HEAD:refs/for/master $_tag"

    if [ "$?" = "0" ]; then
        echo "Done."
    else
        echo "Failed to prepare release..."
        git checkout $_current
        git branch -D $_branch
        exit 1
    fi

}

# prepare for gbs
gbs_prepare() {
    local _specin _specout _no_patches

    _specin=packaging.in/$PKG.spec.in
    _specout=packaging/$PKG.spec
    _no_patches=""

    mkdir -p packaging

    generate_specfile $_specin $_specout $VERSION $_no_patches && \
        git branch gbs-$VERSION $HEAD && \
        git checkout gbs-$VERSION && \
        make generate-linker-scripts && \
        ld_scripts="`find . -name linker-script.*`" && \
        git add $ld_scripts && \
        git commit -m "packaging: pre-generated linker scripts." $ld_scripts
        git add packaging && \
        git commit -m "packaging: added gbs packaging." packaging
}

# export to OBS
obs_export() {
    local _patches _dir _specin _specout

    _dir=obs-$VERSION
    _specin=packaging.in/$PKG.spec.in
    _specout=$_dir/$PKG.spec

    mkdir -p $_dir
    rm -f $_dir/*.spec $_dir/*.tar.gz $_dir/*.patches

    _patches="`generate_patches $BASELINE $HEAD $_dir`"
    generate_specfile $_specin $_specout $VERSION $_patches
    generate_tarball $PKG $VERSION $BASELINE $_dir
}


#########################
# main script

parse_configuration $*

[ -z "$PKG" ]        && PKG=$(basename `pwd`)
[ -z "$BASELINE" ]   && BASELINE=master
[ -z "$VERSION" ]    && VERSION=$(date +'%Y%m%d')
[ -z "$HEAD" ]       && HEAD=HEAD
[ -z "$AUTHOR" ]     && AUTHOR="`git config user.name` <`git config \
                                     user.email`>"

kind=0
[ -n "$GERRIT" ] && kind=$(($kind + 1))
[ -n "$OBS" ] && kind=$(($kind + 1))
[ -n "$GBS" ] && kind=$(($kind + 1))

if [ "$kind" -gt 1 ]; then
    error "Multiple release types specified (--gerrit, --obs, --gbs)."
    exit 1
else
    if [ "$kind" = "0" ]; then
        GERRIT=master
    fi
fi

if [ -n "$GERRIT" ]; then
    DIR=packaging
    TARBALL=""
else
    DIR=obs-$VERSION
    TARBALL=$PKG-$VERSION.tar
fi

echo "Package name: $PKG"
echo "   version:   $VERSION"
echo "   baseline:  $BASELINE"
echo "   head:      $HEAD"
echo " output:      $DIR"
echo " tarball:     $TARBALL"

if [ -n "$GERRIT" ]; then
    gerrit_export
else
    if [ -n "$OBS" ]; then
        obs_export
    else
        gbs_prepare
    fi
fi
