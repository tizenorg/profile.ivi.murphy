# For information about rpm conditional builds supports
#
#    * http://www.rpm.org/wiki/PackagerDocs/ConditionalBuilds
%bcond_with    debug
%bcond_without lua
%bcond_without pulse
%bcond_without ecore
%bcond_without glib
#"qt" macro is for qt4 support
#that is no longer supported in Tizen
#for the support of qt5, use the macro "qt5"
%bcond_with qt
%bcond_without dbus
%bcond_without telephony
%bcond_without audiosession
%bcond_without websockets
%bcond_without smack
%bcond_with    icosyscon
%bcond_without sysmon
%bcond_without squashpkg

# By default we build with distro-default compilation flags which
# enables optimizations. If you want to build with full debugging
# ie. with optimization turned off and full debug info (-O0 -g3)
# pass '--with debug' to rpmbuild on the command line. Similary
# you can chose to compile with/without pulse, ecore, glib, qt,
# dbus, and telephony support. --without squashpkg will prevent
# squashing the -core and -plugins-base packages into the base
# murphy package.

Summary: Resource policy framework
Name: murphy
Version: 0.0.59
Release: 1
License: BSD-3-Clause
Group:   System/Service
URL:     http://01.org/murphy/
Source0:    %{name}-%{version}.tar.gz
Source1001: %{name}.manifest

%if %{with squashpkg}
Requires: %{name}-core = %{version}
%endif

Requires(post): /bin/systemctl
Requires(post): libcap-tools
Requires(postun): /bin/systemctl

BuildRequires: flex
BuildRequires: bison
BuildRequires: pkgconfig(lua)
BuildRequires: pkgconfig(libsystemd-daemon)
BuildRequires: pkgconfig(libsystemd-journal)
BuildRequires: pkgconfig(libcap)
BuildRequires: pkgconfig(libtzplatform-config)
%if %{with pulse}
BuildRequires: pkgconfig(libpulse)
%endif
%if %{with ecore}
BuildRequires: pkgconfig(ecore)
BuildRequires: mesa-libEGL
BuildRequires: mesa-libGLESv2
%endif
%if %{with glib}
BuildRequires: pkgconfig(glib-2.0)
%endif
%if %{with qt}
BuildRequires: pkgconfig(QtCore)
%endif
%if %{with dbus}
BuildRequires: pkgconfig(dbus-1)
%endif
%if %{with telephony}
BuildRequires: pkgconfig(ofono)
%endif
%if %{with audiosession}
BuildRequires: pkgconfig(audio-session-mgr)
BuildRequires: pkgconfig(aul)
%endif
%if %{with websockets}
BuildRequires: libwebsockets-devel
%endif
BuildRequires: pkgconfig(json)
%if %{with smack}
BuildRequires: pkgconfig(libsmack)
%endif
%if %{with icosyscon}
BuildRequires: ico-uxf-weston-plugin-devel
BuildRequires: weston-ivi-shell-devel
BuildRequires: genivi-shell-devel
BuildRequires: pkgconfig(ail)
BuildRequires: pkgconfig(aul)
BuildRequires: libxml2-devel
%endif

%description
This package contains the basic Murphy daemon.

%if %{with squashpkg}
%package core
Summary: Murphy core runtime libraries
Group: System/Libraries

%description core
This package contains the core runtime libraries.

%package plugins-base
Summary: The basic set of Murphy plugins
Group: System/Service
Requires: %{name} = %{version}
Requires: %{name}-core = %{version}

%description plugins-base
This package contains a basic set of plugins.
%endif

%package devel
Summary: The header files and libraries needed for Murphy development
Group: System/Libraries
%if %{with squashpkg}
Requires: %{name}-core = %{version}
%else
Requires: %{name} = %{version}
%endif
Requires: libjson-devel

%description devel
This package contains header files and libraries necessary for development.

%package doc
Summary: Documentation for Murphy
Group: SDK/Documentation

