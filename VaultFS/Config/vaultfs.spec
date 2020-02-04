# https://fedoraproject.org/wiki/How_to_create_an_RPM_package

Name:           vaultfs
Version:        0.1
Release:        1
Summary:        Hashicorp VaultFS Fuse Agent
License:        GPL+
Source0:        https://github.com/jboero/hashifuse/archive/master.zip
Requires(post): libcurl fuse jsoncpp
BuildRequires:  gcc-c++ libcurl-devel fuse-devel jsoncpp-devel
URL:            https://www.vaultproject.io/

%define debug_package %{nil}

%description
FUSE filesystem for browsing and managing Hashicorp Vault secrets. Community project not supported by Hashicorp.

%prep
%autosetup -c %{name}-%{version}

%build
cd hashifuse-master/VaultFS
make

%install

mkdir -p %{buildroot}%{_bindir}/
cp -p hashifuse-master/VaultFS/%{name} %{buildroot}%{_bindir}/

%clean
rm -rf %{buildroot}
rm -rf %{_builddir}/*

%files
%{_bindir}/%{name}

%changelog

