Name:       weston
Summary:    Weston Compositor
Version:    1.7.0.03
Release:    2%{?dist}
Group:      Graphics & UI Framework/Wayland Window System
License:    MIT
URL:        http://wayland.freedesktop.org
Source0:    %{name}-%{version}.tar.gz
Source1:    ias_dualscreen.conf
Source2:    config_parameters

BuildRequires: pkgconfig(wayland-server)
BuildRequires: pkgconfig(egl)
BuildRequires: pkgconfig(glesv2)
BuildRequires: pkgconfig(pixman-1)
BuildRequires: pkgconfig(libpng)
BuildRequires: pkgconfig(libudev)
BuildRequires: pkgconfig(libevdev)
BuildRequires: pkgconfig(libinput)
BuildRequires: pkgconfig(libdrm)
BuildRequires: pkgconfig(gbm)
BuildRequires: pkgconfig(wayland-client)
BuildRequires: pkgconfig(wayland-egl)
BuildRequires: pkgconfig(cairo)
BuildRequires: pkgconfig(glib-2.0)
BuildRequires: pkgconfig(xkbcommon)
BuildRequires: pkgconfig(libpng)
BuildRequires: pkgconfig(mtdev)
BuildRequires: pam-devel
BuildRequires: libjpeg-devel
BuildRequires: expat-devel
BuildRequires: bzip2-devel
Requires: xkeyboard-config
Requires: weston-ias-config = 1

%description
Weston compositor

%package doc
Summary:    Man page for Weston
Group:      System/Libraries
Requires:   %{name} = %{version}-%{release}

%description doc
Man page for Weston

%package ias
Summary:    IAS specific libraries/executable
Group:      System/Libraries
Requires:   %{name} = %{version}-%{release}
Provides:   libias-shell-protocol.so

%description ias
IAS specific libraries/executable

%package ias-devel
Summary:    IAS specific development package
Group:      System/Libraries
Requires:   %{name}-ias = %{version}-%{release}

%description ias-devel
IAS specific development package

%package demo
Summary:    Weston demo clients
Group:      System/Libraries
Requires:   %{name} = %{version}-%{release}

%description demo
Weston demo clients

%package ias-config-default
Summary:    Weston configuration for BfH
Group:      System/Configuration
Provides: weston-ias-config = 1

%description ias-config-default
Weston configuration for BfH

%package ias-config-snb
Summary:    Weston configuration for SNB
Group:      System/Configuration
Provides: weston-ias-config = 1
Requires: coreutils

%description ias-config-snb
Weston configuration for SNB

%package ias-config-vmware
Summary:    Weston configuration for VMWARE
Group:      System/Configuration
Provides: weston-ias-config = 1
Requires: coreutils

%description ias-config-vmware
Weston configuration for VMWARE

%package ias-config-dualscreen
Summary:    Weston configuration for BfH dualscreen setup
Group:      System/Configuration
Provides: weston-ias-config = 1
Requires: coreutils

%description ias-config-dualscreen
Weston configuration for BfH dualscreen setup



%prep
%setup -q

%build
%autogen `cat %{SOURCE2}`

make

%install
rm -rf %{buildroot}
%make_install

%define _weston_confdir /home/pulse/.config

mkdir  -p $RPM_BUILD_ROOT%{_weston_confdir}
install -m 0644 weston.ini      $RPM_BUILD_ROOT%{_weston_confdir}
install -m 0644 ivi-shell/weston.ini      $RPM_BUILD_ROOT%{_weston_confdir}/weston.ini.ivi
install -m 0644 ias_brd2.conf   $RPM_BUILD_ROOT%{_weston_confdir}/ias.conf
install -m 0644 ias_snb.conf    $RPM_BUILD_ROOT%{_weston_confdir}
install -m 0644 ias_vmware.conf $RPM_BUILD_ROOT%{_weston_confdir}

#copy ias_dualscreen.conf
install -m 0644 %{SOURCE1} $RPM_BUILD_ROOT%{_weston_confdir}

