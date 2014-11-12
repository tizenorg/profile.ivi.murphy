%bcond_with icosyscon

# By default we build with distro-default compilation flags which
# enables optimizations. If you want to build with full debugging
# ie. with optimization turned off and full debug info (-O0 -g3)
# pass '--with debug' to rpmbuild on the command line. Similary
# you can chose to compile with/without pulse, ecore, glib, qt,
# dbus, and telephony support. --without squashpkg will prevent
# squashing the -core and -plugins-base packages into the base
# murphy package.


%{!?_with_debug:%{!?_without_debug:%define _without_debug 0}}
%{!?_with_lua:%{!?_without_lua:%define _with_lua 1}}
%{!?_with_pulse:%{!?_without_pulse:%define _with_pulse 1}}
%{!?_with_ecore:%{!?_without_ecore:%define _with_ecore 1}}
%{!?_with_glib:%{!?_without_glib:%define _with_glib 1}}
%{!?_with_qt:%{!?_without_qt:%define _without_qt 1}}
%{!?_with_dbus:%{!?_without_dbus:%define _with_dbus 1}}
%{!?_with_telephony:%{!?_without_telephony:%define _with_telephony 1}}
%{!?_with_audiosession:%{!?_without_audiosession:%define _with_audiosession 1}}
%{!?_with_websockets:%{!?_without_websockets:%define _with_websockets 1}}
%{!?_with_smack:%{!?_without_smack:%define _with_smack 1}}
%{!?_with_icosyscon:%{!?_without_icosyscon:%define _without_icosyscon 1}}
%{!?_with_icoweston:%{!?_without_icoweston:%define _without_icoweston 1}}
%{!?_with_sysmon:%{!?_without_sysmon:%define _with_sysmon 1}}
%{!?_with_squashpkg:%{!?_without_squashpkg:%define _with_squashpkg 1}}

#
# Abnormalize _with_icosyscon to _enable_icosyscon
#
# Since some people seem to have a hard time understanding that
#
# 1) the right way to disable a conditional _with_* rpm macro is to leave it
#    undefined as opposed to defining it to 0
#
# 2) if you decide to do it the wrong way at least you should be consistent
#    about it and not randomly change between the conventions
#
# we need to roll this butt-ugly hack to make sure that we always go with
# the wrong convention. We always set up _enable_icosyscon to 1 or 0 depending
# on how _with_icosyscon happens to be set (or unset).
#

%if %{!?_with_icosyscon:0}%{?_with_icosyscon:1}
%if %{_with_icosyscon}
%define _enable_icosyscon 1
%else
%define _enable_icosyscon 0
%endif
%else
%define _enable_icosyscon 0
%endif

Summary: Resource policy framework
Name: murphy
Version: 0.0.67
Release: 1
License: BSD-3-Clause
Group: System/Service
URL: http://01.org/murphy/
Source0: %{name}-%{version}.tar.gz
Source1001: %{name}.manifest
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root
%if %{?_with_squashpkg:0}%{!?_with_squashpkg:1}
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

%if %{?_with_pulse:1}%{!?_with_pulse:0}
BuildRequires: pkgconfig(libpulse)
%endif
%if %{?_with_ecore:1}%{!?_with_ecore:0}
BuildRequires: pkgconfig(ecore)
BuildRequires: mesa-libEGL
BuildRequires: mesa-libGLESv2
%endif
%if %{?_with_glib:1}%{!?_with_glib:0}
BuildRequires: pkgconfig(glib-2.0)
%endif
%if %{?_with_qt:1}%{!?_with_qt:0}
BuildRequires: pkgconfig(QtCore)
%endif
%if %{?_with_dbus:1}%{!?_with_dbus:0}
BuildRequires: pkgconfig(dbus-1)
%endif
%if %{?_with_telephony:1}%{!?_with_telephony:0}
BuildRequires: pkgconfig(ofono)
%endif
%if %{?_with_audiosession:1}%{!?_with_audiosession:0}
BuildRequires: pkgconfig(audio-session-mgr)
BuildRequires: pkgconfig(aul)
%endif
%if %{?_with_websockets:1}%{!?_with_websockets:0}
BuildRequires: libwebsockets-devel
%endif
BuildRequires: pkgconfig(json)