%description doc
This package contains documentation.

%if %{with pulse}
%package pulse
Summary: Murphy PulseAudio mainloop integration
Group: System/Libraries
%if %{with squashpkg}
Requires: %{name}-core = %{version}
%else
Requires: %{name} = %{version}
%endif

%description pulse
This package contains the Murphy PulseAudio mainloop integration runtime files.

%package pulse-devel
Summary: Murphy PulseAudio mainloop integration development files
Group: System/Libraries
Requires: %{name}-pulse = %{version}
%if %{with squashpkg}
Requires: %{name}-core = %{version}
%else
Requires: %{name} = %{version}
%endif

%description pulse-devel
This package contains the Murphy PulseAudio mainloop integration development
files.
%endif

%if %{with ecore}
%package ecore
Summary: Murphy EFL/ecore mainloop integration
Group: System/Libraries
%if %{with squashpkg}
Requires: %{name}-core = %{version}
%else
Requires: %{name} = %{version}
%endif

%description ecore
This package contains the Murphy EFL/ecore mainloop integration runtime files.

%package ecore-devel
Summary: Murphy EFL/ecore mainloop integration development files
Group: System/Libraries
Requires: %{name}-ecore = %{version}
%if %{with squashpkg}
Requires: %{name}-core = %{version}
%else
Requires: %{name} = %{version}
%endif

%description ecore-devel
This package contains the Murphy EFL/ecore mainloop integration development
files.
%endif

%if %{with glib}
%package glib
Summary: Murphy glib mainloop integration
Group: System/Libraries
%if %{with squashpkg}
Requires: %{name}-core = %{version}
%else
Requires: %{name} = %{version}
%endif

%description glib
This package contains the Murphy glib mainloop integration runtime files.

%package glib-devel
Summary: Murphy glib mainloop integration development files
Group: System/Libraries
Requires: %{name}-glib = %{version}
%if %{with squashpkg}
Requires: %{name}-core = %{version}
%else
Requires: %{name} = %{version}
%endif

%description glib-devel
This package contains the Murphy glib mainloop integration development
files.
%endif

%if %{with qt}
%package qt
Summary: Murphy Qt mainloop integration
Group: System/Libraries
%if %{with squashpkg}
Requires: %{name}-core = %{version}
%else
Requires: %{name} = %{version}
%endif

%description qt
This package contains the Murphy Qt mainloop integration runtime files.

%package qt-devel
Summary: Murphy Qt mainloop integration development files
Group: System/Libraries
Requires: %{name}-qt = %{version}
%if %{with squashpkg}
Requires: %{name}-core = %{version}
%else
Requires: %{name} = %{version}
%endif

%description qt-devel
This package contains the Murphy Qt mainloop integration development
files.
%endif

%package gam
Summary: Murphy support for Genivi Audio Manager
Group: System/Libraries
Requires: %{name} = %{version}

%description gam
This package contains the Murphy plugins for necessary for supporting
Genivi Audio Manager.

%package gam-devel
Summary: Murphy support for Genivi Audio Manager development files
Group: System/Libraries
Requires: %{name}-gam = %{version}

%description gam-devel
This package contains development files for Murphy Genivi Audio Manager
plugins.

%package tests
Summary: Various test binaries for Murphy
Group: System/Testing
Requires: %{name} = %{version}
%if %{with squashpkg}
Requires: %{name}-core = %{version}
%else
Requires: %{name} = %{version}
%endif

%description tests
This package contains various test binaries for Murphy.

%package ivi-resource-manager
Summary: Murphy IVI resource manager plugin
Group: System/Service

%description ivi-resource-manager
This package contains the Murphy IVI resource manager plugin.

%if %{with icosyscon}
%package system-controller
Summary: Murphy IVI System Controller plugin
Group: System/Service
Requires: ico-uxf-homescreen
Conflicts: murphy-ivi-resource-manager
Provides: system-controller
Conflicts: ico-uxf-homescreen-system-controller