mkdir -p $RPM_BUILD_ROOT/%{_libdir}/systemd/system/
install -m 0644 weston.service $RPM_BUILD_ROOT/%{_libdir}/systemd/system/weston.service.orig
mkdir -p $RPM_BUILD_ROOT/%{_libdir}/systemd/system/graphical.target.wants
install -m 0644 weston.service.snbvmware $RPM_BUILD_ROOT/%{_libdir}/systemd/system/

%define _weston_clientdir /opt/weston_test
mkdir -p $RPM_BUILD_ROOT%{_weston_clientdir}
install -m 0775 weston-flower         $RPM_BUILD_ROOT%{_weston_clientdir}
install -m 0775 weston-image          $RPM_BUILD_ROOT%{_weston_clientdir}
install -m 0775 weston-cliptest       $RPM_BUILD_ROOT%{_weston_clientdir}
install -m 0775 weston-dnd            $RPM_BUILD_ROOT%{_weston_clientdir}
install -m 0775 weston-smoke          $RPM_BUILD_ROOT%{_weston_clientdir}
install -m 0775 weston-resizor        $RPM_BUILD_ROOT%{_weston_clientdir}
install -m 0775 weston-eventdemo      $RPM_BUILD_ROOT%{_weston_clientdir}
install -m 0775 weston-clickdot       $RPM_BUILD_ROOT%{_weston_clientdir}
install -m 0775 weston-transformed    $RPM_BUILD_ROOT%{_weston_clientdir}
install -m 0775 weston-fullscreen     $RPM_BUILD_ROOT%{_weston_clientdir}
install -m 0775 weston-stacking       $RPM_BUILD_ROOT%{_weston_clientdir}
install -m 0775 weston-calibrator     $RPM_BUILD_ROOT%{_weston_clientdir}
install -m 0775 weston-scaler         $RPM_BUILD_ROOT%{_weston_clientdir}

#install -m 0775 weston-editor         $RPM_BUILD_ROOT%{_weston_clientdir}

install -m 0775 weston-simple-egl     $RPM_BUILD_ROOT%{_weston_clientdir}
install -m 0775 weston-simple-shm     $RPM_BUILD_ROOT%{_weston_clientdir}
install -m 0775 weston-simple-touch   $RPM_BUILD_ROOT%{_weston_clientdir}
install -m 0775 weston-multi-resource $RPM_BUILD_ROOT%{_weston_clientdir}

install -m 0775 weston-simple-ivi-protocol     $RPM_BUILD_ROOT%{_weston_clientdir}

install -m 0775 wrandr                $RPM_BUILD_ROOT%{_weston_clientdir}
install -m 0775 surfctrl              $RPM_BUILD_ROOT%{_weston_clientdir}
install -m 0775 layoutctrl            $RPM_BUILD_ROOT%{_weston_clientdir}
install -m 0775 inputctrl             $RPM_BUILD_ROOT%{_weston_clientdir}
install -m 0775 traceinfo             $RPM_BUILD_ROOT%{_weston_clientdir}