%if %{?_with_smack:1}%{!?_with_smack:0}
BuildRequires: pkgconfig(libsmack)
%endif

%if %{?_with_icosyscon:1}%{!?_with_icosyscon:0}
# %%if %%{_with_icosyscon} # gbs can't, so don't bother...
BuildRequires: ico-uxf-weston-plugin-devel
BuildRequires: weston-ivi-shell-devel
BuildRequires: genivi-shell-devel
BuildRequires: pkgconfig(ail)
BuildRequires: pkgconfig(aul)
BuildRequires: libxml2-devel
# %%endif
%endif

%if %{?_with_squashpkg:0}%{!?_with_squashpkg:1}
%package core
Summary: Murphy core runtime libraries
Group: System/Libraries

%package plugins-base
Summary: The basic set of Murphy plugins
Group: System/Service
Requires: %{name} = %{version}
Requires: %{name}-core = %{version}
%endif

%package devel
Summary: The header files and libraries needed for Murphy development
Group: System/Libraries
%if %{?_with_squashpkg:0}%{!?_with_squashpkg:1}
Requires: %{name}-core = %{version}
%else
Requires: %{name} = %{version}
%endif
Requires: libjson-devel

%package doc
Summary: Documentation for Murphy
Group: SDK/Documentation

%if %{?_with_pulse:1}%{!?_with_pulse:0}
%package pulse
Summary: Murphy PulseAudio mainloop integration
Group: System/Libraries
%if %{?_with_squashpkg:0}%{!?_with_squashpkg:1}
Requires: %{name}-core = %{version}
%else
Requires: %{name} = %{version}
%endif

%package pulse-devel
Summary: Murphy PulseAudio mainloop integration development files
Group: System/Libraries
Requires: %{name}-pulse = %{version}
%if %{?_with_squashpkg:0}%{!?_with_squashpkg:1}
Requires: %{name}-core = %{version}
%else
Requires: %{name} = %{version}
%endif
%endif

%if %{?_with_ecore:1}%{!?_with_ecore:0}
%package ecore
Summary: Murphy EFL/ecore mainloop integration
Group: System/Libraries
%if %{?_with_squashpkg:0}%{!?_with_squashpkg:1}
Requires: %{name}-core = %{version}
%else
Requires: %{name} = %{version}
%endif

%package ecore-devel
Summary: Murphy EFL/ecore mainloop integration development files
Group: System/Libraries
Requires: %{name}-ecore = %{version}
%if %{?_with_squashpkg:0}%{!?_with_squashpkg:1}
Requires: %{name}-core = %{version}
%else
Requires: %{name} = %{version}
%endif
%endif

%if %{?_with_glib:1}%{!?_with_glib:0}
%package glib
Summary: Murphy glib mainloop integration
Group: System/Libraries
%if %{?_with_squashpkg:0}%{!?_with_squashpkg:1}
Requires: %{name}-core = %{version}
%else
Requires: %{name} = %{version}
%endif

%package glib-devel
Summary: Murphy glib mainloop integration development files
Group: System/Libraries
Requires: %{name}-glib = %{version}
%if %{?_with_squashpkg:0}%{!?_with_squashpkg:1}
Requires: %{name}-core = %{version}
%else
Requires: %{name} = %{version}
%endif
%endif

%if %{?_with_qt:1}%{!?_with_qt:0}
%package qt
Summary: Murphy Qt mainloop integration
Group: System/Libraries
%if %{?_with_squashpkg:0}%{!?_with_squashpkg:1}
Requires: %{name}-core = %{version}
%else
Requires: %{name} = %{version}
%endif

%package qt-devel
Summary: Murphy Qt mainloop integration development files
Group: System/Libraries
Requires: %{name}-qt = %{version}
%if %{?_with_squashpkg:0}%{!?_with_squashpkg:1}
Requires: %{name}-core = %{version}
%else
Requires: %{name} = %{version}
%endif
%endif

