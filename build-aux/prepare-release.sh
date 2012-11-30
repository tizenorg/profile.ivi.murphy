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
    echo "  -r <release-head>   branch, tag or SHA1 to release"
    echo "  --obs <base>        prepare for OBS release"
    echo "  --gbs-release <br>  prepare (gbs-buildable) release to <br>"
    echo "  --gbs-review <br>   prepare (gbs-buildable) review to <br>"
    echo "  --gerrit <br>       prepare for gerrit release for <br>"
    echo "  --baseline <base>   use <base> as baseline for gerrit"
    echo ""
    echo "<name> is the name of the package. <version> is the version you"
    echo "want to name this release in RPM/the specfile. <release-head> is"
    echo "the actual tag, branch, or SHA1 you want to release. In gerrit"
    echo "and OBS modes, <base> will be used for the tarball, and"
    echo "<release-head> will be used to generate a set of patches."
    echo "In gbs mode <release-head> is used to branch off and no patches"
    echo "are generated."
    echo ""
    echo "E.g.:"
    echo "  $0 -v 0.0.9 --gerrit-release tizen"
    echo ""
    echo "This will produce a gerrit release from the tizen branch as"
    echo "version 0.0.9."
    echo ""
    echo "  $0 -v 0.0.9 --gerrit-review tizen --base master"
    echo ""
    echo "This will produce a gerrit review release from the tizen branch as"
    echo "version 0.0.9. Patches will be generated against master."
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

    info "* Generating tarball $_tarball.gz..."
    git archive --format=tar --prefix=$_pkg-$_pkgversion/ \
        $_gitversion > $_tarball && \
    gzip $_tarball
}

# generate patches to a directory against a baseline, print list of patches
generate_patches() {
    local _baseline="$1" _head="$2" _dir="$3" _patchlist

    info "* Generating patches against from $_baseline till $_head..." 1>&2
    pushd $_dir >& /dev/null && \
        _patchlist="`git format-patch -n $_baseline..$_head`" && \
        echo $_patchlist && \
    popd >& /dev/null

    if [ -n "$_patchlist" ]; then
        info "* Generated patches:" 1>&2
        info "$_patchlist" | sed 's/^/    /g' 1>&2
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
    info "* Generating linker scripts..."
    ./bootstrap && make generate-linker-scripts
}

# run package-specific GBS quirks
package_gerrit_quirks() {
    local _ld_scripts
    local _pkg_quirks

    _pkg_quirks="${0%/prepare-release.sh}/gerrit-quirks"
    if [ -f $_pkg_quirks ]; then
        info "* Running package-specific gerrit quirks ($_pkg_quirks)..."
        source $_pkg_quirks
    else
        info "* No package-specific gerrit quirks found ($_pkg_quirks)..."
    fi
}

