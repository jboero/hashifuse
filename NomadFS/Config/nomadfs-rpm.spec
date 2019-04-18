Name:           nomadfs
Version:        0.1
Release:        1
Summary:        Hashicorp NomadFS Fuse Agent
License:        Hashicorp
Source0:        nomadfs
Requires(post): libcurl fuse jsoncpp
URL:            https://www.nomadproject.io/

%define debug_package %{nil}

%description
FUSE filesystem for browsing and managing Hashicorp Nomad jobs.

%prep
%autosetup -c %{name}-%{version}

%build

%install

mkdir -p %{buildroot}%{_bindir}/
cp -p %{name} %{buildroot}%{_bindir}/

%clean
rm -rf %{buildroot}
rm -rf %{_builddir}/*

%files
%{_bindir}/%{name}

%changelog