%package gam
Summary: Murphy support for Genivi Audio Manager
Group: System/Libraries
Requires: %{name} = %{version}

%package gam-devel
Summary: Murphy support for Genivi Audio Manager development files
Group: System/Libraries
Requires: %{name}-gam = %{version}

%package tests
Summary: Various test binaries for Murphy
Group: System/Testing
Requires: %{name} = %{version}
%if %{?_with_squashpkg:0}%{!?_with_squashpkg:1}
Requires: %{name}-core = %{version}
%else
Requires: %{name} = %{version}
%endif

%package ivi-resource-manager
Summary: Murphy IVI resource manager plugin
Group: System/Service

%if %{_enable_icosyscon}
%package system-controller
Summary: Murphy IVI System Controller plugin
Group: System/Service
Requires: ico-uxf-homescreen
Conflicts: murphy-ivi-resource-manager
Provides: system-controller
Conflicts: ico-uxf-homescreen-system-controller
%endif

%description
This package contains the basic Murphy daemon.

%if %{?_with_squashpkg:0}%{!?_with_squashpkg:1}
%description core
This package contains the core runtime libraries.

%description plugins-base
This package contains a basic set of plugins.
%endif

%description devel
This package contains header files and libraries necessary for development.

%description doc
This package contains documentation.

%if %{?_with_pulse:1}%{!?_with_pulse:0}
%description pulse
This package contains the Murphy PulseAudio mainloop integration runtime files.

%description pulse-devel
This package contains the Murphy PulseAudio mainloop integration development
files.
%endif

%if %{?_with_ecore:1}%{!?_with_ecore:0}
%description ecore
This package contains the Murphy EFL/ecore mainloop integration runtime files.

%description ecore-devel
This package contains the Murphy EFL/ecore mainloop integration development
files.
%endif

%if %{?_with_glib:1}%{!?_with_glib:0}
%description glib
This package contains the Murphy glib mainloop integration runtime files.

%description glib-devel
This package contains the Murphy glib mainloop integration development
files.
%endif

%if %{?_with_qt:1}%{!?_with_qt:0}
%description qt
This package contains the Murphy Qt mainloop integration runtime files.

%description qt-devel
This package contains the Murphy Qt mainloop integration development
files.
%endif

%description tests
This package contains various test binaries for Murphy.

%description ivi-resource-manager
This package contains the Murphy IVI resource manager plugin.

%if %{_enable_icosyscon}
%description system-controller
This package contains the Murphy IVI resource manager plugin.
%endif

%description gam
This package contains the Murphy plugins for necessary for supporting
Genivi Audio Manager.

%description gam-devel
This package contains development files for Murphy Genivi Audio Manager
plugins.

%prep
%setup -q
cp %{SOURCE1001} .

echo "_with_icosyscon:   \"%{_with_icosyscon}\""
echo "_enable_icosyscon: \"%{_enable_icosyscon}\""

%build
%if %{?_with_debug:1}%{!?_with_debug:0}
export CFLAGS="-O0 -g3"
V="V=1"
%endif

CONFIG_OPTIONS=""
DYNAMIC_PLUGINS="domain-control,system-controller,ivi-resource-manager"

%if %{?_with_pulse:1}%{!?_with_pulse:0}
CONFIG_OPTIONS="$CONFIG_OPTIONS --enable-gpl --enable-pulse"
%else
CONFIG_OPTIONS="$CONFIG_OPTIONS --disable-pulse"
%endif

%if %{?_with_ecore:1}%{!?_with_ecore:0}
CONFIG_OPTIONS="$CONFIG_OPTIONS --enable-gpl --enable-ecore"
%else
CONFIG_OPTIONS="$CONFIG_OPTIONS --disable-ecore"
%endif

%if %{?_with_glib:1}%{!?_with_glib:0}
CONFIG_OPTIONS="$CONFIG_OPTIONS --enable-gpl --enable-glib"
%else
CONFIG_OPTIONS="$CONFIG_OPTIONS --disable-glib"
%endif