%description system-controller
This package contains the Murphy IVI resource manager plugin.
%endif

%prep
%setup -q
cp %{SOURCE1001} .
%if %{with icosyscon}
echo "Build with icosyscon"
%else
echo "Build without icosyscon"
%endif

%build
%if %{with debug}
export CFLAGS="-O0 -g3"
V="V=1"
%endif

CONFIG_OPTIONS=""
DYNAMIC_PLUGINS="domain-control,system-controller,ivi-resource-manager"

%if %{with pulse}
CONFIG_OPTIONS="$CONFIG_OPTIONS --enable-gpl --enable-pulse"
%else
CONFIG_OPTIONS="$CONFIG_OPTIONS --disable-pulse"
%endif

%if %{with ecore}
CONFIG_OPTIONS="$CONFIG_OPTIONS --enable-gpl --enable-ecore"
%else
CONFIG_OPTIONS="$CONFIG_OPTIONS --disable-ecore"
%endif

%if %{with glib}
CONFIG_OPTIONS="$CONFIG_OPTIONS --enable-gpl --enable-glib"
%else
CONFIG_OPTIONS="$CONFIG_OPTIONS --disable-glib"
%endif

%if %{with qt}
CONFIG_OPTIONS="$CONFIG_OPTIONS --enable-qt"
%else
CONFIG_OPTIONS="$CONFIG_OPTIONS --disable-qt"
%endif

%if %{with dbus}
CONFIG_OPTIONS="$CONFIG_OPTIONS --enable-gpl --enable-libdbus"
%else
CONFIG_OPTIONS="$CONFIG_OPTIONS --disable-libdbus"
%endif

%if %{with telephony}
CONFIG_OPTIONS="$CONFIG_OPTIONS --enable-gpl --enable-telephony"
%else
CONFIG_OPTIONS="$CONFIG_OPTIONS --disable-telephony"
%endif

%if %{with audiosession}
CONFIG_OPTIONS="$CONFIG_OPTIONS --enable-resource-asm"
DYNAMIC_PLUGINS="$DYNAMIC_PLUGINS,resource-asm"
%else
CONFIG_OPTIONS="$CONFIG_OPTIONS --disable-resource-asm"
%endif

%if %{with websockets}
CONFIG_OPTIONS="$CONFIG_OPTIONS --enable-websockets"
%else
CONFIG_OPTIONS="$CONFIG_OPTIONS --disable-websockets"
%endif

%if %{with smack}
CONFIG_OPTIONS="$CONFIG_OPTIONS --enable-smack"
%else
CONFIG_OPTIONS="$CONFIG_OPTIONS --disable-smack"
%endif

%if %{with icosyscon}
CONFIG_OPTIONS="$CONFIG_OPTIONS --enable-system-controller"
%else
CONFIG_OPTIONS="$CONFIG_OPTIONS --disable-system-controller"
%endif

%if %{with sysmon}
CONFIG_OPTIONS="$CONFIG_OPTIONS --enable-system-monitor"
%else
CONFIG_OPTIONS="$CONFIG_OPTIONS --disable-system-monitor"
%endif

./bootstrap
%configure $CONFIG_OPTIONS --with-dynamic-plugins=$DYNAMIC_PLUGINS
%__make clean
%__make %{?_smp_mflags} $V

%install
rm -rf %{buildroot}
%make_install

# Make sure we have a plugin dir even if all the basic plugins
# are configured to be built in.
mkdir -p %{buildroot}%{_libdir}/murphy/plugins