# run package-specific GBS quirks
package_gbs_quirks() {
    local _ld_scripts
    local _pkg_quirks

    _pkg_quirks="${0%/prepare-release.sh}/gbs-quirks"
    if [ -f $_pkg_quirks ]; then
        info "* Running package-specific GBS quirks ($_pkg_quirks)..."
        source $_pkg_quirks
    else
        info "* No package-specific GBS quirks found ($_pkg_quirks)..."
    fi
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
             --head|-H)
                if [ -z "$HEAD" ]; then
                    shift
                    HEAD="$1"
                else
                    error "Multiple git heads ($HEAD, $1) specified."
                    usage
                fi
                ;;
             --gerrit*)
                if [ -z "$GERRIT" ]; then
                    GERRIT_RELEASE_TYPE="${1#--gerrit-}"
                    shift
                    GERRIT="$1"
                else
                    error "Multiple gerrit braches ($GERRIT, $1) specified."
                    usage
                fi
                ;;
             --base|-B)
                if [ -z "$BASELINE" ]; then
                    shift
                    BASELINE="$1"
                else
                    error "Multiple git baselines ($BASELINE, $1) specified."
                    usage
                fi
                ;;
             --obs|-O)
                if [ -z "$OBS" ]; then
                    shift
                    OBS="$1"
                else
                    error "Multiple OBS baselines ($OBS, $1) specified."
                    usage
                fi
                ;;
             --gbs*)
                if [ -z "$GBS" ]; then
                    GBS_RELEASE_TYPE="${1#--gbs-}"
                    shift
                    GBS="$1"
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
    local _specin _specout _dir _patches
    local _stamp _base _branch _remote _current _tag _chlog

    _dir=packaging
    _specin=$_dir.in/$PKG.spec.in
    _specout=$_dir/$PKG.spec

    _stamp=$(date -u +%Y%m%d.%H%M%S)
    _branch=gerrit-release-$_stamp
    _tag=submit/$GERRIT/$_stamp
    _chlog=$_dir/$PKG.changes

    case $GERRIT_RELEASE_TYPE in
        release)   _remote=refs/heads/$GERRIT;;
        review|*)  _remote=refs/for/$GERRIT;;
    esac

    mkdir -p packaging

    if [ -n "$BASELINE" ]; then
        _base="$BASELINE"
        _patches="`generate_patches $_base $HEAD $_dir`"
    else
        _base=$HEAD
        _patches=""
    fi

    echo "*** base: $_base, head: $HEAD"
    generate_specfile $_specin $_specout $VERSION $_patches

    info "* Preparing release branch $_branch (tag $_tag) from $HEAD..."

    _current="`git_current_branch`"
    git branch $_branch $_base && \
        package_gerrit_quirks && \
        git checkout $_branch && \
        git add packaging && \
        git commit -n -m "release: packaged for release." packaging && \
            echo "* $(date '+%a %b %d %H:%M:%S %Z %Y') $AUTHOR - $VERSION" \
                > $_chlog && \
            echo "- release: releasing $VERSION (SHA1: $HEAD)" >> $_chlog && \
               git add $_chlog &&
        git commit -m "release: updated changelog." $_chlog && \
        git tag -a -m "release tagged release." $_tag $HEAD && \
        info "" && \
        info "Branch $_branch is prepared for release." && \
        info "To proceed with the release, please" && \
        info "" && \
        info "    git push <remote> $_branch:$_remote $_tag"

    if [ "$?" = "0" ]; then
        info "Done."
    else
        error "Failed to prepare release..."
        git checkout $_current
        git branch -D $_branch
        exit 1
    fi
}

# prepare for gbs
gbs_prepare() {
    local _specin _specout _no_patches
    local _stamp _branch _tag _remote _current

    _specin=packaging.in/$PKG.spec.in
    _specout=packaging/$PKG.spec
    _no_patches=""
    _stamp=$(date -u +%Y%m%d.%H%M%S)
    _branch=release/$GBS/$_stamp
    _tag=submit/$GBS/$_stamp

    case $GBS_RELEASE_TYPE in
        release)   _remote=refs/heads/$GBS;;
        review|*)  _remote=refs/for/$GBS;;
    esac

    mkdir -p packaging

    _current="`git_current_branch`"
    info "* Generating spec file $_specout (from $_specin)..." && \
    generate_specfile $_specin $_specout $VERSION $_no_patches && \
        info "* Creating release branch $_branch..." && \
        git branch $_branch $HEAD && \
        git checkout $_branch && \
        package_gbs_quirks && \
        info "* Committing packaging to branch..." && \
        git add packaging && \
        git commit -m "packaging: added gbs packaging." packaging && \
        info "* Tagging for relase..." && \
        git tag -a -m "Tagged for release $_stamp to $GBS." $_tag && \
        info ""
        info "Branch $_branch prepared and checked out for release." && \
        info "Now if everything looks okay you can proceed by executing" && \
        info "the following command:" && \
        info "    git push <remote-git-repo> $_branch:$_remote $_tag"

    if [ "$?" != "0" ]; then
        error "Failed to prepare release, cleaning up."
        git checkout $_current
        git branch -D $_branch
        exit 1
    fi

}

# export to OBS
obs_export() {
    local _patches _dir _specin _specout

    _dir=obs-$VERSION
    _specin=packaging.in/$PKG.spec.in
    _specout=$_dir/$PKG.spec

    mkdir -p $_dir
    rm -f $_dir/*.spec $_dir/*.tar.gz $_dir/*.patches

    _patches="`generate_patches $OBS $HEAD $_dir`"
    generate_specfile $_specin $_specout $VERSION $_patches
    generate_tarball $PKG $VERSION $OBS $_dir
}


#########################
# main script

parse_configuration $*

[ -z "$PKG" ]        && PKG=$(basename `pwd`)
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
echo "   releasing: $HEAD"
echo "   output:    $DIR"
echo "   tarball:   $TARBALL"

if [ -n "$GERRIT" ]; then
    gerrit_export
else
    if [ -n "$OBS" ]; then
        obs_export
    else
        gbs_prepare
    fi
fi
