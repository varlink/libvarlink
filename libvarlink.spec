%define build_date %(date +"%%a %%b %%d %%Y")
%define build_timestamp %(date +"%%Y%%m%%d.%%H%M%%S")

Name:           libvarlink
Version:        1
Release:        %{build_timestamp}%{?dist}
Summary:        Varlink C Library
License:        ASL2.0
URL:            https://github.com/varlink/libvarlink
Source0:        https://github.com/varlink/libvarlink/archive/v%{version}.tar.gz
BuildRequires:  autoconf automake pkgconfig

%description
Varlink C Library

%package        devel
Summary:        Development files for %{name}
Requires:       %{name} = %{version}-%{release}

%description    devel
The %{name}-devel package contains libraries and header files for
developing applications that use %{name}.

%prep
%setup -q

%build
./autogen.sh
%configure
make %{?_smp_mflags}

%install
%make_install

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%files
%license AUTHORS
%license COPYRIGHT
%license LICENSE
%{_libdir}/libvarlink.so.*
%{_bindir}/varlink
%{_datadir}/bash-completion/completions/varlink

%files devel
%{_includedir}/varlink.h
%{_libdir}/libvarlink.so
%{_libdir}/pkgconfig/libvarlink.pc

%changelog
* %{build_date} <info@varlink.org> %{version}-%{build_timestamp}
- %{name} %{version}