%if %{?_with_qt:1}%{!?_with_qt:0}
CONFIG_OPTIONS="$CONFIG_OPTIONS --enable-qt"
%else
CONFIG_OPTIONS="$CONFIG_OPTIONS --disable-qt"
%endif

%if %{?_with_dbus:1}%{!?_with_dbus:0}
CONFIG_OPTIONS="$CONFIG_OPTIONS --enable-gpl --enable-libdbus"
%else
CONFIG_OPTIONS="$CONFIG_OPTIONS --disable-libdbus"
%endif

%if %{?_with_telephony:1}%{!?_with_telephony:0}
CONFIG_OPTIONS="$CONFIG_OPTIONS --enable-gpl --enable-telephony"
%else
CONFIG_OPTIONS="$CONFIG_OPTIONS --disable-telephony"
%endif

%if %{?_with_audiosession:1}%{!?_with_audiosession:0}
CONFIG_OPTIONS="$CONFIG_OPTIONS --enable-resource-asm"
DYNAMIC_PLUGINS="$DYNAMIC_PLUGINS,resource-asm"
%else
CONFIG_OPTIONS="$CONFIG_OPTIONS --disable-resource-asm"
%endif

%if %{?_with_websockets:1}%{!?_with_websockets:0}
CONFIG_OPTIONS="$CONFIG_OPTIONS --enable-websockets"
%else
CONFIG_OPTIONS="$CONFIG_OPTIONS --disable-websockets"
%endif

%if %{?_with_smack:1}%{!?_with_smack:0}
CONFIG_OPTIONS="$CONFIG_OPTIONS --enable-smack"
%else
CONFIG_OPTIONS="$CONFIG_OPTIONS --disable-smack"
%endif

%if %{_enable_icosyscon}
CONFIG_OPTIONS="$CONFIG_OPTIONS --enable-system-controller"
%else
CONFIG_OPTIONS="$CONFIG_OPTIONS --disable-system-controller"
%endif

%if %{?_with_sysmon:1}%{!?_with_sysmon:0}
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
mkdir -p %{buildroot}%{_unitdir_user}
cp packaging.in/murphyd.service %{buildroot}%{_unitdir_user}

%if %{?_with_dbus:1}%{!?_with_dbus:0}
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
/bin/systemctl --user enable --global murphyd.service
setcap 'cap_net_admin=+ep' %{_bindir}/murphyd

%if %{?_with_squashpkg:0}%{!?_with_squashpkg:1}
%post core
%endif
ldconfig

%postun
if [ "$1" = "0" ]; then
/bin/systemctl --user disable --global murphyd.service
fi

%if %{?_with_squashpkg:0}%{!?_with_squashpkg:1}
%postun core
%endif
ldconfig

%if %{?_with_glib:1}%{!?_with_glib:0}
%post glib
ldconfig

%postun glib
ldconfig
%endif

%if %{?_with_pulse:1}%{!?_with_pulse:0}
%post pulse
ldconfig

%postun pulse
ldconfig
%endif

%if %{?_with_ecore:1}%{!?_with_ecore:0}
%post ecore
ldconfig

%postun ecore
ldconfig
%endif

%if %{?_with_qt:1}%{!?_with_qt:0}
%post qt
ldconfig

%postun qt
ldconfig
%endif

%post gam
ldconfig

%postun gam
ldconfig

%if %{?_with_squashpkg:1}%{!?_with_squashpkg:0}
%files -f filelist.plugins-base
%else
%files
%endif
%defattr(-,root,root,-)
%manifest murphy.manifest
%{_bindir}/murphyd
%config %{_sysconfdir}/murphy
%{_unitdir_user}/murphyd.service
%{_tmpfilesdir}/murphyd.conf
%if %{?_with_audiosession:1}%{!?_with_audiosession:0}
%{_sbindir}/asm-bridge
%endif
%if %{?_with_dbus:1}%{!?_with_dbus:0}
%{_sysconfdir}/dbus-1/system.d
%config %{_sysconfdir}/dbus-1/system.d/org.Murphy.conf
%endif
%if %{?_with_websockets:1}%{!?_with_websockets:0}
%{_datadir}/murphy
%endif