# Get rid of any *.la files installed by libtool.
rm -f %{buildroot}%{_libdir}/*.la

# Clean up also the murphy DB installation.
rm -f %{buildroot}%{_libdir}/murphy/*.la

# Generate list of linkedin plugins (depends on the configuration).
outdir="`pwd`"
pushd %{buildroot} >& /dev/null && \
find ./%{_libdir} -name libmurphy-plugin-*.so* | \
sed 's#^./*#/#g' > $outdir/filelist.plugins-base && \
popd >& /dev/null
echo "Found the following linked-in plugin files:"
cat $outdir/filelist.plugins-base | sed 's/^/    /g'

# Generate list of header files, filtering ones that go to subpackages.
outdir="`pwd`"
pushd %{buildroot} >& /dev/null && \
find ./%{_includedir}/murphy | \
grep -E -v '((pulse)|(ecore)|(glib)|(qt))-glue' | \
sed 's#^./*#/#g' > $outdir/filelist.devel-includes && \
popd >& /dev/null

# Replace the default sample/test config files with the packaging ones.
rm -f %{buildroot}%{_sysconfdir}/murphy/*
cp packaging.in/murphy-lua.conf %{buildroot}%{_sysconfdir}/murphy/murphy.conf
cp packaging.in/murphy.lua      %{buildroot}%{_sysconfdir}/murphy/murphy.lua

# Copy plugin configuration files in place.
mkdir -p %{buildroot}%{_sysconfdir}/murphy/plugins/amb
cp packaging.in/amb-config.lua \
%{buildroot}%{_sysconfdir}/murphy/plugins/amb/config.lua

# Copy tmpfiles.d config file in place
mkdir -p %{buildroot}%{_tmpfilesdir}
cp packaging.in/murphyd.conf %{buildroot}%{_tmpfilesdir}

# Copy the systemd files in place.
mkdir -p %{buildroot}%{_unitdir}
cp packaging.in/murphyd.service %{buildroot}%{_unitdir}
%if %{_enable_icosyscon}
# cp packaging.in/ico-homescreen.service %{buildroot}%{_unitdir_user}
# cp packaging.in/murphy-wait-for-launchpad-ready.path \
#     %{buildroot}%{_unitdir_user}
%endif

%if %{with dbus}
mkdir -p %{buildroot}%{_sysconfdir}/dbus-1/system.d
sed "s/@TZ_SYS_USER_GROUP@/%{TZ_SYS_USER_GROUP}/g" \
    packaging.in/org.Murphy.conf.in > packaging.in/org.Murphy.conf
cp packaging.in/org.Murphy.conf \
    %{buildroot}%{_sysconfdir}/dbus-1/system.d/org.Murphy.conf
%endif

# copy (experimental) GAM resource backend configuration files
mkdir -p %{buildroot}%{_sysconfdir}/murphy/gam
cp packaging.in/gam-*.names packaging.in/gam-*.tree \
    %{buildroot}%{_sysconfdir}/murphy/gam

%clean
rm -rf %{buildroot}

%post
/bin/systemctl enable murphyd.service
setcap 'cap_net_admin=+ep' %{_bindir}/murphyd
ldconfig

%postun
if [ "$1" = "0" ]; then
/bin/systemctl disable murphyd.service
fi
ldconfig

%if %{with squashpkg}
%post core
ldconfig

%postun core
ldconfig

%post plugins-base
ldconfig

%postun plugins-base
ldconfig
%endif

%if %{with glib}
%post glib
ldconfig

%postun glib
ldconfig
%endif

%if %{with pulse}
%post pulse
ldconfig

%postun pulse
ldconfig
%endif

%if %{with ecore}
%post ecore
ldconfig

%postun ecore
ldconfig
%endif

%if %{with qt}
%post qt
ldconfig

%postun qt
ldconfig
%endif

%if %{with icosyscon}
%post system-controller
# prevent system controller from starting
# rm -f %{_unitdir_user}/weston.target.wants/ico-uxf-wait-launchpad-ready.path
# instead launch just ico-homescreen
# rm -f %{_unitdir_user}/weston.target.wants/murphy-wait-for-launchpad-ready.path
# ln -s %{_unitdir_user}/murphy-wait-for-launchpad-ready.path \
#     %{_unitdir_user}/weston.target.wants/murphy-wait-for-launchpad-ready.path

%postun system-controller
#if [ "$1" = "0" ]; then
# rm -f %{_unitdir_user}/weston.target.wants/murphy-wait-for-launchpad-ready.path
# if [ -f %{_unitdir_user}/ico-uxf-wait-launchpad-ready.path ]; then
#     ln -sf %{_unitdir_user}/ico-uxf-wait-launchpad-ready.path \
#         %{_unitdir_user}/weston.target.wants/ico-uxf-wait-launchpad-ready.path
#fi
fi
%endif

%post gam
ldconfig

%postun gam
ldconfig

%if %{with squashpkg}
%files -f filelist.plugins-base
%else
%files
%endif
%defattr(-,root,root,-)
%manifest murphy.manifest
%{_bindir}/murphyd
%config %{_sysconfdir}/murphy
%{_unitdir}/murphyd.service
%{_tmpfilesdir}/murphyd.conf
%if %{with audiosession}
%{_sbindir}/asm-bridge
%endif
%if %{with dbus}
%{_sysconfdir}/dbus-1/system.d
%config %{_sysconfdir}/dbus-1/system.d/org.Murphy.conf
%endif
%if %{with websockets}
%{_datadir}/murphy
%endif

%if %{with squashpkg}
%files core
%defattr(-,root,root,-)
%endif
%{_libdir}/libmurphy-common.so.*
%{_libdir}/libmurphy-core.so.*
%{_libdir}/libmurphy-resolver.so.*
%{_libdir}/libmurphy-resource.so.*
%{_libdir}/libmurphy-resource-backend.so.*
%if %{with lua}
%{_libdir}/libmurphy-lua-utils.so.*
%{_libdir}/libmurphy-lua-decision.so.*
%endif
%{_libdir}/libmurphy-domain-controller.so.*
%{_libdir}/murphy/*.so.*
%{_libdir}/libbreedline*.so.*
%if %{with dbus}
%{_libdir}/libmurphy-libdbus.so.*
%{_libdir}/libmurphy-dbus-libdbus.so.*
%endif

%if %{with squashpkg}
%files plugins-base -f filelist.plugins-base
%defattr(-,root,root,-)
%endif
%{_libdir}/murphy/plugins/plugin-domain-control.so
%{_libdir}/murphy/plugins/plugin-resource-asm.so
%{_libdir}/murphy/plugins/plugin-resource-native.so

%files devel -f filelist.devel-includes
%defattr(-,root,root,-)
# %%{_includedir}/murphy/config.h
# %%{_includedir}/murphy/common.h
# %%{_includedir}/murphy/core.h
# %%{_includedir}/murphy/common
# %%{_includedir}/murphy/core
# %%{_includedir}/murphy/resolver
# %%{_includedir}/murphy/resource
# # hmmm... should handle disabled plugins properly.
# %%{_includedir}/murphy/domain-control
# %%{_includedir}/murphy/plugins
%{_includedir}/murphy-db
%{_libdir}/libmurphy-common.so
%{_libdir}/libmurphy-core.so
%{_libdir}/libmurphy-resolver.so
%{_libdir}/libmurphy-resource.so
%{_libdir}/libmurphy-resource-backend.so
%if %{with lua}
%{_libdir}/libmurphy-lua-utils.so
%{_libdir}/libmurphy-lua-decision.so
%endif
%{_libdir}/libmurphy-domain-controller.so
%{_libdir}/murphy/*.so
%{_libdir}/pkgconfig/murphy-common.pc
%{_libdir}/pkgconfig/murphy-core.pc
%{_libdir}/pkgconfig/murphy-resolver.pc
# %%{_libdir}/pkgconfig/murphy-resource.pc
%if %{with lua}
%{_libdir}/pkgconfig/murphy-lua-utils.pc
%{_libdir}/pkgconfig/murphy-lua-decision.pc
%endif
%{_libdir}/pkgconfig/murphy-domain-controller.pc
%{_libdir}/pkgconfig/murphy-db.pc
%{_libdir}/pkgconfig/murphy-resource.pc
%{_includedir}/breedline
%{_libdir}/libbreedline*.so
%{_libdir}/pkgconfig/breedline*.pc
%if %{with dbus}
# %%{_includedir}/murphy/dbus
%{_libdir}/libmurphy-libdbus.so
%{_libdir}/libmurphy-dbus-libdbus.so
%{_libdir}/pkgconfig/murphy-libdbus.pc
%{_libdir}/pkgconfig/murphy-dbus-libdbus.pc
%endif

%files doc
%defattr(-,root,root,-)
%doc %{_datadir}/doc/murphy/AUTHORS
%doc %{_datadir}/doc/murphy/CODING-STYLE
%doc %{_datadir}/doc/murphy/ChangeLog
%doc %{_datadir}/doc/murphy/NEWS
%doc %{_datadir}/doc/murphy/README
%license COPYING LICENSE-BSD

%if %{with pulse}
%files pulse
%defattr(-,root,root,-)
%{_libdir}/libmurphy-pulse.so.*
%manifest murphy.manifest

%files pulse-devel
%defattr(-,root,root,-)
%{_includedir}/murphy/common/pulse-glue.h
%{_libdir}/libmurphy-pulse.so
%{_libdir}/pkgconfig/murphy-pulse.pc
%endif

%if %{with ecore}
%files ecore
%defattr(-,root,root,-)
%{_libdir}/libmurphy-ecore.so.*
%manifest murphy.manifest

%files ecore-devel
%defattr(-,root,root,-)
%{_includedir}/murphy/common/ecore-glue.h
%{_libdir}/libmurphy-ecore.so
%{_libdir}/pkgconfig/murphy-ecore.pc
%endif

%if %{with glib}
%files glib
%defattr(-,root,root,-)
%{_libdir}/libmurphy-glib.so.*
%manifest murphy.manifest

%files glib-devel
%defattr(-,root,root,-)
%{_includedir}/murphy/common/glib-glue.h
%{_libdir}/libmurphy-glib.so
%{_libdir}/pkgconfig/murphy-glib.pc
%endif

%if %{with qt}
%files qt
%defattr(-,root,root,-)
%{_libdir}/libmurphy-qt.so.*
%manifest murphy.manifest

%files qt-devel
%defattr(-,root,root,-)
%{_includedir}/murphy/common/qt-glue.h
%{_libdir}/libmurphy-qt.so
%{_libdir}/pkgconfig/murphy-qt.pc
%endif

%files gam
%defattr(-,root,root,-)
%{_libdir}/libmurphy-decision-tree.so.*
%{_libdir}/libmurphy-decision-tree.so.0.0.0
%{_libdir}/murphy/plugins/plugin-gam-resource-manager.so

%files gam-devel
%defattr(-,root,root,-)
%{_bindir}/decision-test
%{_bindir}/pattern-generator
%{_libdir}/libmurphy-decision-tree.so

%files tests
%defattr(-,root,root,-)
%{_bindir}/resource-client
%{_bindir}/resource-api-test
%{_bindir}/resource-api-fuzz
%{_bindir}/test-domain-controller
%{_bindir}/murphy-console
%manifest murphy.manifest

%files ivi-resource-manager
%defattr(-,root,root,-)
%{_libdir}/murphy/plugins/plugin-ivi-resource-manager.so
%manifest murphy.manifest

%if %{with icosyscon}
%files system-controller
%defattr(-,root,root,-)
%{_libdir}/murphy/plugins/plugin-system-controller.so
# %{_unitdir_user}/ico-homescreen.service
# %{_unitdir_user}/murphy-wait-for-launchpad-ready.path
%manifest murphy.manifest
%endif
