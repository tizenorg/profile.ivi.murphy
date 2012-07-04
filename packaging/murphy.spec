# By default we build with distro-default compilation flags which
# enables optimizations. If you want to build with full debugging
# ie. with optimization turned off and full debug info (-O0 -g3)
# pass --with debug to rpmbuild on the command line.

%{!?_with_debug:%{!?_without_debug:%define _without_debug 0}}
%{!?_with_console:%{!?_without_console:%define _without_console 0}}

Summary: Murphy policy framework
Name: murphy
Version: 0.0.1
Release: 1
License: BSD
Group: System Environment/Daemons
URL: http://127.0.0.1/murphy
Source0: %{name}-%{version}.tar.bz2
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root

BuildRequires: pkgconfig(libpulse)
BuildRequires: pkgconfig(glib-2.0)
BuildRequires: pkgconfig(dbus-1)
BuildRequires: flex
BuildRequires: bison

%description
This package contains the the Murphy policy framework daemon.

%package libs
Summary: Murphy basic runtime libraries
Group: System Environment/Libraries

%description libs
This package contains the Murphy basic runtime libraries necessary
for even the most basic Murphy configurations.

%package pulse
Summary: Murphy PulseAudio glue library
Group: System Environment/Libraries

%description pulse
This package contains glue code that enables the Murphy mainloop to
be pumped by the PulseAudio mainloop. This enables applications or
modules that use the common PulseAudio infrastructure to also link
against and use the Murphy common infra.

%package dbus
Summary: Murphy D-Bus library and plugin
Group: System Environment/Libraries

%description dbus
This package contains the D-Bus library and plugin for Murphy. These
provide a low-level D-Bus library, D-Bus transport abstraction and
integration for D-Bus to the Murphy mainloop.

%package glib
Summary: Murphy GLib plugin
Group: System Environment/Libraries

%description glib
This package contains a GLib plugin for Murphy. When loaded, this plugin
will pump GMainLoop from within the Murphy mainloop.

%if %{?_with_console:1}%{!?_with_console:0}
%package console
Summary: Murphy console plugin and console client
Group: System Environment/Daemons
BuildRequires: readline-devel

%description console
This package contains a console plugin for Murphy and the standalone
Murphy console client binary, murphy-console.
%endif

%package devel
Summary: Headers files and libraries for Murphy development
Group: Development/Libraries

%description devel
This package contains header files and libraries necessary for developing
Murphy modules, clients or applications using any of the Murphy infrastructure.

%package doc
Summary: Documentation (rrright...) for Murphy
Group: Documentation

%description doc
This package contains (will eventually contain) documentation for Murphy.

%prep
%setup -q

%build
%if %{?_with_debug:1}%{!?_with_debug:0}
export CFLAGS="-O0 -g3"
V="V=1"
%endif

./bootstrap && \
    %configure --enable-gpl --enable-pulse --enable-dbus \
               --with-dynamic-plugins=dbus,glib,console && \
    make $V

%install
rm -rf $RPM_BUILD_ROOT
%make_install

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root,-)
%{_bindir}/murphyd
%{_sysconfdir}/murphy

%files libs
%defattr(-,root,root,-)
%{_libdir}/libmurphy-common.so.*
%{_libdir}/libmurphy-core.so.*
%{_libdir}/murphy/libmdb.so.*
%{_libdir}/murphy/libmql.so.*
%{_libdir}/murphy/libmqi.so.*

%files pulse
%defattr(-,root,root,-)
%{_libdir}/libmurphy-pulse.so.*

%files dbus
%defattr(-,root,root,-)
%{_libdir}/libmurphy-dbus.so.*
%{_libdir}/murphy/plugins/plugin-dbus.so

%files glib
%defattr(-,root,root,-)
%{_libdir}/murphy/plugins/plugin-glib.so

%if %{?_with_console:1}%{!?_with_console:0}
%files console
%defattr(-,root,root,-)
%{_libdir}/murphy/plugins/plugin-console.so
%{_bindir}/murphy-console
%endif

%files devel
%defattr(-,root,root,-)
%{_includedir}/murphy
%{_includedir}/murphy-db
%{_libdir}/libmurphy*.so
%{_libdir}/murphy/libmdb.so
%{_libdir}/murphy/libmql.so
%{_libdir}/murphy/libmqi.so
%{_libdir}/pkgconfig/*.pc

%files doc
%defattr(-,root,root,-)
%doc %{_docdir}/murphy

