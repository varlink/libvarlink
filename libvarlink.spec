%global _hardened_build 1

Name:           libvarlink
Version:        18
Release:        1%{?dist}
Summary:        Varlink C Library
License:        ASL 2.0
URL:            https://github.com/varlink/%{name}
Source0:        https://github.com/varlink/%{name}/archive/%{version}/%{name}-%{version}.tar.gz
BuildRequires:  meson
BuildRequires:  gcc

%description
Varlink C Library

%package        devel
Summary:        Development files for %{name}
Requires:       %{name}%{?_isa} = %{version}-%{release}

%description    devel
The %{name}-devel package contains libraries and header files for
developing applications that use %{name}.

%package        util
Summary:        Varlink command line tools

%description    util
The %{name}-util package contains varlink command line tools.

%prep
%setup -q

%build
%meson
%meson_build

%check
export LC_CTYPE=C.utf8
%meson_test

%install
%meson_install

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%files
%license LICENSE
%{_libdir}/libvarlink.so.*

%files util
%{_bindir}/varlink
%{_datadir}/bash-completion/completions/varlink
%{_datadir}/vim/vimfiles/after/*

%files devel
%{_includedir}/varlink.h
%{_libdir}/libvarlink.so
%{_libdir}/pkgconfig/libvarlink.pc

%changelog
* Wed May 22 2019 <info@varlink.org> - 18-1
- libvarlink 18

* Fri Feb 15 2019 <info@varlink.org> - 17-1
- libvarlink 17

* Mon Nov  5 2018 <info@varlink.org> 16-1
- libvarlink 16