%if %{?_with_squashpkg:0}%{!?_with_squashpkg:1}
%files core
%defattr(-,root,root,-)
%endif
%{_libdir}/libmurphy-common.so.*
%{_libdir}/libmurphy-core.so.*
%{_libdir}/libmurphy-resolver.so.*
%{_libdir}/libmurphy-resource.so.*
%{_libdir}/libmurphy-resource-backend.so.*
%if %{?_with_lua:1}%{!?_with_lua:0}
%{_libdir}/libmurphy-lua-utils.so.*
%{_libdir}/libmurphy-lua-decision.so.*
%endif
%{_libdir}/libmurphy-domain-controller.so.*
%{_libdir}/murphy/*.so.*
%{_libdir}/libbreedline*.so.*
%if %{?_with_dbus:1}%{!?_with_dbus:0}
%{_libdir}/libmurphy-libdbus.so.*
%{_libdir}/libmurphy-dbus-libdbus.so.*
%endif

%if %{?_with_squashpkg:0}%{!?_with_squashpkg:1}
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
%if %{?_with_lua:1}%{!?_with_lua:0}
%{_libdir}/libmurphy-lua-utils.so
%{_libdir}/libmurphy-lua-decision.so
%endif
%{_libdir}/libmurphy-domain-controller.so
%{_libdir}/murphy/*.so
%{_libdir}/pkgconfig/murphy-common.pc
%{_libdir}/pkgconfig/murphy-core.pc
%{_libdir}/pkgconfig/murphy-resolver.pc
# %%{_libdir}/pkgconfig/murphy-resource.pc
%if %{?_with_lua:1}%{!?_with_lua:0}
%{_libdir}/pkgconfig/murphy-lua-utils.pc
%{_libdir}/pkgconfig/murphy-lua-decision.pc
%endif
%{_libdir}/pkgconfig/murphy-domain-controller.pc
%{_libdir}/pkgconfig/murphy-db.pc
%{_libdir}/pkgconfig/murphy-resource.pc
%{_includedir}/breedline
%{_libdir}/libbreedline*.so
%{_libdir}/pkgconfig/breedline*.pc
%if %{?_with_dbus:1}%{!?_with_dbus:0}
# %%{_includedir}/murphy/dbus
%{_libdir}/libmurphy-libdbus.so
%{_libdir}/libmurphy-dbus-libdbus.so
%{_libdir}/pkgconfig/murphy-libdbus.pc
%{_libdir}/pkgconfig/murphy-dbus-libdbus.pc
%endif

%files doc
%defattr(-,root,root,-)
%doc %{_docdir}/../murphy/AUTHORS
%doc %{_docdir}/../murphy/CODING-STYLE
%doc %{_docdir}/../murphy/ChangeLog
%doc %{_docdir}/../murphy/NEWS
%doc %{_docdir}/../murphy/README
%license COPYING LICENSE-BSD

%if %{?_with_pulse:1}%{!?_with_pulse:0}
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

%if %{?_with_ecore:1}%{!?_with_ecore:0}
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

%if %{?_with_glib:1}%{!?_with_glib:0}
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

%if %{?_with_qt:1}%{!?_with_qt:0}
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
%{_bindir}/resource-context-create
%{_bindir}/test-domain-controller
%{_bindir}/murphy-console
%manifest murphy.manifest

%files ivi-resource-manager
%defattr(-,root,root,-)
%{_libdir}/murphy/plugins/plugin-ivi-resource-manager.so
%manifest murphy.manifest

%if %{_enable_icosyscon}
%files system-controller
%defattr(-,root,root,-)
%{_libdir}/murphy/plugins/plugin-system-controller.so
%manifest murphy.manifest
%endif

%changelog
* Tue Nov 27 2012 Krisztian Litkey <krisztian.litkey@intel.com> -
- Initial build for 2.0alpha.
