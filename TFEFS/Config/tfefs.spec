Name:           tfefs
Version:        0.1
Release:        1
Summary:        Hashicorp Terraform Enterprise Fuse Agent
License:        GPL+
Source0:        https://github.com/jboero/hashifuse/archive/master.zip
Requires:       libcurl fuse jsoncpp
URL:            https://www.terraform.io/

%define debug_package %{nil}

%description
FUSE filesystem for browsing and managing Hashicorp Terraform Enterprise / Terraform Cloud. Community project not supported by Hashicorp.

%prep
%autosetup -c %{name}-%{version}

%build
cd hashifuse-master/TFEFS
make

%install

mkdir -p %{buildroot}%{_bindir}/
cp -p hashifuse-master/TFEFS/%{name} %{buildroot}%{_bindir}/

%clean
rm -rf %{buildroot}
rm -rf %{_builddir}/*

%files
%{_bindir}/%{name}

%changelog