%files
%defattr(-,root,root,-)
%{_bindir}/weston
%{_bindir}/weston-terminal
%{_bindir}/weston-info
%attr(6755,root,root) %{_bindir}/weston-launch
%{_bindir}/wcap-decode
%{_bindir}/extension_test_client
%dir %{_libdir}/weston/
%{_libdir}/weston/*.so
%{_libexecdir}/weston-desktop-shell
%{_libexecdir}/weston-screenshooter
%{_libexecdir}/weston-keyboard
%{_libexecdir}/weston-simple-im
%{_libexecdir}/weston-ivi-shell-user-interface

%dir %{_prefix}/share/weston/
%{_prefix}/share/weston/*.*
###%%% /etc/systemd/system/weston.service
%{_libdir}/systemd/system/weston.service.orig
%{_libdir}/pkgconfig/weston.pc

%dir %{_prefix}/share/wayland-sessions/
%{_prefix}/share/wayland-sessions/weston.desktop


%files demo
%defattr(-,root,root,-)
%{_weston_clientdir}/weston-flower
%{_weston_clientdir}/weston-image
%{_weston_clientdir}/weston-cliptest
%{_weston_clientdir}/weston-dnd
%{_weston_clientdir}/weston-smoke
%{_weston_clientdir}/weston-resizor
%{_weston_clientdir}/weston-eventdemo
%{_weston_clientdir}/weston-clickdot
%{_weston_clientdir}/weston-transformed
%{_weston_clientdir}/weston-fullscreen
%{_weston_clientdir}/weston-stacking
%{_weston_clientdir}/weston-calibrator
%{_weston_clientdir}/weston-scaler

#%{_weston_clientdir}/weston-editor

%{_weston_clientdir}/weston-simple-egl
%{_weston_clientdir}/weston-simple-shm
%{_weston_clientdir}/weston-simple-touch
%{_weston_clientdir}/weston-multi-resource

%{_weston_clientdir}/wrandr
%{_weston_clientdir}/surfctrl
%{_weston_clientdir}/layoutctrl
%{_weston_clientdir}/inputctrl
%{_weston_clientdir}/traceinfo


%files ias
#TODO
###%%% /usr/lib/weston/ias-backend.so
###%%% /usr/lib/weston/ias-shell.so
%{_libdir}/ias/*
%{_libexecdir}/ias-test-hmi

%files ias-devel
%{_includedir}/ias-shell-client-protocol.h
%{_docdir}/../ias/*
%{_includedir}/weston/compositor.h
%{_includedir}/weston/ias-plugin-framework-definitions.h
%{_includedir}/weston/ias-spug.h
%{_includedir}/weston/weston-egl-ext.h
%{_includedir}/weston/ias-common.h
%{_includedir}/weston/config-parser.h
%{_includedir}/weston/matrix.h
%{_includedir}/weston/version.h
%{_includedir}/weston/zalloc.h
%{_includedir}/weston/timeline-object.h


%files doc
%{_mandir}/man1/*
%{_mandir}/man5/*
%{_mandir}/man7/*


%files ias-config-default
%attr(755,pulse,pulse) %dir /home/pulse
%attr(755,pulse,pulse) %dir /home/pulse/.config
%attr(644,pulse,pulse) /home/pulse/.config/weston.ini
%attr(644,pulse,pulse) /home/pulse/.config/ias.conf
%attr(644,pulse,pulse) /home/pulse/.config/weston.ini.ivi


%files ias-config-snb
%attr(755,pulse,pulse) %dir /home/pulse
%attr(755,pulse,pulse) %dir /home/pulse/.config
%attr(644,pulse,pulse) /home/pulse/.config/weston.ini
%attr(644,pulse,pulse) /home/pulse/.config/ias_snb.conf
%{_libdir}/systemd/system/weston.service.snbvmware
%post ias-config-snb
ln -s /home/pulse/.config/ias_snb.conf /home/pulse/.config/ias.conf
ln -s %{_libdir}/systemd/system/weston.service.snbvmware %{_libdir}/systemd/system/weston.service


%files ias-config-vmware
%attr(755,pulse,pulse) %dir /home/pulse
%attr(755,pulse,pulse) %dir /home/pulse/.config
%attr(644,pulse,pulse) /home/pulse/.config/weston.ini
%attr(644,pulse,pulse) /home/pulse/.config/ias_vmware.conf
%{_libdir}/systemd/system/weston.service.snbvmware
%post ias-config-vmware
ln -s /home/pulse/.config/ias_vmware.conf /home/pulse/.config/ias.conf
ln -s %{_libdir}/systemd/system/weston.service.snbvmware %{_libdir}/systemd/system/weston.service

%files ias-config-dualscreen
%attr(755,pulse,pulse) %dir /home/pulse
%attr(755,pulse,pulse) %dir /home/pulse/.config
%attr(644,pulse,pulse) /home/pulse/.config/weston.ini
%attr(644,pulse,pulse) /home/pulse/.config/ias_dualscreen.conf
%post ias-config-dualscreen
ln -s /home/pulse/.config/ias_dualscreen.conf /home/pulse/.config/ias.conf


%changelog
