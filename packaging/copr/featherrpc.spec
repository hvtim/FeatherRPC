Name:           featherrpc
Version:        0.1.1
Release:        1%{?dist}
Summary:        Syncs now-playing media to Discord as a Rich Presence status

License:        GPL-3.0-or-later
URL:            https://github.com/hvtim/FeatherRPC
Source0:        https://github.com/hvtim/FeatherRPC/archive/refs/tags/v%{version}/FeatherRPC-%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  gcc-c++
BuildRequires:  pkgconfig(gio-2.0)
BuildRequires:  pkgconfig(dbus-1)
BuildRequires:  pkgconfig(libcurl)
BuildRequires:  desktop-file-utils
BuildRequires:  systemd-rpm-macros

Requires:       glib2
Requires:       dbus
Requires:       libcurl

%description
FeatherRPC syncs whatever's currently playing to Discord as a Rich
Presence status - iTunes, VLC, browsers, or anything else reporting
now-playing info. On Linux, works with any MPRIS-compliant media player.

%prep
%autosetup -n FeatherRPC-%{version}

%build
%cmake -S native
%cmake_build

%install
install -Dm755 %{_vpath_builddir}/FeatherRPC %{buildroot}%{_bindir}/FeatherRPC
install -Dm755 %{_vpath_builddir}/featherrpc %{buildroot}%{_bindir}/featherrpc
install -Dm644 assets/icon.png %{buildroot}%{_datadir}/icons/hicolor/256x256/apps/featherrpc.png
install -Dm644 installer/linux/featherrpc.service %{buildroot}%{_userunitdir}/featherrpc.service
install -Dm644 packaging/aur/featherrpc.desktop %{buildroot}%{_datadir}/applications/featherrpc.desktop
desktop-file-validate %{buildroot}%{_datadir}/applications/featherrpc.desktop

%files
%license LICENSE
%{_bindir}/FeatherRPC
%{_bindir}/featherrpc
%{_datadir}/icons/hicolor/256x256/apps/featherrpc.png
%{_datadir}/applications/featherrpc.desktop
%{_userunitdir}/featherrpc.service

%post
%systemd_user_post featherrpc.service

%preun
%systemd_user_preun featherrpc.service

%changelog
* Sun Jul 26 2026 hvtim - 0.1.1-1
- Initial COPR package
