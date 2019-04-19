Name:           tfefs
Version:        0.1
Release:        1
Summary:        Hashicorp Terraform Enterprise Fuse Agent
License:        GPL+
Source0:        tfefs
Requires(post): libcurl fuse jsoncpp
URL:            https://www.terraform.io/

%define debug_package %{nil}

%description
FUSE filesystem for browsing and managing Hashicorp Terraform Enterprise.

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
